#include "WebInterface.h"
#include <SPIFFS.h>
#include <functional>
#include "DataLogger.h"
#include "SPIFFSHandlers.h"

WebInterface::WebInterface(uint16_t port) : server(new WebServer(port)), wsServer(new WebSocketsServer(81)), ownsServer(true) {}

WebInterface::WebInterface(WebServer &existingServer) : server(&existingServer), wsServer(new WebSocketsServer(81)), ownsServer(false) {}

WebInterface::~WebInterface()
{
    if (realtimeTaskHandle != nullptr)
    {
        vTaskDelete(realtimeTaskHandle);
        realtimeTaskHandle = nullptr;
    }

    if (wsServer)
    {
        wsServer->disconnect();
        delete wsServer;
        wsServer = nullptr;
    }

    if (ownsServer && server)
    {
        server->stop();
        delete server;
        server = nullptr;
    }
}

void WebInterface::begin()
{
    server->on("/", HTTP_GET, std::bind(&WebInterface::handleRoot, this));
    server->on("/api/status", HTTP_GET, std::bind(&WebInterface::handleGetStatus, this));
    server->on("/api/operating/setpoint", HTTP_POST, std::bind(&WebInterface::handleSetSetpoint, this));
    server->on("/api/operating/smokesetpoint", HTTP_POST, std::bind(&WebInterface::handleSetSmokeSetpoint, this));
    server->on("/api/tunable", HTTP_GET, std::bind(&WebInterface::handleGetTunableParams, this));
    server->on("/api/tunable", HTTP_POST, std::bind(&WebInterface::handleSetTunableParams, this));
    server->on("/api/recipe", HTTP_GET, std::bind(&WebInterface::handleGetRecipeState, this));
    server->on("/api/recipe", HTTP_POST, std::bind(&WebInterface::handleSetRecipeState, this));
    server->on("/api/buttons", HTTP_GET, std::bind(&WebInterface::handleGetButtons, this));
    server->on("/api/buttons", HTTP_POST, std::bind(&WebInterface::handleSetButton, this));
    server->on("/api/actuators", HTTP_GET, std::bind(&WebInterface::handleGetActuatorValues, this));
    server->on("/api/actuators", HTTP_POST, std::bind(&WebInterface::handleSetActuatorValues, this));
    server->on("/api/config/download", HTTP_GET, std::bind(&WebInterface::handleDownloadConfig, this));
    server->on("/api/config/upload", HTTP_POST, std::bind(&WebInterface::handleUploadConfig, this));
    server->on("/api/reboot", HTTP_POST, std::bind(&WebInterface::handleReboot, this));
    server->on("/api/spiffs/list", HTTP_GET, std::bind(&WebInterface::handleSPIFFSList, this));
    server->on("/api/spiffs/download", HTTP_GET, std::bind(&WebInterface::handleSPIFFSDownload, this));
    server->on("/api/spiffs/upload", HTTP_POST, std::bind(&WebInterface::handleSPIFFSUpload, this));
    server->on("/api/spiffs/delete", HTTP_POST, std::bind(&WebInterface::handleSPIFFSDelete, this));
    server->on("/api/logging/config", HTTP_GET, std::bind(&WebInterface::handleGetLoggingConfig, this));
    server->on("/api/logging/config", HTTP_POST, std::bind(&WebInterface::handleSetLoggingConfig, this));
    server->on("/api/logging/clear", HTTP_POST, std::bind(&WebInterface::handleClearLogs, this));
    server->on("/api/logging/download", HTTP_GET, std::bind(&WebInterface::handleDownloadLog, this));
    server->on("/api/logging/data", HTTP_GET, std::bind(&WebInterface::handleGetLogData, this));
    server->onNotFound(std::bind(&WebInterface::handleNotFound, this));

    server->begin();
    if (wsServer)
    {
        wsServer->begin();
        wsServer->onEvent([this](uint8_t clientId, WStype_t type, uint8_t *payload, size_t length)
                          { handleWsEvent(clientId, type, payload, length); });
    }

    if (realtimeTaskHandle == nullptr)
    {
        xTaskCreatePinnedToCore(
            realtimeTaskEntryPoint,
            "RealtimeWsTask",
            4096,
            this,
            1,
            &realtimeTaskHandle,
            1);
    }

    Serial.println("Web server started");
}

void WebInterface::handleClient()
{
    server->handleClient();
}

void WebInterface::stop()
{
    server->stop();
    if (wsServer)
    {
        wsServer->disconnect();
    }
}

void WebInterface::realtimeTaskEntryPoint(void *parameter)
{
    WebInterface *instance = static_cast<WebInterface *>(parameter);
    if (instance != nullptr)
    {
        instance->realtimeTaskLoop();
    }
    vTaskDelete(nullptr);
}

