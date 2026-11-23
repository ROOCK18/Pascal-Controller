// ============================================================
// Display.h - Az idle (alap) kijelző rajzolása
//
// A felhasználó saját tervezett, kifejezetten a 240x135-ös kijelzőre
// méretezett elrendezése:
//   Fent:    rövid mód-kód | óra | BT jel
//   Középen: bal keskeny sáv (R1/R2 relé állapot) + keretes doboz
//            (hőmérő ikon+érték, mód-függően kapcsoló ikon+állapot)
//   Lent:    Ventilátor1 kör+sáv | Ventilátor2 kör+sáv | RGB kör
// ============================================================
#pragma once

uint16_t tempToColor(float t) {
  if (isnan(t)) return ST77XX_WHITE;
  if (t < 15) return ST77XX_BLUE;
  if (t < 28) return ST77XX_GREEN;
  if (t < 38) return ST77XX_ORANGE;
  return ST77XX_RED;
}

// Egyszerű hőmérő-ikon (kis "üveg" + "gömb"), a mockup ikonjának stílusában
void drawThermoIcon(int x, int y, uint16_t col) {
  tft.drawRoundRect(x, y, 6, 16, 3, col);
  tft.fillCircle(x + 3, y + 18, 4, col);
  tft.fillRect(x + 2, y + 10, 2, 9, col);
}

// Kapcsoló-állapot "villám/cikcakk" ikon a mockup stílusában
void drawSwitchGlyph(int x, int y, uint16_t col) {
  tft.drawLine(x, y, x + 5, y + 5, col);
  tft.drawLine(x + 5, y + 5, x, y + 10, col);
  tft.drawLine(x, y + 10, x + 5, y + 15, col);
}

// Relé állapot-doboz a bal sávban (BE=zöld, KI=piros)
void drawRelayBox(int x, int y, const char* label, bool on) {
  uint16_t col = on ? ST77XX_GREEN : ST77XX_RED;
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setCursor(x, y);
  tft.print(label);
  tft.fillRoundRect(x, y + 9, 24, 11, 2, col);
  tft.setTextColor(ST77XX_BLACK, col);
  tft.setCursor(x + 4, y + 11);
  tft.print(on ? "BE" : "KI");
}

