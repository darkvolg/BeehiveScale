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
  #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    analogSetAttenuation(ADC_ATTEN_DB_11);
  #else
    analogSetAttenuation(ADC_11db);
  #endif
#endif
  // Усреднение по 10 выборкам при старте для стабильности
  float sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += bat_read_raw();
    delay(10);
  }
  _batSmoothed = sum / 10.0f;
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
