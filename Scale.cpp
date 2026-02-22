#include "Scale.h"
#include <math.h>

void scale_init(HX711 &scale, int dtPin, int sckPin) {
  scale.begin(dtPin, sckPin);
}

bool check_sensor(HX711 &scale) {
  bool ready = scale.wait_ready_timeout(SENSOR_READY_TIMEOUT_MS);
  if (!ready) {
    Serial.println(F("[HX711] Sensor timeout! Check wiring."));
  }
  return ready;
}

float scale_read_weight(HX711 &scale, int samples) {
  if (!scale.wait_ready_timeout(1000)) {
    return NAN;
  }
  return scale.get_units(samples);
}
