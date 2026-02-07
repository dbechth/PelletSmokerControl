#pragma once

#include <WebServer.h>
#include <ArduinoJson.h>
#include "SmokerControl.h"
#include "SmokerStateMachine.h"

class WebInterface
{
public:
    WebInterface(uint16_t port = 80);
    WebInterface(WebServer &existingServer);
    ~WebInterface();
    void begin();
    void handleClient();
    void stop();

private:
    WebServer *server;
    bool ownsServer = false;
    void attachServer(WebServer &existingServer);

    void handleRoot();
    void handleGetStatus();
    void handleSetSetpoint();
    void handleSetSmokeSetpoint();
    void handleGetTunableParams();
    void handleSetTunableParams();
    void handleGetRecipeState();
    void handleSetRecipeState();
    void handleGetButtons();
    void handleSetButton();
    void handleGetActuatorValues();
    void handleSetActuatorValues();
    void handleDownloadConfig();
    void handleUploadConfig();
    void handleReboot();
    void handleSPIFFSList();
    void handleSPIFFSDownload();
    void handleSPIFFSUpload();
    void handleSPIFFSDelete();
    void handleGetLoggingConfig();
    void handleSetLoggingConfig();
    void handleClearLogs();
    void handleDownloadLog();
    void handleGetLogData();
    void handleNotFound();
};
