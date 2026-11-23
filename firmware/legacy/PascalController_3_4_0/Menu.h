// ============================================================
// Menu.h - Az on-device menürendszer (navigáció, szerkesztés)
// ============================================================
#pragma once

MenuScreen menuScreen = MENU_NONE;

MenuScreen lastDrawnMenuScreen = MENU_NONE; // csak akkor fillScreen, ha ez eltér a menuScreen-től

bool idleNeedsFullClear = true; // egyszeri teljes törlés kell, mielőtt az idle képernyőt rajzoljuk

bool menuDirty = true; // igaz: a menüt újra kell rajzolni (KIZÁRÓLAG tényleges gombesemény után)

int menuCursor = 0;

int menuFanTarget = 1;     // 1 vagy 2

int menuRelayTarget = 1;   // 1 vagy 2

int menuEditPoint = 0;     // görbe-pont szerkesztésénél az aktuális pont indexe

int menuEditField = 0;     // 0=T,1=%,2=Hz (görbe) VAGY 0=BE,1=KI (relé termosztát) VAGY 0=R,1=G,2=B (RGB)

int menuTimeField = 0;     // 0=óra,1=perc

int menuLogicIdx = 0;      // a ténylegesen szerkesztett val[] index (0-3)

unsigned long lastMenuActivity = 0;

void menuTouch() { lastMenuActivity = millis(); }

const char* MODE_NAMES[] = { "2 homero", "1 homero", "Paratartalom", "Kapcsolo+ho", "2 kapcsolo" };

const char* MAIN_ITEMS[] = { "Mod", "Ido", "RGB szin", "Ventilator 1", "Ventilator 2", "Rele 1", "Rele 2" };

void drawValueList(const String &title, ValueRow rows[], int count, int cursor) {
  tft.setFont(&FreeSans9pt7b);
  const int headerH = 20;
  const int rowH = 20;
  const int maxVisible = (135 - headerH) / rowH; // kb. 5 sor fér ki egyszerre

  tft.fillRect(0, 0, 240, headerH, ST77XX_BLACK);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setCursor(2, 15);
  tft.print(title);
  tft.drawFastHLine(0, headerH, 240, ST77XX_WHITE);

  // Görgetési ablak - a kurzor mindig látható marad
  int start = 0;
  if (count > maxVisible) {
    start = cursor - maxVisible / 2;
    if (start < 0) start = 0;
    if (start > count - maxVisible) start = count - maxVisible;
  }

  // A TELJES lista-terület törlése minden rajzoláskor (nem csak soronként!) -
  // ez akadályozza meg, hogy elemszám-csökkenéskor (pl. görbe pontok száma)
  // a régi, hosszabb lista maradék sorai a képernyőn ragadjanak.
  tft.fillRect(0, headerH, 240, 135 - headerH, ST77XX_BLACK);

  int y = headerH;
  for (int i = start; i < count && i < start + maxVisible; i++) {
    uint16_t bg = (i == cursor) ? ST77XX_YELLOW : ST77XX_BLACK;
    tft.fillRect(0, y, 240, rowH, bg);
    tft.setCursor(4, y + 15);
    tft.setTextColor(i == cursor ? ST77XX_BLACK : ST77XX_WHITE, bg);
    tft.print(i == cursor ? "> " : "  ");
    tft.print(rows[i].label);
    if (rows[i].value.length() > 0) {
      tft.print(": ");
      tft.setTextColor(i == cursor ? ST77XX_BLACK : rows[i].valColor, bg);
      tft.print(rows[i].value);
    }
    y += rowH;
  }

  // Görgetés-jelzők, ha van a látható ablakon kívüli tartalom
  tft.setFont();
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
  if (start > 0) { tft.setCursor(230, headerH + 2); tft.print("^"); }
  if (start + maxVisible < count) { tft.setCursor(230, 128); tft.print("v"); }
}

void drawMenuList(const String &title, const char* items[], int count, int cursor) {
  ValueRow rows[8];
  for (int i = 0; i < count && i < 8; i++) { rows[i].label = items[i]; rows[i].value = ""; rows[i].valColor = ST77XX_WHITE; }
  drawValueList(title, rows, count, cursor);
}