void WebInterface::realtimeTaskLoop()
{
    for (;;)
    {
        if (wsServer)
        {
            wsServer->loop();
            broadcastRealtimeSnapshot(false);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void WebInterface::handleWsEvent(uint8_t clientId, WStype_t type, uint8_t *payload, size_t length)
{
    if (!wsServer)
    {
        return;
    }

    switch (type)
    {
    case WStype_CONNECTED:
    {
        StaticJsonDocument<128> helloDoc;
        helloDoc["type"] = "hello";
        helloDoc["protocol"] = 1;
        helloDoc["timestamp"] = millis();
        String hello;
        serializeJson(helloDoc, hello);
        wsServer->sendTXT(clientId, hello);
        broadcastRealtimeSnapshot(true);
        break;
    }
    case WStype_TEXT:
    {
        String message;
        message.reserve(length);
        for (size_t i = 0; i < length; i++)
        {
            message += static_cast<char>(payload[i]);
        }
        if (message == "snapshot")
        {
            String snapshot;
            buildRealtimeSnapshot(snapshot);
            wsServer->sendTXT(clientId, snapshot);
        }
        break;
    }
    default:
        break;
    }
}

void WebInterface::buildRealtimeSnapshot(String &outJson)
{
    StaticJsonDocument<1024> doc;
    doc["type"] = "snapshot";
    doc["timestamp"] = millis();

    JsonObject status = doc.createNestedObject("status");
    status["smokeChamberTemp"] = smokerData.filteredSmokeChamberTemp;
    status["firePotTemp"] = smokerData.filteredFirePotTemp;
    status["operating"]["setpoint"] = smokerConfig.operating.setpoint;
    status["operating"]["smokesetpoint"] = smokerConfig.operating.smokesetpoint;
    status["operating"]["activeState"] = smokerConfig.operating.activeState;
    status["igniterMode"] = static_cast<int>(smokerData.igniter.mode);
    status["augerMode"] = static_cast<int>(smokerData.auger.mode);
    status["augerDutyCycle"] = smokerData.auger.dutyCycle;
    status["augerFrequency"] = smokerData.auger.frequency;
    status["fanMode"] = static_cast<int>(smokerData.fan.mode);
    status["fanDutyCycle"] = smokerData.fan.dutyCycle;
    status["fanFrequency"] = smokerData.fan.frequency;

    JsonObject buttons = doc.createNestedObject("buttons");
    buttons["btn_Startup"] = uiData.btn_Startup;
    buttons["btn_Auto"] = uiData.btn_Auto;
    buttons["btn_Shutdown"] = uiData.btn_Shutdown;
    buttons["btn_Manual"] = uiData.btn_Manual;

    serializeJson(doc, outJson);
}

void WebInterface::broadcastRealtimeSnapshot(bool forceBroadcast)
{
    if (!wsServer)
    {
        return;
    }

    const unsigned long now = millis();
    const unsigned long minPublishIntervalMs = 500;
    const unsigned long heartbeatIntervalMs = 5000;
    const bool heartbeatDue = (now - lastHeartbeatMs) >= heartbeatIntervalMs;

    if (!forceBroadcast && !heartbeatDue && (now - lastBroadcastMs) < minPublishIntervalMs)
    {
        return;
    }

    String payload;
    buildRealtimeSnapshot(payload);

    const bool changed = payload != lastSnapshotPayload;
    if (forceBroadcast || changed || heartbeatDue)
    {
        wsServer->broadcastTXT(payload);
        lastSnapshotPayload = payload;
        lastBroadcastMs = now;
        if (heartbeatDue)
        {
            lastHeartbeatMs = now;
        }
    }
}

void WebInterface::handleRoot()
{
    if (SPIFFS.exists("/index.html"))
    {
        File file = SPIFFS.open("/index.html", "r");
        server->streamFile(file, "text/html");
        file.close();
    }
    else
    {
        server->send(404, "text/plain", "index.html not found in SPIFFS");
    }
}

void WebInterface::handleGetStatus()
{
    StaticJsonDocument<512> doc;

    doc["smokeChamberTemp"] = smokerData.filteredSmokeChamberTemp;
    doc["firePotTemp"] = smokerData.filteredFirePotTemp;

    doc["operating"]["setpoint"] = smokerConfig.operating.setpoint;
    doc["operating"]["smokesetpoint"] = smokerConfig.operating.smokesetpoint;
    doc["operating"]["activeState"] = smokerConfig.operating.activeState;

    doc["igniterMode"] = static_cast<int>(smokerData.igniter.mode);
    doc["augerMode"] = static_cast<int>(smokerData.auger.mode);
    doc["augerDutyCycle"] = smokerData.auger.dutyCycle;
    doc["augerFrequency"] = smokerData.auger.frequency;
    doc["fanMode"] = static_cast<int>(smokerData.fan.mode);
    doc["fanDutyCycle"] = smokerData.fan.dutyCycle;
    doc["fanFrequency"] = smokerData.fan.frequency;

    String response;
    serializeJson(doc, response);
    server->send(200, "application/json", response);
}

void WebInterface::handleSetSetpoint()
{
    if (server->hasArg("plain"))
    {
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, server->arg("plain")) == DeserializationError::Ok)
        {
            if (doc.containsKey("setpoint"))
            {
                float newSetpoint = doc["setpoint"].as<float>();
                if (newSetpoint > 0)
                {
                    smokerConfig.operating.setpoint = newSetpoint;
                    SaveConfigToSPIFFS(smokerConfig);
                    server->send(200, "application/json", "{\"status\":\"ok\"}");
                    return;
                }
            }
        }
    }
    server->send(400, "application/json", "{\"status\":\"error\"}");
}

void WebInterface::handleSetSmokeSetpoint()
{
    if (server->hasArg("plain"))
    {
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, server->arg("plain")) == DeserializationError::Ok)
        {
            if (doc.containsKey("smokesetpoint"))
            {
                float newSmokeSetpoint = doc["smokesetpoint"].as<float>();
                if (newSmokeSetpoint >= 0)
                {
                    smokerConfig.operating.smokesetpoint = newSmokeSetpoint;
                    SaveConfigToSPIFFS(smokerConfig);
                    server->send(200, "application/json", "{\"status\":\"ok\"}");
                    return;
                }
            }
        }
    }
    server->send(400, "application/json", "{\"status\":\"error\"}");
}

