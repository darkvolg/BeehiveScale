#include "Button.h"

static const unsigned long DEBOUNCE_MS = 50;
static const unsigned long LONG_PRESS_MS = 2000;

ButtonAction read_button(int pin, ButtonState &state) {
  bool physicallyPressed = (digitalRead(pin) == LOW);
  unsigned long now = millis();

  if (physicallyPressed) {
    if (!state.isPressed) {
      state.pressStart = now;
      state.isPressed = true;
      state.longFired = false;
    } else if (!state.longFired && (now - state.pressStart >= LONG_PRESS_MS)) {
      state.longFired = true;
      return LONG_PRESS;
    }
  } else {
    if (state.isPressed) {
      state.isPressed = false;
      if (!state.longFired && (now - state.pressStart >= DEBOUNCE_MS)) {
        return SHORT_PRESS;
      }
    }
  }
  return NO_ACTION;
}
