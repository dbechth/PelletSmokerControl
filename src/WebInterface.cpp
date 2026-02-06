#include "WebInterface.h"
#include <SPIFFS.h>
#include <functional>

WebInterface::WebInterface(uint16_t port) : server(new WebServer(port)), ownsServer(true) {}

WebInterface::WebInterface(WebServer &existingServer) : server(&existingServer), ownsServer(false) {}

WebInterface::~WebInterface() {
    if (ownsServer && server) {
        server->stop();
        delete server;
        server = nullptr;
    }
}

void WebInterface::begin() {
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
    server->on("/api/config/download", HTTP_GET, std::bind(&WebInterface::handleDownloadConfig, this));
    server->on("/api/config/upload", HTTP_POST, std::bind(&WebInterface::handleUploadConfig, this));
    server->on("/api/spiffs/list", HTTP_GET, std::bind(&WebInterface::handleSPIFFSList, this));
    server->on("/api/spiffs/download", HTTP_GET, std::bind(&WebInterface::handleSPIFFSDownload, this));
    server->on("/api/spiffs/upload", HTTP_POST, std::bind(&WebInterface::handleSPIFFSUpload, this));
    server->on("/api/spiffs/delete", HTTP_POST, std::bind(&WebInterface::handleSPIFFSDelete, this));
    server->onNotFound(std::bind(&WebInterface::handleNotFound, this));
    
    server->begin();
    Serial.println("Web server started");
}

void WebInterface::handleClient() {
    server->handleClient();
}

void WebInterface::stop() {
    server->stop();
}

void WebInterface::handleRoot() {
    if (SPIFFS.exists("/index.html")) {
        File file = SPIFFS.open("/index.html", "r");
        server->streamFile(file, "text/html");
        file.close();
    } else {
        server->send(404, "text/plain", "index.html not found in SPIFFS");
    }
}

void WebInterface::handleGetStatus() {
    StaticJsonDocument<512> doc;
    
    doc["smokeChamberTemp"] = smokerData.filteredSmokeChamberTemp;
    doc["firePotTemp"] = smokerData.filteredFirePotTemp;
    
    doc["operating"]["setpoint"] = smokerConfig.operating.setpoint;
    doc["operating"]["smokesetpoint"] = smokerConfig.operating.smokesetpoint;
    doc["operating"]["activeState"] = smokerConfig.operating.activeState;
    
    doc["igniterMode"] = static_cast<int>(smokerData.igniter.mode);
    doc["augerMode"] = static_cast<int>(smokerData.auger.mode);
    doc["augerDutyCycle"] = smokerData.auger.dutyCycle_Manual;
    doc["fanMode"] = static_cast<int>(smokerData.fan.mode);
    doc["fanDutyCycle"] = smokerData.fan.dutyCycle_Manual;
    
    String response;
    serializeJson(doc, response);
    server->send(200, "application/json", response);
}

