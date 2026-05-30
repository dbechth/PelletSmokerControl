#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
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
    WebSocketsServer *wsServer;
    bool ownsServer = false;
    TaskHandle_t realtimeTaskHandle = nullptr;
    String lastSnapshotPayload;
    unsigned long lastBroadcastMs = 0;
    unsigned long lastHeartbeatMs = 0;
    void attachServer(WebServer &existingServer);

    static void realtimeTaskEntryPoint(void *parameter);
    void realtimeTaskLoop();
    void handleWsEvent(uint8_t clientId, WStype_t type, uint8_t *payload, size_t length);
    void buildRealtimeSnapshot(String &outJson);
    void broadcastRealtimeSnapshot(bool forceBroadcast);

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
