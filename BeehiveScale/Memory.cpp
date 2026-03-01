#include "Memory.h"
#include <math.h>

// Статические переменные для хранения настроек
static float _alertDelta = 0.5f;
static float _calibWeight = 1000.0f;
static float _emaAlpha = 0.2f;
static bool _settingsLoaded = false;

static bool is_eeprom_valid() {
  byte magic = 0;
  EEPROM.get(EEPROM_ADDR_MAGIC, magic);
  return (magic == EEPROM_MAGIC_VALUE);
}

static void mark_eeprom_valid() {
  byte magic = EEPROM_MAGIC_VALUE;
  EEPROM.put(EEPROM_ADDR_MAGIC, magic);
}

// Инициализация настроек веб-сервера
void web_settings_init() {
  if (_settingsLoaded) return;

  byte magic2 = 0;
  EEPROM.get(EEPROM_ADDR_MAGIC2, magic2);

  if (magic2 != EEPROM_MAGIC2_VALUE) {
    _alertDelta = 0.5f;
    _calibWeight = 1000.0f;
    _emaAlpha = 0.2f;
  } else {
    EEPROM.get(EEPROM_ADDR_ALERT_DELTA, _alertDelta);
    EEPROM.get(EEPROM_ADDR_CALIB_WEIGHT, _calibWeight);
    EEPROM.get(EEPROM_ADDR_EMA_ALPHA, _emaAlpha);

    if (isnan(_alertDelta) || _alertDelta < 0.1f || _alertDelta > 10.0f) _alertDelta = 0.5f;
    if (isnan(_calibWeight) || _calibWeight < 100.0f || _calibWeight > 5000.0f) _calibWeight = 1000.0f;
    if (isnan(_emaAlpha) || _emaAlpha < 0.05f || _emaAlpha > 0.9f) _emaAlpha = 0.2f;
  }
  _settingsLoaded = true;
}

float web_get_alert_delta() {
  if (!_settingsLoaded) web_settings_init();
  return _alertDelta;
}

float web_get_calib_weight() {
  if (!_settingsLoaded) web_settings_init();
  return _calibWeight;
}

float web_get_ema_alpha() {
  if (!_settingsLoaded) web_settings_init();
  return _emaAlpha;
}

void load_calibration_data(float &factor, long &offset, float &weight) {
  if (!is_eeprom_valid()) {
    factor = 2280.0f;
    offset = 0;
    weight = 0.0f;
    mark_eeprom_valid();
    EEPROM.commit();
    return;
  }

  EEPROM.get(EEPROM_ADDR_CALIB, factor);
  EEPROM.get(EEPROM_ADDR_OFFSET, offset);
  EEPROM.get(EEPROM_ADDR_WEIGHT, weight);

  if (isnan(factor) || factor < 100.0f || factor > 100000.0f) factor = 2280.0f;
  if (offset < -16777216L || offset > 16777216L) offset = 0;
  if (isnan(weight) || weight < 0.0f) weight = 0.0f;
}

void save_calibration(float factor) {
  EEPROM.put(EEPROM_ADDR_CALIB, factor);
  mark_eeprom_valid();
  EEPROM.commit();
}

void save_offset(long offset) {
  EEPROM.put(EEPROM_ADDR_OFFSET, offset);
  mark_eeprom_valid();
  EEPROM.commit();
}

void save_weight(float &lastWeight, float currentWeight) {
  lastWeight = currentWeight;
  EEPROM.put(EEPROM_ADDR_WEIGHT, lastWeight);
  EEPROM.commit();
}

void load_web_settings(float &alertDelta, float &calibWeight, float &emaAlpha) {
  if (!_settingsLoaded) web_settings_init();
  alertDelta = _alertDelta;
  calibWeight = _calibWeight;
  emaAlpha = _emaAlpha;
}

void save_prev_offset(long prevOffset) {
  EEPROM.put(EEPROM_ADDR_PREV_OFFSET, prevOffset);
  EEPROM.commit();
}

long load_prev_offset() {
  long val = 0;
  EEPROM.get(EEPROM_ADDR_PREV_OFFSET, val);
  if (val < -16777216L || val > 16777216L) val = 0;
  return val;
}

void save_prev_weight(float w) {
  EEPROM.put(EEPROM_ADDR_PREV_WEIGHT, w);
  EEPROM.commit();
}

float load_prev_weight(float fallback) {
  float w = 0.0f;
  EEPROM.get(EEPROM_ADDR_PREV_WEIGHT, w);
  if (isnan(w) || w <= 0.0f || w > 500.0f) return fallback;
  return w;
}

