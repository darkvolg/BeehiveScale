#include "Connectivity.h"
#include <ArduinoJson.h>
#include <time.h>
#include <RTClib.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#else
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#endif

#define HTTP_TIMEOUT_MS  5000

static WifiStatus _wifiStatus = WIFI_DISCONNECTED;

// Инициализация WiFi в режиме STA или AP
bool wifi_init() {
#if defined(WIFI_MODE_AP)
  // Режим точки доступа (AP) - подключение напрямую без роутера
  Serial.println(F("[WiFi] Starting AP mode..."));
  WiFi.mode(WIFI_AP);
  // Пароль AP: из EEPROM (если установлен через веб) или из хардкода
  char apPassBuf[24];
  get_ap_pass(apPassBuf, sizeof(apPassBuf));
  WiFi.softAP(AP_SSID, apPassBuf, AP_CHANNEL, false, AP_MAX_CLIENTS);

  delay(1000);  // Увеличенная задержка для инициализации AP
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print(F("[WiFi] AP IP address: "));
  Serial.println(IP);
  
  if (IP == IPAddress(0,0,0,0)) {
    Serial.println(F("[WiFi] AP failed to start!"));
    _wifiStatus = WIFI_DISCONNECTED;
    return false;
  }

  _wifiStatus = WIFI_CONNECTED;
  Serial.println(F("[WiFi] AP mode ready"));
  return true;

#else
  // Режим станции (STA) - подключение к роутеру
  return wifi_connect();
#endif
}

bool wifi_connect() {
  Serial.print(F("[WiFi] Connecting to: "));
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  _wifiStatus = WIFI_CONNECTING;
  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > WIFI_TIMEOUT_MS) {
      _wifiStatus = WIFI_DISCONNECTED;
      Serial.println(F("[WiFi] Timeout!"));
      return false;
    }
    yield();
  }
  _wifiStatus = WIFI_CONNECTED;
  Serial.print(F("[WiFi] Connected, IP: "));
  Serial.println(WiFi.localIP());
  return true;
}

WifiStatus wifi_status() {
  return _wifiStatus;
}

void wifi_ensure_connected() {
#if defined(WIFI_MODE_AP)
  // В режиме AP просто проверяем, что точка доступа активна
  if (WiFi.softAPIP() == IPAddress(0,0,0,0)) {
    _wifiStatus = WIFI_DISCONNECTED;
  } else {
    _wifiStatus = WIFI_CONNECTED;
  }
#else
  static unsigned long lastReconnectAttempt = 0;
  static bool firstCall = true;
  if (firstCall) { lastReconnectAttempt = millis(); firstCall = false; }
  const unsigned long RECONNECT_DEBOUNCE_MS = 10000UL;

  if (WiFi.status() != WL_CONNECTED) {
    _wifiStatus = WIFI_DISCONNECTED;

    if (millis() - lastReconnectAttempt < RECONNECT_DEBOUNCE_MS) {
      return;
    }

    Serial.println(F("[WiFi] Lost connection, reconnecting..."));
    WiFi.reconnect();
    lastReconnectAttempt = millis();
    unsigned long start = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
      yield();
    }
    if (WiFi.status() == WL_CONNECTED) {
      _wifiStatus = WIFI_CONNECTED;
      Serial.println(F("[WiFi] Reconnected."));
    } else {
      Serial.println(F("[WiFi] Reconnect failed"));
    }
  }
#endif
}

static bool _wifi_active() {
#if defined(WIFI_MODE_AP)
  return WiFi.softAPIP() != IPAddress(0,0,0,0);
#else
  return WiFi.status() == WL_CONNECTED;
#endif
}

static const char* TG_HOST = "api.telegram.org";

static bool _tg_post(const char* message) {
  if (!_wifi_active()) return false;
  if (strncmp(TG_BOT_TOKEN, "YOUR_", 5) == 0) return false;

#if defined(ESP8266)
  BearSSL::WiFiClientSecure client;
#else
  WiFiClientSecure client;
#endif
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);

  char url[128];
  snprintf(url, sizeof(url), "https://%s/bot%s/sendMessage", TG_HOST, TG_BOT_TOKEN);

#if defined(ESP8266)
  if (!http.begin(client, url)) return false;
#else
  http.begin(client, url);
#endif
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<512> doc;
  doc["chat_id"] = TG_CHAT_ID;
  doc["text"] = message;
  doc["parse_mode"] = "HTML";
  char body[512];
  serializeJson(doc, body, sizeof(body));

  int code = http.POST(body);
  http.end();

  if (code == 200) {
    Serial.println(F("[TG] Message sent OK"));
    return true;
  }
  Serial.print(F("[TG] Error code: ")); Serial.println(code);
  return false;
}

