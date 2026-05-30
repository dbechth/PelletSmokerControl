// AC2.h

#ifndef _AC2_h
#define _AC2_h

#include <ArduinoOTA.h>
#ifdef ARDUINO_ARCH_ESP32
    #include <WebServer.h>
    #include <WiFi.h>
#else
    #include <ESP8266WiFi.h>
    #include <ESP8266WebServer.h>
#endif
//#include <SPIFFSLogger.h>
#define ACTerminalBufferSize   64


class AC2Class
{
protected:
    void SetupOTA();
    void task();

#ifdef ARDUINO_ARCH_ESP32
    static void AC2TaskEntryPoint(void *parameter);
    TaskHandle_t ac2TaskHandle = nullptr;
    int taskRateMs = 100;
#endif

public:
    bool init(String name, int taskRateMs);
    void print(String message);
    void println(String message);
    String deviceName;
    String terminalBuffer[ACTerminalBufferSize];
    int terminalBufferCount;
    #ifdef ARDUINO_ARCH_ESP32
        WebServer webserver;
    #else
        ESP8266WebServer webserver;
    #endif
};

extern AC2Class AC2;

#endif

