#include "Battery.h"

static float _batSmoothed = 0.0f;
static bool  _batInitialized = false;

static float bat_read_raw() {
  int raw = analogRead(BAT_PIN);
  float vAdc = raw * 3.3f / BAT_ADC_MAX;
  return vAdc * BAT_DIVIDER_RATIO;
}

void bat_init() {
#if defined(ESP32)
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
#endif
  // Первичное чтение для инициализации EMA
  _batSmoothed = bat_read_raw();
  _batInitialized = true;
}

float bat_voltage() {
  if (!_batInitialized) return 0.0f;

  float raw = bat_read_raw();
  _batSmoothed = BAT_EMA_ALPHA * raw + (1.0f - BAT_EMA_ALPHA) * _batSmoothed;
  return _batSmoothed;
}

int bat_percent() {
  float v = _batSmoothed;
  float pct = (v - BAT_VMIN) / (BAT_VMAX - BAT_VMIN) * 100.0f;
  return constrain((int)pct, 0, 100);
}
