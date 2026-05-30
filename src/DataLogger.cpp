#include "DataLogger.h"

#include <math.h>

// Static member initialization
LogConfig DataLogger::config = {};
unsigned long DataLogger::lastLogTime = 0;
int DataLogger::currentLogFileIndex = 0;
unsigned long DataLogger::currentLogFileSize = 0;

namespace
{
float clampThreshold(float threshold)
{
    return threshold < 0.0f ? 0.0f : threshold;
}

int clampThreshold(int threshold)
{
    return threshold < 0 ? 0 : threshold;
}

bool hasChanged(float currentValue, float lastValue, float threshold)
{
    return fabsf(currentValue - lastValue) >= clampThreshold(threshold);
}

bool hasChanged(int currentValue, int lastValue, int threshold)
{
    return abs(currentValue - lastValue) >= clampThreshold(threshold);
}

bool hasLastLoggedSample = false;
float lastSmokeChamberTemp = 0.0f;
float lastFirePotTemp = 0.0f;
float lastSetpoint = 0.0f;
float lastSmokeSetpoint = 0.0f;
String lastActiveState = "";
int lastIgniterMode = 0;
int lastAugerMode = 0;
float lastAugerDutyCycle = 0.0f;
float lastAugerFrequency = 0.0f;
int lastFanMode = 0;
float lastFanDutyCycle = 0.0f;
float lastFanFrequency = 0.0f;
}

void DataLogger::init(const LogConfig &newConfig)
{
    config = newConfig;
    lastLogTime = millis();
    currentLogFileIndex = 0;
    currentLogFileSize = 0;
    hasLastLoggedSample = false;

    if (!config.enabled)
        return;

    // Create initial log file
    if (!createNewLogFile())
    {
        Serial.println("Failed to create initial log file");
    }
}

bool DataLogger::createNewLogFile()
{
    // Ensure logs directory exists
    if (!SPIFFS.exists("/logs"))
    {
        // SPIFFS doesn't have mkdir, but creating a file will create the path
        File dir = SPIFFS.open("/logs/.keep", "w");
        if (dir)
            dir.close();
    }

    String filepath = getLogFilePath(currentLogFileIndex);

    // Check if file already exists and get its size
    if (SPIFFS.exists(filepath))
    {
        File file = SPIFFS.open(filepath, "r");
        if (file)
        {
            currentLogFileSize = file.size();
            file.close();
        }
    }
    else
    {
        currentLogFileSize = 0;
        File file = SPIFFS.open(filepath, "w");
        if (!file)
        {
            Serial.println("Failed to create log file: " + filepath);
            return false;
        }
        writeLogHeader();
        file.close();
    }

    return true;
}

String DataLogger::getLogFilePath(int index)
{
    return "/logs/data_" + String(index) + ".csv";
}

void DataLogger::rotateLogFile()
{
    currentLogFileIndex++;
    if (currentLogFileIndex >= config.maxLogFiles)
    {
        currentLogFileIndex = 0;
    }

    String filepath = getLogFilePath(currentLogFileIndex);

    // Delete old file if it exists
    if (SPIFFS.exists(filepath))
    {
        SPIFFS.remove(filepath);
    }

    // Create new log file
    currentLogFileSize = 0;
    createNewLogFile();
    hasLastLoggedSample = false;
}

void DataLogger::writeLogHeader()
{
    String filepath = getLogFilePath(currentLogFileIndex);
    File file = SPIFFS.open(filepath, "a");
    if (!file)
    {
        Serial.println("Failed to open log file for header: " + filepath);
        return;
    }

    String header = "Timestamp,SmokeChamberTemp,FirePotTemp,Setpoint,SmokeSetpoint,ActiveState,"
                    "IgniterMode,AugerMode,AugerDutyCycle,AugerFrequency,"
                    "FanMode,FanDutyCycle,FanFrequency\n";

    file.print(header);
    currentLogFileSize = file.size();
    file.close();
}

