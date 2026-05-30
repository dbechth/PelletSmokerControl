#include "SPIFFSConfig.h"

#include <ArduinoJson.h>
#include <SPIFFS.h>

#include "SmokerControl.h"

namespace
{
    const char *CONFIG_FILE = "/smokerConfig.json";
}

bool SaveConfigToSPIFFS(const SmokerConfig &config)
{
    JsonDocument doc;

    doc["operating"]["setpoint"] = config.operating.setpoint;
    doc["operating"]["smokesetpoint"] = config.operating.smokesetpoint;

    doc["tunable"]["minAutoRestartTemp"] = config.tunable.minAutoRestartTemp;
    doc["tunable"]["minIdleTemp"] = config.tunable.minIdleTemp;
    doc["tunable"]["firePotBurningTemp"] = config.tunable.firePotBurningTemp;
    doc["tunable"]["startupFillTime"] = config.tunable.startupFillTime;
    doc["tunable"]["igniterPreheatTime"] = config.tunable.igniterPreheatTime;
    doc["tunable"]["stabilizeTime"] = config.tunable.stabilizeTime;

    JsonArray augerTransfer = doc["tunable"]["augerTransferFunc"].to<JsonArray>();
    for (int i = 0; i < 11; ++i)
    {
        JsonArray point = augerTransfer.add<JsonArray>();
        point.add(config.tunable.augerTransferFunc[i][0]);
        point.add(config.tunable.augerTransferFunc[i][1]);
    }

    JsonArray fanTransfer = doc["tunable"]["fanTransferFunc"].to<JsonArray>();
    for (int i = 0; i < 11; ++i)
    {
        JsonArray point = fanTransfer.add<JsonArray>();
        point.add(config.tunable.fanTransferFunc[i][0]);
        point.add(config.tunable.fanTransferFunc[i][1]);
    }

    doc["tunable"]["augerFrequency_Auto"] = config.tunable.augerFrequency;
    doc["tunable"]["fanfrequency_Auto"] = config.tunable.fanFrequency;

    doc["recipe"]["recipeStepIndex"] = config.recipe.recipeStepIndex;
    doc["recipe"]["selectedRecipeIndex"] = config.recipe.selectedRecipeIndex;

    JsonArray recipes = doc["recipe"]["recipeData"].to<JsonArray>();
    for (int r = 0; r < MAX_RECIPES; ++r)
    {
        JsonObject recipe = recipes.add<JsonObject>();
        recipe["name"] = config.recipe.recipeData[r].name;
        recipe["stepCount"] = config.recipe.recipeData[r].stepCount;
        recipe["enabled"] = config.recipe.recipeData[r].enabled;

        JsonArray steps = recipe["steps"].to<JsonArray>();
        for (int s = 0; s < MAX_RECIPE_STEPS; ++s)
        {
            JsonObject step = steps.add<JsonObject>();
            step["name"] = config.recipe.recipeData[r].steps[s].name;
            step["enabled"] = config.recipe.recipeData[r].steps[s].enabled;
            step["startTempSetpoint"] = config.recipe.recipeData[r].steps[s].startTempSetpoint;
            step["endTempSetpoint"] = config.recipe.recipeData[r].steps[s].endTempSetpoint;
            step["startSmokeSetpoint"] = config.recipe.recipeData[r].steps[s].startSmokeSetpoint;
            step["endSmokeSetpoint"] = config.recipe.recipeData[r].steps[s].endSmokeSetpoint;
            step["stepDurationMs"] = config.recipe.recipeData[r].steps[s].stepDurationMs;
            step["meatProbeExitTemp"] = config.recipe.recipeData[r].steps[s].meatProbeExitTemp;
        }
    }

    doc["logging"]["enabled"] = config.logging.enabled;
    doc["logging"]["logIntervalMs"] = config.logging.logIntervalMs;
    doc["logging"]["maxLogFiles"] = config.logging.maxLogFiles;
    doc["logging"]["maxLogFileSizeBytes"] = config.logging.maxLogFileSizeBytes;
    doc["logging"]["smokeChamberTempThreshold"] = config.logging.smokeChamberTempThreshold;
    doc["logging"]["firePotTempThreshold"] = config.logging.firePotTempThreshold;
    doc["logging"]["setpointThreshold"] = config.logging.setpointThreshold;
    doc["logging"]["smokeSetpointThreshold"] = config.logging.smokeSetpointThreshold;
    doc["logging"]["igniterModeThreshold"] = config.logging.igniterModeThreshold;
    doc["logging"]["augerModeThreshold"] = config.logging.augerModeThreshold;
    doc["logging"]["augerDutyCycleThreshold"] = config.logging.augerDutyCycleThreshold;
    doc["logging"]["augerFrequencyThreshold"] = config.logging.augerFrequencyThreshold;
    doc["logging"]["fanModeThreshold"] = config.logging.fanModeThreshold;
    doc["logging"]["fanDutyCycleThreshold"] = config.logging.fanDutyCycleThreshold;
    doc["logging"]["fanFrequencyThreshold"] = config.logging.fanFrequencyThreshold;

    File file = SPIFFS.open(CONFIG_FILE, "w");
    if (!file)
    {
        Serial.println("Failed to open config file for writing");
        return false;
    }

    if (serializeJson(doc, file) == 0)
    {
        Serial.println("Failed to write config to file");
        file.close();
        return false;
    }

    file.close();
    return true;
}

