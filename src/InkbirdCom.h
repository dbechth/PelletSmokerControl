#ifndef _InkbirdCom_h
#define _InkbirdCom_h

#include "Arduino.h"
#include <sstream>
#include <string>
#include <cstring>
#include "BLEDevice.h"
#include <rom/rtc.h>
#include <esp_int_wdt.h>
#include <esp_task_wdt.h>
#include "Wifi.h"

#define PROBEERRORVALUE 6550 //ERROR Value, if Probe Value greater than this Value it is not published

extern BLEAddress *pServerAddress;
extern boolean doConnect;
extern boolean connected;
extern float Probes[8];
extern float BatteryPCT;


bool connectToBLEServer(BLEAddress pAddress);
void getBatteryData();

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
  public:
  void onResult(BLEAdvertisedDevice advertisedDevice);
};    

#endif