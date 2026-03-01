#include "Logger.h"
#include <math.h>

// ─── Файловая система ─────────────────────────────────────────────────────
#ifdef USE_SD_CARD
  #include <SPI.h>
  #include <SD.h>
  #if defined(ESP8266)
    #include <LittleFS.h>
  #elif defined(ESP32)
    #include <LittleFS.h>
  #endif
  static bool _sdOk      = false;
  static bool _fallback  = false;  // true = SD недоступна, используем LittleFS
#else
  #if defined(ESP8266)
    #include <LittleFS.h>
  #elif defined(ESP32)
    #include <LittleFS.h>
  #endif
  static bool _flashOk   = false;
  static bool _fallback  = false;
#endif

static const char CSV_HEADER[] = "datetime,weight_kg,temp_c,humidity_pct,bat_v\n";

// ─── Внутренние хелперы (абстракция над SD / LittleFS) ──────────────────

static bool _fs_exists(const char *path) {
#ifdef USE_SD_CARD
  if (!_fallback) return SD.exists(path);
  return LittleFS.exists(path);
#else
  return LittleFS.exists(path);
#endif
}

static bool _fs_remove(const char *path) {
#ifdef USE_SD_CARD
  if (!_fallback) return SD.remove(path);
  return LittleFS.remove(path);
#else
  return LittleFS.remove(path);
#endif
}

static bool _fs_rename(const char *from, const char *to) {
#ifdef USE_SD_CARD
  if (!_fallback) {
    // ESP8266 SD нет rename — эмулируем копированием
    File src = SD.open(from, FILE_READ);
    if (!src) return false;
    File dst = SD.open(to, FILE_WRITE);
    if (!dst) { src.close(); return false; }
    uint8_t buf[256];
    while (src.available()) {
      int n = src.read(buf, sizeof(buf));
      if (n > 0) dst.write(buf, n);
    }
    src.close(); dst.close();
    SD.remove(from);
    return true;
  }
  return LittleFS.rename(from, to);
#else
  return LittleFS.rename(from, to);
#endif
}

static File _fs_open_read(const char *path) {
#ifdef USE_SD_CARD
  if (!_fallback) return SD.open(path, FILE_READ);
  return LittleFS.open(path, "r");
#else
  return LittleFS.open(path, "r");
#endif
}

static File _fs_open_write(const char *path) {
#ifdef USE_SD_CARD
  if (!_fallback) return SD.open(path, FILE_WRITE);
  return LittleFS.open(path, "w");
#else
  return LittleFS.open(path, "w");
#endif
}

static File _fs_open_append(const char *path) {
#ifdef USE_SD_CARD
  if (!_fallback) {
  #if defined(ESP32)
    return SD.open(path, FILE_APPEND);
  #else
    return SD.open(path, FILE_WRITE);  // ESP8266 SD: FILE_WRITE = append если файл есть
  #endif
  }
  return LittleFS.open(path, "a");
#else
  return LittleFS.open(path, "a");
#endif
}

static bool _fs_ok() {
#ifdef USE_SD_CARD
  return _sdOk || _fallback;
#else
  return _flashOk;
#endif
}

// ─── Инициализация ────────────────────────────────────────────────────────