void WebInterface::handleGetTunableParams()
{
    StaticJsonDocument<2048> doc;

    doc["minAutoRestartTemp"] = smokerConfig.tunable.minAutoRestartTemp;
    doc["minIdleTemp"] = smokerConfig.tunable.minIdleTemp;
    doc["firePotBurningTemp"] = smokerConfig.tunable.firePotBurningTemp;
    doc["startupFillTime"] = smokerConfig.tunable.startupFillTime;
    doc["igniterPreheatTime"] = smokerConfig.tunable.igniterPreheatTime;
    doc["stabilizeTime"] = smokerConfig.tunable.stabilizeTime;
    doc["augerFrequency_Auto"] = smokerConfig.tunable.augerFrequency;
    doc["fanfrequency_Auto"] = smokerConfig.tunable.fanFrequency;

    // Add auger transfer function
    JsonArray augerTransfer = doc.createNestedArray("augerTransferFunc");
    for (int i = 0; i < 11; ++i)
    {
        JsonArray point = augerTransfer.createNestedArray();
        point.add(smokerConfig.tunable.augerTransferFunc[i][0]);
        point.add(smokerConfig.tunable.augerTransferFunc[i][1]);
    }

    // Add fan transfer function
    JsonArray fanTransfer = doc.createNestedArray("fanTransferFunc");
    for (int i = 0; i < 11; ++i)
    {
        JsonArray point = fanTransfer.createNestedArray();
        point.add(smokerConfig.tunable.fanTransferFunc[i][0]);
        point.add(smokerConfig.tunable.fanTransferFunc[i][1]);
    }

    String response;
    serializeJson(doc, response);
    server->send(200, "application/json", response);
}

void WebInterface::handleSetTunableParams()
{
    if (server->hasArg("plain"))
    {
        StaticJsonDocument<2048> doc;
        if (deserializeJson(doc, server->arg("plain")) == DeserializationError::Ok)
        {
            if (doc.containsKey("minAutoRestartTemp"))
                smokerConfig.tunable.minAutoRestartTemp = doc["minAutoRestartTemp"];
            if (doc.containsKey("minIdleTemp"))
                smokerConfig.tunable.minIdleTemp = doc["minIdleTemp"];
            if (doc.containsKey("firePotBurningTemp"))
                smokerConfig.tunable.firePotBurningTemp = doc["firePotBurningTemp"];
            if (doc.containsKey("startupFillTime"))
                smokerConfig.tunable.startupFillTime = doc["startupFillTime"];
            if (doc.containsKey("igniterPreheatTime"))
                smokerConfig.tunable.igniterPreheatTime = doc["igniterPreheatTime"];
            if (doc.containsKey("stabilizeTime"))
                smokerConfig.tunable.stabilizeTime = doc["stabilizeTime"];
            if (doc.containsKey("augerFrequency_Auto"))
                smokerConfig.tunable.augerFrequency = doc["augerFrequency_Auto"];
            if (doc.containsKey("fanfrequency_Auto"))
                smokerConfig.tunable.fanFrequency = doc["fanfrequency_Auto"];

            // Update auger transfer function if provided
            if (doc.containsKey("augerTransferFunc") && doc["augerTransferFunc"].is<JsonArray>())
            {
                JsonArray augerTransfer = doc["augerTransferFunc"].as<JsonArray>();
                for (int i = 0; i < 11 && i < augerTransfer.size(); ++i)
                {
                    if (augerTransfer[i].is<JsonArray>())
                    {
                        JsonArray point = augerTransfer[i].as<JsonArray>();
                        smokerConfig.tunable.augerTransferFunc[i][0] = point[0];
                        smokerConfig.tunable.augerTransferFunc[i][1] = point[1];
                    }
                }
            }

            // Update fan transfer function if provided
            if (doc.containsKey("fanTransferFunc") && doc["fanTransferFunc"].is<JsonArray>())
            {
                JsonArray fanTransfer = doc["fanTransferFunc"].as<JsonArray>();
                for (int i = 0; i < 11 && i < fanTransfer.size(); ++i)
                {
                    if (fanTransfer[i].is<JsonArray>())
                    {
                        JsonArray point = fanTransfer[i].as<JsonArray>();
                        smokerConfig.tunable.fanTransferFunc[i][0] = point[0];
                        smokerConfig.tunable.fanTransferFunc[i][1] = point[1];
                    }
                }
            }

            SaveConfigToSPIFFS(smokerConfig);
            server->send(200, "application/json", "{\"status\":\"ok\"}");
            return;
        }
    }
    server->send(400, "application/json", "{\"status\":\"error\"}");
}

