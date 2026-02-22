# BeehiveScale — Архитектура

## Модули (файлы в BeehiveScale/)
| Файл | Назначение |
|------|------------|
| `BeehiveScale.ino` | Главный скетч: setup/loop, SystemState, экраны LCD, кнопки, serial команды |
| `Scale.h/.cpp` | HX711: init, check, read_weight |
| `Display.h/.cpp` | LCD 16x2 I2C: init, lcd_print_padded |
| `Button.h/.cpp` | Debounce кнопок: SHORT_PRESS, LONG_PRESS |
| `Memory.h/.cpp` | EEPROM: калибровка, offset, вес, web-настройки, prevOffset |
| `RTC_Module.h/.cpp` | DS3231 RTC: время, температура |
| `Temperature.h/.cpp` | DHT/DS18B20: температура, влажность |
| `Connectivity.h/.cpp` | WiFi (AP/STA), NTP, ThingSpeak, Telegram |
| `SleepManager.h/.cpp` | Deep sleep, RTC memory persist |
| `WebServerModule.h/.cpp` | HTTP сервер: HTML UI, REST API, настройки |
| `Battery.h/.cpp` | **НОВЫЙ** — ADC чтение Li-Ion через делитель, EMA сглаживание |

## Ключевые паттерны
- `SystemState sys` — глобальная структура состояния
- `WebData` — указатели на поля sys для веб-сервера (не копии!)
- `WebActions` — callback'и для действий из веб-интерфейса
- LCD экраны: switch/case по `sys.menuScreen` (0-5)
- Кнопки: BUTTON_PIN(GPIO0) — тарировка/калибровка, MENU_BTN_PIN(GPIO2) — переключение экранов + двойное нажатие для отмены тары
- EEPROM: magic bytes для валидации, commit() после записи
- EMA сглаживание для веса (настраиваемый alpha) и батареи (alpha=0.1)

## Пины
- HX711: DT=14, SCK=12
- Button: GPIO 0
- Menu: GPIO 2
- LCD I2C: 0x27
- Battery ADC: GPIO 34 (через делитель 100K+100K)

## Web API
- `GET /` — HTML страница
- `GET /api/data` — JSON со всеми показаниями
- `POST /api/tare` — тарировка
- `POST /api/save` — сохранить эталон
- `POST /api/settings` — настройки (alertDelta, calibWeight, emaAlpha)
- `POST /api/ntp` — синхронизация времени
- `POST /api/reboot` — перезагрузка
