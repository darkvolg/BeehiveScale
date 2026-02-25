#ifndef MEMORY_H
#define MEMORY_H

#include <Arduino.h>
#include <EEPROM.h>

#define EEPROM_ADDR_CALIB        0
#define EEPROM_ADDR_OFFSET       4
#define EEPROM_ADDR_WEIGHT       8
#define EEPROM_ADDR_MAGIC        12
#define EEPROM_ADDR_ALERT_DELTA  13
#define EEPROM_ADDR_CALIB_WEIGHT 17
#define EEPROM_ADDR_EMA_ALPHA    21
#define EEPROM_ADDR_MAGIC2       25
#define EEPROM_MAGIC_VALUE       0xA5
#define EEPROM_ADDR_PREV_OFFSET  26
#define EEPROM_MAGIC2_VALUE      0xA6
#define EEPROM_ADDR_PREV_WEIGHT  30
// Дополнительные настройки (addr 34-63)
#define EEPROM_ADDR_SLEEP_SEC    34   // uint32_t — интервал сна (секунды)
#define EEPROM_ADDR_LCD_BL_SEC   38   // uint16_t — таймаут подсветки (секунды, 0=всегда вкл)
#define EEPROM_ADDR_AP_PASS      40   // char[24] — пароль Wi-Fi AP
#define EEPROM_ADDR_MAGIC3       64   // не используем (нет места), magic проверяется через MAGIC3_VALUE
#define EEPROM_MAGIC3_VALUE      0xA7
#define EEPROM_SIZE              80   // расширяем до 80 байт

// Веб-настройки (alertDelta, calibWeight, emaAlpha)
void  web_settings_init();
float web_get_alert_delta();
float web_get_calib_weight();
float web_get_ema_alpha();
void  load_web_settings(float &alertDelta, float &calibWeight, float &emaAlpha);
void  save_web_settings(float alertDelta, float calibWeight, float emaAlpha);

// Расширенные настройки (sleep, LCD backlight, AP password)
void     ext_settings_init();
uint32_t get_sleep_sec();
void     set_sleep_sec(uint32_t sec);
uint16_t get_lcd_bl_sec();
void     set_lcd_bl_sec(uint16_t sec);
void     get_ap_pass(char *buf, size_t maxLen);
void     set_ap_pass(const char *pass);

// Калибровка и вес
void load_calibration_data(float &factor, long &offset, float &weight);
void save_calibration(float factor);
void save_offset(long offset);
void save_weight(float &lastWeight, float currentWeight);

// Предыдущий offset (для отмены тары)
void save_prev_offset(long prevOffset);
long load_prev_offset();

// Опорный вес для дельты (не перезаписывается авто-фиксацией)
void  save_prev_weight(float w);
float load_prev_weight(float fallback);

#endif