bool tg_send_message(const String &text) {
  return _tg_post(text.c_str());
}

bool tg_send_alert(float weight, float tempC, const String &datetime) {
  char msg[256];
  char tempStr[16];
  if (tempC > -90) {
    snprintf(tempStr, sizeof(tempStr), "%.1f C", tempC);
  } else {
    snprintf(tempStr, sizeof(tempStr), "n/d");
  }
  snprintf(msg, sizeof(msg),
    "<b>TREVOGA: uley</b>\n"
    "Vremya: %s\n"
    "Ves: <b>%.2f kg</b>\n"
    "Temp: %s",
    datetime.c_str(), weight, tempStr);
  return _tg_post(msg);
}

bool tg_send_report(float weight, float tempC, float humidity, const String &datetime) {
  char msg[320];
  int pos = 0;
  pos += snprintf(msg + pos, sizeof(msg) - pos,
    "<b>Otchet: uley</b>\nVremya: %s\nVes: %.2f kg\n",
    datetime.c_str(), weight);
  if (tempC > -90) {
    pos += snprintf(msg + pos, sizeof(msg) - pos, "Temp: %.1f C\n", tempC);
  }
  if (humidity > -90) {
    pos += snprintf(msg + pos, sizeof(msg) - pos, "Vlazhn: %.1f %%\n", humidity);
  }
  return _tg_post(msg);
}

bool ts_send(float weight, float tempC, float humidity, float rtcTempC) {
  if (!_wifi_active()) return false;
  if (strncmp(TS_API_KEY, "YOUR_", 5) == 0) return false;

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);

  char url[256];
  snprintf(url, sizeof(url),
    "https://api.thingspeak.com/update?api_key=%s&field1=%.2f&field2=%.1f&field3=%.1f&field4=%.2f",
    TS_API_KEY, weight, tempC, humidity, rtcTempC);

#if defined(ESP8266)
  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  if (!http.begin(client, url)) return false;
#else
  WiFiClientSecure client;
  client.setInsecure();
  http.begin(client, url);
#endif
  int code = http.GET();
  http.end();

  if (code > 0) {
    Serial.print(F("[TS] OK, entry: ")); Serial.println(code);
    return true;
  }
  Serial.print(F("[TS] Error: ")); Serial.println(code);
  return false;
}

// ─── NTP синхронизация ────────────────────────────────────────────────────
static unsigned long _lastNtpSync = 0;
static bool _ntpInitialized = false;

extern bool rtc_set(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s);

bool ntp_sync_time() {
  if (!_wifi_active()) {
    Serial.println(F("[NTP] Error: no WiFi"));
    return false;
  }

  Serial.println(F("[NTP] Sync time..."));
  Serial.print(F("[NTP] Server: "));
  Serial.println(NTP_SERVER_1);

#if defined(ESP32)
  configTime(NTP_TIMEZONE * 3600, 0, NTP_SERVER_1, NTP_SERVER_2);

  struct tm timeinfo;
  int retry = 0;
  Serial.print(F("[NTP] Waiting"));

  while (!getLocalTime(&timeinfo) && retry < 15) {
    delay(1000);
    retry++;
    Serial.print(F("."));
    yield();
  }
  Serial.println();

  if (retry >= 15) {
    Serial.println(F("[NTP] Sync failed!"));
    return false;
  }

  char timeStr[64];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
  Serial.print(F("[NTP] Got time: "));
  Serial.println(timeStr);

  if (rtc_set(
    timeinfo.tm_year + 1900,
    timeinfo.tm_mon + 1,
    timeinfo.tm_mday,
    timeinfo.tm_hour,
    timeinfo.tm_min,
    timeinfo.tm_sec
  )) {
    Serial.println(F("[NTP] Time set to RTC!"));
    _lastNtpSync = millis();
    _ntpInitialized = true;
    return true;
  } else {
    Serial.println(F("[NTP] RTC error"));
    return false;
  }

#elif defined(ESP8266)
  Serial.println(F("[NTP] ESP8266: use compile time"));
  return false;
#endif

  return false;
}

void ntp_loop() {
  if (!_ntpInitialized) {
    _ntpInitialized = true;
    _lastNtpSync = millis();
    return;
  }

  if (millis() - _lastNtpSync >= NTP_SYNC_INTERVAL) {
    Serial.println(F("[NTP] Periodic sync..."));
    if (ntp_sync_time()) {
      Serial.println(F("[NTP] Sync OK"));
    } else {
      Serial.println(F("[NTP] Sync failed"));
    }
    _lastNtpSync = millis();
  }
}
