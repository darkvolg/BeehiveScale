#ifndef SLEEP_MANAGER_H
#define SLEEP_MANAGER_H

#include <Arduino.h>

#define SLEEP_MODE_CONTINUOUS
#define SLEEP_INTERVAL_SEC   900UL
#define SLEEP_WAKEUP_PIN     0

#if defined(ESP8266)
#define PERIPHERAL_POWER_PIN -1
#else
#define PERIPHERAL_POWER_PIN 26
#endif

struct SleepPersistData {
  uint32_t magic;
  float    lastWeight;
  float    lastTempC;
  uint32_t wakeupCount;
  bool     alertSent;
};

void sleep_init();
void sleep_load_persistent(SleepPersistData &data);
void sleep_save_persistent(const SleepPersistData &data);
void sleep_enter(uint64_t seconds);
bool sleep_was_wakeup_by_timer();
bool sleep_was_wakeup_by_button();

#endif
