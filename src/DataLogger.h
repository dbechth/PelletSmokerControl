#pragma once

#include <Arduino.h>
#include <SPIFFS.h>

// Data logging configuration
struct LogConfig
{
    bool enabled;
    unsigned long logIntervalMs;     // How often to log (in milliseconds)
    int maxLogFiles;                  // Maximum number of log files to keep
    unsigned long maxLogFileSizeBytes; // Max size per log file before rolling to next

    // Per-parameter change thresholds for sparse logging
    float smokeChamberTempThreshold;
    float firePotTempThreshold;
    float setpointThreshold;
    float smokeSetpointThreshold;
    int igniterModeThreshold;
    int augerModeThreshold;
    float augerDutyCycleThreshold;
    float augerFrequencyThreshold;
    int fanModeThreshold;
    float fanDutyCycleThreshold;
    float fanFrequencyThreshold;
};

class DataLogger
{
public:
    // Initialize the logger with configuration
    static void init(const LogConfig &config);

    // Log a line of CSV data with timestamp
    static void logData(
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
        float fanFrequency);

    // Get the current log configuration
    static LogConfig getConfig();

    // Update the log configuration
    static void setConfig(const LogConfig &config);

    // Choose whether to append to current file or start a fresh file for a new session
    static void startSession(bool appendToCurrentFile);

    // Delete all log files
    static void clearAllLogs();

    // Get list of log files
    static void listLogFiles();

    // Get current active log file name
    static String getActiveLogFile();

private:
    static LogConfig config;
    static unsigned long lastLogTime;
    static int currentLogFileIndex;
    static unsigned long currentLogFileSize;

    // Create a new log file and write header
    static bool createNewLogFile();

    // Get the path for a log file by index
    static String getLogFilePath(int index);

    // Rotate to the next log file (delete oldest if needed)
    static void rotateLogFile();

    // Write CSV header to the current log file
    static void writeLogHeader();

    // Check if it's time to log data
    static bool shouldLog();
};
