# BeehiveScale Project Memory

## Проект
- **BeehiveScale v4.1** — пчелиные весы на **NodeMCU ESP8266** (не ESP32!)
- Язык: Arduino C++
- Пользователь общается на **русском**

## Структура проекта
```
BeehiveScale/
  BeehiveScale.ino   — главный скетч (setup/loop, логика)
  Battery.h/.cpp     — мониторинг батареи (A0, делитель 2:1)
  Button.h/.cpp      — обработка кнопок (short/long/double press, ISR)
  Connectivity.h/.cpp — Wi-Fi (AP/STA), Telegram, ThingSpeak, NTP
  Display.h/.cpp     — LCD 1602 I2C (7 экранов)
  Logger.h/.cpp      — CSV логирование на SD-карту (SPI)
  Memory.h/.cpp      — EEPROM чтение/запись (калибровка, настройки)
  RTC_Module.h/.cpp  — DS3231 часы реального времени
  Scale.h/.cpp       — HX711 весовой модуль (DT=D0, SCK=TX)
  SleepManager.h/.cpp — Deep Sleep (GPIO16→RST)
  Temperature.h/.cpp — DS18B20 (GPIO13/D7, общий с SD MOSI)
  WebServerModule.h/.cpp — веб-сервер и REST API
manual.html          — HTML инструкция с SVG схемой
pages/               — отдельные страницы документации
```

## Актуальная распиновка (NodeMCU ESP8266)

| Компонент | Сигнал | GPIO | Пин NodeMCU | Примечание |
|-----------|--------|------|-------------|------------|
| **HX711** | DT | GPIO16 | **D0** | Данные тензодатчика |
| **HX711** | SCK | GPIO1 | **TX** | Тактирование (Serial отключён!) |
| **LCD I2C** | SDA | GPIO4 | **D2** | I2C данные |
| **LCD I2C** | SCL | GPIO5 | **D1** | I2C тактовая |
| **DS3231 RTC** | SDA | GPIO4 | **D2** | Параллельно LCD |
| **DS3231 RTC** | SCL | GPIO5 | **D1** | Параллельно LCD |
| **DS18B20** | Data | GPIO13 | **D7** | Общий с SD MOSI! |
| **Кнопка MAIN** | — | GPIO0 | **D3** | Тарировка/калибровка |
| **Кнопка MENU** | — | GPIO2 | **D4** | Переключение экранов |
| **Батарея** | ADC | A0 | **A0** | Делитель 2:1 (100k+100k) |
| **SD-карта** | CS | GPIO15 | **D8** | SPI Chip Select |
| **SD-карта** | SCK | GPIO14 | **D5** | SPI Clock |
| **SD-карта** | MISO | GPIO12 | **D6** | SPI MISO |
| **SD-карта** | MOSI | GPIO13 | **D7** | SPI MOSI (общий с DS18B20) |

### Критические изменения
1. **HX711 перенесён на D0 (GPIO16) и TX (GPIO1)** — пины D5/D6 освобождены под SPI SD-карты
2. **DS18B20 на D7 (GPIO13)** — общий пин с SD MOSI, работает корректно
3. **Serial Monitor не работает** — GPIO1 (TX) занят HX711 SCK, `Serial.end()` вызывается в setup

## Реализованные фичи

### Авто-фиксация стабильных показаний
- EMA-сглаживание (alpha=0.2 по умолчанию)
- Буфер из 6 отсчётов, стабильность при размахе < 0.02 кг
- Сохранение в EEPROM каждые 10 мин при стабильном весе
- Spike-фильтр: отброс показаний при скачке > 5 кг

### Отмена тары (двойное нажатие MENU)
- `perform_taring()` сохраняет `prevOffset` перед тарированием
- Двойное нажатие MENU (< 500мс) → `undo_tare()` → восстановление offset
- prevOffset хранится в EEPROM (адрес 26)

### Мониторинг батареи
- `Battery.h/.cpp` — GPIO A0, делитель 2:1 (100k+100k)
- EMA alpha=0.1, чтение каждые 10с
- LCD экран #2: "Bat: X.XXV XX%", предупреждение при < 10%
- Web API: `batV`, `batPct` в `/api/data`

### Deep Sleep
- Авто-сон через 3 мин бездействия (кнопки, веб)
- GPIO16 (D0) → RST для пробуждения
- RTC user memory для хранения данных между циклами сна

### Watchdog HX711
- Проверка готовности датчика каждые 5с
- 6 неудачных попыток (30с) → power-cycle HX711
- Авто-восстановление без перезагрузки ESP

