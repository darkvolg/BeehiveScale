#include "Display.h"

void lcd_init(LiquidCrystal_I2C &lcd) {
  delay(100);          // дать LCD время на включение до init
  lcd.init();
  delay(50);           // пауза после инициализации
  lcd.backlight();
  lcd.clear();
  lcd.noCursor();
  lcd.setCursor(0, 0);
}

void lcd_print_padded(LiquidCrystal_I2C &lcd, const char* text) {
  char buf[LCD_COLS + 1];
  int len = 0;
  if (text) {
    while (len < LCD_COLS && text[len]) len++;
  }
  for (int i = 0; i < len; i++) buf[i] = text[i];
  for (int i = len; i < LCD_COLS; i++) buf[i] = ' ';
  buf[LCD_COLS] = '\0';
  lcd.print(buf);
}

void lcd_print_padded(LiquidCrystal_I2C &lcd, const String &text) {
  lcd_print_padded(lcd, text.c_str());
}

static unsigned long _blLastActivity = 0;
static bool _blOn = true;

void lcd_backlight_activity(LiquidCrystal_I2C &lcd) {
  _blLastActivity = millis();
  if (!_blOn) {
    lcd.backlight();
    _blOn = true;
  }
}

void lcd_backlight_tick(LiquidCrystal_I2C &lcd, uint16_t timeoutSec) {
  if (timeoutSec == 0) {
    // Всегда включена
    if (!_blOn) { lcd.backlight(); _blOn = true; }
    return;
  }
  if (_blOn && (millis() - _blLastActivity >= (unsigned long)timeoutSec * 1000UL)) {
    lcd.noBacklight();
    _blOn = false;
  }
}
