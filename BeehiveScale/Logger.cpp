#include "Logger.h"
#include <math.h>

// ─── Файловая система ─────────────────────────────────────────────────────
#ifdef USE_SD_CARD
  #include <SPI.h>
  #include <SD.h>
  #include <LittleFS.h>
  static bool _sdOk      = false;
  static bool _fallback  = false;  // true = SD недоступна, используем LittleFS
#else
  #include <LittleFS.h>
  static bool _flashOk   = false;
  static bool _fallback  = false;
#endif

// UTF-8 BOM (\xEF\xBB\xBF) + разделитель ";" для корректного открытия в Excel
// (русская локаль Excel использует ";" как разделитель столбцов)
static const char CSV_HEADER[] = "\xEF\xBB\xBF" "datetime;weight_kg;temp_c;humidity_pct;bat_v\n";

// ─── Хелпер: запятая→точка для парсинга CSV с десятичной запятой ─────────
static float commaToFloat(const String &s) {
  String tmp = s;
  tmp.replace(',', '.');
  return tmp.toFloat();
}
// Замена запятой на точку in-place для вставки в JSON
static String commaToPoint(const String &s) {
  String tmp = s;
  tmp.replace(',', '.');
  return tmp;
}

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
    // ESP8266 SD: FILE_WRITE создаёт/перезаписывает файл.
    // Для дозаписи нужно открыть и перемотать в конец (seek).
    File f = SD.open(path, FILE_WRITE);
    if (f) f.seek(f.size());
    return f;
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
      Serial.println(F("[Log] LittleFS not formatted, formatting..."));
      LittleFS.format();
      _fallback = LittleFS.begin();
    }
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
    // Проверяем заголовок: должен содержать "datetime" и ";" (новый формат).
    // Файл с BOM начинается с \xEF\xBB\xBF, поэтому indexOf("datetime") вместо startsWith.
    // Если заголовок в старом формате (запятые) или повреждён — пересоздаём.
    File f = _fs_open_read(LOG_FILE);
    if (f) {
      String hdr = f.readStringUntil('\n');
      f.close();
      hdr.trim();
      bool hdrOk = (hdr.indexOf("datetime") >= 0) && (hdr.indexOf(';') >= 0);
      if (!hdrOk) {
        Serial.println(F("[Log] Header outdated/invalid — recreating log"));
        File fw = _fs_open_write(LOG_FILE);
        if (fw) { fw.print(CSV_HEADER); fw.close(); }
      }
    }
  }

  // Проверяем что append реально работает
  File testF = _fs_open_append(LOG_FILE);
  if (!testF) {
    Serial.println(F("[Log] WARNING: append test FAILED!"));
#ifdef USE_SD_CARD
    if (!_fallback) {
      Serial.println(F("[Log] SD append broken — switching to LittleFS"));
      _sdOk = false;
      _fallback = LittleFS.begin();
      if (!_fallback) {
        LittleFS.format();
        _fallback = LittleFS.begin();
      }
      if (_fallback) {
        if (!LittleFS.exists(LOG_FILE)) {
          File fw = LittleFS.open(LOG_FILE, "w");
          if (fw) { fw.print(CSV_HEADER); fw.close(); }
        }
      }
    }
#endif
  } else {
    testF.close();
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
  if (datetime.length() == 0)         return false;  // совсем пустая дата
  if (isnan(weight) || isinf(weight)) return false;
  if (weight < -5.0f || weight > 500.0f) return false;  // физически невозможный вес
  // Значения ≤ -90 — это сентинел ошибки датчика (-99 = TEMP_ERROR_VALUE).
  // Пропускаем валидацию диапазона для них; реальные физические значения проверяем.
  if (!isnan(tempC) && !isinf(tempC) && tempC > -90.0f) {
    if (tempC < -50.0f || tempC > 100.0f) return false;
  }
  if (!isnan(humidity) && !isinf(humidity) && humidity > -90.0f) {
    if (humidity < 0.0f || humidity > 100.0f) return false;
  }
  if (!isnan(batV) && !isinf(batV)) {
    if (batV < 0.0f || batV > 6.0f) return false;  // батарея не может быть > 6В
  }
  return true;
}

// ─── Форматирование и запись одной CSV-строки ─────────────────────────────
static void _write_csv_row(File &f, const String &datetime, float weight,
                           float tempC, float humidity, float batV) {
  if (isnan(tempC)    || isinf(tempC)    || tempC    <= -90.0f) tempC    = 0.0f;
  if (isnan(humidity) || isinf(humidity) || humidity <= -90.0f) humidity = 0.0f;
  if (batV < 0.1f) batV = 0.0f;

  char wBuf[12], tBuf[12], hBuf[12], bBuf[12];
  snprintf(wBuf, sizeof(wBuf), "%.2f", weight);
  snprintf(tBuf, sizeof(tBuf), "%.1f", tempC);
  snprintf(hBuf, sizeof(hBuf), "%.1f", humidity);
  snprintf(bBuf, sizeof(bBuf), "%.2f", batV);
  for (char *p = wBuf; *p; p++) if (*p == '.') *p = ',';
  for (char *p = tBuf; *p; p++) if (*p == '.') *p = ',';
  for (char *p = hBuf; *p; p++) if (*p == '.') *p = ',';
  for (char *p = bBuf; *p; p++) if (*p == '.') *p = ',';

  f.print(datetime); f.print(';');
  f.print(wBuf);     f.print(';');
  f.print(tBuf);     f.print(';');
  f.print(hBuf);     f.print(';');
  f.print(bBuf);     f.print('\n');
}

// ─── Запись строки ────────────────────────────────────────────────────────