bool log_init() {
#ifdef USE_SD_CARD
  _sdOk = SD.begin(SD_CS_PIN);
  if (!_sdOk) {
    Serial.println(F("[Log] SD FAILED — trying LittleFS fallback"));
    _fallback = LittleFS.begin();
    if (!_fallback) {
      Serial.println(F("[Log] LittleFS fallback FAILED too"));
      return false;
    }
    Serial.println(F("[Log] Using LittleFS as fallback"));
  } else {
    _fallback = false;
    Serial.println(F("[Log] SD OK"));
  }
#else
  _flashOk = LittleFS.begin();
  if (!_flashOk) {
    Serial.println(F("[Log] LittleFS FAILED"));
    return false;
  }
  Serial.println(F("[Log] LittleFS OK"));
#endif

  // Создать файл с заголовком если не существует
  if (!_fs_exists(LOG_FILE)) {
    File f = _fs_open_write(LOG_FILE);
    if (f) { f.print(CSV_HEADER); f.close(); }
  } else {
    // Пункт 6: проверяем заголовок — если повреждён, пересоздаём
    File f = _fs_open_read(LOG_FILE);
    if (f) {
      String hdr = f.readStringUntil('\n');
      f.close();
      hdr.trim();
      if (!hdr.startsWith("datetime")) {
        Serial.println(F("[Log] Header invalid — recreating log"));
        File fw = _fs_open_write(LOG_FILE);
        if (fw) { fw.print(CSV_HEADER); fw.close(); }
      }
    }
  }

  Serial.print(F("[Log] Ready"));
  if (_fallback) Serial.print(F(" [fallback:LittleFS]"));
  Serial.print(F(", size="));
  Serial.println(log_size());
  return true;
}

// ─── Пункт 6: Валидация данных перед записью ─────────────────────────────

static bool _validate_row(const String &datetime, float weight, float tempC,
                           float humidity, float batV) {
  if (datetime.length() < 5)          return false;  // пустая/слишком короткая дата
  if (isnan(weight) || isinf(weight)) return false;
  if (weight < -5.0f || weight > 500.0f) return false;  // физически невозможный вес
  if (!isnan(tempC) && !isinf(tempC)) {
    if (tempC < -50.0f || tempC > 100.0f) return false;  // невозможная температура
  }
  if (!isnan(humidity) && !isinf(humidity)) {
    if (humidity < 0.0f || humidity > 100.0f) return false;
  }
  if (!isnan(batV) && !isinf(batV)) {
    if (batV < 0.0f || batV > 6.0f) return false;  // батарея не может быть > 6В
  }
  return true;
}

// ─── Запись строки ────────────────────────────────────────────────────────

void log_append(const String &datetime, float weight, float tempC,
                float humidity, float batV, int batPct) {
  if (!_fs_ok()) return;

  // Защита от записи при критически низком заряде батареи
  if (batPct < 5) {
    Serial.println(F("[Log] Skip: low battery"));
    return;
  }

  // Пункт 6: Валидация данных
  if (!_validate_row(datetime, weight, tempC, humidity, batV)) {
    Serial.println(F("[Log] Skip: invalid data"));
    return;
  }

  // Ротация: если файл > LOG_MAX_SIZE, переименовать и начать новый
  if (log_size() >= LOG_MAX_SIZE) {
    if (_fs_exists(LOG_FILE_OLD)) _fs_remove(LOG_FILE_OLD);
    _fs_rename(LOG_FILE, LOG_FILE_OLD);
    File fn = _fs_open_write(LOG_FILE);
    if (fn) { fn.print(CSV_HEADER); fn.close(); }
    Serial.println(F("[Log] Rotated"));
  }

  File f = _fs_open_append(LOG_FILE);
  if (!f) {
    Serial.println(F("[Log] Open FAILED"));
#ifdef USE_SD_CARD
    // Пункт 7: если запись на SD провалилась — переключаемся на LittleFS
    if (!_fallback) {
      Serial.println(F("[Log] Switching to LittleFS fallback"));
      _sdOk = false;
      _fallback = LittleFS.begin();
      if (_fallback) {
        File ff = LittleFS.open(LOG_FILE, "a");
        if (!ff) {
          File fw = LittleFS.open(LOG_FILE, "w");
          if (fw) { fw.print(CSV_HEADER); fw.close(); }
          ff = LittleFS.open(LOG_FILE, "a");
        }
        if (ff) {
          char buf[64];
          snprintf(buf, sizeof(buf), "%.2f,%.1f,%.1f,%.2f", weight, tempC, humidity, batV);
          ff.print(datetime); ff.print(','); ff.print(buf); ff.print('\n');
          ff.close();
        }
      }
    }
#endif
    return;
  }

  char buf[64];
  snprintf(buf, sizeof(buf), "%.2f,%.1f,%.1f,%.2f", weight, tempC, humidity, batV);
  f.print(datetime);
  f.print(',');
  f.print(buf);
  f.print('\n');
  f.close();
}