void WebInterface::handleGetRecipeState()
{
    StaticJsonDocument<2048> doc;

    doc["recipeStepIndex"] = smokerConfig.recipe.recipeStepIndex;
    doc["selectedRecipeIndex"] = smokerConfig.recipe.selectedRecipeIndex;

    JsonArray recipes = doc["recipes"].to<JsonArray>();
    for (int r = 0; r < MAX_RECIPES; ++r)
    {
        JsonObject recipe = recipes.createNestedObject();
        recipe["index"] = r;
        recipe["name"] = smokerConfig.recipe.recipeData[r].name;
        recipe["enabled"] = smokerConfig.recipe.recipeData[r].enabled;
        recipe["stepCount"] = smokerConfig.recipe.recipeData[r].stepCount;

        JsonArray steps = recipe["steps"].to<JsonArray>();
        for (int s = 0; s < smokerConfig.recipe.recipeData[r].stepCount && s < MAX_RECIPE_STEPS; ++s)
        {
            JsonObject step = steps.createNestedObject();
            step["name"] = smokerConfig.recipe.recipeData[r].steps[s].name;
            step["enabled"] = smokerConfig.recipe.recipeData[r].steps[s].enabled;
            step["startTempSetpoint"] = smokerConfig.recipe.recipeData[r].steps[s].startTempSetpoint;
            step["endTempSetpoint"] = smokerConfig.recipe.recipeData[r].steps[s].endTempSetpoint;
            step["startSmokeSetpoint"] = smokerConfig.recipe.recipeData[r].steps[s].startSmokeSetpoint;
            step["endSmokeSetpoint"] = smokerConfig.recipe.recipeData[r].steps[s].endSmokeSetpoint;
            step["stepDurationMs"] = smokerConfig.recipe.recipeData[r].steps[s].stepDurationMs;
            step["meatProbeExitTemp"] = smokerConfig.recipe.recipeData[r].steps[s].meatProbeExitTemp;
        }
    }

    String response;
    serializeJson(doc, response);
    server->send(200, "application/json", response);
}

