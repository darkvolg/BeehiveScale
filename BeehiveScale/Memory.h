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
#define EEPROM_ADDR_MAGIC3       64   // после AP_PASS[24] (40..63), проверяется через MAGIC3_VALUE
#define EEPROM_MAGIC3_VALUE      0xA7
// Telegram настройки (addr 65-132)
#define EEPROM_ADDR_TG_MAGIC     65   // 1 байт magic для TG блока
#define EEPROM_MAGIC_TG_VALUE    0xB1
#define EEPROM_ADDR_TG_TOKEN     66   // char[50] — Telegram bot token
#define EEPROM_ADDR_TG_CHATID    116  // char[16] — Telegram chat_id
// WiFi STA настройки (addr 133-199)
#define EEPROM_ADDR_WIFI_MAGIC   133  // 1 байт magic
#define EEPROM_MAGIC_WIFI_VALUE  0xC1
#define EEPROM_ADDR_WIFI_MODE    134  // 1 байт: 0=AP, 1=STA
#define EEPROM_ADDR_WIFI_SSID    135  // char[33] — SSID роутера
#define EEPROM_ADDR_WIFI_PASS    168  // char[33] — Пароль роутера
#define EEPROM_SIZE              256  // запас для будущих настроек

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

// Telegram настройки (token, chat_id)
void tg_settings_init();
void get_tg_token(char *buf, size_t maxLen);
void set_tg_token(const char *token);
void get_tg_chatid(char *buf, size_t maxLen);
void set_tg_chatid(const char *chatid);

// WiFi режим и STA credentials
void     wifi_settings_init();
uint8_t  get_wifi_mode();          // 0=AP, 1=STA
void     set_wifi_mode(uint8_t m);
void     get_wifi_ssid(char *buf, size_t maxLen);
void     set_wifi_ssid(const char *ssid);
void     get_wifi_sta_pass(char *buf, size_t maxLen);
void     set_wifi_sta_pass(const char *pass);

// Предыдущий offset (для отмены тары)
void save_prev_offset(long prevOffset);
long load_prev_offset();

// Опорный вес для дельты (не перезаписывается авто-фиксацией)
void  save_prev_weight(float w);
float load_prev_weight(float fallback);

#endif
