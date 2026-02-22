# BeehiveScale Project Memory

## Проект
- **BeehiveScale v4.1** — пчелиные весы на ESP32
- Язык: Arduino C++ (ESP32)
- Пользователь общается на **русском**

## Структура проекта
- Основная папка: `BeehiveScale/` (Arduino sketch)
- **Дублирование**: файлы существуют и в корне, и в `BeehiveScale/` — нужно синхронизировать обе копии после изменений
- Подробности → [architecture.md](architecture.md)

## Реализованные фичи (завершено)
### Отмена тары (двойное нажатие MENU)
- `perform_taring()` сохраняет `sys.prevOffset` перед тарированием
- Двойное нажатие MENU (< 500мс) → `undo_tare()` → восстановление offset
- prevOffset хранится в EEPROM (адрес 26)

### Мониторинг батареи
- `Battery.h/.cpp` — GPIO 34, делитель 2:1, EMA alpha=0.1, чтение каждые 10с
- LCD экран #5: "Bat: X.XXV  XX%", предупреждение при < 10%
- Web API: `batV`, `batPct` в `/api/data`
- HTML: строка батареи в карточке статуса с цветовой индикацией (ok/warn/err)

## EEPROM карта адресов
| Адрес | Размер | Данные |
|-------|--------|--------|
| 0     | 4      | calibrationFactor (float) |
| 4     | 4      | offset (long) |
| 8     | 4      | weight (float) |
| 12    | 1      | magic (0xA5) |
| 13    | 4      | alertDelta (float) |
| 17    | 4      | calibWeight (float) |
| 21    | 4      | emaAlpha (float) |
| 25    | 1      | magic2 (0xA6) |
| 26    | 4      | prevOffset (long) — НОВЫЙ |
| EEPROM_SIZE = 64 |