void WebInterface::handleSetRecipeState()
{
    if (server->hasArg("plain"))
    {
        StaticJsonDocument<2048> doc;
        if (deserializeJson(doc, server->arg("plain")) == DeserializationError::Ok)
        {
            if (doc.containsKey("selectedRecipeIndex"))
            {
                smokerConfig.recipe.selectedRecipeIndex = doc["selectedRecipeIndex"];
            }
            if (doc.containsKey("recipeStepIndex"))
            {
                smokerConfig.recipe.recipeStepIndex = doc["recipeStepIndex"];
            }
            if (doc.containsKey("recipes"))
            {
                JsonArray recipes = doc["recipes"].as<JsonArray>();
                for (int r = 0; r < MAX_RECIPES && r < recipes.size(); ++r)
                {
                    JsonObject recipe = recipes[r];
                    strlcpy(smokerConfig.recipe.recipeData[r].name, recipe["name"] | "", sizeof(smokerConfig.recipe.recipeData[r].name));
                    smokerConfig.recipe.recipeData[r].enabled = recipe["enabled"];
                    smokerConfig.recipe.recipeData[r].stepCount = recipe["stepCount"];

                    if (recipe.containsKey("steps"))
                    {
                        JsonArray steps = recipe["steps"].as<JsonArray>();
                        for (int s = 0; s < MAX_RECIPE_STEPS && s < steps.size(); ++s)
                        {
                            JsonObject step = steps[s];
                            strlcpy(smokerConfig.recipe.recipeData[r].steps[s].name, step["name"] | "", sizeof(smokerConfig.recipe.recipeData[r].steps[s].name));
                            smokerConfig.recipe.recipeData[r].steps[s].enabled = step["enabled"];
                            smokerConfig.recipe.recipeData[r].steps[s].startTempSetpoint = step["startTempSetpoint"];
                            smokerConfig.recipe.recipeData[r].steps[s].endTempSetpoint = step["endTempSetpoint"];
                            smokerConfig.recipe.recipeData[r].steps[s].startSmokeSetpoint = step["startSmokeSetpoint"];
                            smokerConfig.recipe.recipeData[r].steps[s].endSmokeSetpoint = step["endSmokeSetpoint"];
                            smokerConfig.recipe.recipeData[r].steps[s].stepDurationMs = step["stepDurationMs"];
                            smokerConfig.recipe.recipeData[r].steps[s].meatProbeExitTemp = step["meatProbeExitTemp"];
                        }
                    }
                }
            }

            SaveConfigToSPIFFS(smokerConfig);
            server->send(200, "application/json", "{\"status\":\"ok\"}");
            return;
        }
    }
    server->send(400, "application/json", "{\"status\":\"error\"}");
}

void WebInterface::handleDownloadConfig()
{
    File file = SPIFFS.open("/smokerConfig.json", "r");
    if (!file)
    {
        server->send(404, "application/json", "{\"status\":\"file not found\"}");
        return;
    }

    server->sendHeader("Content-Disposition", "attachment; filename=\"smokerConfig.json\"");
    server->streamFile(file, "application/json");
    file.close();
}

void WebInterface::handleUploadConfig()
{
    if (server->hasArg("plain"))
    {
        StaticJsonDocument<4096> doc;
        if (deserializeJson(doc, server->arg("plain")) == DeserializationError::Ok)
        {
            if (doc.containsKey("operating") && doc.containsKey("tunable") && doc.containsKey("recipe"))
            {
                smokerConfig.operating.setpoint = doc["operating"]["setpoint"];
                smokerConfig.operating.smokesetpoint = doc["operating"]["smokesetpoint"];

                smokerConfig.tunable.minAutoRestartTemp = doc["tunable"]["minAutoRestartTemp"];
                smokerConfig.tunable.minIdleTemp = doc["tunable"]["minIdleTemp"];
                smokerConfig.tunable.firePotBurningTemp = doc["tunable"]["firePotBurningTemp"];
                smokerConfig.tunable.startupFillTime = doc["tunable"]["startupFillTime"];
                smokerConfig.tunable.igniterPreheatTime = doc["tunable"]["igniterPreheatTime"];
                smokerConfig.tunable.stabilizeTime = doc["tunable"]["stabilizeTime"];

                smokerConfig.recipe.recipeStepIndex = doc["recipe"]["recipeStepIndex"];
                smokerConfig.recipe.selectedRecipeIndex = doc["recipe"]["selectedRecipeIndex"];

                if (doc["recipe"].containsKey("recipeData"))
                {
                    JsonArray recipes = doc["recipe"]["recipeData"].as<JsonArray>();
                    for (int r = 0; r < MAX_RECIPES && r < recipes.size(); ++r)
                    {
                        JsonObject recipe = recipes[r];
                        strlcpy(smokerConfig.recipe.recipeData[r].name, recipe["name"] | "", sizeof(smokerConfig.recipe.recipeData[r].name));
                        smokerConfig.recipe.recipeData[r].stepCount = recipe["stepCount"];
                        smokerConfig.recipe.recipeData[r].enabled = recipe["enabled"];

                        if (recipe.containsKey("steps"))
                        {
                            JsonArray steps = recipe["steps"].as<JsonArray>();
                            for (int s = 0; s < MAX_RECIPE_STEPS && s < steps.size(); ++s)
                            {
                                JsonObject step = steps[s];
                                strlcpy(smokerConfig.recipe.recipeData[r].steps[s].name, step["name"] | "", sizeof(smokerConfig.recipe.recipeData[r].steps[s].name));
                                smokerConfig.recipe.recipeData[r].steps[s].enabled = step["enabled"];
                                smokerConfig.recipe.recipeData[r].steps[s].startTempSetpoint = step["startTempSetpoint"];
                                smokerConfig.recipe.recipeData[r].steps[s].endTempSetpoint = step["endTempSetpoint"];
                                smokerConfig.recipe.recipeData[r].steps[s].startSmokeSetpoint = step["startSmokeSetpoint"];
                                smokerConfig.recipe.recipeData[r].steps[s].endSmokeSetpoint = step["endSmokeSetpoint"];
                                smokerConfig.recipe.recipeData[r].steps[s].stepDurationMs = step["stepDurationMs"];
                                smokerConfig.recipe.recipeData[r].steps[s].meatProbeExitTemp = step["meatProbeExitTemp"];
                            }
                        }
                    }
                }

                SaveConfigToSPIFFS(smokerConfig);
                server->send(200, "application/json", "{\"status\":\"ok\"}");
                return;
            }
        }
    }
    server->send(400, "application/json", "{\"status\":\"error\"}");
}