void WebInterface::handleSetSetpoint() {
    if (server->hasArg("plain")) {
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, server->arg("plain")) == DeserializationError::Ok) {
            if (doc.containsKey("setpoint")) {
                float newSetpoint = doc["setpoint"].as<float>();
                if (newSetpoint > 0) {
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

void WebInterface::handleSetSmokeSetpoint() {
    if (server->hasArg("plain")) {
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, server->arg("plain")) == DeserializationError::Ok) {
            if (doc.containsKey("smokesetpoint")) {
                float newSmokeSetpoint = doc["smokesetpoint"].as<float>();
                if (newSmokeSetpoint >= 0) {
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

void WebInterface::handleGetTunableParams() {
    StaticJsonDocument<512> doc;
    
    doc["minAutoRestartTemp"] = smokerConfig.tunable.minAutoRestartTemp;
    doc["minIdleTemp"] = smokerConfig.tunable.minIdleTemp;
    doc["firePotBurningTemp"] = smokerConfig.tunable.firePotBurningTemp;
    doc["startupFillTime"] = smokerConfig.tunable.startupFillTime;
    doc["igniterPreheatTime"] = smokerConfig.tunable.igniterPreheatTime;
    doc["stabilizeTime"] = smokerConfig.tunable.stabilizeTime;
    
    String response;
    serializeJson(doc, response);
    server->send(200, "application/json", response);
}

void WebInterface::handleSetTunableParams() {
    if (server->hasArg("plain")) {
        StaticJsonDocument<512> doc;
        if (deserializeJson(doc, server->arg("plain")) == DeserializationError::Ok) {
            if (doc.containsKey("minAutoRestartTemp")) smokerConfig.tunable.minAutoRestartTemp = doc["minAutoRestartTemp"];
            if (doc.containsKey("minIdleTemp")) smokerConfig.tunable.minIdleTemp = doc["minIdleTemp"];
            if (doc.containsKey("firePotBurningTemp")) smokerConfig.tunable.firePotBurningTemp = doc["firePotBurningTemp"];
            if (doc.containsKey("startupFillTime")) smokerConfig.tunable.startupFillTime = doc["startupFillTime"];
            if (doc.containsKey("igniterPreheatTime")) smokerConfig.tunable.igniterPreheatTime = doc["igniterPreheatTime"];
            if (doc.containsKey("stabilizeTime")) smokerConfig.tunable.stabilizeTime = doc["stabilizeTime"];
            
            SaveConfigToSPIFFS(smokerConfig);
            server->send(200, "application/json", "{\"status\":\"ok\"}");
            return;
        }
    }
    server->send(400, "application/json", "{\"status\":\"error\"}");
}

void WebInterface::handleGetRecipeState() {
    StaticJsonDocument<2048> doc;
    
    doc["recipeStepIndex"] = smokerConfig.recipe.recipeStepIndex;
    doc["selectedRecipeIndex"] = smokerConfig.recipe.selectedRecipeIndex;
    
    JsonArray recipes = doc["recipes"].to<JsonArray>();
    for (int r = 0; r < MAX_RECIPES; ++r) {
        JsonObject recipe = recipes.createNestedObject();
        recipe["index"] = r;
        recipe["name"] = smokerConfig.recipe.recipeData[r].name;
        recipe["enabled"] = smokerConfig.recipe.recipeData[r].enabled;
        recipe["stepCount"] = smokerConfig.recipe.recipeData[r].stepCount;
        
        JsonArray steps = recipe["steps"].to<JsonArray>();
        for (int s = 0; s < smokerConfig.recipe.recipeData[r].stepCount && s < MAX_RECIPE_STEPS; ++s) {
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

void WebInterface::handleSetRecipeState() {
    if (server->hasArg("plain")) {
        StaticJsonDocument<2048> doc;
        if (deserializeJson(doc, server->arg("plain")) == DeserializationError::Ok) {
            if (doc.containsKey("selectedRecipeIndex")) {
                smokerConfig.recipe.selectedRecipeIndex = doc["selectedRecipeIndex"];
            }
            if (doc.containsKey("recipeStepIndex")) {
                smokerConfig.recipe.recipeStepIndex = doc["recipeStepIndex"];
            }
            if (doc.containsKey("recipes")) {
                JsonArray recipes = doc["recipes"].as<JsonArray>();
                for (int r = 0; r < MAX_RECIPES && r < recipes.size(); ++r) {
                    JsonObject recipe = recipes[r];
                    strlcpy(smokerConfig.recipe.recipeData[r].name, recipe["name"] | "", sizeof(smokerConfig.recipe.recipeData[r].name));
                    smokerConfig.recipe.recipeData[r].enabled = recipe["enabled"];
                    smokerConfig.recipe.recipeData[r].stepCount = recipe["stepCount"];
                    
                    if (recipe.containsKey("steps")) {
                        JsonArray steps = recipe["steps"].as<JsonArray>();
                        for (int s = 0; s < MAX_RECIPE_STEPS && s < steps.size(); ++s) {
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

void WebInterface::handleDownloadConfig() {
    File file = SPIFFS.open("/smokerConfig.json", "r");
    if (!file) {
        server->send(404, "application/json", "{\"status\":\"file not found\"}");
        return;
    }
    
    server->sendHeader("Content-Disposition", "attachment; filename=\"smokerConfig.json\"");
    server->streamFile(file, "application/json");
    file.close();
}

void WebInterface::handleUploadConfig() {
    if (server->hasArg("plain")) {
        StaticJsonDocument<4096> doc;
        if (deserializeJson(doc, server->arg("plain")) == DeserializationError::Ok) {
            if (doc.containsKey("operating") && doc.containsKey("tunable") && doc.containsKey("recipe")) {
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
                
                if (doc["recipe"].containsKey("recipeData")) {
                    JsonArray recipes = doc["recipe"]["recipeData"].as<JsonArray>();
                    for (int r = 0; r < MAX_RECIPES && r < recipes.size(); ++r) {
                        JsonObject recipe = recipes[r];
                        strlcpy(smokerConfig.recipe.recipeData[r].name, recipe["name"] | "", sizeof(smokerConfig.recipe.recipeData[r].name));
                        smokerConfig.recipe.recipeData[r].stepCount = recipe["stepCount"];
                        smokerConfig.recipe.recipeData[r].enabled = recipe["enabled"];
                        
                        if (recipe.containsKey("steps")) {
                            JsonArray steps = recipe["steps"].as<JsonArray>();
                            for (int s = 0; s < MAX_RECIPE_STEPS && s < steps.size(); ++s) {
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

void WebInterface::handleNotFound() {
    server->send(404, "text/plain", "Not Found");
}

void WebInterface::handleSPIFFSList() {
    StaticJsonDocument<1024> doc;
    JsonArray files = doc.createNestedArray("files");

    File root = SPIFFS.open("/");
    if (!root) {
        server->send(500, "application/json", "{\"status\":\"cannot open root\"}");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        JsonObject f = files.createNestedObject();
        f["name"] = String(file.name());
        f["size"] = file.size();
        file = root.openNextFile();
    }
    root.close();

    String resp;
    serializeJson(doc, resp);
    server->send(200, "application/json", resp);
}

void WebInterface::handleSPIFFSDownload() {
    String path = server->arg("path");
    if (path.length() == 0) {
        server->send(400, "application/json", "{\"status\":\"missing path\"}");
        return;
    }
    if (!path.startsWith("/")) path = "/" + path;

    if (!SPIFFS.exists(path)) {
        server->send(404, "application/json", "{\"status\":\"file not found\"}");
        return;
    }

    File file = SPIFFS.open(path, "r");
    if (!file) {
            server->send(500, "application/json", "{\"status\":\"unable to open file\"}");
        return;
    }

    String filename = path;
    filename.remove(0, filename.lastIndexOf('/') + 1);
    server->sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
    server->streamFile(file, "application/octet-stream");
    file.close();
}

void WebInterface::handleSPIFFSUpload() {
    String path = server->arg("path");
    if (path.length() == 0) {
        server->send(400, "application/json", "{\"status\":\"missing path\"}");
        return;
    }
    if (!path.startsWith("/")) path = "/" + path;

    if (!server->hasArg("plain")) {
        server->send(400, "application/json", "{\"status\":\"missing body\"}");
        return;
    }

    String body = server->arg("plain");
    File file = SPIFFS.open(path, "w");
    if (!file) {
        server->send(500, "application/json", "{\"status\":\"cannot open file for writing\"}");
        return;
    }
    file.write((const uint8_t*)body.c_str(), body.length());
    file.close();

    server->send(200, "application/json", "{\"status\":\"ok\"}");
}

void WebInterface::handleSPIFFSDelete() {
    String path = server->arg("path");
    if (path.length() == 0) {
        server->send(400, "application/json", "{\"status\":\"missing path\"}");
        return;
    }
    if (!path.startsWith("/")) path = "/" + path;

    if (!SPIFFS.exists(path)) {
        server->send(404, "application/json", "{\"status\":\"file not found\"}");
        return;
    }

    if (SPIFFS.remove(path)) {
        server->send(200, "application/json", "{\"status\":\"deleted\"}");
    } else {
        server->send(500, "application/json", "{\"status\":\"delete failed\"}");
    }
}

void WebInterface::handleGetButtons() {
    StaticJsonDocument<256> doc;
    
    doc["btn_Startup"] = uiData.btn_Startup;
    doc["btn_Auto"] = uiData.btn_Auto;
    doc["btn_Shutdown"] = uiData.btn_Shutdown;
    doc["btn_Manual"] = uiData.btn_Manual;
    
    String response;
    serializeJson(doc, response);
    server->send(200, "application/json", response);
}

void WebInterface::handleSetButton() {
    if (server->hasArg("plain")) {
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, server->arg("plain")) == DeserializationError::Ok) {
            if (doc.containsKey("button") && doc.containsKey("state")) {
                String buttonName = doc["button"];
                bool state = doc["state"];
                
                if (buttonName == "btn_Startup") {
                    uiData.btn_Startup = state;
                } else if (buttonName == "btn_Auto") {
                    uiData.btn_Auto = state;
                } else if (buttonName == "btn_Shutdown") {
                    uiData.btn_Shutdown = state;
                } else if (buttonName == "btn_Manual") {
                    uiData.btn_Manual = state;
                }
                
                server->send(200, "application/json", "{\"status\":\"ok\"}");
                return;
            }
        }
    }
    server->send(400, "application/json", "{\"status\":\"error\"}");
}
