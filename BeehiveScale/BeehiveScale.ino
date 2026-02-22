/*
 * BeehiveScale v4.1 - Весы пчеловода (ESP32)
 * NTP синхронизация времени через интернет
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <HX711.h>
#include <EEPROM.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#elif defined(ESP32)
#include <WiFi.h>
#include <esp_task_wdt.h>
#endif

#include "Display.h"
#include "Scale.h"
#include "Button.h"
#include "Memory.h"
#include "RTC_Module.h"
#include "Temperature.h"
#include "Connectivity.h"
#include "SleepManager.h"
#include "WebServerModule.h"
#include "Battery.h"
#include "Logger.h"

#define DT_PIN          16
#define SCK_PIN          1
#define BUTTON_PIN       0
#define MENU_BTN_PIN     2
#define LCD_ADDR      0x27

#define WEIGHT_SAVE_MS    300000UL
#define WEIGHT_SAVE_THR     0.05f
#define TARE_COUNT             4
#define TARE_TIMEOUT_MS     3000UL
#define MENU_SCREENS           7
#define STABLE_BUF_SIZE        6
#define STABLE_THR             0.02f
#define STABLE_SAVE_MIN_MS 600000UL  // 10 мин — минимальный интервал между EEPROM-записями при стабилизации
#define SPIKE_FILTER_KG      5.0f    // Отбросить показание если скачок > 5 кг
#define WDT_TIMEOUT_SEC        8
#define AUTO_SLEEP_MS     180000UL  // 3 минуты бездействия → deep sleep

static inline void app_wdt_init() {
#if defined(ESP32)
  #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_task_wdt_config_t wdt_cfg = {
      .timeout_ms = WDT_TIMEOUT_SEC * 1000,
      .idle_core_mask = 0,
      .trigger_panic = true
    };
    esp_task_wdt_reconfigure(&wdt_cfg);
    esp_task_wdt_add(NULL);
  #else
    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
    esp_task_wdt_add(NULL);
  #endif
#elif defined(ESP8266)
  ESP.wdtFeed();
#endif
}

static inline void app_wdt_reset() {
#if defined(ESP32)
  esp_task_wdt_reset();
#elif defined(ESP8266)
  ESP.wdtFeed();
  yield();
#endif
}

LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
HX711             scale;

struct SystemState {
  float calibrationFactor = 2280.0f;
  long  offset            = 0;
  float lastSavedWeight   = 0.0f;
  float smoothedWeight    = 0.0f;
  bool  sensorReady       = false;
  bool  emaInitialized    = false;
  TempData tempData;
  float    rtcTempC       = NAN;
  TimeStamp currentTime;
  int  menuScreen         = 0;
  int  lastMenuScreen     = -1;
  bool needsRedraw        = true;
  bool wifiOk             = false;
  float prevWeight        = 0.0f;
  long  prevOffset        = 0;
  bool  hasPrevOffset     = false;  // true после первого тарирования в сессии
  float batVoltage        = 0.0f;
  int   batPercent        = 0;
  String datetimeStr      = "--";
  bool  weightStable      = false;
};

SystemState    sys;
ButtonState    btnMain;
ButtonState    btnMenu;
SleepPersistData persist;
bool webServerStarted = false;
String serialBuffer = "";
unsigned long lastActivityTime = 0;  // Таймер бездействия для auto-sleep

void handle_buttons();
void process_weight();
void process_temperature();
void update_interface();
void display_screen_weight();
void display_screen_temp();
void display_screen_diff();
void display_screen_status();
void display_screen_datetime();
void display_screen_battery();
void display_error();
void display_screen_calib_menu();
void perform_taring();
void undo_tare();
void perform_calibration();
void adjust_calibration();
void show_splash_screen();
void start_webserver();
void handle_serial_commands();
void check_auto_sleep();

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n[BeehiveScale] v4.1 boot"));

  app_wdt_init();
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(MENU_BTN_PIN, INPUT_PULLUP);

#if defined(ESP8266)
  Wire.begin(4, 5);
  Wire.setClockStretchLimit(1500);  // таймаут I2C чтобы не зависал
#else
  Wire.begin();
#endif
#if defined(ESP32)
  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println(F("[EEPROM] Init FAILED!"));
  }
#elif defined(ESP8266)
  EEPROM.begin(EEPROM_SIZE);
#endif

  sleep_init();
  sleep_load_persistent(persist);

  lcd_init(lcd);
  Serial.end();              // Освободить GPIO1 (TX) перед HX711
  scale_init(scale, DT_PIN, SCK_PIN);
  sys.sensorReady = check_sensor(scale);

  bool rtcOk = rtc_init();
  if (!rtcOk) {
    Serial.println(F("[RTC] WARNING: no RTC"));
  }

  temp_init();

  load_calibration_data(sys.calibrationFactor, sys.offset, sys.lastSavedWeight);
  sys.prevOffset = load_prev_offset();
  web_settings_init();
  // prevWeight: из RTC RAM (deep sleep) или из отдельного EEPROM-слота (холодный старт)
  // ВАЖНО: используем EEPROM_ADDR_PREV_WEIGHT, а не ADDR_WEIGHT —
  // авто-фиксация перезаписывает ADDR_WEIGHT, что обнуляло бы дельту при перезагрузке.
  if (persist.lastWeight != 0.0f) {
    sys.prevWeight = persist.lastWeight;
  } else {
    sys.prevWeight = load_prev_weight(sys.lastSavedWeight);
  }

  bat_init();

  if (sys.sensorReady) {
    scale.set_scale(sys.calibrationFactor);
    scale.set_offset(sys.offset);
  }

  sys.wifiOk = wifi_init();  // Инициализация WiFi (AP или STA режим) — ДО splash, чтобы AP поднялась пока идёт задержка
  show_splash_screen();
  if (sys.wifiOk) {
    Serial.println(F("[WiFi] Connected"));
    Serial.print(F("[WiFi] IP: "));
    Serial.println(WiFi.status() == WL_CONNECTED ? WiFi.localIP() : WiFi.softAPIP());
#if !defined(WIFI_MODE_AP)
    ntp_sync_time();  // Синхронизация времени (только в STA режиме)
#endif
    start_webserver();
  } else {
    Serial.println(F("[WiFi] Initialization failed!"));
  }

  log_init();
  lastActivityTime = millis();
  Serial.println(F("[Setup] Done"));
}

void start_webserver() {
  if (webServerStarted) return;

  WebData wd;
  wd.weight          = &sys.smoothedWeight;
  wd.lastSavedWeight = &sys.lastSavedWeight;
  wd.tempC           = &sys.tempData.temperature;
  wd.humidity        = &sys.tempData.humidity;
  wd.rtcTempC        = &sys.rtcTempC;
  wd.calibFactor     = &sys.calibrationFactor;
  wd.offset          = &sys.offset;
  wd.sensorReady     = &sys.sensorReady;
  wd.wifiOk          = &sys.wifiOk;
  wd.datetime        = &sys.datetimeStr;
  wd.wakeupCount     = &persist.wakeupCount;
  wd.batVoltage      = &sys.batVoltage;
  wd.batPercent      = &sys.batPercent;
  wd.prevWeight      = &sys.prevWeight;

  WebActions wa;
  wa.doTare = perform_taring;
  wa.doSave = []() {
    sys.prevWeight = sys.smoothedWeight;
    save_weight(sys.lastSavedWeight, sys.smoothedWeight);
    save_prev_weight(sys.prevWeight);
  };

  webserver_init(wd, wa);
  webServerStarted = true;
}

void loop() {
  app_wdt_reset();
  handle_serial_commands();

  static unsigned long lastTempRead = 0;
  static unsigned long lastTsUpload = 0;
  static unsigned long lastTgReport = 0;
  static unsigned long lastBatRead  = 0;
  static unsigned long lastLogWrite = 0;
  unsigned long now = millis();

  if (now - lastTempRead >= TEMP_READ_INTERVAL_MS) {
    process_temperature();
    lastTempRead = now;
    sys.needsRedraw = true;
  }

  if (now - lastBatRead >= BAT_READ_INTERVAL_MS) {
    sys.batVoltage = bat_voltage();
    sys.batPercent  = bat_percent();
    lastBatRead = now;
    sys.needsRedraw = true;
  }

  handle_buttons();
  process_weight();
  update_interface();

  if (now - lastLogWrite >= LOG_INTERVAL_MS) {
    log_append(sys.datetimeStr, sys.smoothedWeight,
               sys.tempData.temperature, sys.tempData.humidity, sys.batVoltage);
    lastLogWrite = now;
  }

  if (sys.currentTime.valid) {
    sys.datetimeStr = rtc_format_datetime(sys.currentTime);
  }

  wifi_ensure_connected();
#if defined(WIFI_MODE_AP)
  sys.wifiOk = (WiFi.softAPIP() != IPAddress(0,0,0,0));
#else
  sys.wifiOk = (WiFi.status() == WL_CONNECTED);
  ntp_loop();
#endif

  if (sys.wifiOk && !webServerStarted) {
    start_webserver();
  }

  if (sys.wifiOk && webServerStarted) {
    webserver_handle();
  }

#if !defined(WIFI_MODE_AP)
  if (sys.wifiOk) {
    if (now - lastTsUpload >= TS_UPDATE_INTERVAL_MS) {
      float rtcT = rtc_temperature();
      ts_send(sys.smoothedWeight, sys.tempData.temperature,
              sys.tempData.humidity, rtcT);
      lastTsUpload = now;
    }

    if (now - lastTgReport >= TG_REPORT_INTERVAL) {
      TimeStamp ts = rtc_now();
      tg_send_report(sys.smoothedWeight, sys.tempData.temperature,
                     sys.tempData.humidity, rtc_format_datetime(ts));
      lastTgReport = now;
    }
  }
#endif

  check_auto_sleep();

#ifdef SLEEP_MODE_DEEP_SLEEP
  log_append(sys.datetimeStr, sys.smoothedWeight,
             sys.tempData.temperature, sys.tempData.humidity, sys.batVoltage);
  persist.lastWeight = sys.smoothedWeight;
  persist.lastTempC = sys.tempData.temperature;
  persist.wakeupCount++;
  sleep_save_persistent(persist);
  sleep_enter(SLEEP_INTERVAL_SEC);
#endif
}

void handle_buttons() {
  static int pressCount = 0;
  static unsigned long lastPressTime = 0;

  ButtonAction actMain = read_button(BUTTON_PIN, btnMain);
  ButtonAction actMenu = read_button(MENU_BTN_PIN, btnMenu);

  if (actMain != NO_ACTION || actMenu != NO_ACTION) {
    lastActivityTime = millis();
  }

  if (actMain == SHORT_PRESS) {
    if (sys.menuScreen == 6) {
      adjust_calibration();
      pressCount = 0;
      sys.needsRedraw = true;
    } else {
      pressCount++;
      lastPressTime = millis();
      sys.needsRedraw = true;
      if (pressCount >= TARE_COUNT) {
        perform_taring();
        pressCount = 0;
      }
    }
  }
  if (actMain == LONG_PRESS && sys.menuScreen != 6) {
    perform_calibration();
    pressCount = 0;
    sys.needsRedraw = true;
  }
  if (pressCount > 0 && millis() - lastPressTime > TARE_TIMEOUT_MS) {
    pressCount = 0;
  }
  static int menuPressCount = 0;
  static unsigned long lastMenuPressTime = 0;

  if (actMenu == SHORT_PRESS) {
    menuPressCount++;
    if (menuPressCount == 1) {
      lastMenuPressTime = millis();
    } else if (menuPressCount >= 2 && millis() - lastMenuPressTime < 500UL) {
      undo_tare();
      menuPressCount = 0;
      return;
    }
  }
  if (menuPressCount == 1 && millis() - lastMenuPressTime >= 500UL) {
    sys.menuScreen = (sys.menuScreen + 1) % MENU_SCREENS;
    sys.needsRedraw = true;
    menuPressCount = 0;
  }
}

void process_weight() {
  static unsigned long lastSaveTime       = 0;
  static unsigned long lastStableSaveTime = 0;
  static unsigned long lastReadTime       = 0;
  static float stableBuf[STABLE_BUF_SIZE];
  static int   stableBufIdx = 0;
  static int   stableBufCnt = 0;
  static bool  stableSaved  = false;

  if (millis() - lastReadTime < 500UL) return;
  lastReadTime = millis();

  float raw = scale_read_weight(scale, SCALE_READ_SAMPLES);
  if (isnan(raw)) {
    // spike-фильтр применяется ниже, но NaN пропускаем сразу
    if (sys.sensorReady) {
      sys.sensorReady = false;
      sys.needsRedraw = true;
    }
    return;
  }

  if (!sys.sensorReady) {
    sys.sensorReady = true;
    sys.needsRedraw = true;
    Serial.println(F("[HX711] Sensor recovered!"));
  }

  // --- Spike-фильтр: отбросить показание если скачок > SPIKE_FILTER_KG ---
  if (sys.emaInitialized && fabsf(raw - sys.smoothedWeight) > SPIKE_FILTER_KG) {
    Serial.print(F("[Spike] Rejected raw="));
    Serial.println(raw, 2);
    return;
  }

  float alpha = web_get_ema_alpha();
  if (!sys.emaInitialized) {
    sys.smoothedWeight = raw;
    sys.emaInitialized = true;
  } else {
    sys.smoothedWeight = alpha * raw + (1.0f - alpha) * sys.smoothedWeight;
  }

  // --- Авто-фиксация стабильных показаний ---
  stableBuf[stableBufIdx] = sys.smoothedWeight;
  stableBufIdx = (stableBufIdx + 1) % STABLE_BUF_SIZE;
  if (stableBufCnt < STABLE_BUF_SIZE) stableBufCnt++;

  bool wasStable = sys.weightStable;
  if (stableBufCnt >= STABLE_BUF_SIZE) {
    float mn = stableBuf[0], mx = stableBuf[0];
    for (int i = 1; i < STABLE_BUF_SIZE; i++) {
      if (stableBuf[i] < mn) mn = stableBuf[i];
      if (stableBuf[i] > mx) mx = stableBuf[i];
    }
    sys.weightStable = (mx - mn) < STABLE_THR;
  } else {
    sys.weightStable = false;
  }

  if (sys.weightStable && !stableSaved) {
    unsigned long nowMs = millis();
    if (nowMs - lastStableSaveTime >= STABLE_SAVE_MIN_MS) {
      save_weight(sys.lastSavedWeight, sys.smoothedWeight);
      stableSaved = true;
      lastStableSaveTime = nowMs;
      lastSaveTime = nowMs;
      Serial.println(F("[Stable] Weight locked"));
    } else {
      stableSaved = true;  // не писать в EEPROM, но флаг ставим
    }
  }
  if (!sys.weightStable) {
    stableSaved = false;
  }
  if (sys.weightStable != wasStable) {
    sys.needsRedraw = true;
  }
  // --- конец авто-фиксации ---

  float delta = fabsf(sys.smoothedWeight - sys.lastSavedWeight);
  unsigned long now = millis();
  if (delta > WEIGHT_SAVE_THR && now - lastSaveTime >= WEIGHT_SAVE_MS) {
    save_weight(sys.lastSavedWeight, sys.smoothedWeight);
    lastSaveTime = now;
    sys.needsRedraw = true;
  }

#if !defined(WIFI_MODE_AP)
  float alertDelta = web_get_alert_delta();
  float absDelta = fabsf(sys.smoothedWeight - sys.prevWeight);
  if (absDelta >= alertDelta && !persist.alertSent && sys.wifiOk) {
    TimeStamp ts = rtc_now();
    tg_send_alert(sys.smoothedWeight, sys.tempData.temperature,
                  rtc_format_datetime(ts));
    persist.alertSent = true;
    // Сбрасываем опорный вес на текущий, чтобы следующая дельта считалась от него
    sys.prevWeight = sys.smoothedWeight;
    save_prev_weight(sys.prevWeight);
  }
  if (absDelta < alertDelta * 0.5f) {
    persist.alertSent = false;
  }
#endif
}

void process_temperature() {
  sys.tempData = temp_read();
  sys.rtcTempC = rtc_temperature();
  sys.currentTime = rtc_now();
}

void update_interface() {
  if (!sys.needsRedraw) return;
  if (sys.menuScreen != sys.lastMenuScreen) {
    lcd.clear();
    sys.lastMenuScreen = sys.menuScreen;
  }
  if (sys.sensorReady) {
    switch (sys.menuScreen) {
      case 0: display_screen_weight();     break;
      case 1: display_screen_diff();       break;
      case 2: display_screen_battery();    break;
      case 3: display_screen_temp();       break;
      case 4: display_screen_datetime();   break;
      case 5: display_screen_status();     break;
      case 6: display_screen_calib_menu(); break;
    }
    // Номер экрана в правом углу строки 2 (экран 6 — калибровка, там другой формат)
    if (sys.menuScreen < 6) {
      show_screen_num(sys.menuScreen);
    }
  } else {
    display_error();
  }
  sys.needsRedraw = sys.sensorReady ? false : true;  // мигать пока ошибка
}

// Показывает номер экрана "N/7" в позиции 13 строки 1
void show_screen_num(int n) {
  char buf[4];
  snprintf(buf, sizeof(buf), "%d/7", n + 1);
  lcd.setCursor(13, 1);
  lcd.print(buf);
}

void display_screen_weight() {
  char buf[24];
  lcd.setCursor(0, 0);
  snprintf(buf, sizeof(buf), "Ves:%6.2f%ckg", sys.smoothedWeight, sys.weightStable ? '*' : ' ');
  lcd_print_padded(lcd, buf);
  lcd.setCursor(0, 1);
  if (sys.currentTime.valid) {
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u %s",
             sys.currentTime.hour, sys.currentTime.minute, sys.currentTime.second,
             sys.wifiOk ? " W" : "  ");
  } else {
    snprintf(buf, sizeof(buf), "No RTC     %s", sys.wifiOk ? " W" : "  ");
  }
  lcd_print_padded(lcd, buf);
}

void display_screen_temp() {
  char buf[24];
  lcd.setCursor(0, 0);
  if (sys.tempData.valid) {
    snprintf(buf, sizeof(buf), "T:%4.1fC R:%4.1fC",
             sys.tempData.temperature, sys.rtcTempC);
  } else {
    snprintf(buf, sizeof(buf), "T: --.- C       ");
  }
  lcd_print_padded(lcd, buf);

  lcd.setCursor(0, 1);
  if (sys.tempData.humidity > -90) {
    snprintf(buf, sizeof(buf), "H: %4.1f %%", sys.tempData.humidity);
  } else {
    snprintf(buf, sizeof(buf), "H: -- %%  (DS18)");
  }
  lcd_print_padded(lcd, buf);
}

void display_screen_diff() {
  char buf[24];
  float diff = sys.smoothedWeight - sys.prevWeight;
  lcd.setCursor(0, 0);
  snprintf(buf, sizeof(buf), "D:%+6.2fkg", diff);
  lcd_print_padded(lcd, buf);
  lcd.setCursor(0, 1);
  snprintf(buf, sizeof(buf), "Pred:%5.2fkg", sys.prevWeight);
  lcd_print_padded(lcd, buf);
}

void display_screen_status() {
  char buf[24];
  lcd.setCursor(0, 0);
  snprintf(buf, sizeof(buf), "CF:%.0f", sys.calibrationFactor);
  lcd_print_padded(lcd, buf);
  lcd.setCursor(0, 1);
  snprintf(buf, sizeof(buf), "W:%s N:%lu",
           sys.wifiOk ? "OK" : "--", persist.wakeupCount);
  lcd_print_padded(lcd, buf);
}

void display_screen_datetime() {
  lcd.setCursor(0, 0);
  lcd_print_padded(lcd, rtc_format_datetime(sys.currentTime));
  lcd.setCursor(0, 1);
  lcd_print_padded(lcd, sys.currentTime.valid ? "RTC OK          " : "RTC ERROR!      ");
}

void display_screen_battery() {
  char buf[24];
  lcd.setCursor(0, 0);
  snprintf(buf, sizeof(buf), "Bat:%4.2fV %3d%%", sys.batVoltage, sys.batPercent);
  lcd_print_padded(lcd, buf);
  lcd.setCursor(0, 1);
  if (sys.batPercent < 10) {
    lcd_print_padded(lcd, "!! LOW BATTERY !!");
  } else {
    lcd_print_padded(lcd, "Li-Ion 1S       ");
  }
}

void display_screen_calib_menu() {
  char buf[24];
  lcd.setCursor(0, 0);
  snprintf(buf, sizeof(buf), "CF:%.1f", sys.calibrationFactor);
  lcd_print_padded(lcd, buf);
  lcd.setCursor(0, 1);
  lcd_print_padded(lcd, "MAIN=Vojti      ");
}

void adjust_calibration() {
  Serial.println(F("[AdjCal] Enter"));
  float cf = sys.calibrationFactor;
  const float steps[] = {10.0f, 1.0f, 0.1f};
  int stepIdx = 0;
  char buf[24];

  lcd.clear();
  unsigned long lastWeighTime = 0;
  float liveWeight = sys.smoothedWeight;

  for (;;) {
    app_wdt_reset();

    // Обновляем вес каждые 500мс
    unsigned long now = millis();
    if (now - lastWeighTime >= 500UL) {
      float raw = scale_read_weight(scale, SCALE_READ_SAMPLES);
      if (!isnan(raw)) liveWeight = raw;
      lastWeighTime = now;

      // Обновляем LCD
      lcd.setCursor(0, 0);
      if (steps[stepIdx] >= 1.0f)
        snprintf(buf, sizeof(buf), "CF:%-7.1f+/-%.0f", cf, steps[stepIdx]);
      else
        snprintf(buf, sizeof(buf), "CF:%-7.1f+/-.1", cf);
      lcd_print_padded(lcd, buf);

      lcd.setCursor(0, 1);
      snprintf(buf, sizeof(buf), "Ves:%6.2fkg", liveWeight);
      lcd_print_padded(lcd, buf);
    }

    // Чтение кнопок
    ButtonAction actMain = read_button(BUTTON_PIN, btnMain);
    ButtonAction actMenu = read_button(MENU_BTN_PIN, btnMenu);

    if (actMain == SHORT_PRESS) {
      // Переключить шаг: 10 → 1 → 0.1 → 10 ...
      stepIdx = (stepIdx + 1) % 3;
      lastWeighTime = 0;  // форсировать перерисовку
    }

    if (actMenu == SHORT_PRESS) {
      // CF + шаг
      cf = constrain(cf + steps[stepIdx], 100.0f, 50000.0f);
      scale.set_scale(cf);
      lastWeighTime = 0;
    }

    if (actMenu == LONG_PRESS) {
      // CF − шаг
      cf = constrain(cf - steps[stepIdx], 100.0f, 50000.0f);
      scale.set_scale(cf);
      lastWeighTime = 0;
    }

    if (actMain == LONG_PRESS) {
      // Сохранить и выйти
      cf = constrain(cf, 100.0f, 50000.0f);
      sys.calibrationFactor = cf;
      scale.set_scale(cf);
      save_calibration(cf);
      sys.emaInitialized = false;

      lcd.clear();
      lcd.setCursor(0, 0); lcd_print_padded(lcd, "Sokhraneno!     ");
      snprintf(buf, sizeof(buf), "CF:%.1f", cf);
      lcd.setCursor(0, 1); lcd_print_padded(lcd, buf);
      { unsigned long _t0=millis(); while(millis()-_t0<1500UL){app_wdt_reset();yield();} }
      sys.needsRedraw = true;
      lastActivityTime = millis();
      Serial.print(F("[AdjCal] Saved CF=")); Serial.println(cf, 1);
      return;
    }

    yield();
  }
}

void display_error() {
  static bool blink = false;
  static unsigned long lastBlink = 0;
  unsigned long _nb = millis();
  if (_nb - lastBlink >= 500UL) {
    blink = !blink;
    lastBlink = _nb;
  }
  lcd.setCursor(0, 0);
  lcd_print_padded(lcd, blink ? "*** OSHIBKA! ***" : "                ");
  lcd.setCursor(0, 1);
  lcd_print_padded(lcd, "Check HX711 wire");
}

void perform_taring() {
  Serial.println(F("[Tare] Start"));

  // Сохраняем предыдущий offset для возможности отмены
  sys.prevOffset = sys.offset;
  sys.hasPrevOffset = true;
  save_prev_offset(sys.prevOffset);

  lcd.clear();
  lcd.setCursor(0, 0); lcd_print_padded(lcd, " Tarirovka...   ");
  lcd.setCursor(0, 1); lcd_print_padded(lcd, " Podozhdite...  ");

  app_wdt_reset();
  scale.tare(10);
  sys.offset = scale.get_offset();
  sys.smoothedWeight = 0.0f;
  sys.emaInitialized = false;
  save_offset(sys.offset);

  char _ofs[17]; snprintf(_ofs, sizeof(_ofs), "Ofs:%ld", sys.offset);
  lcd.clear();
  lcd.setCursor(0, 0); lcd_print_padded(lcd, "Tara: OK        ");
  lcd.setCursor(0, 1); lcd_print_padded(lcd, _ofs);
  app_wdt_reset();
  { unsigned long _t0=millis(); while(millis()-_t0<1200UL){app_wdt_reset();yield();} }
  sys.needsRedraw = true;
  Serial.print(F("[Tare] Offset=")); Serial.println(sys.offset);
}

void undo_tare() {
  if (!sys.hasPrevOffset) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd_print_padded(lcd, "Tara: net       ");
    lcd.setCursor(0, 1); lcd_print_padded(lcd, "predydushchej   ");
    { unsigned long _t0=millis(); while(millis()-_t0<1200UL){app_wdt_reset();yield();} }
    sys.needsRedraw = true;
    return;
  }

  Serial.println(F("[Tare] Undo"));
  sys.offset = sys.prevOffset;
  scale.set_offset(sys.offset);
  save_offset(sys.offset);
  sys.emaInitialized = false;

  lcd.clear();
  lcd.setCursor(0, 0); lcd_print_padded(lcd, "Tara: Otmena    ");
  char _ofs[17]; snprintf(_ofs, sizeof(_ofs), "Ofs:%ld", sys.offset);
  lcd.setCursor(0, 1); lcd_print_padded(lcd, _ofs);
  app_wdt_reset();
  { unsigned long _t0=millis(); while(millis()-_t0<1200UL){app_wdt_reset();yield();} }
  sys.needsRedraw = true;
  Serial.print(F("[Tare] Restored offset=")); Serial.println(sys.offset);
}

void perform_calibration() {
  Serial.println(F("[Calib] Start"));

  auto wait_press = [](int pin, uint32_t timeout_ms) -> bool {
    unsigned long t = millis();
    while (digitalRead(pin) == HIGH) {
      app_wdt_reset(); yield();
      if (millis() - t > timeout_ms) return false;
    }
    while (digitalRead(pin) == LOW) {
      app_wdt_reset(); yield();
    }
    return true;
  };

  lcd.clear();
  lcd.setCursor(0, 0); lcd_print_padded(lcd, "Ubrat gruz!     ");
  lcd.setCursor(0, 1); lcd_print_padded(lcd, "Zhmi knopku...  ");
  unsigned long start = millis();
  while (digitalRead(BUTTON_PIN) == LOW) {
    app_wdt_reset(); yield();
    if (millis() - start > 10000) { sys.needsRedraw = true; return; }
  }
  if (!wait_press(BUTTON_PIN, 30000)) { sys.needsRedraw = true; return; }

  scale.set_scale();
  scale.tare(10);

  lcd.clear();
  lcd.setCursor(0, 0); lcd_print_padded(lcd, "Polozh. 1 kg    ");
  lcd.setCursor(0, 1); lcd_print_padded(lcd, "Zhmi knopku...  ");
  if (!wait_press(BUTTON_PIN, 30000)) { sys.needsRedraw = true; return; }

  lcd.clear();
  lcd.setCursor(0, 0); lcd_print_padded(lcd, "Kalibrovka...   ");

  float raw = scale.get_units(SCALE_CALIB_SAMPLES);
  if (raw == 0.0f || isnan(raw)) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd_print_padded(lcd, "OSHIBKA raw=0   ");
    lcd.setCursor(0, 1); lcd_print_padded(lcd, "Povtorite       ");
    { unsigned long _t0=millis(); while(millis()-_t0<2000UL){app_wdt_reset();yield();} }
    sys.needsRedraw = true;
    return;
  }

  sys.calibrationFactor = raw / (web_get_calib_weight() / 1000.0f);
  scale.set_scale(sys.calibrationFactor);
  save_calibration(sys.calibrationFactor);
  sys.emaInitialized = false;

  lcd.clear();
  lcd.setCursor(0, 0); lcd_print_padded(lcd, "OK! Factor:     ");
  lcd.setCursor(0, 1); lcd_print_padded(lcd, String(sys.calibrationFactor, 3));
  app_wdt_reset();
  { unsigned long _t0=millis(); while(millis()-_t0<2500UL){app_wdt_reset();yield();} }
  sys.needsRedraw = true;
  Serial.print(F("[Calib] Factor=")); Serial.println(sys.calibrationFactor, 4);
}

void check_auto_sleep() {
  if (millis() - lastActivityTime < AUTO_SLEEP_MS) return;

  Serial.println(F("[AutoSleep] 3 min idle — shutting down..."));

  // Показываем сообщение на LCD
  lcd.clear();
  lcd.setCursor(0, 0); lcd_print_padded(lcd, "Auto sleep...   ");
  lcd.setCursor(0, 1); lcd_print_padded(lcd, "Btn to wake up  ");
  { unsigned long _t0=millis(); while(millis()-_t0<1500UL){app_wdt_reset();yield();} }

  // Гасим подсветку LCD
  lcd.noBacklight();
  lcd.clear();

  // Отключаем WiFi
#if defined(ESP32)
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
#elif defined(ESP8266)
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
#endif
  webServerStarted = false;

  // Записываем лог перед сном
  log_append(sys.datetimeStr, sys.smoothedWeight,
             sys.tempData.temperature, sys.tempData.humidity, sys.batVoltage);

  // Сохраняем данные
  save_weight(sys.lastSavedWeight, sys.smoothedWeight);
  persist.lastWeight = sys.smoothedWeight;
  persist.lastTempC = sys.tempData.temperature;
  persist.wakeupCount++;
  sleep_save_persistent(persist);

  // Уходим в deep sleep (пробуждение по кнопке GPIO 0)
  sleep_enter(0);  // 0 = без таймера, только по кнопке
}

void show_splash_screen() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd_print_padded(lcd, " Vesy Pchelovod ");
  lcd.setCursor(0, 1); lcd_print_padded(lcd, "   Versiya 4.1  ");
  { unsigned long _t0=millis(); while(millis()-_t0<1800UL){app_wdt_reset();yield();} }
  lcd.clear();
  sys.needsRedraw = true;
}

void handle_serial_commands() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuffer.length() > 0) {
        if (serialBuffer == "NTP") {
          Serial.println(F("\n[CMD] NTP sync..."));
          if (ntp_sync_time()) {
            Serial.println(F("[CMD] OK!"));
          } else {
            Serial.println(F("[CMD] ERROR!"));
          }
        }
        else if (serialBuffer == "TIME") {
          if (sys.currentTime.valid) {
            Serial.print(F("\n[CMD] Time: "));
            Serial.println(rtc_format_datetime(sys.currentTime));
          } else {
            Serial.println(F("\n[CMD] RTC error"));
          }
        }
        else if (serialBuffer.startsWith("SETTIME")) {
          int y, mo, d, h, mi, s;
          if (sscanf(serialBuffer.c_str(), "SETTIME %d %d %d %d %d %d",
                     &y, &mo, &d, &h, &mi, &s) == 6) {
            if (y < 2000 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > 31 ||
                h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59) {
              Serial.println(F("\n[CMD] Invalid date/time values!"));
            } else if (rtc_set(y, mo, d, h, mi, s)) {
              Serial.println(F("\n[CMD] Time set!"));
            } else {
              Serial.println(F("\n[CMD] RTC error!"));
            }
          } else {
            Serial.println(F("\n[CMD] Format: SETTIME YYYY MM DD HH MM SS"));
          }
        }
        else if (serialBuffer == "STATUS") {
          Serial.println(F("\n=== STATUS ==="));
          Serial.print(F("Weight: ")); Serial.println(sys.smoothedWeight, 2);
          Serial.print(F("Temp: ")); Serial.println(sys.tempData.temperature, 1);
          Serial.print(F("WiFi: ")); Serial.println(sys.wifiOk ? "OK" : "NO");
          Serial.print(F("Sensor: ")); Serial.println(sys.sensorReady ? "OK" : "ERROR");
#if defined(ESP32)
          Serial.print(F("Heap: ")); Serial.print(ESP.getFreeHeap()); Serial.println(" b");
#endif
          Serial.println(F("=============\n"));
        }
        else if (serialBuffer == "REBOOT") {
          Serial.println(F("\n[CMD] Rebooting..."));
          Serial.flush();
          delay(500);
          ESP.restart();
        }
        else if (serialBuffer == "HELP") {
          Serial.println(F("\n=== COMMANDS ==="));
          Serial.println(F("  NTP                     - sync time"));
          Serial.println(F("  TIME                    - show time"));
          Serial.println(F("  SETTIME YYYY MM DD ...  - set time"));
          Serial.println(F("  STATUS                  - system status"));
          Serial.println(F("  REBOOT                  - restart\n"));
        }
        else {
          Serial.println(F("\n[CMD] Unknown. Type HELP"));
        }
        serialBuffer = "";
      }
    } else {
      if (serialBuffer.length() < 64) {
        serialBuffer += c;
      }
    }
  }
}