void WebInterface::handleNotFound()
{
    server->send(404, "text/plain", "Not Found");
}

void WebInterface::handleSPIFFSList()
{
    SpiffsHandlers::HandleList(*server);
}

void WebInterface::handleSPIFFSDownload()
{
    SpiffsHandlers::HandleDownload(*server);
}

void WebInterface::handleSPIFFSUpload()
{
    SpiffsHandlers::HandleUpload(*server);
}

void WebInterface::handleSPIFFSDelete()
{
    SpiffsHandlers::HandleDelete(*server);
}

void WebInterface::handleGetButtons()
{
    StaticJsonDocument<256> doc;

    doc["btn_Startup"] = uiData.btn_Startup;
    doc["btn_Auto"] = uiData.btn_Auto;
    doc["btn_Shutdown"] = uiData.btn_Shutdown;
    doc["btn_Manual"] = uiData.btn_Manual;

    String response;
    serializeJson(doc, response);
    server->send(200, "application/json", response);
}

void WebInterface::handleSetButton()
{
    if (server->hasArg("plain"))
    {
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, server->arg("plain")) == DeserializationError::Ok)
        {
            if (doc.containsKey("button") && doc.containsKey("state"))
            {
                String buttonName = doc["button"];
                bool state = doc["state"];

                if (buttonName == "btn_Startup")
                {
                    uiData.btn_Startup = state;
                }
                else if (buttonName == "btn_Auto")
                {
                    uiData.btn_Auto = state;
                }
                else if (buttonName == "btn_Shutdown")
                {
                    uiData.btn_Shutdown = state;
                }
                else if (buttonName == "btn_Manual")
                {
                    uiData.btn_Manual = state;
                }

                server->send(200, "application/json", "{\"status\":\"ok\"}");
                return;
            }
        }
    }
    server->send(400, "application/json", "{\"status\":\"error\"}");
}

void WebInterface::handleGetActuatorValues()
{
    StaticJsonDocument<256> doc;

    doc["augerDutyCycle"] = smokerData.auger.dutyCycle;
    doc["augerFrequency"] = smokerData.auger.frequency;
    doc["fanDutyCycle"] = smokerData.fan.dutyCycle;
    doc["fanFrequency"] = smokerData.fan.frequency;

    String response;
    serializeJson(doc, response);
    server->send(200, "application/json", response);
}

void WebInterface::handleSetActuatorValues()
{
    if (server->hasArg("plain"))
    {
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, server->arg("plain")) == DeserializationError::Ok)
        {
            if (doc.containsKey("augerDutyCycle"))
                smokerData.auger.dutyCycle = doc["augerDutyCycle"];
            if (doc.containsKey("augerFrequency"))
                smokerData.auger.frequency = doc["augerFrequency"];
            if (doc.containsKey("fanDutyCycle"))
                smokerData.fan.dutyCycle = doc["fanDutyCycle"];
            if (doc.containsKey("fanFrequency"))
                smokerData.fan.frequency = doc["fanFrequency"];

            server->send(200, "application/json", "{\"status\":\"ok\"}");
            return;
        }
    }
    server->send(400, "application/json", "{\"status\":\"error\"}");
}

void WebInterface::handleReboot()
{
    server->send(200, "application/json", "{\"status\":\"rebooting\"}");
    delay(100);
    ESP.restart();
}

void WebInterface::handleGetLoggingConfig()
{
    StaticJsonDocument<768> doc;
    LogConfig config = DataLogger::getConfig();

    doc["enabled"] = config.enabled;
    doc["logIntervalMs"] = config.logIntervalMs;
    doc["maxLogFiles"] = config.maxLogFiles;
    doc["maxLogFileSizeBytes"] = config.maxLogFileSizeBytes;
    doc["smokeChamberTempThreshold"] = config.smokeChamberTempThreshold;
    doc["firePotTempThreshold"] = config.firePotTempThreshold;
    doc["setpointThreshold"] = config.setpointThreshold;
    doc["smokeSetpointThreshold"] = config.smokeSetpointThreshold;
    doc["igniterModeThreshold"] = config.igniterModeThreshold;
    doc["augerModeThreshold"] = config.augerModeThreshold;
    doc["augerDutyCycleThreshold"] = config.augerDutyCycleThreshold;
    doc["augerFrequencyThreshold"] = config.augerFrequencyThreshold;
    doc["fanModeThreshold"] = config.fanModeThreshold;
    doc["fanDutyCycleThreshold"] = config.fanDutyCycleThreshold;
    doc["fanFrequencyThreshold"] = config.fanFrequencyThreshold;
    doc["activeLogFile"] = DataLogger::getActiveLogFile();

    String response;
    serializeJson(doc, response);
    server->send(200, "application/json", response);
}

