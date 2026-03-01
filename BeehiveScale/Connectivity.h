#ifndef CONNECTIVITY_H
#define CONNECTIVITY_H

#include <Arduino.h>

// ─── Настройки Wi-Fi ──────────────────────────────────────────────────────
#define WIFI_SSID        "Beeline"
#define WIFI_PASSWORD    "YOUR_WIFI_PASSWORD"
#define WIFI_TIMEOUT_MS  10000UL

// ─── Режим WiFi ───────────────────────────────────────────────────────────
// Раскомментируйте для работы в режиме точки доступа (без роутера)
#define WIFI_MODE_AP

// Настройки точки доступа (AP mode)
#define AP_SSID          "BeehiveScale"
#define AP_PASSWORD      "12345678"
#define AP_CHANNEL       1
#define AP_MAX_CLIENTS   4

// ─── Настройки NTP ────────────────────────────────────────────────────────
#define NTP_TIMEZONE         3
#define NTP_SERVER_1         "pool.ntp.org"
#define NTP_SERVER_2         "time.nist.gov"
#define NTP_SYNC_INTERVAL    3600000UL

// ─── Настройки Telegram ───────────────────────────────────────────────────
#define TG_BOT_TOKEN     "YOUR_BOT_TOKEN"
#define TG_CHAT_ID       "YOUR_CHAT_ID"
#define TG_ALERT_DELTA_KG   1.0f
#define TG_REPORT_INTERVAL  21600000UL

// ─── Настройки ThingSpeak ─────────────────────────────────────────────────
#define USE_THINGSPEAK   1
#define TS_API_KEY       "YOUR_THINGSPEAK_WRITE_KEY"
#define TS_CHANNEL_ID    0
#define TS_UPDATE_INTERVAL_MS  60000UL

enum WifiStatus { WIFI_DISCONNECTED, WIFI_CONNECTING, WIFI_CONNECTED };

struct UnsentData {
  float weight;
  float temp;
  float hum;
  float rtcTemp;
  char  datetime[20];
};

void       queue_add(float weight, float temp, float hum, float rtcTemp, const String& dt);
void       queue_process();

bool       wifi_init();           // Инициализация WiFi (AP или STA режим)
bool       wifi_connect();        // Подключение к роутеру (STA режим)
WifiStatus wifi_status();
void       wifi_ensure_connected();

bool       ntp_sync_time();
void       ntp_loop();

bool tg_send_message(const String &text);
bool tg_send_alert(float weight, float tempC, const String &datetime);
bool tg_send_report(float weight, float tempC, float humidity, const String &datetime);
bool ts_send(float weight, float tempC, float humidity, float rtcTempC);

#endif