void drawEditScreen(const String &title, const String &line1, const String &line2) {
  tft.setFont(&FreeSans9pt7b);
  tft.fillRect(0, 0, 240, 20, ST77XX_BLACK);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setCursor(2, 15);
  tft.print(title);
  tft.drawFastHLine(0, 20, 240, ST77XX_WHITE);

  // Bőkezű, fix méretű törlés az érték-területen (nem a szöveg tényleges
  // hosszához igazítva) - így BÁRMILYEN régi/új szöveghossz-kombináció
  // esetén garantáltan nem marad "szellem" karakter a képernyőn.
  tft.fillRect(0, 20, 240, 68, ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setCursor(4, 48);
  tft.print(line1);
  if (line2.length() > 0) { tft.setCursor(4, 78); tft.print(line2); }

  tft.setFont();
  tft.setTextSize(1);
  tft.fillRect(0, 108, 240, 16, ST77XX_BLACK);
  tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
  tft.setCursor(2, 118);
  tft.print("Fel/Le=ertek  Dupla=OK");
}

// Aktuálisan szerkesztett ThermFan/ThermRelay/SwitchLogicChannel referenciák
// a menuFanTarget/menuRelayTarget alapján.
ThermFan &curThermFan()   { return (menuFanTarget == 1) ? thermF1 : thermF2; }

ThermRelay &curThermRelay() { return (menuRelayTarget == 1) ? thermR1 : thermR2; }

bool &curFanAuto()   { return (menuFanTarget == 1) ? fan1Auto : fan2Auto; }

bool &curRelayAuto() { return (menuRelayTarget == 1) ? relay1Auto : relay2Auto; }

SwitchLogicChannel &curLogicFan()   { return (menuFanTarget == 1) ? logicF1 : logicF2; }

SwitchLogicChannel &curLogicRelay() { return (menuRelayTarget == 1) ? logicR1 : logicR2; }

bool isSwitchMode() { return currentMode == 3 || currentMode == 4; }

// A soron következő érvényes szenzor-forrás az aktuális módban:
// mode0: NTC1<->NTC2 | mode1: csak NTC1 | mode2: NTC1(ho)<->Paratartalom
uint8_t nextSensorForMode(uint8_t cur) {
  if (currentMode == 2) return (cur == 1) ? 3 : 1;
  if (currentMode == 1) return 1;
  return (cur == 1) ? 2 : 1;
}

String sensorLabel(uint8_t s) { return s == 3 ? "Paratartalom" : (s == 2 ? "NTC2" : "NTC1"); }

String logicValLabel(int v, bool isFan) {
  if (isFan) return v < 0 ? "GORBE" : (String(v) + "%");
  return v != 0 ? "BE" : "KI";
}

void enterMenu() {
  menuScreen = MENU_MAIN;
  menuCursor = 0;
  lastDrawnMenuScreen = MENU_NONE; // kényszeríti az EGYETLEN teljes törlést a menübe lépéskor
  menuDirty = true;
  menuTouch();
}

void exitMenuToIdle() {
  menuScreen = MENU_NONE;
  idleNeedsFullClear = true; // egyszeri teljes törlés, hogy a menü maradványai eltűnjenek
}

void drawMenu() {
  // Csak akkor van TELJES képernyő-törlés, ha ténylegesen MÁSIK menü-
  // képernyőre váltottunk (más elrendezés) - ugyanannak a képernyőnek az
  // ismételt újrarajzolása NEM töröl mindent, ezért nem villódzik a kijelző.
  if (menuScreen != lastDrawnMenuScreen) {
    tft.fillScreen(ST77XX_BLACK);
    lastDrawnMenuScreen = menuScreen;
  }
  ValueRow rows[8];

  switch (menuScreen) {
    case MENU_MAIN:
      drawMenuList("FOMENU", MAIN_ITEMS, MAIN_COUNT, menuCursor);
      break;

    case MENU_MODE_SET:
      drawEditScreen("MOD VALASZTAS", String(currentMode) + ": " + MODE_NAMES[currentMode]);
      break;

    case MENU_TIME_SET:
      drawEditScreen("IDO BEALLITAS",
        String(menuTimeField == 0 ? "[" : "") + (hr < 10 ? "0" : "") + String(hr) + String(menuTimeField == 0 ? "]" : "") + ":" +
        String(menuTimeField == 1 ? "[" : "") + (min_t < 10 ? "0" : "") + String(min_t) + String(menuTimeField == 1 ? "]" : ""));
      break;

    case MENU_RGB_LIST: {
      bool on = digitalRead(RGB_ON);
      rows[0] = { "Be/Ki", on ? "BEKAPCSOLVA" : "KIKAPCSOLVA", (uint16_t)(on ? ST77XX_GREEN : ST77XX_RED) };
      rows[1] = { "Piros", String(pwmR), ST77XX_CYAN };
      rows[2] = { "Zold", String(pwmG), ST77XX_CYAN };
      rows[3] = { "Kek", String(pwmB), ST77XX_CYAN };
      rows[4] = { "Vissza", "", ST77XX_WHITE };
      drawValueList("RGB SZIN", rows, 5, menuCursor);
      break;
    }
    case MENU_RGB_EDIT: {
      const char* names[3] = {"PIROS", "ZOLD", "KEK"};
      int val = menuEditField == 0 ? pwmR : (menuEditField == 1 ? pwmG : pwmB);
      drawEditScreen(names[menuEditField], String(val));
      break;
    }

    case MENU_FAN_SUB: {
      rows[0] = { "Auto/Kezi", curFanAuto() ? "AUTOMATA" : "KEZI", (uint16_t)(curFanAuto() ? ST77XX_GREEN : ST77XX_ORANGE) };
      rows[1] = { "Kezi %", String((int)((menuFanTarget == 1 ? pwmFan : pwmFan2) / 2.55)) + "%", ST77XX_CYAN };
      if (isSwitchMode()) {
        rows[2] = { "Logika", curLogicFan().enabled ? "BEALLITVA" : "NINCS", (uint16_t)(curLogicFan().enabled ? ST77XX_GREEN : ST77XX_WHITE) };
      } else {
        rows[2] = { "Gorbe pontjai", curThermFan().enabled ? (String(curThermFan().count) + " pont") : "NINCS",
                    (uint16_t)(curThermFan().enabled ? ST77XX_GREEN : ST77XX_WHITE) };
      }
      rows[3] = { "Vissza", "", ST77XX_WHITE };
      drawValueList(menuFanTarget == 1 ? "VENTILATOR 1" : "VENTILATOR 2", rows, 4, menuCursor);
      break;
    }
    case MENU_FAN_MANUAL_PCT:
      drawEditScreen("KEZI FORDULATSZAM", String((int)((menuFanTarget == 1 ? pwmFan : pwmFan2) / 2.55)) + " %");
      break;
    case MENU_FAN_SENSOR_SEL: {
      int n = 0;
      if (currentMode == 2) { rows[n].label = "NTC1 (Ho)"; rows[n].value = ""; rows[n].valColor = ST77XX_WHITE; n++;
                               rows[n].label = "Paratartalom"; rows[n].value = ""; rows[n].valColor = ST77XX_WHITE; n++; }
      else if (currentMode == 1) { rows[n].label = "NTC1"; rows[n].value = ""; rows[n].valColor = ST77XX_WHITE; n++; }
      else { rows[n].label = "NTC1"; rows[n].value = ""; rows[n].valColor = ST77XX_WHITE; n++;
             rows[n].label = "NTC2"; rows[n].value = ""; rows[n].valColor = ST77XX_WHITE; n++; }
      rows[n].label = "Vissza"; rows[n].value = ""; rows[n].valColor = ST77XX_WHITE; n++;
      drawValueList("ERZEKELO VALASZTAS", rows, n, menuCursor);
      break;
    }
    case MENU_FAN_CURVE_LIST: {
      ThermFan &tf = curThermFan();
      int n = 0;
      rows[n].label = "Forras"; rows[n].value = sensorLabel(tf.sensor); rows[n].valColor = ST77XX_CYAN; n++;
      rows[n].label = "Pontok szama"; rows[n].value = String(tf.count); rows[n].valColor = ST77XX_CYAN; n++;
      for (int i = 0; i < tf.count && n < 7; i++) {
        rows[n].label = String(i + 1) + ".";
        rows[n].value = String(tf.temp[i], 0) + "C " + String(tf.pct[i]) + "% " + String(tf.freq[i]) + "Hz";
        rows[n].valColor = ST77XX_CYAN;
        n++;
      }
      rows[n].label = "Vissza"; rows[n].value = ""; rows[n].valColor = ST77XX_WHITE; n++;
      drawValueList("GORBE PONTJAI", rows, n, menuCursor);
      break;
    }
    case MENU_FAN_CURVE_POINT: {
      ThermFan &tf = curThermFan();
      String fieldName = menuEditField == 0 ? "Homerseklet (C)" : (menuEditField == 1 ? "Fordulatszam (%)" : "Frekvencia (Hz)");
      String val = menuEditField == 0 ? String(tf.temp[menuEditPoint], 1) :
                   (menuEditField == 1 ? String(tf.pct[menuEditPoint]) : String(tf.freq[menuEditPoint]));
      drawEditScreen("PONT " + String(menuEditPoint + 1), fieldName, val);
      break;
    }
    case MENU_FAN_LOGIC_LIST: {
      SwitchLogicChannel &ch = curLogicFan();
      int n = 0;
      if (currentMode == 4) {
        const char* labels[4] = {"K1N-K2N", "K1N-K2C", "K1C-K2N", "K1C-K2C"};
        for (int i = 0; i < 4; i++) { rows[n].label = labels[i]; rows[n].value = logicValLabel(ch.val[i], true); rows[n].valColor = ST77XX_CYAN; n++; }
      } else {
        rows[n].label = "K1 Nyitott"; rows[n].value = logicValLabel(ch.val[0], true); rows[n].valColor = ST77XX_CYAN; n++;
        rows[n].label = "K1 Zart";    rows[n].value = logicValLabel(ch.val[2], true); rows[n].valColor = ST77XX_CYAN; n++;
      }
      rows[n].label = "Vissza"; rows[n].value = ""; rows[n].valColor = ST77XX_WHITE; n++;
      drawValueList("LOGIKA", rows, n, menuCursor);
      break;
    }
    case MENU_FAN_LOGIC_EDIT: {
      int v = curLogicFan().val[menuLogicIdx];
      String val = v < 0 ? "GORBE SZERINT" : (String(v) + " %");
      drawEditScreen("LOGIKA ERTEK", val, "(-1=gorbe, 0-100=fix %)");
      break;
    }

    case MENU_RELAY_SUB: {
      rows[0] = { "Auto/Kezi", curRelayAuto() ? "AUTOMATA" : "KEZI", (uint16_t)(curRelayAuto() ? ST77XX_GREEN : ST77XX_ORANGE) };
      int pin = (menuRelayTarget == 1) ? RELAY1_PIN : RELAY2_PIN;
      rows[1] = { "Kezi Be/Ki", digitalRead(pin) ? "BE" : "KI", (uint16_t)(digitalRead(pin) ? ST77XX_GREEN : ST77XX_RED) };
      if (isSwitchMode()) {
        rows[2] = { "Logika", curLogicRelay().enabled ? "BEALLITVA" : "NINCS", (uint16_t)(curLogicRelay().enabled ? ST77XX_GREEN : ST77XX_WHITE) };
      } else {
        rows[2] = { "Termosztat", curThermRelay().enabled ? "BEALLITVA" : "NINCS", (uint16_t)(curThermRelay().enabled ? ST77XX_GREEN : ST77XX_WHITE) };
      }
      rows[3] = { "Vissza", "", ST77XX_WHITE };
      drawValueList(menuRelayTarget == 1 ? "RELE 1" : "RELE 2", rows, 4, menuCursor);
      break;
    }
    case MENU_RELAY_MANUAL: {
      int pin = (menuRelayTarget == 1) ? RELAY1_PIN : RELAY2_PIN;
      drawEditScreen("KEZI RELE", digitalRead(pin) ? "BEKAPCSOLVA" : "KIKAPCSOLVA");
      break;
    }
    case MENU_RELAY_SENSOR_SEL: {
      int n = 0;
      if (currentMode == 2) { rows[n].label = "NTC1 (Ho)"; rows[n].value = ""; rows[n].valColor = ST77XX_WHITE; n++;
                               rows[n].label = "Paratartalom"; rows[n].value = ""; rows[n].valColor = ST77XX_WHITE; n++; }
      else if (currentMode == 1) { rows[n].label = "NTC1"; rows[n].value = ""; rows[n].valColor = ST77XX_WHITE; n++; }
      else { rows[n].label = "NTC1"; rows[n].value = ""; rows[n].valColor = ST77XX_WHITE; n++;
             rows[n].label = "NTC2"; rows[n].value = ""; rows[n].valColor = ST77XX_WHITE; n++; }
      rows[n].label = "Vissza"; rows[n].value = ""; rows[n].valColor = ST77XX_WHITE; n++;
      drawValueList("ERZEKELO VALASZTAS", rows, n, menuCursor);
      break;
    }
    case MENU_RELAY_THERM_LIST: {
      ThermRelay &tr = curThermRelay();
      bool isHum = (tr.sensor == 3);
      rows[0] = { "Erzekelo", sensorLabel(tr.sensor), ST77XX_CYAN };
      rows[1] = { "Bekapcsol", String(tr.ton, isHum ? 0 : 1) + (isHum ? "%" : "C"), ST77XX_CYAN };
      rows[2] = { "Kikapcsol", String(tr.toff, isHum ? 0 : 1) + (isHum ? "%" : "C"), ST77XX_CYAN };
      rows[3] = { "Vissza", "", ST77XX_WHITE };
      drawValueList("RELE TERMOSZTAT", rows, 4, menuCursor);
      break;
    }
    case MENU_RELAY_THERM_EDIT: {
      ThermRelay &tr = curThermRelay();
      bool isHum = (tr.sensor == 3);
      String title = menuEditField == 0 ? "BEKAPCSOL" : "KIKAPCSOL";
      float val = menuEditField == 0 ? tr.ton : tr.toff;
      drawEditScreen(title, String(val, isHum ? 0 : 1) + (isHum ? " %" : " C"));
      break;
    }
    case MENU_RELAY_LOGIC_LIST: {
      SwitchLogicChannel &ch = curLogicRelay();
      int n = 0;
      if (currentMode == 4) {
        const char* labels[4] = {"K1N-K2N", "K1N-K2C", "K1C-K2N", "K1C-K2C"};
        for (int i = 0; i < 4; i++) { rows[n].label = labels[i]; rows[n].value = logicValLabel(ch.val[i], false); rows[n].valColor = (uint16_t)(ch.val[i] ? ST77XX_GREEN : ST77XX_CYAN); n++; }
      } else {
        rows[n].label = "K1 Nyitott"; rows[n].value = logicValLabel(ch.val[0], false); rows[n].valColor = (uint16_t)(ch.val[0] ? ST77XX_GREEN : ST77XX_CYAN); n++;
        rows[n].label = "K1 Zart";    rows[n].value = logicValLabel(ch.val[2], false); rows[n].valColor = (uint16_t)(ch.val[2] ? ST77XX_GREEN : ST77XX_CYAN); n++;
      }
      rows[n].label = "Vissza"; rows[n].value = ""; rows[n].valColor = ST77XX_WHITE; n++;
      drawValueList("LOGIKA", rows, n, menuCursor);
      break;
    }
    case MENU_RELAY_LOGIC_EDIT: {
      int v = curLogicRelay().val[menuLogicIdx];
      drawEditScreen("LOGIKA ERTEK", v != 0 ? "BEKAPCSOLVA" : "KIKAPCSOLVA");
      break;
    }
    default: break;
  }
}

void handleMenuInput(ButtonEvent ev) {
  if (ev == BTN_NONE) return;
  menuTouch();

  switch (menuScreen) {
    case MENU_MAIN:
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % MAIN_COUNT;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + MAIN_COUNT) % MAIN_COUNT;
      else if (ev == BTN_BACK) exitMenuToIdle();
      else if (ev == BTN_SELECT) {
        switch (menuCursor) {
          case 0: menuScreen = MENU_MODE_SET; break;
          case 1: menuScreen = MENU_TIME_SET; menuTimeField = 0; break;
          case 2: menuScreen = MENU_RGB_LIST; menuCursor = 0; break;
          case 3: menuFanTarget = 1; menuScreen = MENU_FAN_SUB; menuCursor = 0; break;
          case 4: menuFanTarget = 2; menuScreen = MENU_FAN_SUB; menuCursor = 0; break;
          case 5: menuRelayTarget = 1; menuScreen = MENU_RELAY_SUB; menuCursor = 0; break;
          case 6: menuRelayTarget = 2; menuScreen = MENU_RELAY_SUB; menuCursor = 0; break;
        }
      }
      break;

    case MENU_MODE_SET:
      if (ev == BTN_UP) currentMode = (currentMode + 1) % 5;
      else if (ev == BTN_DOWN) currentMode = (currentMode + 4) % 5;
      else if (ev == BTN_SELECT || ev == BTN_BACK) { menuScreen = MENU_MAIN; menuCursor = 0; }
      break;

    case MENU_TIME_SET:
      if (ev == BTN_UP) { if (menuTimeField == 0) hr = (hr + 1) % 24; else min_t = (min_t + 1) % 60; }
      else if (ev == BTN_DOWN) { if (menuTimeField == 0) hr = (hr + 23) % 24; else min_t = (min_t + 59) % 60; }
      else if (ev == BTN_SELECT) {
        if (menuTimeField == 0) { menuTimeField = 1; }
        else { sec = 0; menuScreen = MENU_MAIN; menuCursor = 1; }
      }
      else if (ev == BTN_BACK) { menuScreen = MENU_MAIN; menuCursor = 1; }
      break;

    case MENU_RGB_LIST:
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % 5;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + 5) % 5;
      else if (ev == BTN_BACK) { menuScreen = MENU_MAIN; menuCursor = 2; }
      else if (ev == BTN_SELECT) {
        switch (menuCursor) {
          case 0: digitalWrite(RGB_ON, digitalRead(RGB_ON) ? LOW : HIGH); break;
          case 1: menuEditField = 0; menuScreen = MENU_RGB_EDIT; break;
          case 2: menuEditField = 1; menuScreen = MENU_RGB_EDIT; break;
          case 3: menuEditField = 2; menuScreen = MENU_RGB_EDIT; break;
          case 4: menuScreen = MENU_MAIN; menuCursor = 2; break;
        }
      }
      break;

    case MENU_RGB_EDIT: {
      int delta = (ev == BTN_UP) ? 5 : (ev == BTN_DOWN ? -5 : 0);
      if (delta != 0) {
        if (menuEditField == 0) pwmR = constrain(pwmR + delta, 0, 255);
        else if (menuEditField == 1) pwmG = constrain(pwmG + delta, 0, 255);
        else pwmB = constrain(pwmB + delta, 0, 255);
        updatePWMOutputs();
      }
      else if (ev == BTN_SELECT || ev == BTN_BACK) { menuScreen = MENU_RGB_LIST; menuCursor = 1 + menuEditField; }
      break;
    }

    case MENU_FAN_SUB:
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % 4;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + 4) % 4;
      else if (ev == BTN_BACK) { menuScreen = MENU_MAIN; menuCursor = (menuFanTarget == 1) ? 3 : 4; }
      else if (ev == BTN_SELECT) {
        switch (menuCursor) {
          case 0: curFanAuto() = !curFanAuto(); break;
          case 1: menuScreen = MENU_FAN_MANUAL_PCT; break;
          case 2:
            if (isSwitchMode()) { menuScreen = MENU_FAN_LOGIC_LIST; menuCursor = 0; }
            else { menuScreen = MENU_FAN_CURVE_LIST; menuCursor = 0; }
            break;
          case 3: menuScreen = MENU_MAIN; menuCursor = (menuFanTarget == 1) ? 3 : 4; break;
        }
      }
      break;

    case MENU_FAN_MANUAL_PCT: {
      int &pwm = (menuFanTarget == 1) ? pwmFan : pwmFan2;
      if (ev == BTN_UP) pwm = constrain(pwm + 13, 0, 255);
      else if (ev == BTN_DOWN) pwm = constrain(pwm - 13, 0, 255);
      if (ev == BTN_UP || ev == BTN_DOWN) setPWM(menuFanTarget == 1 ? 3 : 4, pwm);
      if (ev == BTN_SELECT || ev == BTN_BACK) { menuScreen = MENU_FAN_SUB; menuCursor = 1; }
      break;
    }

    case MENU_FAN_SENSOR_SEL: {
      int n = (currentMode == 1) ? 2 : 3;
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % n;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + n) % n;
      else if (ev == BTN_BACK) { menuScreen = MENU_FAN_CURVE_LIST; menuCursor = 0; }
      else if (ev == BTN_SELECT) {
        if (currentMode == 1) curThermFan().sensor = 1;
        else if (menuCursor == 0) curThermFan().sensor = 1;
        else if (menuCursor == 1) curThermFan().sensor = (currentMode == 2) ? 3 : 2;
        menuScreen = MENU_FAN_CURVE_LIST; menuCursor = 0;
      }
      break;
    }

    case MENU_FAN_CURVE_LIST: {
      ThermFan &tf = curThermFan();
      int n = tf.count + 3; // Forras + Pontok szama + N pont + Vissza
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % n;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + n) % n;
      else if (ev == BTN_BACK) { menuScreen = MENU_FAN_SUB; menuCursor = 2; }
      else if (ev == BTN_SELECT) {
        if (menuCursor == 0) { menuScreen = MENU_FAN_SENSOR_SEL; menuCursor = 0; }
        else if (menuCursor == 1) { tf.count = (tf.count % FAN_CURVE_MAX_POINTS) + 1; }
        else if (menuCursor == n - 1) { tf.enabled = true; menuScreen = MENU_FAN_SUB; menuCursor = 2; }
        else { menuEditPoint = menuCursor - 2; menuEditField = 0; menuScreen = MENU_FAN_CURVE_POINT; }
      }
      break;
    }

    case MENU_FAN_CURVE_POINT: {
      ThermFan &tf = curThermFan();
      if (ev == BTN_UP || ev == BTN_DOWN) {
        int dir = (ev == BTN_UP) ? 1 : -1;
        if (menuEditField == 0) tf.temp[menuEditPoint] = constrain(tf.temp[menuEditPoint] + dir, -40, 120);
        else if (menuEditField == 1) tf.pct[menuEditPoint] = (uint8_t)constrain((int)tf.pct[menuEditPoint] + dir * 5, 0, 100);
        else tf.freq[menuEditPoint] = (uint16_t)constrain((int)tf.freq[menuEditPoint] + dir * 10, 10, 25000);
      }
      else if (ev == BTN_SELECT) {
        if (menuEditField < 2) menuEditField++;
        else {
          sortFanCurve(tf, tf.count);
          tf.enabled = true;
          menuScreen = MENU_FAN_CURVE_LIST; menuCursor = menuEditPoint + 2;
        }
      }
      else if (ev == BTN_BACK) {
        sortFanCurve(tf, tf.count);
        menuScreen = MENU_FAN_CURVE_LIST; menuCursor = menuEditPoint + 2;
      }
      break;
    }

    case MENU_FAN_LOGIC_LIST: {
      int n = (currentMode == 4) ? 5 : 3;
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % n;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + n) % n;
      else if (ev == BTN_BACK) { menuScreen = MENU_FAN_SUB; menuCursor = 2; }
      else if (ev == BTN_SELECT) {
        if (menuCursor == n - 1) { menuScreen = MENU_FAN_SUB; menuCursor = 2; }
        else {
          menuLogicIdx = (currentMode == 4) ? menuCursor : (menuCursor == 0 ? 0 : 2);
          curLogicFan().axis2Mode = (currentMode == 4) ? 0 : 2;
          curLogicFan().enabled = true;
          menuScreen = MENU_FAN_LOGIC_EDIT;
        }
      }
      break;
    }

    case MENU_FAN_LOGIC_EDIT: {
      int &v = curLogicFan().val[menuLogicIdx];
      if (ev == BTN_UP) v = (v >= 100) ? 100 : (v < 0 ? 0 : v + 5);
      else if (ev == BTN_DOWN) v = (v <= 0) ? -1 : v - 5;
      else if (ev == BTN_SELECT || ev == BTN_BACK) {
        menuScreen = MENU_FAN_LOGIC_LIST;
        menuCursor = (currentMode == 4) ? menuLogicIdx : (menuLogicIdx == 0 ? 0 : 1);
      }
      break;
    }

    case MENU_RELAY_SUB:
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % 4;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + 4) % 4;
      else if (ev == BTN_BACK) { menuScreen = MENU_MAIN; menuCursor = (menuRelayTarget == 1) ? 5 : 6; }
      else if (ev == BTN_SELECT) {
        switch (menuCursor) {
          case 0: curRelayAuto() = !curRelayAuto(); break;
          case 1: menuScreen = MENU_RELAY_MANUAL; break;
          case 2:
            if (isSwitchMode()) { menuScreen = MENU_RELAY_LOGIC_LIST; menuCursor = 0; }
            else { menuScreen = MENU_RELAY_THERM_LIST; menuCursor = 0; }
            break;
          case 3: menuScreen = MENU_MAIN; menuCursor = (menuRelayTarget == 1) ? 5 : 6; break;
        }
      }
      break;

    case MENU_RELAY_MANUAL: {
      int pin = (menuRelayTarget == 1) ? RELAY1_PIN : RELAY2_PIN;
      if (ev == BTN_UP || ev == BTN_DOWN) digitalWrite(pin, digitalRead(pin) ? LOW : HIGH);
      if (ev == BTN_SELECT || ev == BTN_BACK) { menuScreen = MENU_RELAY_SUB; menuCursor = 1; }
      break;
    }

    case MENU_RELAY_SENSOR_SEL: {
      int n = (currentMode == 1) ? 2 : 3;
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % n;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + n) % n;
      else if (ev == BTN_BACK) { menuScreen = MENU_RELAY_THERM_LIST; menuCursor = 0; }
      else if (ev == BTN_SELECT) {
        if (currentMode == 1) curThermRelay().sensor = 1;
        else if (menuCursor == 0) curThermRelay().sensor = 1;
        else if (menuCursor == 1) curThermRelay().sensor = (currentMode == 2) ? 3 : 2;
        menuScreen = MENU_RELAY_THERM_LIST; menuCursor = 0;
      }
      break;
    }

    case MENU_RELAY_THERM_LIST:
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % 4;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + 4) % 4;
      else if (ev == BTN_BACK) { menuScreen = MENU_RELAY_SUB; menuCursor = 2; }
      else if (ev == BTN_SELECT) {
        switch (menuCursor) {
          case 0: menuScreen = MENU_RELAY_SENSOR_SEL; menuCursor = 0; break;
          case 1: menuEditField = 0; menuScreen = MENU_RELAY_THERM_EDIT; break;
          case 2: menuEditField = 1; menuScreen = MENU_RELAY_THERM_EDIT; break;
          case 3: curThermRelay().enabled = true; menuScreen = MENU_RELAY_SUB; menuCursor = 2; break;
        }
      }
      break;

    case MENU_RELAY_THERM_EDIT: {
      ThermRelay &tr = curThermRelay();
      bool isHum = (tr.sensor == 3);
      if (ev == BTN_UP || ev == BTN_DOWN) {
        float dir = (ev == BTN_UP) ? 1.0 : -1.0;
        if (menuEditField == 0) tr.ton = constrain(tr.ton + dir, isHum ? 0 : -40, isHum ? 100 : 120);
        else tr.toff = constrain(tr.toff + dir, isHum ? 0 : -40, isHum ? 100 : 120);
      }
      else if (ev == BTN_SELECT || ev == BTN_BACK) {
        tr.enabled = true;
        menuScreen = MENU_RELAY_THERM_LIST; menuCursor = 1 + menuEditField;
      }
      break;
    }

    case MENU_RELAY_LOGIC_LIST: {
      int n = (currentMode == 4) ? 5 : 3;
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % n;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + n) % n;
      else if (ev == BTN_BACK) { menuScreen = MENU_RELAY_SUB; menuCursor = 2; }
      else if (ev == BTN_SELECT) {
        if (menuCursor == n - 1) { menuScreen = MENU_RELAY_SUB; menuCursor = 2; }
        else {
          menuLogicIdx = (currentMode == 4) ? menuCursor : (menuCursor == 0 ? 0 : 2);
          curLogicRelay().axis2Mode = (currentMode == 4) ? 0 : 2;
          curLogicRelay().enabled = true;
          menuScreen = MENU_RELAY_LOGIC_EDIT;
        }
      }
      break;
    }

    case MENU_RELAY_LOGIC_EDIT: {
      SwitchLogicChannel &ch = curLogicRelay();
      if (ev == BTN_UP || ev == BTN_DOWN) ch.val[menuLogicIdx] = ch.val[menuLogicIdx] ? 0 : 1;
      else if (ev == BTN_SELECT || ev == BTN_BACK) {
        menuScreen = MENU_RELAY_LOGIC_LIST;
        menuCursor = (currentMode == 4) ? menuLogicIdx : (menuLogicIdx == 0 ? 0 : 1);
      }
      break;
    }

    default: break;
  }
}
