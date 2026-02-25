#include "Logger.h"

#ifdef USE_SD_CARD
  #include <SPI.h>
  #include <SD.h>
  #define FS_OBJ   SD
  #define FS_FILE  File
  static bool _fsOk = false;
  static bool fs_begin() {
    _fsOk = SD.begin(SD_CS_PIN);
    return _fsOk;
  }
  static bool fs_ok() { return _fsOk; }
#else
  #if defined(ESP8266)
    #include <LittleFS.h>
    #define FS_OBJ   LittleFS
  #elif defined(ESP32)
    #include <SPIFFS.h>
    #define FS_OBJ   SPIFFS
  #endif
  #define FS_FILE  File
  static bool _fsOk = false;
  static bool fs_begin() {
    _fsOk = FS_OBJ.begin();
    return _fsOk;
  }
  static bool fs_ok() { return _fsOk; }
#endif

static const char CSV_HEADER[] = "datetime,weight_kg,temp_c,humidity_pct,bat_v\n";

bool log_init() {
  if (!fs_begin()) {
    Serial.println(F("[Log] FS init FAILED"));
    return false;
  }
  // Создать файл с заголовком если не существует
  if (!FS_OBJ.exists(LOG_FILE)) {
    FS_FILE f = FS_OBJ.open(LOG_FILE, "w");
    if (f) {
      f.print(CSV_HEADER);
      f.close();
    }
  }
  Serial.print(F("[Log] OK, size="));
  Serial.println(log_size());
  return true;
}

void log_append(const String &datetime, float weight, float tempC,
                float humidity, float batV, int batPct) {
  if (!fs_ok()) return;
  // Защита от записи при критически низком заряде батареи
  if (batPct < 5) {
    Serial.println(F("[Log] Skip: low battery"));
    return;
  }

  // Ротация: если файл > LOG_MAX_SIZE, переименовать и начать новый
  if (log_size() >= LOG_MAX_SIZE) {
    if (FS_OBJ.exists(LOG_FILE_OLD)) FS_OBJ.remove(LOG_FILE_OLD);
    FS_OBJ.rename(LOG_FILE, LOG_FILE_OLD);
    FS_FILE fn = FS_OBJ.open(LOG_FILE, "w");
    if (fn) { fn.print(CSV_HEADER); fn.close(); }
    Serial.println(F("[Log] Rotated"));
  }

  FS_FILE f = FS_OBJ.open(LOG_FILE, "a");
  if (!f) {
    Serial.println(F("[Log] Open FAILED"));
    return;
  }

  char buf[64];
  // datetime,weight,temp,humidity,batV
  snprintf(buf, sizeof(buf), "%.2f,%.1f,%.1f,%.2f",
           weight, tempC, humidity, batV);
  f.print(datetime);
  f.print(',');
  f.print(buf);
  f.print('\n');
  f.close();
}

void log_clear() {
  if (!fs_ok()) return;
  if (FS_OBJ.exists(LOG_FILE))     FS_OBJ.remove(LOG_FILE);
  if (FS_OBJ.exists(LOG_FILE_OLD)) FS_OBJ.remove(LOG_FILE_OLD);
  FS_FILE f = FS_OBJ.open(LOG_FILE, "w");
  if (f) { f.print(CSV_HEADER); f.close(); }
  Serial.println(F("[Log] Cleared"));
}

size_t log_size() {
  if (!fs_ok()) return 0;
  if (!FS_OBJ.exists(LOG_FILE)) return 0;
  FS_FILE f = FS_OBJ.open(LOG_FILE, "r");
  if (!f) return 0;
  size_t sz = f.size();
  f.close();
  return sz;
}

bool log_exists() {
  if (!fs_ok()) return false;
  return FS_OBJ.exists(LOG_FILE);
}

uint32_t log_free_space() {
#ifdef USE_SD_CARD
  if (!fs_ok()) return 0;
  return (uint32_t)(SD.totalBytes() - SD.usedBytes());
#else
  if (!fs_ok()) return 0;
  FSInfo info;
  if (FS_OBJ.info(info)) {
    return (uint32_t)(info.totalBytes - info.usedBytes);
  }
  return 0;
#endif
}

// Парсит CSV-лог и возвращает JSON-массив для графика/экспорта
// Формат CSV: datetime,weight_kg,temp_c,humidity_pct,bat_v
String log_to_json(int maxRows) {
  if (!fs_ok() || !FS_OBJ.exists(LOG_FILE)) return "[]";
#ifdef USE_SD_CARD
  FS_FILE f = FS_OBJ.open(LOG_FILE, FILE_READ);
#else
  FS_FILE f = FS_OBJ.open(LOG_FILE, "r");
#endif
  if (!f) return "[]";

  // Сначала считаем строки чтобы пропустить лишние
  int totalLines = 0;
  while (f.available()) {
    char c = f.read();
    if (c == '\n') totalLines++;
  }
  f.seek(0);

  // Пропускаем заголовок
  f.readStringUntil('\n');
  int dataLines = totalLines - 1;  // минус заголовок
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