void log_clear() {
  if (!_fs_ok()) return;
  if (_fs_exists(LOG_FILE))     _fs_remove(LOG_FILE);
  if (_fs_exists(LOG_FILE_OLD)) _fs_remove(LOG_FILE_OLD);
  File f = _fs_open_write(LOG_FILE);
  if (f) { f.print(CSV_HEADER); f.close(); }
  Serial.println(F("[Log] Cleared"));
}

size_t log_size() {
  if (!_fs_ok()) return 0;
  if (!_fs_exists(LOG_FILE)) return 0;
  File f = _fs_open_read(LOG_FILE);
  if (!f) return 0;
  size_t sz = f.size();
  f.close();
  return sz;
}

bool log_exists() {
  if (!_fs_ok()) return false;
  return _fs_exists(LOG_FILE);
}

uint32_t log_free_space() {
#ifdef USE_SD_CARD
  if (_fallback) {
#if defined(ESP8266)
    FSInfo info;
    if (LittleFS.info(info)) return (uint32_t)(info.totalBytes - info.usedBytes);
#elif defined(ESP32)
    return (uint32_t)(LittleFS.totalBytes() - LittleFS.usedBytes());
#endif
    return 0;
  }
  return 0;  // ESP8266 SD library does not expose free space
#else
  if (!_flashOk) return 0;
#if defined(ESP8266)
  FSInfo info;
  if (LittleFS.info(info)) return (uint32_t)(info.totalBytes - info.usedBytes);
  return 0;
#elif defined(ESP32)
  return (uint32_t)(LittleFS.totalBytes() - LittleFS.usedBytes());
#else
  return 0;
#endif
#endif
}

// Возвращает true если активен резервный LittleFS (SD недоступна)
bool log_using_fallback() {
  return _fallback;
}

// ─── Фича 11: стрим CSV за указанную дату ────────────────────────────────
// date — строка вида "DD.MM.YYYY"; если пустая — отдаём весь файл
size_t log_stream_csv_date(Stream &out, const String &date) {
  if (!_fs_ok() || !_fs_exists(LOG_FILE)) return 0;
  File f = _fs_open_read(LOG_FILE);
  if (!f) return 0;

  // Всегда печатаем заголовок
  out.print(CSV_HEADER);
  size_t count = 0;

  // Пропускаем заголовок из файла
  f.readStringUntil('\n');

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    if (date.length() > 0) {
      // datetime в формате "DD.MM.YYYY HH:MM:SS" → первые 10 символов = "DD.MM.YYYY"
      // date параметр может прийти как "YYYY-MM-DD" (из браузера) или "DD.MM.YYYY"
      String rowDate = line.substring(0, 10);  // "DD.MM.YYYY"
      // Нормализуем date к "DD.MM.YYYY" если пришло "YYYY-MM-DD"
      String cmpDate = date;
      if (date.length() == 10 && date.charAt(4) == '-') {
        // "YYYY-MM-DD" → "DD.MM.YYYY"
        cmpDate = date.substring(8, 10) + "." + date.substring(5, 7) + "." + date.substring(0, 4);
      }
      if (!rowDate.equals(cmpDate)) continue;
    }
    out.print(line);
    out.print('\n');
    count++;
  }
  f.close();
  return count;
}

