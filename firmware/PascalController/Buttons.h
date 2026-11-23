// ============================================================
// Buttons.h - Fizikai gombok debounce + eseményfelismerés
// ============================================================
#pragma once

bool btnSelectConsumed = false;

void debounceButton(ButtonState &b, int pin) {
  bool r = digitalRead(pin) == LOW;
  unsigned long now = millis();
  if (r != b.raw) { b.raw = r; b.changeT = now; }
  if (now - b.changeT > BTN_DEBOUNCE_MS && b.stable != b.raw) {
    b.stable = b.raw;
    if (b.stable) { b.pressT = now; b.longFired = false; b.consumedBySelect = false; }
  }
}

ButtonEvent pollButtons() {
  debounceButton(btnL, BTN_LEFT);
  debounceButton(btnR, BTN_RIGHT);
  unsigned long now = millis();

  if (btnL.stable && btnR.stable) {
    if (!btnSelectConsumed) {
      btnSelectConsumed = true;
      btnL.consumedBySelect = true;
      btnR.consumedBySelect = true;
      return BTN_SELECT;
    }
    return BTN_NONE;
  } else {
    btnSelectConsumed = false;
  }

  if (btnL.stable && !btnR.stable && !btnL.consumedBySelect && !btnL.longFired) {
    if (now - btnL.pressT > BTN_LONG_MS) {
      btnL.longFired = true;
      return BTN_BACK;
    }
  }

  if (btnL.stable) btnL.wasDown = true;
  if (btnR.stable) btnR.wasDown = true;

  if (!btnL.stable && btnL.wasDown) {
    btnL.wasDown = false;
    if (!btnL.consumedBySelect && !btnL.longFired) return BTN_DOWN;
  }
  if (!btnR.stable && btnR.wasDown) {
    btnR.wasDown = false;
    if (!btnR.consumedBySelect) return BTN_UP;
  }
  return BTN_NONE;
}