void DataLogger::logData(
    float smokeChamberTemp,
    float firePotTemp,
    float setpoint,
    float smokesetpoint,
    const char *activeState,
    int igniterMode,
    int augerMode,
    float augerDutyCycle,
    float augerFrequency,
    int fanMode,
    float fanDutyCycle,
    float fanFrequency)
{
    if (!config.enabled || !shouldLog())
        return;

    const String activeStateValue = activeState != nullptr ? String(activeState) : String("");

    const bool changedSmokeChamberTemp = !hasLastLoggedSample || hasChanged(smokeChamberTemp, lastSmokeChamberTemp, config.smokeChamberTempThreshold);
    const bool changedFirePotTemp = !hasLastLoggedSample || hasChanged(firePotTemp, lastFirePotTemp, config.firePotTempThreshold);
    const bool changedSetpoint = !hasLastLoggedSample || hasChanged(setpoint, lastSetpoint, config.setpointThreshold);
    const bool changedSmokeSetpoint = !hasLastLoggedSample || hasChanged(smokesetpoint, lastSmokeSetpoint, config.smokeSetpointThreshold);
    const bool changedActiveState = !hasLastLoggedSample || (activeStateValue != lastActiveState);
    const bool changedIgniterMode = !hasLastLoggedSample || hasChanged(igniterMode, lastIgniterMode, config.igniterModeThreshold);
    const bool changedAugerMode = !hasLastLoggedSample || hasChanged(augerMode, lastAugerMode, config.augerModeThreshold);
    const bool changedAugerDutyCycle = !hasLastLoggedSample || hasChanged(augerDutyCycle, lastAugerDutyCycle, config.augerDutyCycleThreshold);
    const bool changedAugerFrequency = !hasLastLoggedSample || hasChanged(augerFrequency, lastAugerFrequency, config.augerFrequencyThreshold);
    const bool changedFanMode = !hasLastLoggedSample || hasChanged(fanMode, lastFanMode, config.fanModeThreshold);
    const bool changedFanDutyCycle = !hasLastLoggedSample || hasChanged(fanDutyCycle, lastFanDutyCycle, config.fanDutyCycleThreshold);
    const bool changedFanFrequency = !hasLastLoggedSample || hasChanged(fanFrequency, lastFanFrequency, config.fanFrequencyThreshold);

    if (!(changedSmokeChamberTemp ||
          changedFirePotTemp ||
          changedSetpoint ||
          changedSmokeSetpoint ||
          changedActiveState ||
          changedIgniterMode ||
          changedAugerMode ||
          changedAugerDutyCycle ||
          changedAugerFrequency ||
          changedFanMode ||
          changedFanDutyCycle ||
          changedFanFrequency))
    {
        return;
    }

    String filepath = getLogFilePath(currentLogFileIndex);
    File file = SPIFFS.open(filepath, "a");
    if (!file)
    {
        Serial.println("Failed to open log file: " + filepath);
        return;
    }

    // Sparse CSV row: unchanged fields are left empty.
    String line = String(millis()) + ",";
    line += changedSmokeChamberTemp ? String(smokeChamberTemp, 2) : String("");
    line += ",";
    line += changedFirePotTemp ? String(firePotTemp, 2) : String("");
    line += ",";
    line += changedSetpoint ? String(setpoint, 2) : String("");
    line += ",";
    line += changedSmokeSetpoint ? String(smokesetpoint, 2) : String("");
    line += ",";
    line += changedActiveState ? activeStateValue : String("");
    line += ",";
    line += changedIgniterMode ? String(igniterMode) : String("");
    line += ",";
    line += changedAugerMode ? String(augerMode) : String("");
    line += ",";
    line += changedAugerDutyCycle ? String(augerDutyCycle, 2) : String("");
    line += ",";
    line += changedAugerFrequency ? String(augerFrequency, 2) : String("");
    line += ",";
    line += changedFanMode ? String(fanMode) : String("");
    line += ",";
    line += changedFanDutyCycle ? String(fanDutyCycle, 2) : String("");
    line += ",";
    line += changedFanFrequency ? String(fanFrequency, 2) : String("");
    line += "\n";

    file.print(line);
    currentLogFileSize = file.size();
    file.close();

    // Check if we need to rotate to next file
    if (currentLogFileSize >= config.maxLogFileSizeBytes)
    {
        rotateLogFile();
    }

    hasLastLoggedSample = true;
    lastSmokeChamberTemp = smokeChamberTemp;
    lastFirePotTemp = firePotTemp;
    lastSetpoint = setpoint;
    lastSmokeSetpoint = smokesetpoint;
    lastActiveState = activeStateValue;
    lastIgniterMode = igniterMode;
    lastAugerMode = augerMode;
    lastAugerDutyCycle = augerDutyCycle;
    lastAugerFrequency = augerFrequency;
    lastFanMode = fanMode;
    lastFanDutyCycle = fanDutyCycle;
    lastFanFrequency = fanFrequency;

    lastLogTime = millis();
}

LogConfig DataLogger::getConfig()
{
    return config;
}

void DataLogger::setConfig(const LogConfig &newConfig)
{
    config = newConfig;
    if (config.enabled)
    {
        lastLogTime = millis();
    }
}

void DataLogger::startSession(bool appendToCurrentFile)
{
    if (!config.enabled)
    {
        return;
    }

    String filepath = getLogFilePath(currentLogFileIndex);
    if (!appendToCurrentFile)
    {
        if (SPIFFS.exists(filepath))
        {
            SPIFFS.remove(filepath);
        }
        currentLogFileSize = 0;
    }

    createNewLogFile();
    hasLastLoggedSample = false;
    lastLogTime = millis();
}

void DataLogger::clearAllLogs()
{
    for (int i = 0; i < config.maxLogFiles; i++)
    {
        String filepath = getLogFilePath(i);
        if (SPIFFS.exists(filepath))
        {
            SPIFFS.remove(filepath);
        }
    }
    currentLogFileIndex = 0;
    currentLogFileSize = 0;
    createNewLogFile();
    hasLastLoggedSample = false;
}

void DataLogger::listLogFiles()
{
    Serial.println("Log files:");
    for (int i = 0; i < config.maxLogFiles; i++)
    {
        String filepath = getLogFilePath(i);
        if (SPIFFS.exists(filepath))
        {
            File file = SPIFFS.open(filepath, "r");
            if (file)
            {
                Serial.print("  ");
                Serial.print(filepath);
                Serial.print(" - ");
                Serial.print(file.size());
                Serial.println(" bytes");
                file.close();
            }
        }
    }
}

String DataLogger::getActiveLogFile()
{
    return getLogFilePath(currentLogFileIndex);
}

bool DataLogger::shouldLog()
{
    return (millis() - lastLogTime) >= config.logIntervalMs;
}
