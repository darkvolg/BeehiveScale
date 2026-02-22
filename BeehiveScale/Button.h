#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>

enum ButtonAction { NO_ACTION, SHORT_PRESS, LONG_PRESS };

struct ButtonState {
  unsigned long pressStart = 0;
  bool isPressed = false;
  bool longFired = false;
};

ButtonAction read_button(int pin, ButtonState &state);

#endif
