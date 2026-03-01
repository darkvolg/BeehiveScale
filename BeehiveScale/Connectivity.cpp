#include "Connectivity.h"
#include "Memory.h"
#include <ArduinoJson.h>
#include <time.h>
#include <RTClib.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ESP8266mDNS.h>
#else
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ESPmDNS.h>
#include <esp_task_wdt.h>
#endif

#define HTTP_TIMEOUT_MS  5000

static WifiStatus _wifiStatus = WIFI_DISCONNECTED;

// Инициализация WiFi в режиме STA или AP
bool wifi_init() {
  wifi_settings_init();
  // Режим из EEPROM: 0=AP (по умолчанию), 1=STA
  uint8_t mode = get_wifi_mode();

  if (mode == 1) {
    // STA режим — подключение к роутеру
    return wifi_connect();
  }

  // AP режим — точка доступа без роутера
  Serial.println(F("[WiFi] Starting AP mode..."));
  WiFi.mode(WIFI_AP);
  char apPassBuf[24];
  get_ap_pass(apPassBuf, sizeof(apPassBuf));
  WiFi.softAP(AP_SSID, apPassBuf, AP_CHANNEL, false, AP_MAX_CLIENTS);

  delay(1000);

  IPAddress IP = WiFi.softAPIP();
  Serial.print(F("[WiFi] AP IP address: "));
  Serial.println(IP);

  if (IP == IPAddress(0,0,0,0)) {
    Serial.println(F("[WiFi] AP failed to start!"));
    _wifiStatus = WIFI_DISCONNECTED;
    return false;
  }

  _wifiStatus = WIFI_CONNECTED;
  Serial.println(F("[WiFi] AP mode ready, IP: 192.168.4.1"));

  if (MDNS.begin("beehivescale")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println(F("[mDNS] beehivescale.local ready"));
  }
  return true;
}

bool wifi_connect() {
  // SSID и пароль: из EEPROM если сохранены, иначе из хардкода
  char ssidBuf[33], passBuf[33];
  get_wifi_ssid(ssidBuf, sizeof(ssidBuf));
  get_wifi_sta_pass(passBuf, sizeof(passBuf));
  const char *ssid = (ssidBuf[0] != '\0') ? ssidBuf : WIFI_SSID;
  const char *pass = (passBuf[0] != '\0') ? passBuf : WIFI_PASSWORD;

  Serial.print(F("[WiFi] Connecting to: "));
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  _wifiStatus = WIFI_CONNECTING;
  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > WIFI_TIMEOUT_MS) {
      _wifiStatus = WIFI_DISCONNECTED;
      Serial.println(F("[WiFi] Timeout!"));
      return false;
    }
#if defined(ESP32)
    esp_task_wdt_reset();
#elif defined(ESP8266)
    ESP.wdtFeed();
#endif
    yield();
    delay(10);
  }
  _wifiStatus = WIFI_CONNECTED;
  Serial.print(F("[WiFi] Connected, IP: "));
  Serial.println(WiFi.localIP());

  // mDNS — доступ по http://beehivescale.local
  if (MDNS.begin("beehivescale")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println(F("[mDNS] beehivescale.local ready"));
  }
  return true;
}

WifiStatus wifi_status() {
  return _wifiStatus;
}

void wifi_ensure_connected() {
  if (get_wifi_mode() == 0) {
    _wifiStatus = (WiFi.softAPIP() == IPAddress(0,0,0,0)) ? WIFI_DISCONNECTED : WIFI_CONNECTED;
    return;
  }

  static unsigned long lastReconnectAttempt = 0;
  static unsigned long connectionFailedAt = 0;
  const unsigned long RECONNECT_DEBOUNCE_MS = 10000UL;
  const unsigned long PENALTY_MS = 300000UL; // 5 минут отдыха при неудаче

  if (WiFi.status() != WL_CONNECTED) {
    _wifiStatus = WIFI_DISCONNECTED;
    unsigned long now = millis();

    // Если была неудача, ждем PENALTY_MS перед следующей попыткой
    if (connectionFailedAt > 0 && now - connectionFailedAt < PENALTY_MS) {
      return;
    }

    if (now - lastReconnectAttempt < RECONNECT_DEBOUNCE_MS) {
      return;
    }

    Serial.println(F("[WiFi] Lost connection, reconnecting..."));
    WiFi.reconnect();
    lastReconnectAttempt = now;
    unsigned long start = now;

    // Сокращенный таймаут для переподключения (7 сек)
    while (WiFi.status() != WL_CONNECTED && millis() - start < 7000UL) {
#if defined(ESP32)
      esp_task_wdt_reset();
#elif defined(ESP8266)
      ESP.wdtFeed();
#endif
      yield();
      delay(10);
    }

    if (WiFi.status() == WL_CONNECTED) {
      _wifiStatus = WIFI_CONNECTED;
      connectionFailedAt = 0;
      Serial.println(F("[WiFi] Reconnected."));
    } else {
      connectionFailedAt = millis();
      Serial.println(F("[WiFi] Reconnect failed, cooling down..."));
    }
  }
}

