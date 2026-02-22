#include "Temperature.h"

#ifdef TEMP_SENSOR_DS18B20
  #include <OneWire.h>
  #include <DallasTemperature.h>
  static OneWire           _ow(TEMP_PIN);
  static DallasTemperature _ds(&_ow);
#endif

bool temp_init() {
#ifdef TEMP_SENSOR_DS18B20
  _ds.begin();
  uint8_t count = _ds.getDeviceCount();
  Serial.print(F("[Temp] DS18B20 sensors found: "));
  Serial.println(count);
  if (count == 0) return false;
  _ds.setResolution(12);
  _ds.setWaitForConversion(false);
  _ds.requestTemperatures();
  return true;
#endif
  return false;
}

TempData temp_read() {
  TempData td;

#ifdef TEMP_SENSOR_DS18B20
  float t = _ds.getTempCByIndex(0);
  _ds.requestTemperatures();
  if (t == DEVICE_DISCONNECTED_C || t < -55.0f || t > 125.0f) {
    td.valid = false;
    td.temperature = TEMP_ERROR_VALUE;
  } else {
    td.temperature = t;
    td.valid = true;
  }
  td.humidity = TEMP_ERROR_VALUE;
#endif

  return td;
}
