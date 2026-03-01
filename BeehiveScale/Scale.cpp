#include "Scale.h"
#include <math.h>

static int _dtPin = -1;
static int _sckPin = -1;

void scale_init(HX711 &scale, int dtPin, int sckPin) {
  _dtPin = dtPin;
  _sckPin = sckPin;
  scale.begin(dtPin, sckPin);
  // GPIO16 не поддерживает INPUT_PULLUP на ESP8266 — только INPUT_PULLDOWN_16
  // Для GPIO14 можно было бы INPUT_PULLUP, но GPIO16 — нет
  if (dtPin != 16) {
    pinMode(dtPin, INPUT_PULLUP);
  }
}

// Power-cycle HX711: SCK HIGH >60us = power down, затем LOW = power up
static void scale_power_cycle(HX711 &scale) {
  // НЕ вызываем Serial.println — GPIO1(TX)=SCK, Serial.end() уже вызван
  scale.power_down();
  delayMicroseconds(100);
  scale.power_up();
  delay(400);  // HX711 нужно ~400ms после включения для стабилизации
}

bool check_sensor(HX711 &scale) {
  bool ready = scale.wait_ready_timeout(1500);  // 1.5с вместо 3с — WDT safe
  if (!ready) {
    scale_power_cycle(scale);  // power-cycle ~500ms
    ready = scale.wait_ready_timeout(1500);
  }
  return ready;
}

float scale_read_weight(HX711 &scale, int samples) {
  if (!scale.wait_ready_timeout(1000)) {
    return NAN;
  }

  float val = scale.get_units(samples);

  // Защита от залипшего HX711: проверяем два сырых чтения
  // Залипание может быть на любом значении, не только 0
  // Проверка залипания: 3 подряд одинаковых raw = залипание
  // (2 одинаковых — нормально для стабильного груза)
  {
    long raw1 = scale.read();
    long raw2 = scale.read();
    long raw3 = scale.read();
    if (raw1 == raw2 && raw2 == raw3) {
      scale_power_cycle(scale);
      if (!scale.wait_ready_timeout(1000)) return NAN;
      val = scale.get_units(samples);
    }
  }

  return val;
}
