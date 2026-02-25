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
  if (isnan(w) || w < 0.0f || w > 500.0f) return fallback;
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
  // Проверяем magic3 — хранится в первом байте EEPROM_ADDR_AP_PASS+23
  // Используем отдельную ячейку для magic3: запишем флаг в конец блока AP пароля
  // magic3 хранится по адресу EEPROM_ADDR_AP_PASS + 23 (последний байт 24-байтного поля)
  // Но лучше использовать отдельный байт: addr = EEPROM_ADDR_AP_PASS - 1 = 39... нет, 40-1=39
  // Используем байт по адресу 63 как magic3
  byte magic3 = 0;
  EEPROM.get(63, magic3);
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
  EEPROM.put(63, magic3);
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