// ─── Фича 12: суточная статистика min/max вес и температура ──────────────
DayStat log_day_stat(const String &todayDate) {
  DayStat s;
  s.wMin = 1e9f; s.wMax = -1e9f;
  s.tMin = 1e9f; s.tMax = -1e9f;
  s.count = 0; s.valid = false;

  if (!_fs_ok() || !_fs_exists(LOG_FILE)) return s;
  File f = _fs_open_read(LOG_FILE);
  if (!f) return s;

  // Нормализуем дату к "DD.MM.YYYY"
  String cmpDate = todayDate;
  if (todayDate.length() == 10 && todayDate.charAt(4) == '-') {
    cmpDate = todayDate.substring(8,10) + "." + todayDate.substring(5,7) + "." + todayDate.substring(0,4);
  }

  f.readStringUntil('\n');  // пропустить заголовок
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    // Фильтр по дате
    if (cmpDate.length() > 0 && !line.startsWith(cmpDate)) continue;

    int c1 = line.indexOf(',');
    if (c1 < 0) continue;
    int c2 = line.indexOf(',', c1+1);
    if (c2 < 0) continue;
    int c3 = line.indexOf(',', c2+1);
    if (c3 < 0) continue;

    float w = line.substring(c1+1, c2).toFloat();
    float t = line.substring(c2+1, c3).toFloat();

    if (isnan(w) || w < -5.0f || w > 500.0f) continue;

    if (w < s.wMin) s.wMin = w;
    if (w > s.wMax) s.wMax = w;
    if (!isnan(t) && t > -90.0f) {
      if (t < s.tMin) s.tMin = t;
      if (t > s.tMax) s.tMax = t;
    }
    s.count++;
  }
  f.close();

  if (s.count > 0) s.valid = true;
  else { s.wMin=0; s.wMax=0; s.tMin=0; s.tMax=0; }
  return s;
}

// ─── Парсит CSV-лог и возвращает JSON-массив для графика/экспорта ────────
// Формат CSV: datetime,weight_kg,temp_c,humidity_pct,bat_v
String log_to_json(int maxRows) {
  // Ограничиваем максимум на ESP8266 — heap ~40 КБ, каждая строка ~80 байт JSON
#if defined(ESP8266)
  if (maxRows > 100) maxRows = 100;
#else
  if (maxRows > 200) maxRows = 200;
#endif
  if (!_fs_ok() || !_fs_exists(LOG_FILE)) return "[]";
  File f = _fs_open_read(LOG_FILE);
  if (!f) return "[]";

  // Считаем строки чтобы пропустить лишние
  int totalLines = 0;
  while (f.available()) {
    char c = f.read();
    if (c == '\n') totalLines++;
  }
  f.close();
  f = _fs_open_read(LOG_FILE);
  if (!f) return "[]";

  // Пропускаем заголовок
  f.readStringUntil('\n');
  int dataLines = totalLines - 1;
  int skipLines = (dataLines > maxRows) ? (dataLines - maxRows) : 0;

  String out = "[";
  bool first = true;
  int lineIdx = 0;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    if (lineIdx++ < skipLines) continue;

    // Парсим: datetime,weight,temp,hum,batV
    int c1 = line.indexOf(',');
    if (c1 < 0) continue;
    int c2 = line.indexOf(',', c1+1);
    if (c2 < 0) continue;
    int c3 = line.indexOf(',', c2+1);
    if (c3 < 0) continue;
    int c4 = line.indexOf(',', c3+1);
    if (c4 < 0) continue;

    String dt  = line.substring(0, c1);
    String wgt = line.substring(c1+1, c2);
    String tmp = line.substring(c2+1, c3);
    String hum = line.substring(c3+1, c4);
    String bat = line.substring(c4+1);

    // Пункт 6: пропускаем невалидные строки при чтении
    float w = wgt.toFloat();
    if (isnan(w) || isinf(w) || w < -5.0f || w > 500.0f) continue;

    if (!first) out += ',';
    out += F("{\"dt\":\"");
    out += dt;
    out += F("\",\"w\":");
    out += wgt;
    out += F(",\"t\":");
    out += tmp;
    out += F(",\"h\":");
    out += hum;
    out += F(",\"b\":");
    out += bat;
    out += '}';
    first = false;
  }
  f.close();
  out += ']';
  return out;
}
