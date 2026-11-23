// ============================================================
// Buttons.h - Fizikai gombok debounce + eseményfelismerés
// ============================================================
#pragma once

bool btnSelectConsumed = false;
bool btnBothWasHeld = false; // menuUsesLongSelect módban: mindkét gomb nyomva volt-e ebben a ciklusban

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
    if (menuUsesLongSelect) {
      // WiFi jelszó-szerkesztő módja: rövid mindkét-gomb = karakter beírása
      // (SELECT), HOSSZÚ (>1,2mp) mindkét-gomb = csatlakozás indítása
      // (LONG_SELECT). A rövid SELECT-et csak ELENGEDÉSKOR tüzeljük, hogy
      // addig várhassunk, amíg kiderül, hosszú lesz-e a nyomás.
      btnBothWasHeld = true;
      if (!btnSelectConsumed) {
        unsigned long heldSince = (btnL.pressT > btnR.pressT) ? btnL.pressT : btnR.pressT;
        if (now - heldSince > BTN_LONG_SELECT_MS) {
          btnSelectConsumed = true;
          btnL.consumedBySelect = true;
          btnR.consumedBySelect = true;
          return BTN_LONG_SELECT;
        }
      }
      return BTN_NONE;
    } else {
      if (!btnSelectConsumed) {
        btnSelectConsumed = true;
        btnL.consumedBySelect = true;
        btnR.consumedBySelect = true;
        return BTN_SELECT;
      }
      return BTN_NONE;
    }
  } else {
    if (menuUsesLongSelect && btnBothWasHeld && !btnSelectConsumed) {
      // mindkét gomb nyomva volt, de RÖVID ideig - ez a sima SELECT esete,
      // most (elengedéskor) tüzeljük
      btnBothWasHeld = false;
      btnL.consumedBySelect = true; btnR.consumedBySelect = true;
      return BTN_SELECT;
    }
    btnBothWasHeld = false;
    btnSelectConsumed = false;
  }

  // ── Érték-szerkesztő képernyőkön (%-os/numerikus mezők, RGB csatornák):
  // a hosszan nyomott gomb NEM lép ki a menüből, hanem 2 mp után elkezd
  // automatikusan, ismételve léptetni - nem kell sokszor megnyomni. ──
  if (menuAllowsHoldRepeat) {
    if (btnL.stable && !btnR.stable && !btnL.consumedBySelect) {
      unsigned long held = now - btnL.pressT;
      if (held > BTN_HOLD_REPEAT_START_MS) {
        btnL.longFired = true; // jelezzük, hogy már "hosszan" kezelt - felengedéskor ne tüzeljen még egy BTN_DOWN-t
        if (now - btnL.lastRepeatT > BTN_HOLD_REPEAT_INTERVAL_MS) {
          btnL.lastRepeatT = now;
          return BTN_DOWN;
        }
      }
    }
    if (btnR.stable && !btnL.stable && !btnR.consumedBySelect) {
      unsigned long held = now - btnR.pressT;
      if (held > BTN_HOLD_REPEAT_START_MS) {
        btnR.longFired = true;
        if (now - btnR.lastRepeatT > BTN_HOLD_REPEAT_INTERVAL_MS) {
          btnR.lastRepeatT = now;
          return BTN_UP;
        }
      }
    }
  } else {
    // Normál (nem érték-szerkesztő) képernyőkön a megszokott hosszú nyomás
    // működik: LE hosszan = Vissza, FEL hosszan = LONG_UP (pl. digit-editor).
    if (btnL.stable && !btnR.stable && !btnL.consumedBySelect && !btnL.longFired) {
      if (now - btnL.pressT > BTN_LONG_MS) {
        btnL.longFired = true;
        return BTN_BACK;
      }
    }
    if (btnR.stable && !btnL.stable && !btnR.consumedBySelect && !btnR.longFired) {
      if (now - btnR.pressT > BTN_LONG_MS) {
        btnR.longFired = true;
        return BTN_LONG_UP;
      }
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
    if (!btnR.consumedBySelect && !btnR.longFired) return BTN_UP;
  }
  return BTN_NONE;
}