void save_web_settings(float alertDelta, float calibWeight, float emaAlpha) {
  EEPROM.put(EEPROM_ADDR_ALERT_DELTA, alertDelta);
  EEPROM.put(EEPROM_ADDR_CALIB_WEIGHT, calibWeight);
  EEPROM.put(EEPROM_ADDR_EMA_ALPHA, emaAlpha);
  byte magic2 = EEPROM_MAGIC2_VALUE;
  EEPROM.put(EEPROM_ADDR_MAGIC2, magic2);
  EEPROM.commit();

  _alertDelta = alertDelta;
  _calibWeight = calibWeight;
  _emaAlpha = emaAlpha;
  _settingsLoaded = true;
}

// ─── Расширенные настройки (sleep, LCD BL, AP pass) ──────────────────────
static uint32_t _sleepSec    = 900UL;
static uint16_t _lcdBlSec    = 30;
static char     _apPass[24]  = "12345678";
static bool     _extLoaded   = false;

void ext_settings_init() {
  if (_extLoaded) return;
  // magic3 по адресу 64 (после AP_PASS[24]: 40..63)
  byte magic3 = 0;
  EEPROM.get(EEPROM_ADDR_MAGIC3, magic3);
  if (magic3 != EEPROM_MAGIC3_VALUE) {
    _sleepSec = 900UL;
    _lcdBlSec = 30;
    strncpy(_apPass, "12345678", sizeof(_apPass));
  } else {
    EEPROM.get(EEPROM_ADDR_SLEEP_SEC, _sleepSec);
    EEPROM.get(EEPROM_ADDR_LCD_BL_SEC, _lcdBlSec);
    EEPROM.get(EEPROM_ADDR_AP_PASS, _apPass);
    _apPass[23] = '\0';  // гарантируем нуль-терминатор

    if (_sleepSec < 30UL || _sleepSec > 86400UL) _sleepSec = 900UL;
    if (_lcdBlSec > 3600) _lcdBlSec = 30;
    if (_apPass[0] == '\0' || strlen(_apPass) < 8) strncpy(_apPass, "12345678", sizeof(_apPass));
  }
  _extLoaded = true;
}

static void _ext_save() {
  EEPROM.put(EEPROM_ADDR_SLEEP_SEC, _sleepSec);
  EEPROM.put(EEPROM_ADDR_LCD_BL_SEC, _lcdBlSec);
  EEPROM.put(EEPROM_ADDR_AP_PASS, _apPass);
  byte magic3 = EEPROM_MAGIC3_VALUE;
  EEPROM.put(EEPROM_ADDR_MAGIC3, magic3);
  EEPROM.commit();
}

uint32_t get_sleep_sec() { if (!_extLoaded) ext_settings_init(); return _sleepSec; }
void set_sleep_sec(uint32_t sec) {
  if (sec < 30UL || sec > 86400UL) return;
  _sleepSec = sec; _ext_save();
}

uint16_t get_lcd_bl_sec() { if (!_extLoaded) ext_settings_init(); return _lcdBlSec; }
void set_lcd_bl_sec(uint16_t sec) {
  _lcdBlSec = sec; _ext_save();
}

void get_ap_pass(char *buf, size_t maxLen) {
  if (!_extLoaded) ext_settings_init();
  strncpy(buf, _apPass, maxLen - 1);
  buf[maxLen - 1] = '\0';
}

void set_ap_pass(const char *pass) {
  if (!pass || strlen(pass) < 8 || strlen(pass) > 23) return;
  strncpy(_apPass, pass, sizeof(_apPass) - 1);
  _apPass[sizeof(_apPass) - 1] = '\0';
  _ext_save();
}

// ─── Telegram настройки ───────────────────────────────────────────────────
static char _tgToken[50]  = "";
static char _tgChatId[16] = "";
static bool _tgLoaded     = false;

void tg_settings_init() {
  if (_tgLoaded) return;
  byte magic = 0;
  EEPROM.get(EEPROM_ADDR_TG_MAGIC, magic);
  if (magic != EEPROM_MAGIC_TG_VALUE) {
    _tgToken[0]  = '\0';
    _tgChatId[0] = '\0';
  } else {
    EEPROM.get(EEPROM_ADDR_TG_TOKEN,  _tgToken);
    EEPROM.get(EEPROM_ADDR_TG_CHATID, _tgChatId);
    _tgToken[49]  = '\0';
    _tgChatId[15] = '\0';
  }
  _tgLoaded = true;
}

