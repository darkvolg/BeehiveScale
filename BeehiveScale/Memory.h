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
#define EEPROM_SIZE              64

// Веб-настройки (alertDelta, calibWeight, emaAlpha)
void  web_settings_init();
float web_get_alert_delta();
float web_get_calib_weight();
float web_get_ema_alpha();
void  load_web_settings(float &alertDelta, float &calibWeight, float &emaAlpha);
void  save_web_settings(float alertDelta, float calibWeight, float emaAlpha);

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