bool LoadConfigFromSPIFFS(SmokerConfig &config)
{
    if (!SPIFFS.exists(CONFIG_FILE))
    {
        Serial.println("Config file does not exist");
        return false;
    }

    File file = SPIFFS.open(CONFIG_FILE, "r");
    if (!file)
    {
        Serial.println("Failed to open config file for reading");
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error)
    {
        Serial.print("Config file is corrupt: ");
        Serial.println(error.c_str());
        return false;
    }

    if (!doc["recipe"]["recipeData"].is<JsonArray>())
    {
        Serial.println("Config file missing recipeData");
        return false;
    }

    config.operating.setpoint = doc["operating"]["setpoint"];
    config.operating.smokesetpoint = doc["operating"]["smokesetpoint"];

    config.tunable.minAutoRestartTemp = doc["tunable"]["minAutoRestartTemp"];
    config.tunable.minIdleTemp = doc["tunable"]["minIdleTemp"];
    config.tunable.firePotBurningTemp = doc["tunable"]["firePotBurningTemp"];
    config.tunable.startupFillTime = doc["tunable"]["startupFillTime"];
    config.tunable.igniterPreheatTime = doc["tunable"]["igniterPreheatTime"];
    config.tunable.stabilizeTime = doc["tunable"]["stabilizeTime"];

    config.tunable.augerFrequency = doc["tunable"]["augerFrequency_Auto"] | 1.0f;
    config.tunable.fanFrequency = doc["tunable"]["fanfrequency_Auto"] | 1.0f;

    if (doc["tunable"]["augerTransferFunc"].is<JsonArray>())
    {
        JsonArray augerTransfer = doc["tunable"]["augerTransferFunc"].as<JsonArray>();
        for (int i = 0; i < 11 && i < augerTransfer.size(); ++i)
        {
            if (augerTransfer[i].is<JsonArray>())
            {
                JsonArray point = augerTransfer[i].as<JsonArray>();
                config.tunable.augerTransferFunc[i][0] = point[0];
                config.tunable.augerTransferFunc[i][1] = point[1];
            }
        }
    }

    if (doc["tunable"]["fanTransferFunc"].is<JsonArray>())
    {
        JsonArray fanTransfer = doc["tunable"]["fanTransferFunc"].as<JsonArray>();
        for (int i = 0; i < 11 && i < fanTransfer.size(); ++i)
        {
            if (fanTransfer[i].is<JsonArray>())
            {
                JsonArray point = fanTransfer[i].as<JsonArray>();
                config.tunable.fanTransferFunc[i][0] = point[0];
                config.tunable.fanTransferFunc[i][1] = point[1];
            }
        }
    }

    config.recipe.recipeStepIndex = doc["recipe"]["recipeStepIndex"];
    config.recipe.selectedRecipeIndex = doc["recipe"]["selectedRecipeIndex"];

    JsonArray recipes = doc["recipe"]["recipeData"].as<JsonArray>();
    for (int r = 0; r < MAX_RECIPES && r < recipes.size(); ++r)
    {
        JsonObject recipe = recipes[r];
        strlcpy(config.recipe.recipeData[r].name, recipe["name"] | "", sizeof(config.recipe.recipeData[r].name));
        config.recipe.recipeData[r].stepCount = recipe["stepCount"];
        config.recipe.recipeData[r].enabled = recipe["enabled"];

        if (recipe["steps"].is<JsonArray>())
        {
            JsonArray steps = recipe["steps"].as<JsonArray>();
            for (int s = 0; s < MAX_RECIPE_STEPS && s < steps.size(); ++s)
            {
                JsonObject step = steps[s];
                strlcpy(config.recipe.recipeData[r].steps[s].name, step["name"] | "", sizeof(config.recipe.recipeData[r].steps[s].name));
                config.recipe.recipeData[r].steps[s].enabled = step["enabled"];
                config.recipe.recipeData[r].steps[s].startTempSetpoint = step["startTempSetpoint"];
                config.recipe.recipeData[r].steps[s].endTempSetpoint = step["endTempSetpoint"];
                config.recipe.recipeData[r].steps[s].startSmokeSetpoint = step["startSmokeSetpoint"];
                config.recipe.recipeData[r].steps[s].endSmokeSetpoint = step["endSmokeSetpoint"];
                config.recipe.recipeData[r].steps[s].stepDurationMs = step["stepDurationMs"];
                config.recipe.recipeData[r].steps[s].meatProbeExitTemp = step["meatProbeExitTemp"];
            }
        }
    }

    config.logging.enabled = doc["logging"]["enabled"] | false;
    config.logging.logIntervalMs = doc["logging"]["logIntervalMs"] | 5000;
    config.logging.maxLogFiles = doc["logging"]["maxLogFiles"] | 10;
    config.logging.maxLogFileSizeBytes = doc["logging"]["maxLogFileSizeBytes"] | 100000;
    config.logging.smokeChamberTempThreshold = doc["logging"]["smokeChamberTempThreshold"] | 1.0f;
    config.logging.firePotTempThreshold = doc["logging"]["firePotTempThreshold"] | 1.0f;
    config.logging.setpointThreshold = doc["logging"]["setpointThreshold"] | 1.0f;
    config.logging.smokeSetpointThreshold = doc["logging"]["smokeSetpointThreshold"] | 1.0f;
    config.logging.igniterModeThreshold = doc["logging"]["igniterModeThreshold"] | 1;
    config.logging.augerModeThreshold = doc["logging"]["augerModeThreshold"] | 1;
    config.logging.augerDutyCycleThreshold = doc["logging"]["augerDutyCycleThreshold"] | 1.0f;
    config.logging.augerFrequencyThreshold = doc["logging"]["augerFrequencyThreshold"] | 1.0f;
    config.logging.fanModeThreshold = doc["logging"]["fanModeThreshold"] | 1;
    config.logging.fanDutyCycleThreshold = doc["logging"]["fanDutyCycleThreshold"] | 1.0f;
    config.logging.fanFrequencyThreshold = doc["logging"]["fanFrequencyThreshold"] | 1.0f;

    return true;
}