void WebInterface::handleSetLoggingConfig()
{
    if (server->hasArg("plain"))
    {
        StaticJsonDocument<768> doc;
        if (deserializeJson(doc, server->arg("plain")) == DeserializationError::Ok)
        {
            LogConfig config = DataLogger::getConfig();

            if (doc.containsKey("enabled"))
                config.enabled = doc["enabled"];
            if (doc.containsKey("logIntervalMs"))
                config.logIntervalMs = doc["logIntervalMs"];
            if (doc.containsKey("maxLogFiles"))
                config.maxLogFiles = doc["maxLogFiles"];
            if (doc.containsKey("maxLogFileSizeBytes"))
                config.maxLogFileSizeBytes = doc["maxLogFileSizeBytes"];
            if (doc.containsKey("smokeChamberTempThreshold"))
                config.smokeChamberTempThreshold = doc["smokeChamberTempThreshold"];
            if (doc.containsKey("firePotTempThreshold"))
                config.firePotTempThreshold = doc["firePotTempThreshold"];
            if (doc.containsKey("setpointThreshold"))
                config.setpointThreshold = doc["setpointThreshold"];
            if (doc.containsKey("smokeSetpointThreshold"))
                config.smokeSetpointThreshold = doc["smokeSetpointThreshold"];
            if (doc.containsKey("igniterModeThreshold"))
                config.igniterModeThreshold = doc["igniterModeThreshold"];
            if (doc.containsKey("augerModeThreshold"))
                config.augerModeThreshold = doc["augerModeThreshold"];
            if (doc.containsKey("augerDutyCycleThreshold"))
                config.augerDutyCycleThreshold = doc["augerDutyCycleThreshold"];
            if (doc.containsKey("augerFrequencyThreshold"))
                config.augerFrequencyThreshold = doc["augerFrequencyThreshold"];
            if (doc.containsKey("fanModeThreshold"))
                config.fanModeThreshold = doc["fanModeThreshold"];
            if (doc.containsKey("fanDutyCycleThreshold"))
                config.fanDutyCycleThreshold = doc["fanDutyCycleThreshold"];
            if (doc.containsKey("fanFrequencyThreshold"))
                config.fanFrequencyThreshold = doc["fanFrequencyThreshold"];

            DataLogger::setConfig(config);

            // Update persistent config
            smokerConfig.logging.enabled = config.enabled;
            smokerConfig.logging.logIntervalMs = config.logIntervalMs;
            smokerConfig.logging.maxLogFiles = config.maxLogFiles;
            smokerConfig.logging.maxLogFileSizeBytes = config.maxLogFileSizeBytes;
            smokerConfig.logging.smokeChamberTempThreshold = config.smokeChamberTempThreshold;
            smokerConfig.logging.firePotTempThreshold = config.firePotTempThreshold;
            smokerConfig.logging.setpointThreshold = config.setpointThreshold;
            smokerConfig.logging.smokeSetpointThreshold = config.smokeSetpointThreshold;
            smokerConfig.logging.igniterModeThreshold = config.igniterModeThreshold;
            smokerConfig.logging.augerModeThreshold = config.augerModeThreshold;
            smokerConfig.logging.augerDutyCycleThreshold = config.augerDutyCycleThreshold;
            smokerConfig.logging.augerFrequencyThreshold = config.augerFrequencyThreshold;
            smokerConfig.logging.fanModeThreshold = config.fanModeThreshold;
            smokerConfig.logging.fanDutyCycleThreshold = config.fanDutyCycleThreshold;
            smokerConfig.logging.fanFrequencyThreshold = config.fanFrequencyThreshold;
            SaveConfigToSPIFFS(smokerConfig);

            server->send(200, "application/json", "{\"status\":\"ok\"}");
            return;
        }
    }
    server->send(400, "application/json", "{\"status\":\"error\"}");
}

void WebInterface::handleClearLogs()
{
    DataLogger::clearAllLogs();
    server->send(200, "application/json", "{\"status\":\"ok\"}");
}