void get_tg_token(char *buf, size_t maxLen) {
  if (!_tgLoaded) tg_settings_init();
  strncpy(buf, _tgToken, maxLen - 1);
  buf[maxLen - 1] = '\0';
}

void set_tg_token(const char *token) {
  if (!token) return;
  strncpy(_tgToken, token, sizeof(_tgToken) - 1);
  _tgToken[sizeof(_tgToken) - 1] = '\0';
  EEPROM.put(EEPROM_ADDR_TG_TOKEN,  _tgToken);
  byte magic = EEPROM_MAGIC_TG_VALUE;
  EEPROM.put(EEPROM_ADDR_TG_MAGIC, magic);
  EEPROM.commit();
}

void get_tg_chatid(char *buf, size_t maxLen) {
  if (!_tgLoaded) tg_settings_init();
  strncpy(buf, _tgChatId, maxLen - 1);
  buf[maxLen - 1] = '\0';
}

void set_tg_chatid(const char *chatid) {
  if (!chatid) return;
  strncpy(_tgChatId, chatid, sizeof(_tgChatId) - 1);
  _tgChatId[sizeof(_tgChatId) - 1] = '\0';
  EEPROM.put(EEPROM_ADDR_TG_CHATID, _tgChatId);
  byte magic = EEPROM_MAGIC_TG_VALUE;
  EEPROM.put(EEPROM_ADDR_TG_MAGIC, magic);
  EEPROM.commit();
}

// ─── WiFi режим и STA credentials ─────────────────────────────────────────
static uint8_t _wifiMode     = 0;           // 0=AP, 1=STA
static char    _wifiSsid[33] = "";
static char    _wifiStaPass[33] = "";
static bool    _wifiCfgLoaded = false;

void wifi_settings_init() {
  if (_wifiCfgLoaded) return;
  byte magic = 0;
  EEPROM.get(EEPROM_ADDR_WIFI_MAGIC, magic);
  if (magic != EEPROM_MAGIC_WIFI_VALUE) {
    _wifiMode = 0;
    _wifiSsid[0] = '\0';
    _wifiStaPass[0] = '\0';
  } else {
    EEPROM.get(EEPROM_ADDR_WIFI_MODE, _wifiMode);
    EEPROM.get(EEPROM_ADDR_WIFI_SSID, _wifiSsid);
    EEPROM.get(EEPROM_ADDR_WIFI_PASS, _wifiStaPass);
    _wifiSsid[32]    = '\0';
    _wifiStaPass[32] = '\0';
    if (_wifiMode > 1) _wifiMode = 0;
  }
  _wifiCfgLoaded = true;
}

static void _wifi_save() {
  byte magic = EEPROM_MAGIC_WIFI_VALUE;
  EEPROM.put(EEPROM_ADDR_WIFI_MAGIC, magic);
  EEPROM.put(EEPROM_ADDR_WIFI_MODE,  _wifiMode);
  EEPROM.put(EEPROM_ADDR_WIFI_SSID,  _wifiSsid);
  EEPROM.put(EEPROM_ADDR_WIFI_PASS,  _wifiStaPass);
  EEPROM.commit();
}

uint8_t get_wifi_mode() { if (!_wifiCfgLoaded) wifi_settings_init(); return _wifiMode; }
void set_wifi_mode(uint8_t m) {
  if (m > 1) return;
  _wifiMode = m; _wifi_save();
}

void get_wifi_ssid(char *buf, size_t maxLen) {
  if (!_wifiCfgLoaded) wifi_settings_init();
  strncpy(buf, _wifiSsid, maxLen - 1);
  buf[maxLen - 1] = '\0';
}
void set_wifi_ssid(const char *ssid) {
  if (!ssid) return;
  strncpy(_wifiSsid, ssid, sizeof(_wifiSsid) - 1);
  _wifiSsid[sizeof(_wifiSsid) - 1] = '\0';
  _wifi_save();
}

void get_wifi_sta_pass(char *buf, size_t maxLen) {
  if (!_wifiCfgLoaded) wifi_settings_init();
  strncpy(buf, _wifiStaPass, maxLen - 1);
  buf[maxLen - 1] = '\0';
}
void set_wifi_sta_pass(const char *pass) {
  if (!pass) return;
  strncpy(_wifiStaPass, pass, sizeof(_wifiStaPass) - 1);
  _wifiStaPass[sizeof(_wifiStaPass) - 1] = '\0';
  _wifi_save();
}
