#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

// ─── Конфигурация логгера ─────────────────────────────────────────────────
#define LOG_INTERVAL_MS    60000UL   // 1 минута между записями
#define LOG_MAX_SIZE      102400UL   // 100 КБ — ротация (переименовать → log_old.csv)
#define LOG_FILE          "/log.csv"
#define LOG_FILE_OLD      "/log_old.csv"

// Раскомментируйте, если используется SD-карта вместо LittleFS:
// Требует перепайки: HX711 DT→GPIO16, HX711 SCK→GPIO1, DS18B20→GPIO3, SD CS→GPIO15
#define USE_SD_CARD
#define SD_CS_PIN 15

// ─── API ──────────────────────────────────────────────────────────────────
bool   log_init();
void   log_append(const String &datetime, float weight, float tempC,
                  float humidity, float batV);
void   log_clear();
size_t log_size();
bool   log_exists();

#endif