void WebInterface::handleDownloadLog()
{
    if (server->hasArg("file"))
    {
        String filename = server->arg("file");
        String filepath = "/logs/" + filename;

        if (SPIFFS.exists(filepath))
        {
            File file = SPIFFS.open(filepath, "r");
            if (file)
            {
                server->streamFile(file, "text/csv");
                file.close();
                return;
            }
        }
        server->send(404, "text/plain", "File not found");
    }
    else
    {
        // Download active log file if no file specified
        String activeFile = DataLogger::getActiveLogFile();
        if (SPIFFS.exists(activeFile))
        {
            File file = SPIFFS.open(activeFile, "r");
            if (file)
            {
                server->streamFile(file, "text/csv");
                file.close();
                return;
            }
        }
        server->send(404, "text/plain", "No active log file");
    }
}

void WebInterface::handleGetLogData()
{
    String activeFile = DataLogger::getActiveLogFile();
    if (!SPIFFS.exists(activeFile))
    {
        server->send(404, "application/json", "{\"error\":\"No log file found\"}");
        return;
    }

    File file = SPIFFS.open(activeFile, "r");
    if (!file)
    {
        server->send(500, "application/json", "{\"error\":\"Could not open log file\"}");
        return;
    }

    // Get parameters: duration in minutes (default 60), fields to include
    int durationMinutes = 60;
    if (server->hasArg("duration"))
    {
        durationMinutes = server->arg("duration").toInt();
    }

    unsigned long requestedMs = 0;
    if (durationMinutes > 0)
    {
        requestedMs = (unsigned long)durationMinutes * 60000UL;
    }

    // Pass 1: determine latest synthetic timestamp while accounting for resets.
    // Sparse rows are supported by preserving empty CSV tokens.
    if (file.available())
    {
        file.readStringUntil('\n');
    }

    unsigned long latestSyntheticTs = 0;
    unsigned long previousRawTs = 0;
    unsigned long rolloverOffset = 0;
    bool hasPreviousRawTs = false;

    while (file.available())
    {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0)
            continue;

        int commaPos = line.indexOf(',');
        if (commaPos <= 0)
            continue;

        unsigned long rawTs = line.substring(0, commaPos).toInt();
        if (hasPreviousRawTs && rawTs < previousRawTs)
        {
            rolloverOffset += previousRawTs;
        }

        unsigned long syntheticTs = rawTs + rolloverOffset;
        latestSyntheticTs = syntheticTs;
        previousRawTs = rawTs;
        hasPreviousRawTs = true;
    }

    unsigned long cutoffTime = 0;
    if (requestedMs > 0 && latestSyntheticTs > requestedMs)
    {
        cutoffTime = latestSyntheticTs - requestedMs;
    }

    file.seek(0, SeekSet);

    String response = "{\"data\":[";
    bool firstEntry = true;
    String lastFieldValues[12];
    bool hasLastFieldValues = false;
    previousRawTs = 0;
    rolloverOffset = 0;
    hasPreviousRawTs = false;

    if (file.available())
    {
        file.readStringUntil('\n');
    }

    while (file.available())
    {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0)
            continue;

        int commaPos = line.indexOf(',');
        if (commaPos <= 0)
            continue;

        unsigned long rawTs = line.substring(0, commaPos).toInt();
        if (hasPreviousRawTs && rawTs < previousRawTs)
        {
            rolloverOffset += previousRawTs;
        }
        unsigned long syntheticTs = rawTs + rolloverOffset;
        previousRawTs = rawTs;
        hasPreviousRawTs = true;

        if (syntheticTs < cutoffTime)
            continue;

        if (!firstEntry)
            response += ",";
        firstEntry = false;

        response += "{\"timestamp\":" + String(syntheticTs);

        String remainder = line.substring(commaPos + 1);
        const char *fieldNames[] = {"smokeChamberTemp", "firePotTemp", "setpoint", "smokeSetpoint",
                                    "activeState", "igniterMode", "augerMode", "augerDutyCycle",
                                    "augerFrequency", "fanMode", "fanDutyCycle", "fanFrequency"};

        for (int i = 0; i < 12; i++)
        {
            String value;
            if (remainder.length() == 0)
            {
                value = "";
            }
            else
            {
                int fieldSep = remainder.indexOf(',');
                if (fieldSep >= 0)
                {
                    value = remainder.substring(0, fieldSep);
                    remainder = remainder.substring(fieldSep + 1);
                }
                else
                {
                    value = remainder;
                    remainder = "";
                }
            }

            value.trim();
            if (value.length() == 0)
            {
                if (hasLastFieldValues)
                {
                    value = lastFieldValues[i];
                }
                else
                {
                    value = (i == 4) ? String("") : String("0");
                }
            }
            lastFieldValues[i] = value;

            if (i == 4)
            {
                response += ",\"" + String(fieldNames[i]) + "\":\"" + value + "\"";
            }
            else
            {
                response += ",\"" + String(fieldNames[i]) + "\":" + value;
            }
        }

        hasLastFieldValues = true;
        response += "}";
    }

    response += "]}";
    file.close();

    server->send(200, "application/json", response);
}
