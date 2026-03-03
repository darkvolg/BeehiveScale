#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

// ─── Конфигурация логгера ─────────────────────────────────────────────────
#define LOG_INTERVAL_MS    60000UL   // 1 минута между записями
#define LOG_MAX_SIZE      102400UL   // 100 КБ — ротация (переименовать → log_old.csv)
#define LOG_FILE          "/log.csv"
#define LOG_FILE_OLD      "/log_old.csv"

// DS18B20 перенесён на GPIO3 (D9/RX) — конфликт с SPI MOSI устранён.
#define USE_SD_CARD
#define SD_CS_PIN 15

// ─── API ──────────────────────────────────────────────────────────────────
bool   log_init();
// batPct: процент заряда батареи; если < 5 — запись пропускается (защита от разряда)
void   log_append(const String &datetime, float weight, float tempC,
                  float humidity, float batV, int batPct);
void   log_clear();
size_t log_size();
bool   log_exists();
// Свободное место на SD (байт); 0 если SD недоступна
uint32_t log_free_space();
// Парсит CSV и возвращает JSON-массив последних maxRows строк
String   log_to_json(int maxRows = 200);
// Стримит CSV только за указанную дату (формат: "DD.MM.YYYY") прямо в поток
// Возвращает кол-во строк; если date пустая — возвращает весь файл
size_t   log_stream_csv_date(Stream &out, const String &date);
// Суточная статистика: min/max вес и температура за сегодня
struct DayStat {
  float wMin, wMax, tMin, tMax;
  int   count;
  bool  valid;
};
DayStat  log_day_stat(const String &todayDate);
// Возвращает true если SD недоступна и используется LittleFS как резервный FS
bool     log_using_fallback();
// Возвращает true если файловая система (SD или LittleFS) смонтирована и доступна
bool     log_fs_ok();

#endif