static bool _wifi_active() {
  if (get_wifi_mode() == 0) {
    return WiFi.softAPIP() != IPAddress(0,0,0,0);
  }
  return WiFi.status() == WL_CONNECTED;
}

static const char* TG_HOST = "api.telegram.org";

#if defined(ESP8266)
#include <LittleFS.h>
#else
#include <LittleFS.h>
#endif

#define QUEUE_FILE "/queue.bin"
#define MAX_QUEUE_ITEMS 50

static bool _ensureFS() {
  static bool _fsReady = false;
  if (_fsReady) return true;
  _fsReady = LittleFS.begin();
  return _fsReady;
}

void queue_add(float weight, float temp, float hum, float rtcTemp, const String& dt) {
  if (!_ensureFS()) return;
  File f = LittleFS.open(QUEUE_FILE, "a");
  if (!f) return;

  if (f.size() >= MAX_QUEUE_ITEMS * sizeof(UnsentData)) {
    f.close();
    Serial.println(F("[Queue] Full, skipping"));
    return;
  }

  UnsentData data;
  data.weight = weight;
  data.temp = temp;
  data.hum = hum;
  data.rtcTemp = rtcTemp;
  memset(data.datetime, 0, sizeof(data.datetime));
  strncpy(data.datetime, dt.c_str(), sizeof(data.datetime) - 1);

  f.write((uint8_t*)&data, sizeof(UnsentData));
  f.close();
  Serial.println(F("[Queue] Data saved offline"));
}

void queue_process() {
  if (!_wifi_active()) return;
  if (!_ensureFS()) return;
  if (!LittleFS.exists(QUEUE_FILE)) return;

  File f = LittleFS.open(QUEUE_FILE, "r");
  if (!f) return;

  size_t count = f.size() / sizeof(UnsentData);
  if (count == 0) { f.close(); LittleFS.remove(QUEUE_FILE); return; }

  Serial.print(F("[Queue] Processing items: ")); Serial.println(count);

  // Читаем и отправляем по одному элементу — без выделения heap под весь массив
  for (size_t i = 0; i < count; i++) {
    UnsentData item;
    if (f.read((uint8_t*)&item, sizeof(UnsentData)) != sizeof(UnsentData)) break;
    Serial.print(F("[Queue] Sending item ")); Serial.println(i+1);
    ts_send(item.weight, item.temp, item.hum, item.rtcTemp);
    delay(500);
#if defined(ESP8266)
    ESP.wdtFeed();
#endif
    tg_send_report(item.weight, item.temp, item.hum, item.datetime);
    delay(1000);
#if defined(ESP8266)
    ESP.wdtFeed();
#endif
    yield();
  }
  f.close();
  LittleFS.remove(QUEUE_FILE);
  Serial.println(F("[Queue] Done"));
}

static bool _tg_post(const char* message) {
  if (!_wifi_active()) return false;

  // Приоритет: EEPROM-настройки → хардкод из Connectivity.h
  char tgToken[50];
  char tgChatId[16];
  get_tg_token(tgToken, sizeof(tgToken));
  get_tg_chatid(tgChatId, sizeof(tgChatId));

  const char* useToken  = (tgToken[0]  != '\0') ? tgToken  : TG_BOT_TOKEN;
  const char* useChatId = (tgChatId[0] != '\0') ? tgChatId : TG_CHAT_ID;

  if (strncmp(useToken, "YOUR_", 5) == 0) return false;

#if defined(ESP8266)
  BearSSL::WiFiClientSecure client;
#else
  WiFiClientSecure client;
#endif
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);

  char url[128];
  snprintf(url, sizeof(url), "https://%s/bot%s/sendMessage", TG_HOST, useToken);

#if defined(ESP8266)
  if (!http.begin(client, url)) return false;
#else
  http.begin(client, url);
#endif
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<512> doc;
  doc["chat_id"] = useChatId;
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

  if (code == 200) {
    Serial.println(F("[TS] OK"));
    return true;
  }
  Serial.print(F("[TS] Error code: ")); Serial.println(code);
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
    esp_task_wdt_reset();
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