### SD-карта логирование
- CSV файл с BOM и разделителем ";" (для Excel)
- Ротация при 100 КБ (переименование в log_old.csv)
- LittleFS fallback если SD недоступна
- Валидация данных перед записью

### Веб-интерфейс
- REST API: `/api/data`, `/api/tare`, `/api/save`, `/api/log`, `/api/reboot`
- Live-дашборд с AJAX обновлением (5с)
- Настройки: alertDelta, calibWeight, emaAlpha, sleep, backlight, AP password
- Telegram интеграция: уведомления при изменении веса ≥ порога
- ThingSpeak: отправка данных в облако

## LCD экраны (8 штук)

| # | Экран | Описание |
|---|-------|----------|
| 0 | Вес | `Ves: 25.40*kg` (* = стабильно) |
| 1 | Дельта | `D:+01.60kg` / `Pred:23.80kg` |
| 2 | Батарея | `Bat:3.85V 71%` / `Li-Ion 1S` |
| 3 | Температура | `T:28.5C R:22.1C` / `H: 65.2 %` |
| 4 | Дата/время | `2026-02-23 15:30` / `RTC OK` |
| 5 | Статус | `CF:2280` / `W:OK N:14` |
| 6 | Калибровка | `CF:2280.0` / `MAIN=Vojti` |
| 7 | Диагностика | `Diagnostika...` → тест HX711, DS18B20, RTC, Battery |

## EEPROM карта адресов

| Адрес | Размер | Данные |
|-------|--------|--------|
| 0 | 4 | calibrationFactor (float) |
| 4 | 4 | offset (long) |
| 8 | 4 | weight (float) |
| 12 | 1 | magic (0xA5) |
| 13 | 4 | alertDelta (float) |
| 17 | 4 | calibWeight (float) |
| 21 | 4 | emaAlpha (float) |
| 25 | 1 | magic2 (0xA6) |
| 26 | 4 | prevOffset (long) — для отмены тары |
| 30 | 4 | prevWeight (float) — опорный вес для дельты |
| 34 | 4 | sleepSec (uint32_t) — интервал сна |
| 38 | 2 | lcdBlSec (uint16_t) — таймаут подсветки |
| 40 | 24 | apPass (char[24]) — пароль Wi-Fi AP |
| 64 | 1 | magic3 (0xA7) |
| 65 | 1 | tgMagic (0xB1) |
| 66 | 50 | tgToken (char[50]) — Telegram bot token |
| 116 | 16 | tgChatId (char[16]) — Telegram chat_id |
| 133 | 1 | wifiMagic (0xC1) |
| 134 | 1 | wifiMode (uint8_t) — 0=AP, 1=STA |
| 135 | 33 | wifiSsid (char[33]) — SSID роутера |
| 168 | 33 | wifiPass (char[33]) — Пароль роутера |
| **EEPROM_SIZE** | **256** | |

## Команды сборки и загрузки

### Arduino IDE настройки
1. **Плата**: NodeMCU 1.0 (ESP-12E Module)
2. **CPU Frequency**: 80 MHz
3. **Flash Size**: 4M (1M SPIFFS)
4. **Upload Speed**: 115200
5. **Port**: COM-порт (зависит от системы)

### Зависимости (Library Manager)
- `HX711` by bogde
- `LiquidCrystal I2C` by Frank de Brabander
- `OneWire` by Paul Stoffregen
- `DallasTemperature` by Miles Burton
- `RTClib` by Adafruit
- `ArduinoJson` v6 by Benoit Blanchon
- `SD` (встроенная)

## Текущий статус
- ✅ Все фичи реализованы и протестированы
- ✅ Код стабилен, работает на ESP8266
- ⚠️ **Документация требует обновления** — схема в pages/02_connection.html показывает ESP32 вместо NodeMCU

## Изменения в сессии (обновление документации) ✅

### 1. Исправление схемы подключения
- **Файл**: `pages/02_connection.html` (полная замена)
- **Проблема**: Старая схема показывала ESP32 с неверными пинами
- **Решение**: Новая SVG-схема с NodeMCU ESP8266 и правильными пинами

### 2. Добавлен экран диагностики (№7)
- **Файлы**: `manual.html`, `README.md`, `docs/MEMORY.md`
- **Изменения**: Добавлено описание 7-го экрана "Диагностика"
- **Функционал**: Тест HX711, DS18B20, RTC, батареи по кнопке MAIN

### 3. Обновлены таблицы распиновки
- **README.md**: Актуализирована таблица LCD экранов (8 шт)
- **docs/MEMORY.md**: Добавлена секция "LCD экраны (8 штук)"

---
**Сессия завершена**: Все изменения сохранены. Документация обновлена.
