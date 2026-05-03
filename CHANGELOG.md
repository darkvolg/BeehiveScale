# Changelog

Формат основан на [Keep a Changelog](https://keepachangelog.com/ru/1.1.0/),
проект использует [Semantic Versioning](https://semver.org/lang/ru/).

## [Unreleased]

## [4.2.1] — 2026-05-03

### Исправлено — корневая причина нестабильного веса
- **HX711 перенесён с GPIO16/GPIO1 на GPIO14/GPIO12 (D5/D6)** — стандартные пины ESP8266 для HX711, как в эталонной прошивке Bee_Lite_D1 v1.1. Прежняя распиновка не имела внутреннего pull-up на DOUT (GPIO16) и использовала UART TX как SCK (GPIO1), что давало плавающий вес и ложные срабатывания spike-фильтра / watchdog.
- Убран костыль `Serial.end()` перед `scale_init()` — больше не нужен, UART TX свободен.
- Удалён anti-stuck блок из `scale_read_weight()` (3 лишних raw чтения после `get_units`) — удваивал окно ловли наводок и не давал реальной защиты.
- Удалён дублирующий вызов `set_scale()`/`set_offset()` после `wifi_init()` — это были no-op в RAM, не имели смысла.
- Интервал чтения веса 800мс → 2500мс — снижает температурный дрейф HX711 и даёт WebServer окно на обработку запросов.

### Изменено
- Эталонный груз калибровки: верхний предел 5 кг → 50 кг (для калибровки по тяжёлым гирям при больших ульях).

### Удалено
- **SD-карта отключена** (`#define USE_SD_CARD` закомментирован в Logger.h). Логи и бэкапы теперь только в LittleFS (внутренний flash ESP8266). Причина: D5/D6/D7/D8 освобождаются от SPI, чтобы HX711 мог встать на стандартную D5/D6. В полевых условиях LittleFS надёжнее SD (нет окисления контактов слота, выдерживает -40..+85°C, FS с journaling).

### Изменено
- `LOG_INTERVAL_MS`: 1 мин → 5 мин (для пасеки достаточно, снижает износ LittleFS в 5x).

### Hardware
- ⚠ **Требуется перекоммутация HX711:** DT с D0 на D5, SCK с TX на D6. SD-карту физически можно оставить на коннекторе — она просто не будет использоваться.
- ⚠ **При прошивке в Arduino IDE выбрать**: Tools → Flash Size → **4MB (FS:2MB OTA:~512KB)** — для запаса места в LittleFS.
- Admin-пароль и OTA-пароль вынесены в EEPROM (больше не хардкод в исходниках)
- Добавлена CSRF-защита на все POST endpoints
- Санитизация CSV (защита от формула-инъекций в Excel/LibreOffice)
- `_maskSecret` больше не обрезает токены >63 байт, а маскирует середину
- Rate limit на `/api/log` и `/api/data`

### Исправлено
- `start_webserver()` теперь идемпотентный при Wi-Fi reconnect
- Guard на `sleepDur=0` в `loop()`
- `/api/reboot` отдаёт JSON до `ESP.restart()`, а не после
- RTC fallback больше не использует `__DATE__` — берёт последнее время из EEPROM
- Atomic rotate лог-файла на SD (write→verify→remove вместо copy+rename)
- Убран автоматический `LittleFS.format()` при ошибке `begin()`
- Сброс `spikeRejectCnt` при NaN
- Partial queue send: позиция снимается до закрытия файла

### Изменено
- `String` заменён на `char[]` + `snprintf` в горячих путях (Logger, WebServer, RTC)
- `/api/backup/restore` делает один `EEPROM.commit()` вместо 18 подряд (износ flash)
- `yield()` добавлен в BearSSL handshake
- Log rotate: при ошибке ротации лог truncate'ится вместо роста в бесконечность
- `apPassBuf` стал локальной переменной (race fix)

### Удалено
- Dummy-поле `humidity` из API (нет физического датчика)
- Посторонний `сервер.txt` (документация VPN от другого проекта)

### Документация
- Добавлен `LICENSE` (MIT)
- Добавлен `CHANGELOG.md`
- Добавлен `platformio.ini` для сборки через PlatformIO
- Добавлен `.github/workflows/build.yml` (CI на каждый push)
- Расширен `.gitignore` (корректные правила)
- Исправлена распиновка DS18B20 в `README.md` (GPIO3 вместо GPIO13)

## [4.1.0] — 2026-03-09

Baseline версия. Полный функционал:

- HX711 тензодатчик с EMA-сглаживанием и spike-фильтром
- DS18B20 термометр, DS3231 RTC
- LCD 1602 I2C с 7 экранами меню
- SD-карта CSV логирование
- Веб-интерфейс с REST API (Basic Auth `admin/beehive`)
- Telegram уведомления
- ThingSpeak облако
- Deep Sleep через 3 мин неактивности
- EEPROM калибровка с magic bytes
- OTA обновление (пароль `ota_beehive`)
