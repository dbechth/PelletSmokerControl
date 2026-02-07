#include "DataLogger.h"

// Static member initialization
LogConfig DataLogger::config = DEFAULT_LOG_CONFIG;
unsigned long DataLogger::lastLogTime = 0;
int DataLogger::currentLogFileIndex = 0;
unsigned long DataLogger::currentLogFileSize = 0;

void DataLogger::init(const LogConfig &newConfig)
{
    config = newConfig;
    lastLogTime = millis();
    currentLogFileIndex = 0;
    currentLogFileSize = 0;

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

    String filepath = getLogFilePath(currentLogFileIndex);
    File file = SPIFFS.open(filepath, "a");
    if (!file)
    {
        Serial.println("Failed to open log file: " + filepath);
        return;
    }

    // Format: timestamp,value1,value2,...
    String line = String(millis()) + ",";
    line += String(smokeChamberTemp, 2) + ",";
    line += String(firePotTemp, 2) + ",";
    line += String(setpoint, 2) + ",";
    line += String(smokesetpoint, 2) + ",";
    line += String(activeState) + ",";
    line += String(igniterMode) + ",";
    line += String(augerMode) + ",";
    line += String(augerDutyCycle, 2) + ",";
    line += String(augerFrequency, 2) + ",";
    line += String(fanMode) + ",";
    line += String(fanDutyCycle, 2) + ",";
    line += String(fanFrequency, 2) + "\n";

    file.print(line);
    currentLogFileSize = file.size();
    file.close();

    // Check if we need to rotate to next file
    if (currentLogFileSize >= config.maxLogFileSizeBytes)
    {
        rotateLogFile();
    }

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
