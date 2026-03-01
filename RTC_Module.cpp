#include "RTC_Module.h"

static RTC_DS3231 _rtc;
static bool       _rtcOk = false;

bool rtc_init() {
  _rtcOk = _rtc.begin();
  if (!_rtcOk) {
    Serial.println(F("[RTC] DS3231 not found!"));
    return false;
  }
  if (_rtc.lostPower()) {
    Serial.println(F("[RTC] Power lost, setting compile time."));
    _rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  Serial.println(F("[RTC] OK"));
  return true;
}

TimeStamp rtc_now() {
  TimeStamp ts = {};
  ts.valid = _rtcOk;
  if (!_rtcOk) return ts;

  DateTime dt = _rtc.now();
  ts.year   = dt.year();
  ts.month  = dt.month();
  ts.day    = dt.day();
  ts.hour   = dt.hour();
  ts.minute = dt.minute();
  ts.second = dt.second();
  return ts;
}

bool rtc_set(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s) {
  if (!_rtcOk) return false;
  _rtc.adjust(DateTime(y, mo, d, h, mi, s));
  return true;
}

bool rtc_lost_power() {
  return _rtcOk ? _rtc.lostPower() : true;
}

float rtc_temperature() {
  if (!_rtcOk) return NAN;
  return _rtc.getTemperature();
}

String rtc_format_datetime(const TimeStamp &t) {
  if (!t.valid) return F("??/?? ??:??  ");
  char buf[17];
  snprintf(buf, sizeof(buf), "%02u.%02u.%04u %02u:%02u",
           t.day, t.month, t.year, t.hour, t.minute);
  return String(buf);
}

String rtc_format_time(const TimeStamp &t) {
  if (!t.valid) return F("??:??:??");
  char buf[9];
  snprintf(buf, sizeof(buf), "%02u:%02u:%02u", t.hour, t.minute, t.second);
  return String(buf);
}