// Ventilátor kör + alatta százalék-sáv
void drawFanCircleBar(int cx, int topY, int w, int pct) {
  uint16_t col = pct > 0 ? ST77XX_GREEN : ST77XX_RED;
  tft.fillCircle(cx, topY, 14, col);
  tft.drawCircle(cx, topY, 14, ST77XX_WHITE);
  tft.setTextColor(ST77XX_BLACK, col);
  tft.setTextSize(1);
  tft.setCursor(cx - 3, topY - 4);
  tft.print(pct > 0 ? "1" : "0");

  int bx = cx - w / 2;
  int by = topY + 18;
  tft.drawRect(bx, by, w, 10, ST77XX_WHITE);
  tft.fillRect(bx + 1, by + 1, w - 2, 8, ST77XX_BLACK);
  int fillW = ((w - 2) * constrain(pct, 0, 100)) / 100;
  if (fillW > 0) tft.fillRect(bx + 1, by + 1, fillW, 8, ST77XX_GREEN);
  tft.fillRect(bx, by + 12, w, 9, ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setCursor(cx - 10, by + 13);
  tft.printf("%d%%", pct);
}

void drawBtIcon(int x, int y) {
  tft.fillCircle(x, y, 5, deviceConnected ? ST77XX_GREEN : ST77XX_RED);
  tft.drawCircle(x, y, 5, ST77XX_WHITE);
}

void drawRgbSwatch(int cx, int cy, int r) {
  uint16_t col = digitalRead(RGB_ON) ? tft.color565(pwmR, pwmG, pwmB) : 0x0000;
  tft.fillCircle(cx, cy, r, col);
  tft.drawCircle(cx, cy, r, ST77XX_WHITE);
  tft.setTextColor(ST77XX_WHITE, col);
  tft.setTextSize(1);
  tft.setCursor(cx - 10, cy - 3);
  tft.print("RGB");
}

// Nem használt a jelenlegi elrendezésben, de más képernyők (menü) hivatkozhatnak rá
void drawBar(int x, int y, int w, int h, int pct, uint16_t color) {
  tft.drawRect(x, y, w, h, ST77XX_WHITE);
  tft.fillRect(x + 1, y + 1, w - 2, h - 2, ST77XX_BLACK);
  int fillW = ((w - 2) * constrain(pct, 0, 100)) / 100;
  if (fillW > 0) tft.fillRect(x + 1, y + 1, fillW, h - 2, color);
}

// "Utoljára kirajzolt" értékek - csak akkor írunk a kijelzőre, ha a
// ténylegesen megjelenő érték változott. Ez jelentősen csökkenti az SPI
// forgalmat, ami a villódzás/szemcsézés fő oka volt.
int lastShownSec = -1, lastShownMin = -1, lastShownHr = -1;
bool lastShownBt = false;
uint16_t lastShownRgb = 0xFFFF;
int lastShownMode = -1;
float lastShownT1 = -999, lastShownT2 = -999, lastShownHum = -999;
bool lastShownSw1 = false, lastShownSw2 = false;
int lastShownFan1Pct = -1, lastShownFan2Pct = -1;
bool lastShownR1 = false, lastShownR2 = false;

// Rövid mód-kódok a bal felső sarokba
const char* MODE_SHORT[] = { "2Ho", "1Ho", "Ho+Pa", "Ho+K1", "2K" };

#define BOX_X   28
#define BOX_Y   15
#define BOX_W   210
#define BOX_H   58

void displayInfo() {
  tft.setFont();

  if (idleNeedsFullClear) {
    tft.fillScreen(ST77XX_BLACK);
    idleNeedsFullClear = false;

    tft.drawFastHLine(0, 13, 240, ST77XX_WHITE);
    tft.drawRoundRect(BOX_X, BOX_Y, BOX_W, BOX_H, 4, ST77XX_WHITE);
    tft.drawFastHLine(0, 75, 240, ST77XX_WHITE);

    lastShownSec = -1; lastShownMode = -1; lastShownBt = !deviceConnected;
    lastShownRgb = 0xFFFF; lastShownT1 = -999; lastShownT2 = -999; lastShownHum = -999;
    lastShownSw1 = !isSwitchClosed(NTC1_PIN); lastShownSw2 = !isSwitchClosed(NTC2_PIN);
    lastShownFan1Pct = -1; lastShownFan2Pct = -1;
    lastShownR1 = !digitalRead(RELAY1_PIN); lastShownR2 = !digitalRead(RELAY2_PIN);
  }

  // Mód rövid kódja (bal felül) - csak mód-váltáskor
  if (currentMode != lastShownMode) {
    lastShownMode = currentMode;
    tft.fillRect(0, 0, 60, 12, ST77XX_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    tft.setCursor(2, 2);
    tft.print(MODE_SHORT[currentMode]);
  }

  // Óra középen - csak percváltáskor
  if (min_t != lastShownMin || hr != lastShownHr) {
    lastShownSec = sec; lastShownMin = min_t; lastShownHr = hr;
    tft.fillRect(85, 0, 90, 12, ST77XX_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.setCursor(95, 2);
    tft.printf("%02d:%02d", hr, min_t);
  }

  // BT jelzőpont - csak állapotváltáskor
  if (deviceConnected != lastShownBt) {
    lastShownBt = deviceConnected;
    drawBtIcon(228, 6);
  }

  // Relé állapotok a bal sávban - csak változáskor
  bool r1 = digitalRead(RELAY1_PIN), r2 = digitalRead(RELAY2_PIN);
  if (r1 != lastShownR1) { lastShownR1 = r1; drawRelayBox(0, 17, "R1", r1); }
  if (r2 != lastShownR2) { lastShownR2 = r2; drawRelayBox(0, 47, "R2", r2); }

  // Középső doboz tartalma: hőmérő/kapcsoló, mód-függően
  switch (currentMode) {
    case 0: {
      if (fabs(ntc1Temperature - lastShownT1) >= 0.1) {
        lastShownT1 = ntc1Temperature;
        drawThermoIcon(BOX_X + 8, BOX_Y + 10, tempToColor(ntc1Temperature));
        tft.fillRect(BOX_X + 20, BOX_Y + 8, 80, 32, ST77XX_BLACK);
        tft.setTextSize(1); tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        tft.setCursor(BOX_X + 20, BOX_Y + 8); tft.print("Homero1");
        tft.setTextSize(2); tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        tft.setCursor(BOX_X + 20, BOX_Y + 20); tft.printf("%.1fC", ntc1Temperature);
      }
      if (fabs(ntc2Temperature - lastShownT2) >= 0.1) {
        lastShownT2 = ntc2Temperature;
        drawThermoIcon(BOX_X + 112, BOX_Y + 10, tempToColor(ntc2Temperature));
        tft.fillRect(BOX_X + 124, BOX_Y + 8, 80, 32, ST77XX_BLACK);
        tft.setTextSize(1); tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        tft.setCursor(BOX_X + 124, BOX_Y + 8); tft.print("Homero2");
        tft.setTextSize(2); tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        tft.setCursor(BOX_X + 124, BOX_Y + 20); tft.printf("%.1fC", ntc2Temperature);
      }
      break;
    }
    case 1: {
      if (fabs(ntc1Temperature - lastShownT1) >= 0.1) {
        lastShownT1 = ntc1Temperature;
        drawThermoIcon(BOX_X + 20, BOX_Y + 10, tempToColor(ntc1Temperature));
        tft.fillRect(BOX_X + 34, BOX_Y + 8, 160, 32, ST77XX_BLACK);
        tft.setTextSize(1); tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        tft.setCursor(BOX_X + 34, BOX_Y + 8); tft.print("Homero");
        tft.setTextSize(2); tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        tft.setCursor(BOX_X + 34, BOX_Y + 20); tft.printf("%.1fC", ntc1Temperature);
      }
      break;
    }
    case 2: {
      if (fabs(ntc1Temperature - lastShownT1) >= 0.1) {
        lastShownT1 = ntc1Temperature;
        drawThermoIcon(BOX_X + 8, BOX_Y + 10, tempToColor(ntc1Temperature));
        tft.fillRect(BOX_X + 20, BOX_Y + 8, 80, 32, ST77XX_BLACK);
        tft.setTextSize(1); tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        tft.setCursor(BOX_X + 20, BOX_Y + 8); tft.print("Homero");
        tft.setTextSize(2); tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        tft.setCursor(BOX_X + 20, BOX_Y + 20); tft.printf("%.1fC", ntc1Temperature);
      }
      bool humChanged = (isnan(humidity) != isnan(lastShownHum)) ||
                         (!isnan(humidity) && fabs(humidity - lastShownHum) >= 0.5);
      if (humChanged) {
        lastShownHum = humidity;
        tft.fillRect(BOX_X + 112, BOX_Y + 8, 92, 32, ST77XX_BLACK);
        tft.setTextSize(1); tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        tft.setCursor(BOX_X + 112, BOX_Y + 8); tft.print("Paratartalom");
        tft.setTextSize(2); tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
        tft.setCursor(BOX_X + 112, BOX_Y + 20);
        if (isnan(humidity)) tft.print("--%");
        else tft.printf("%.0f%%", humidity);
      }
      break;
    }
    case 3: {
      if (fabs(ntc1Temperature - lastShownT1) >= 0.1) {
        lastShownT1 = ntc1Temperature;
        drawThermoIcon(BOX_X + 8, BOX_Y + 10, tempToColor(ntc1Temperature));
        tft.fillRect(BOX_X + 20, BOX_Y + 8, 80, 32, ST77XX_BLACK);
        tft.setTextSize(1); tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        tft.setCursor(BOX_X + 20, BOX_Y + 8); tft.print("Homero");
        tft.setTextSize(2); tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        tft.setCursor(BOX_X + 20, BOX_Y + 20); tft.printf("%.1fC", ntc1Temperature);
      }
      bool sw = isSwitchClosed(NTC2_PIN);
      if (sw != lastShownSw2) {
        lastShownSw2 = sw;
        uint16_t col = sw ? ST77XX_GREEN : ST77XX_RED;
        drawSwitchGlyph(BOX_X + 114, BOX_Y + 10, col);
        tft.fillRect(BOX_X + 124, BOX_Y + 8, 80, 32, ST77XX_BLACK);
        tft.setTextSize(1); tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        tft.setCursor(BOX_X + 124, BOX_Y + 8); tft.print("Kapcsolo");
        tft.setTextSize(2); tft.setTextColor(col, ST77XX_BLACK);
        tft.setCursor(BOX_X + 124, BOX_Y + 20); tft.print(sw ? "Zart" : "Nyit");
      }
      break;
    }
    case 4: {
      bool sw1 = isSwitchClosed(NTC1_PIN), sw2 = isSwitchClosed(NTC2_PIN);
      if (sw1 != lastShownSw1) {
        lastShownSw1 = sw1;
        uint16_t col = sw1 ? ST77XX_GREEN : ST77XX_RED;
        drawSwitchGlyph(BOX_X + 8, BOX_Y + 10, col);
        tft.fillRect(BOX_X + 20, BOX_Y + 8, 80, 32, ST77XX_BLACK);
        tft.setTextSize(1); tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        tft.setCursor(BOX_X + 20, BOX_Y + 8); tft.print("K1");
        tft.setTextSize(2); tft.setTextColor(col, ST77XX_BLACK);
        tft.setCursor(BOX_X + 20, BOX_Y + 20); tft.print(sw1 ? "Zart" : "Nyit");
      }
      if (sw2 != lastShownSw2) {
        lastShownSw2 = sw2;
        uint16_t col = sw2 ? ST77XX_GREEN : ST77XX_RED;
        drawSwitchGlyph(BOX_X + 114, BOX_Y + 10, col);
        tft.fillRect(BOX_X + 124, BOX_Y + 8, 80, 32, ST77XX_BLACK);
        tft.setTextSize(1); tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        tft.setCursor(BOX_X + 124, BOX_Y + 8); tft.print("K2");
        tft.setTextSize(2); tft.setTextColor(col, ST77XX_BLACK);
        tft.setCursor(BOX_X + 124, BOX_Y + 20); tft.print(sw2 ? "Zart" : "Nyit");
      }
      break;
    }
  }

  // Alsó sor: ventilátor1 kör+sáv, ventilátor2 kör+sáv, RGB kör
  int f1pct = (int)(pwmFan / 2.55), f2pct = (int)(pwmFan2 / 2.55);
  if (f1pct != lastShownFan1Pct) { lastShownFan1Pct = f1pct; drawFanCircleBar(40, 92, 76, f1pct); }
  if (f2pct != lastShownFan2Pct) { lastShownFan2Pct = f2pct; drawFanCircleBar(122, 92, 76, f2pct); }

  uint16_t curRgb = digitalRead(RGB_ON) ? tft.color565(pwmR, pwmG, pwmB) : 0x0000;
  if (curRgb != lastShownRgb) { lastShownRgb = curRgb; drawRgbSwatch(207, 105, 24); }
}
