#ifndef WEB_SERVER_MODULE_H
#define WEB_SERVER_MODULE_H

#include <Arduino.h>

#define WEB_SERVER_PORT   80
#define WEB_ADMIN_USER    "admin"
#define WEB_ADMIN_PASS    "beehive"
#define WEB_REFRESH_SEC   5

struct WebData {
  float*  weight;
  float*  lastSavedWeight;
  float*  tempC;
  float*  humidity;
  float*  rtcTempC;
  float*  calibFactor;
  long*   offset;
  bool*   sensorReady;
  bool*   wifiOk;
  String* datetime;
  uint32_t* wakeupCount;
  float*  batVoltage;
  int*    batPercent;
  float*  prevWeight;
};

struct WebActions {
  void (*doTare)();
  void (*doSave)();
};

extern unsigned long lastActivityTime;

void webserver_init(WebData &data, WebActions &actions);
void webserver_handle();

#endif
