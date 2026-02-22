#ifndef DISPLAY_H
#define DISPLAY_H

#include <LiquidCrystal_I2C.h>

#define LCD_COLS 16
#define LCD_ROWS 2

void lcd_init(LiquidCrystal_I2C &lcd);
void lcd_print_padded(LiquidCrystal_I2C &lcd, const char* text);
void lcd_print_padded(LiquidCrystal_I2C &lcd, const String &text);

#endif
