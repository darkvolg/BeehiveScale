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