void log_append(const String &datetime, float weight, float tempC,
                float humidity, float batV, int batPct) {
  if (!_fs_ok()) return;

  // Защита от записи при критически низком заряде батареи.
  // batV < 1.0V — батарея не подключена (питание от USB) → не пропускать.
  if (batPct < 5 && batV > 1.0f) {
    Serial.println(F("[Log] Skip: low battery"));
    return;
  }

  // Пункт 6: Валидация данных
  if (!_validate_row(datetime, weight, tempC, humidity, batV)) {
    Serial.println(F("[Log] Skip: invalid data"));
    return;
  }

  // Ротация: если файл > LOG_MAX_SIZE — архивировать с датой
  if (log_size() >= LOG_MAX_SIZE) {
    // Формируем имя архива: /log_YYMMDD_HHMM.csv
    // datetime передаётся в формате "DD.MM.YYYY HH:MM:SS"
    char arcName[32];
    if (datetime.length() >= 16) {
      // "DD.MM.YYYY HH:MM" → "YYMMDD_HHMM"
      snprintf(arcName, sizeof(arcName), "/log_%c%c%c%c%c%c_%c%c%c%c.csv",
        datetime[8], datetime[9],  // YY (последние 2 цифры года)
        datetime[3], datetime[4],  // MM
        datetime[0], datetime[1],  // DD
        datetime[11], datetime[12], // HH
        datetime[14], datetime[15]  // MM
      );
    } else {
      // Фоллбэк — перезаписать log_old.csv
      strncpy(arcName, LOG_FILE_OLD, sizeof(arcName));
    }
    if (_fs_exists(arcName)) _fs_remove(arcName);
    _fs_rename(LOG_FILE, arcName);
    File fn = _fs_open_write(LOG_FILE);
    if (fn) { fn.print(CSV_HEADER); fn.close(); }
    Serial.print(F("[Log] Rotated → "));
    Serial.println(arcName);
  }

  File f = _fs_open_append(LOG_FILE);
  if (!f) {
    Serial.println(F("[Log] Open FAILED for append"));
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
          _write_csv_row(ff, datetime, weight, tempC, humidity, batV);
          ff.close();
        }
      }
    }
#endif
    return;
  }

  _write_csv_row(f, datetime, weight, tempC, humidity, batV);
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

bool log_fs_ok() {
  return _fs_ok();
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
    // Пропускаем повторные заголовки
    if (line.startsWith("datetime") || line.indexOf("weight_kg") >= 0) continue;
    // Фильтр по дате
    if (cmpDate.length() > 0 && !line.startsWith(cmpDate)) continue;

    int c1 = line.indexOf(';');
    if (c1 < 0) continue;
    int c2 = line.indexOf(';', c1+1);
    if (c2 < 0) continue;
    int c3 = line.indexOf(';', c2+1);
    if (c3 < 0) continue;

    float w = commaToFloat(line.substring(c1+1, c2));
    String tStr = line.substring(c2+1, c3);
    float t = (tStr.length() == 0) ? -99.0f : commaToFloat(tStr);

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
  f.seek(0);

  // Пропускаем заголовок
  f.readStringUntil('\n');
  int dataLines = totalLines - 1;
  int skipLines = (dataLines > maxRows) ? (dataLines - maxRows) : 0;

  String out = "[";
  // Pre-allocate: ~80 байт JSON на строку, снижает фрагментацию heap на ESP8266
  int rows = (dataLines < 0) ? 0 : (dataLines < maxRows ? dataLines : maxRows);
  out.reserve(2 + rows * 80);
  bool first = true;
  int lineIdx = 0;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    if (lineIdx++ < skipLines) continue;

    // Пропускаем повторные заголовки (могут появиться после ротации/перезагрузки)
    if (line.startsWith("datetime") || line.indexOf("weight_kg") >= 0) continue;

    // Парсим: datetime;weight;temp;hum;batV
    int c1 = line.indexOf(';');
    if (c1 < 0) continue;
    int c2 = line.indexOf(';', c1+1);
    if (c2 < 0) continue;
    int c3 = line.indexOf(';', c2+1);
    if (c3 < 0) continue;
    int c4 = line.indexOf(';', c3+1);
    if (c4 < 0) continue;

    String dt  = line.substring(0, c1);
    String wgt = line.substring(c1+1, c2);
    String tmp = line.substring(c2+1, c3);
    String hum = line.substring(c3+1, c4);
    String bat = line.substring(c4+1);

    // Пункт 6: пропускаем невалидные строки при чтении
    float w = commaToFloat(wgt);
    if (isnan(w) || isinf(w) || w < -5.0f || w > 500.0f) continue;

    if (!first) out += ',';
    out += F("{\"dt\":\"");
    out += dt;
    out += F("\",\"w\":");
    out += commaToPoint(wgt);
    out += F(",\"t\":");
    if (tmp.length() == 0) out += F("-99"); else out += commaToPoint(tmp);
    out += F(",\"h\":");
    if (hum.length() == 0) out += F("-99"); else out += commaToPoint(hum);
    out += F(",\"b\":");
    out += commaToPoint(bat);
    out += '}';
    first = false;
  }
  f.close();
  out += ']';
  return out;
}

// ─── Бэкап/восстановление настроек на SD/LittleFS ─────────────────────

bool log_save_backup(const String &json) {
  if (!_fs_ok()) return false;
  File f = _fs_open_write(BACKUP_FILE);
  if (!f) return false;
  f.print(json);
  f.close();
  Serial.println(F("[Log] Backup saved to SD/FS"));
  return true;
}

String log_read_backup() {
  if (!_fs_ok() || !_fs_exists(BACKUP_FILE)) return "";
  File f = _fs_open_read(BACKUP_FILE);
  if (!f) return "";
  String json = f.readString();
  f.close();
  return json;
}
