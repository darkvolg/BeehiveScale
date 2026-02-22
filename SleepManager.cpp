#include "SleepManager.h"
#include "Temperature.h"

#if defined(ESP32)
#include <esp_sleep.h>
#include "driver/rtc_io.h"
#endif

#if defined(ESP32)
RTC_DATA_ATTR static SleepPersistData _persist;
#else
static SleepPersistData _persist;  // загружается из RTC RAM в sleep_load_persistent
#endif

void sleep_init() {
#if PERIPHERAL_POWER_PIN >= 0
  pinMode(PERIPHERAL_POWER_PIN, OUTPUT);
  digitalWrite(PERIPHERAL_POWER_PIN, HIGH);
#endif
#if defined(ESP32)
  esp_sleep_enable_ext0_wakeup((gpio_num_t)SLEEP_WAKEUP_PIN, LOW);
#endif
  Serial.println(F("[Sleep] Init OK"));
}

void sleep_load_persistent(SleepPersistData &data) {
#if defined(ESP8266)
  ESP.rtcUserMemoryRead(0, (uint32_t*)&_persist, sizeof(_persist));
#endif
  if (_persist.magic == 0xDEADBEEF) {
    data = _persist;
    Serial.print(F("[Sleep] Wakeup #"));
    Serial.println(_persist.wakeupCount);
  } else {
    data.magic = 0xDEADBEEF;
    data.lastWeight = 0.0f;
    data.lastTempC = TEMP_ERROR_VALUE;
    data.wakeupCount = 0;
    data.alertSent = false;
    Serial.println(F("[Sleep] First boot."));
  }
}

void sleep_save_persistent(const SleepPersistData &data) {
  _persist = data;
  _persist.magic = 0xDEADBEEF;
#if defined(ESP8266)
  ESP.rtcUserMemoryWrite(0, (uint32_t*)&_persist, sizeof(_persist));
#endif
}

void sleep_enter(uint64_t seconds) {
  Serial.print(F("[Sleep] Going to sleep for "));
  Serial.print(seconds);
  Serial.println(F(" sec..."));
  Serial.flush();

#if PERIPHERAL_POWER_PIN >= 0
  digitalWrite(PERIPHERAL_POWER_PIN, LOW);
#if defined(ESP32)
  rtc_gpio_init((gpio_num_t)PERIPHERAL_POWER_PIN);
  rtc_gpio_set_direction((gpio_num_t)PERIPHERAL_POWER_PIN, RTC_GPIO_MODE_OUTPUT_ONLY);
  rtc_gpio_set_level((gpio_num_t)PERIPHERAL_POWER_PIN, 0);
#endif
#endif

#if defined(ESP32)
  if (seconds > 0) {
    esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
  }
  esp_deep_sleep_start();
#elif defined(ESP8266)
  if (seconds > 0) {
    ESP.deepSleep(seconds * 1000000ULL);
  } else {
    ESP.deepSleep(0);  // Бесконечный сон, пробуждение по RST
  }
#endif
}

bool sleep_was_wakeup_by_timer() {
#if defined(ESP32)
  return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
#else
  return false;
#endif
}

bool sleep_was_wakeup_by_button() {
#if defined(ESP32)
  return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
#else
  return false;
#endif
}
