// ============================================================
// Menu.h - Az on-device menürendszer (navigáció, szerkesztés)
// ============================================================
#pragma once

MenuScreen menuScreen = MENU_NONE;

MenuScreen lastDrawnMenuScreen = MENU_NONE; // csak akkor fillScreen, ha ez eltér a menuScreen-től

// (idleNeedsFullClear a Globals.h-ban van deklarálva, mert a Timers.h - ami
// korábban települ - is hivatkozik rá)

bool menuDirty = true; // igaz: a menüt újra kell rajzolni (KIZÁRÓLAG tényleges gombesemény után)

int menuCursor = 0;

int menuFanTarget = 1;     // 1 vagy 2

int menuRelayTarget = 1;   // 1 vagy 2

int menuEditPoint = 0;     // görbe-pont szerkesztésénél az aktuális pont indexe

int menuEditField = 0;     // 0=T,1=%,2=Hz (görbe) VAGY 0=BE,1=KI (relé termosztát) VAGY 0=R,1=G,2=B (RGB)

int menuLogicIdx = 0;      // a ténylegesen szerkesztett val[] index (0-3)

// Frekvencia digit-szerkesztő állapota
int menuFreqTargetType = 0;  // 0 = kezi ventilátor frekvencia (FAN1/2_PWM_FREQ), 1 = göbre pont frekvenciája
uint8_t freqDigits[5] = {0, 0, 0, 3, 0}; // 5 jegyű kijelzés (pl. 00030 = 30Hz)
int freqDigitPos = 0;      // melyik digit van épp kiválasztva (0=legbal, 4=legjobb)

unsigned long lastMenuActivity = 0;

void menuTouch() { lastMenuActivity = millis(); }

const char* MODE_NAMES[] = { "2 hőmérő", "1 hőmérő", "Páratartalom", "Kapcsoló+hő", "2 kapcsoló" };

const char* MAIN_ITEMS[] = { "Beállítások", "RGB szín", "Ventilátor 1", "Ventilátor 2", "Relé 1", "Relé 2" };

// A tényleges rajzolás az ui.h-ban van (LVGL) - ezek a vékony átvezetők
// őrzik meg a menü eredeti hívási felületét.
void drawValueList(const String &title, ValueRow rows[], int count, int cursor) {
  ui_list(title, rows, count, cursor);
}

void drawMenuList(const String &title, const char* items[], int count, int cursor) {
  ValueRow rows[8];
  for (int i = 0; i < count && i < 8; i++) { rows[i].label = items[i]; rows[i].value = ""; rows[i].valColor = UI_WHITE; }
  drawValueList(title, rows, count, cursor);
}

void drawEditScreen(const String &title, const String &line1, const String &line2) {
  ui_edit(title, line1, line2);
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

String sensorLabel(uint8_t s) { return s == 3 ? "Páratartalom" : (s == 2 ? "NTC2" : "NTC1"); }

// A digit-tömbből (5 jegy) összeállítja a tényleges Hz értéket
uint16_t freqDigitsToValue() {
  uint32_t v = 0;
  for (int i = 0; i < 5; i++) v = v * 10 + freqDigits[i];
  return (uint16_t)constrain((long)v, 0, 65535);
}

// A digit-tömböt feltölti egy meglévő Hz érték számjegyeivel
void freqValueToDigits(uint16_t val) {
  for (int i = 4; i >= 0; i--) { freqDigits[i] = val % 10; val /= 10; }
}

// A frekvencia tényleges alkalmazása - vagy a kézi ventilátor PWM
// frekvenciájára (CSAK Kézi módban hat), vagy egy görbe-pont frekvenciájára
// (görbe-pontok mindig szerkeszthetők, azok automatika-konfigurációnak
// számítanak, nem "kézi" beavatkozásnak).
void applyFrequencyValue(uint16_t freq) {
  if (menuFreqTargetType == 0 && curFanAuto()) return; // kezi freki csak Kezi modban hat
  freq = (uint16_t)constrain((int)freq, 10, 25000);
  if (menuFreqTargetType == 0) {
    int pin = (menuFanTarget == 1) ? FAN1_PWM_PIN : FAN2_PWM_PIN;
    int &f = (menuFanTarget == 1) ? FAN1_PWM_FREQ : FAN2_PWM_FREQ;
    if (f != (int)freq) {
      ledcDetach(pin);
      f = freq;
      ledcAttach(pin, f, PWM_RESOLUTION); // real-time alkalmazás
    }
  } else if (menuFreqTargetType == 1) {
    curThermFan().freq[menuEditPoint] = freq;
  } else {
    curLogicFan().freq[menuLogicIdx] = freq;
  }
  markDirty();
}

// Visszalépés a frekvencia-szerkesztőből a hívó képernyőre
void exitFreqEdit() {
  if (menuFreqTargetType == 0) { menuScreen = MENU_FAN_SUB; menuCursor = 3; }
  else if (menuFreqTargetType == 1) { menuScreen = MENU_FAN_CURVE_POINT; menuCursor = 2; }
  else { menuScreen = MENU_FAN_LOGIC_EDIT; menuCursor = 1; }
}

String logicValLabel(int v, bool isFan) {
  if (isFan) return v < 0 ? "GÖRBE" : (String(v) + "%");
  if (v < 0) return "TERMOSZTÁT";
  return v != 0 ? "BE" : "KI";
}

// ============================================================
// SZENZOR BEÁLLÍTÁSOK (menü)
// ============================================================
enum SensField : uint8_t { SF_TYPE, SF_R0, SF_T0, SF_BETA, SF_RS, SF_VS, SF_COMP, SF_LIVE, SF_BACK };
const char* SENS_TYPE_NAMES[4] = { "PT1000", "NTC 10k", "NTC 100k", "Egyedi" };

float &sensComp(int s) { return s == 0 ? tempCompNTC1 : tempCompNTC2; }

// Az aktuális típushoz tartozó mezők sora (PT1000-nél nincs alap hőfok és béta).
int sensBuildFields(int s, SensField out[]) {
  int n = 0;
  out[n++] = SF_TYPE; out[n++] = SF_R0;
  if (sensCfg[s].type != 0) { out[n++] = SF_T0; out[n++] = SF_BETA; }
  out[n++] = SF_RS; out[n++] = SF_VS; out[n++] = SF_COMP; out[n++] = SF_LIVE; out[n++] = SF_BACK;
  return n;
}

const char* sensFieldLabel(SensField f) {
  switch (f) {
    case SF_TYPE: return "Típus";
    case SF_R0:   return "Névleges ellenállás";
    case SF_T0:   return "Alap hőfok";
    case SF_BETA: return "Béta (β)";
    case SF_RS:   return "Soros ellenállás";
    case SF_VS:   return "Tápfeszültség";
    case SF_COMP: return "Kompenzáció";
    case SF_LIVE: return "Mért érték";
    default:      return "Vissza";
  }
}

String sensFieldValue(int s, SensField f) {
  const SensorCfg &c = sensCfg[s];
  switch (f) {
    case SF_TYPE: return SENS_TYPE_NAMES[c.type < 4 ? c.type : 3];
    case SF_R0:   return String(c.r0, 0) + " Ω";
    case SF_T0:   return String(c.t0, 0) + " °C";
    case SF_BETA: return String(c.beta, 0);
    case SF_RS:   return String(c.rSeries, 0) + " Ω";
    case SF_VS:   return String(c.vSupply, 1) + " V";
    case SF_COMP: return String(sensComp(s), 1) + " °C";
    case SF_LIVE: { float t = (s == 0) ? ntc1Temperature : ntc2Temperature; return isnan(t) ? String("HIBA") : (String(t, 1) + " °C"); }
    default:      return "";
  }
}

// Egy lépés fel/le. Hosszan nyomva (ismétlődő események) a lépés felgyorsul,
// így a nagy értékek (pl. 10000 Ω) is gyorsan beállíthatók, a finomhangolás pedig pontos marad.
void sensEditStep(int s, SensField f, int dir) {
  static unsigned long lastT = 0;
  static int lastDir = 0, rep = 0;
  unsigned long now = millis();
  if (dir == lastDir && now - lastT < 400) rep++; else rep = 0;
  lastDir = dir; lastT = now;
  float mul = rep > 25 ? 100.0f : (rep > 8 ? 10.0f : 1.0f);
  SensorCfg &c = sensCfg[s];
  switch (f) {
    case SF_R0:   c.r0 = constrain(c.r0 + dir * mul, 1.0f, 1000000.0f); break;
    case SF_T0:   c.t0 = constrain(c.t0 + dir * (mul > 10.0f ? 10.0f : mul), -50.0f, 150.0f); break;
    case SF_BETA: c.beta = constrain(c.beta + dir * mul, 500.0f, 10000.0f); break;
    case SF_RS:   c.rSeries = constrain(c.rSeries + dir * mul, 10.0f, 1000000.0f); break;
    case SF_VS:   c.vSupply = constrain(roundf((c.vSupply + dir * (mul > 1.0f ? 0.5f : 0.1f)) * 10.0f) / 10.0f, 1.0f, 6.0f); break;
    case SF_COMP: { float &v = sensComp(s); v = constrain(roundf((v + dir * (mul > 1.0f ? 0.5f : 0.1f)) * 10.0f) / 10.0f, -20.0f, 20.0f); break; }
    default: break;
  }
  // A modell-paraméterek kézi módosítása után a típus "Egyedi" lesz
  // (a PT1000 típus modellje fix, ott az R0/soros/táp módosítása nem változtat típust).
  if ((f == SF_R0 || f == SF_T0 || f == SF_BETA) && (c.type == 1 || c.type == 2)) c.type = 3;
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
  // Hosszú nyomás automatikus ismétlése CSAK az érték-szerkesztő
  // képernyőkön engedélyezett (itt a LE gomb hosszú nyomása NEM lép ki a
  // menüből, hanem 2 mp után elkezd automatikusan léptetni). A
  // MENU_FREQ_DIGIT_EDIT szándékosan KIMARAD, mert ott a FEL gomb hosszú
  // nyomása a digit-pozíció váltására szolgál, azt nem szabad felülírni.
  menuAllowsHoldRepeat =
    (menuScreen == MENU_SENS_EDIT) ||
    (menuScreen == MENU_BRIGHTNESS_EDIT) ||
    (menuScreen == MENU_RGB_EDIT) ||
    (menuScreen == MENU_FAN_MANUAL_PCT) ||
    (menuScreen == MENU_FAN_CURVE_FIELD_EDIT) ||
    (menuScreen == MENU_FAN_LOGIC_VAL_EDIT) ||
    (menuScreen == MENU_RELAY_HOLD_EDIT) ||
    (menuScreen == MENU_TIME_EDIT) ||
    (menuScreen == MENU_WIFI_PASSWORD_EDIT);

  menuUsesLongSelect = (menuScreen == MENU_WIFI_PASSWORD_EDIT);

  // Csak akkor van TELJES képernyő-törlés, ha ténylegesen MÁSIK menü-
  // képernyőre váltottunk (más elrendezés) - ugyanannak a képernyőnek az
  // ismételt újrarajzolása NEM töröl mindent, ezért nem villódzik a kijelző.
  lastDrawnMenuScreen = menuScreen;
  ValueRow rows[12];

  switch (menuScreen) {
    case MENU_MAIN:
      drawMenuList("FŐMENÜ", MAIN_ITEMS, MAIN_COUNT, menuCursor);
      break;

    case MENU_SETTINGS_SUB: {
      const char* items[8] = { "Mód", "Dátum/Idő", "Szenzor beállítások", "Kijelző", "WiFi", "MQTT", "Factory reset", "Vissza" };
      drawMenuList("BEÁLLÍTÁSOK", items, 8, menuCursor);
      break;
    }

    case MENU_DISPLAY_SUB: {
      const char* items[3] = { "Fényerő", "Elalvási idő", "Vissza" };
      drawMenuList("KIJELZŐ", items, 3, menuCursor);
      break;
    }

    case MENU_BRIGHTNESS_EDIT:
      drawEditScreen("FÉNYERŐ", String(displayBrightness) + " %");
      break;

    case MENU_SLEEP_SEL: {
      int n = 0;
      for (int i = 0; i < SLEEP_OPTIONS_COUNT; i++) { rows[n].label = SLEEP_NAMES[i]; rows[n].value = (i == sleepTimeoutIdx) ? "AKTÍV" : ""; rows[n].valColor = UI_GREEN; n++; }
      rows[n] = { "Vissza", "", UI_WHITE }; n++;
      drawValueList("ELALVÁSI IDŐ", rows, n, menuCursor);
      break;
    }

    case MENU_MODE_LIST: {
      int n = 0;
      for (int i = 0; i < 5; i++) { rows[n].label = MODE_NAMES[i]; rows[n].value = (i == currentMode) ? "AKTÍV" : ""; rows[n].valColor = UI_GREEN; n++; }
      rows[n] = { "Vissza", "", UI_WHITE }; n++;
      drawValueList("MÓD VÁLASZTÁS", rows, n, menuCursor);
      break;
    }

    case MENU_COMP_LIST: {
      // Csak azok a szenzorok jelennek meg, amik az AKTUÁLIS módban tényleg
      // hőmérőként vannak bekötve - a DHT11-nek és a kapcsolóknak nincs szenzor-beállítása.
      int n = 0;
      if (currentMode == 0) {
        rows[n] = { "1. szenzor", SENS_TYPE_NAMES[sensCfg[0].type < 4 ? sensCfg[0].type : 3], UI_CYAN }; n++;
        rows[n] = { "2. szenzor", SENS_TYPE_NAMES[sensCfg[1].type < 4 ? sensCfg[1].type : 3], UI_CYAN }; n++;
      } else if (currentMode == 1 || currentMode == 3) {
        rows[n] = { "1. szenzor", SENS_TYPE_NAMES[sensCfg[0].type < 4 ? sensCfg[0].type : 3], UI_CYAN }; n++;
      }
      if (n == 0) { rows[n] = { "Nincs hőmérő ebben a módban", "", UI_WHITE }; n++; }
      rows[n] = { "Vissza", "", UI_WHITE }; n++;
      drawValueList("SZENZOR BEÁLLÍTÁSOK", rows, n, menuCursor);
      break;
    }

    case MENU_SENS_LIST: {
      SensField f[9];
      int n = sensBuildFields(menuSensTarget, f);
      for (int i = 0; i < n; i++) {
        uint16_t col = UI_CYAN;
        if (f[i] == SF_BACK) col = UI_WHITE;
        if (f[i] == SF_LIVE) {
          float t = (menuSensTarget == 0) ? ntc1Temperature : ntc2Temperature;
          col = isnan(t) ? UI_RED : UI_GREEN;
        }
        rows[i] = { sensFieldLabel(f[i]), sensFieldValue(menuSensTarget, f[i]), col };
      }
      drawValueList(menuSensTarget == 0 ? "1. SZENZOR" : "2. SZENZOR", rows, n, menuCursor);
      break;
    }

    case MENU_SENS_EDIT: {
      String title = String(sensFieldLabel((SensField)menuSensField));
      title.toUpperCase();
      drawEditScreen(title, sensFieldValue(menuSensTarget, (SensField)menuSensField));
      break;
    }

    case MENU_SENS_TYPE_SEL: {
      int n = 0;
      for (int i = 0; i < 4; i++) { rows[n].label = SENS_TYPE_NAMES[i]; rows[n].value = (i == sensCfg[menuSensTarget].type) ? "AKTÍV" : ""; rows[n].valColor = UI_GREEN; n++; }
      rows[n] = { "Vissza", "", UI_WHITE }; n++;
      drawValueList("SZENZOR TÍPUSA", rows, n, menuCursor);
      break;
    }

    case MENU_MQTT_LIST: {
      const char* st[4] = { "kikapcsolva", "nincs WiFi", "csatlakozik...", "csatlakozva" };
      uint16_t stCol = (mqttState == 3) ? UI_GREEN : (mqttState == 0 ? UI_WHITE : UI_YELLOW);
      String broker = mqttCfg.host[0] ? (String(mqttCfg.host) + ":" + String(mqttCfg.port)) : String("nincs (app)");
      rows[0] = { "Bekapcsolva", mqttCfg.enabled ? "BE" : "KI", mqttCfg.enabled ? UI_GREEN : UI_RED };
      rows[1] = { "Állapot", st[mqttState < 4 ? mqttState : 0], stCol };
      rows[2] = { "Bróker", broker, UI_CYAN };
      rows[3] = { "Név", String(mqttCfg.name), UI_CYAN };
      rows[4] = { "Vissza", "", UI_WHITE };
      drawValueList("MQTT", rows, 5, menuCursor);
      break;
    }

    case MENU_FACTORY_CONFIRM:
      drawEditScreen("GYÁRI VISSZAÁLLÍTÁS", "Biztos, hogy visszaállítja", "a gyári beállításokat?");
      break;

    case MENU_TIME_LIST: {
      const char* tfNames[3] = { "O:P:MP", "O:P", "AM/PM" };
      const char* dfNames[5] = { "É/H/N", "N/H/É", "H/N/É", "H/N", "N/H" };
      const char* posNames[3] = { "KI", "BALRA", "KÖZÉPEN" };
      rows[0] = { "Év", String(yr), UI_CYAN };
      rows[1] = { "Hónap", String(month), UI_CYAN };
      rows[2] = { "Nap", String(day), UI_CYAN };
      rows[3] = { "Óra", String(hr), UI_CYAN };
      rows[4] = { "Perc", String(min_t), UI_CYAN };
      rows[5] = { "Másodperc", String(sec), UI_CYAN };
      rows[6] = { "Idő formátum", tfNames[timeFormat], UI_CYAN };
      rows[7] = { "Dátum formátum", dfNames[dateFormat], UI_CYAN };
      rows[8] = { "Óra megjelenítés", posNames[clockPos], (uint16_t)(clockPos ? UI_GREEN : UI_WHITE) };
      rows[9] = { "Dátum megjelenítés", posNames[datePos], (uint16_t)(datePos ? UI_GREEN : UI_WHITE) };
      rows[10] = { "Nap neve megjelenítés", posNames[dayNamePos], (uint16_t)(dayNamePos ? UI_GREEN : UI_WHITE) };
      rows[11] = { "Vissza", "", UI_WHITE };
      drawValueList("DÁTUM/IDŐ", rows, 12, menuCursor);
      break;
    }

    case MENU_TIME_EDIT: {
      const char* names[6] = { "ÉV", "HÓNAP", "NAP", "ÓRA", "PERC", "MÁSODPERC" };
      int vals[6] = { yr, month, day, hr, min_t, sec };
      drawEditScreen(names[menuEditField], String(vals[menuEditField]));
      break;
    }

    case MENU_TIMEFORMAT_SEL: {
      const char* items[4] = { "O:P:MP", "O:P", "AM/PM (angol)", "Vissza" };
      drawMenuList("IDŐ FORMÁTUM", items, 4, menuCursor);
      break;
    }

    case MENU_DATEFORMAT_SEL: {
      const char* items[6] = { "Év/Hónap/Nap", "Nap/Hónap/Év", "Hónap/Nap/Év", "Hónap/Nap", "Nap/Hónap", "Vissza" };
      drawMenuList("DÁTUM FORMÁTUM", items, 6, menuCursor);
      break;
    }

    case MENU_RGB_LIST: {
      bool on = digitalRead(RGB_ON);
      rows[0] = { "Állapot", on ? "BE" : "KI", (uint16_t)(on ? UI_GREEN : UI_RED) };
      rows[1] = { "Piros", String(pwmR), UI_CYAN };
      rows[2] = { "Zöld", String(pwmG), UI_CYAN };
      rows[3] = { "Kék", String(pwmB), UI_CYAN };
      rows[4] = { "Vissza", "", UI_WHITE };
      drawValueList("RGB SZÍN", rows, 5, menuCursor);
      break;
    }
    case MENU_RGB_EDIT: {
      const char* names[3] = {"PIROS", "ZÖLD", "KÉK"};
      int val = menuEditField == 0 ? pwmR : (menuEditField == 1 ? pwmG : pwmB);
      drawEditScreen(names[menuEditField], String(val));
      break;
    }

    case MENU_FAN_SUB: {
      int curPwm = (menuFanTarget == 1) ? pwmFan : pwmFan2;
      int curFreq = (menuFanTarget == 1) ? FAN1_PWM_FREQ : FAN2_PWM_FREQ;
      rows[0] = { "Auto/Kézi", curFanAuto() ? "AUTOMATA" : "KÉZI", (uint16_t)(curFanAuto() ? UI_GREEN : UI_ORANGE) };
      rows[1] = { "Állapot", curPwm > 0 ? "BE" : "KI", (uint16_t)(curPwm > 0 ? UI_GREEN : UI_RED) };
      rows[2] = { "Kézi %", String((int)round(curPwm / 2.55)) + "%", UI_CYAN };
      rows[3] = { "Frekvencia", String(curFreq) + " Hz", UI_CYAN };
      int n = 4;
      // mode3 ("1 kapcsoló + 1 hőmérő"): VAN homero ES kapcsolo is, ezert
      // mindket lehetoseg (Gorbe ES Logika) elerheto egyszerre.
      if (currentMode == 3 || !isSwitchMode()) {
        rows[n] = { "Görbe pontjai", curThermFan().enabled ? "BE" : "KI", (uint16_t)(curThermFan().enabled ? UI_GREEN : UI_RED) }; n++;
      }
      if (currentMode == 3 || isSwitchMode()) {
        rows[n] = { "Logika", curLogicFan().enabled ? "BE" : "KI", (uint16_t)(curLogicFan().enabled ? UI_GREEN : UI_RED) }; n++;
      }
      rows[n] = { "Vissza", "", UI_WHITE }; n++;
      drawValueList(menuFanTarget == 1 ? "VENTILÁTOR 1" : "VENTILÁTOR 2", rows, n, menuCursor);
      break;
    }
    case MENU_FAN_MANUAL_PCT:
      drawEditScreen("KÉZI FORDULATSZÁM", String((int)round((menuFanTarget == 1 ? pwmFan : pwmFan2) / 2.55)) + " %");
      break;
    case MENU_FAN_SENSOR_SEL: {
      int n = 0;
      if (currentMode == 2) { rows[n].label = "NTC1 (Hő)"; rows[n].value = ""; rows[n].valColor = UI_WHITE; n++;
                               rows[n].label = "Páratartalom"; rows[n].value = ""; rows[n].valColor = UI_WHITE; n++; }
      else if (currentMode == 1) { rows[n].label = "NTC1"; rows[n].value = ""; rows[n].valColor = UI_WHITE; n++; }
      else { rows[n].label = "NTC1"; rows[n].value = ""; rows[n].valColor = UI_WHITE; n++;
             rows[n].label = "NTC2"; rows[n].value = ""; rows[n].valColor = UI_WHITE; n++; }
      rows[n].label = "Vissza"; rows[n].value = ""; rows[n].valColor = UI_WHITE; n++;
      drawValueList("ÉRZÉKELŐ VÁLASZTÁS", rows, n, menuCursor);
      break;
    }
    case MENU_FAN_CURVE_LIST: {
      ThermFan &tf = curThermFan();
      int n = 0;
      rows[n].label = "Forrás"; rows[n].value = sensorLabel(tf.sensor); rows[n].valColor = UI_CYAN; n++;
      rows[n].label = "Állapot"; rows[n].value = tf.enabled ? "BE" : "KI"; rows[n].valColor = (uint16_t)(tf.enabled ? UI_GREEN : UI_RED); n++;
      rows[n].label = "Pontok száma"; rows[n].value = String(tf.count); rows[n].valColor = UI_CYAN; n++;
      for (int i = 0; i < tf.count && n < 11; i++) {
        rows[n].label = String(i + 1) + ".";
        rows[n].value = String(tf.temp[i], 0) + "C " + String(tf.pct[i]) + "% " + String(tf.freq[i]) + "Hz";
        rows[n].valColor = UI_CYAN;
        n++;
      }
      rows[n].label = "Vissza"; rows[n].value = ""; rows[n].valColor = UI_WHITE; n++;
      drawValueList("GÖRBE PONTJAI", rows, n, menuCursor);
      break;
    }
    case MENU_FAN_CURVE_POINT: {
      ThermFan &tf = curThermFan();
      rows[0] = { "Hőmérséklet", String(tf.temp[menuEditPoint], 0) + " C", UI_CYAN };
      rows[1] = { "PWM", String(tf.pct[menuEditPoint]) + " %", UI_CYAN };
      rows[2] = { "Frekvencia", String(tf.freq[menuEditPoint]) + " Hz", UI_CYAN };
      rows[3] = { "Vissza", "", UI_WHITE };
      drawValueList("PONT " + String(menuEditPoint + 1), rows, 4, menuCursor);
      break;
    }
    case MENU_FAN_CURVE_FIELD_EDIT: {
      ThermFan &tf = curThermFan();
      String title = menuEditField == 0 ? "HŐMÉRSÉKLET" : "PWM";
      String val = menuEditField == 0 ? (String(tf.temp[menuEditPoint], 0) + " C") : (String(tf.pct[menuEditPoint]) + " %");
      drawEditScreen(title, val);
      break;
    }
    case MENU_FREQ_PRESET_SEL: {
      const char* items[4] = { "Kézi érték", "30 Hz", "20000 Hz", "Vissza" };
      drawMenuList("FREKVENCIA", items, 4, menuCursor);
      break;
    }
    case MENU_FREQ_DIGIT_EDIT: {
      ui_freq(freqDigits, freqDigitPos);
      break;
    }
    case MENU_FAN_LOGIC_LIST: {
      SwitchLogicChannel &ch = curLogicFan();
      int n = 0;
      rows[n].label = "Állapot"; rows[n].value = ch.enabled ? "BE" : "KI"; rows[n].valColor = (uint16_t)(ch.enabled ? UI_GREEN : UI_RED); n++;
      if (currentMode == 4) {
        const char* labels[4] = {"K1N-K2N", "K1N-K2C", "K1C-K2N", "K1C-K2C"};
        for (int i = 0; i < 4; i++) { rows[n].label = labels[i]; rows[n].value = logicValLabel(ch.val[i], true); rows[n].valColor = UI_CYAN; n++; }
      } else {
        rows[n].label = "K1 Nyitott"; rows[n].value = logicValLabel(ch.val[0], true); rows[n].valColor = UI_CYAN; n++;
        rows[n].label = "K1 Zárt";    rows[n].value = logicValLabel(ch.val[2], true); rows[n].valColor = UI_CYAN; n++;
      }
      rows[n].label = "Vissza"; rows[n].value = ""; rows[n].valColor = UI_WHITE; n++;
      drawValueList("LOGIKA", rows, n, menuCursor);
      break;
    }
    case MENU_FAN_LOGIC_EDIT: {
      SwitchLogicChannel &ch = curLogicFan();
      int v = ch.val[menuLogicIdx];
      rows[0] = { "PWM", v < 0 ? "GÖRBE SZERINT" : (String(v) + " %"), UI_CYAN };
      rows[1] = { "Frekvencia", String(ch.freq[menuLogicIdx]) + " Hz", UI_CYAN };
      rows[2] = { "Vissza", "", UI_WHITE };
      drawValueList("LOGIKA ÉRTÉK", rows, 3, menuCursor);
      break;
    }
    case MENU_FAN_LOGIC_VAL_EDIT: {
      int v = curLogicFan().val[menuLogicIdx];
      String val = v < 0 ? "GÖRBE SZERINT" : (String(v) + " %");
      String hint = (currentMode == 3) ? "(-1=görbe, 0-100=fix %)" : "(0-100=fix %)";
      drawEditScreen("PWM ÉRTÉK", val, hint);
      break;
    }

    case MENU_RELAY_SUB: {
      rows[0] = { "Auto/Kézi", curRelayAuto() ? "AUTOMATA" : "KÉZI", (uint16_t)(curRelayAuto() ? UI_GREEN : UI_ORANGE) };
      int pin = (menuRelayTarget == 1) ? RELAY1_PIN : RELAY2_PIN;
      rows[1] = { "Kézi Be/Ki", digitalRead(pin) ? "BE" : "KI", (uint16_t)(digitalRead(pin) ? UI_GREEN : UI_RED) };
      int n = 2;
      // mode3 ("1 kapcsoló + 1 hőmérő"): VAN homero ES kapcsolo is, ezert
      // mindket lehetoseg (Termosztat ES Logika) elerheto egyszerre.
      if (currentMode == 3 || !isSwitchMode()) {
        rows[n] = { "Termosztát", curThermRelay().enabled ? "BE" : "KI", (uint16_t)(curThermRelay().enabled ? UI_GREEN : UI_RED) }; n++;
      }
      if (currentMode == 3 || isSwitchMode()) {
        rows[n] = { "Logika", curLogicRelay().enabled ? "BE" : "KI", (uint16_t)(curLogicRelay().enabled ? UI_GREEN : UI_RED) }; n++;
      }
      RelayTimer &curRt = (menuRelayTarget == 1) ? relayTimerR1 : relayTimerR2;
      rows[n] = { "Kapuzás", curRt.enabled ? "BE" : "KI", (uint16_t)(curRt.enabled ? UI_GREEN : UI_RED) }; n++;
      rows[n] = { "Vissza", "", UI_WHITE }; n++;
      drawValueList(menuRelayTarget == 1 ? "RELÉ 1" : "RELÉ 2", rows, n, menuCursor);
      break;
    }
    case MENU_RELAY_HOLD_LIST: {
      RelayTimer &rt = (menuRelayTarget == 1) ? relayTimerR1 : relayTimerR2;
      const char* lockNames[3] = { "NINCS", "IDŐ ALAPÚ", "JELVÁLTÁSIG" };
      const char* stateNames[3] = { "BE", "KI", "MIND" };
      rows[0] = { "Be/Ki", rt.enabled ? "BEKAPCSOLVA" : "KIKAPCSOLVA", (uint16_t)(rt.enabled ? UI_GREEN : UI_RED) };
      rows[1] = { "Mód", rt.timerMode == 0 ? "FIX IDŐ" : "MAX IDŐ", UI_CYAN };
      rows[2] = { "Időtartam", String(rt.onDurationSec) + " mp", UI_CYAN };
      rows[3] = { "Zárolás", lockNames[rt.lockMode], UI_CYAN };
      int n = 4;
      if (rt.lockMode == 1) { rows[n].label = "Zárolás ideje"; rows[n].value = String(rt.cooldownSec) + " mp"; rows[n].valColor = UI_CYAN; n++; }
      rows[n].label = "Állapot"; rows[n].value = stateNames[rt.stateFilter]; rows[n].valColor = UI_CYAN; n++;
      rows[n] = { "Vissza", "", UI_WHITE }; n++;
      drawValueList("KAPUZÁS", rows, n, menuCursor);
      break;
    }
    case MENU_RELAY_LOCKMODE_SEL: {
      const char* items[4] = { "Nincs zárolás", "Idő alapú", "Jelváltásig", "Vissza" };
      drawMenuList("ZÁROLÁS MÓDJA", items, 4, menuCursor);
      break;
    }
    case MENU_RELAY_STATE_SEL: {
      const char* items[4] = { "BE (csak bekapcsolásra)", "KI (csak kikapcsolásra)", "MIND (mindkét irányra)", "Vissza" };
      drawMenuList("ÁLLAPOT VÁLASZTÁS", items, 4, menuCursor);
      break;
    }
    case MENU_RELAY_HOLD_EDIT: {
      RelayTimer &rt = (menuRelayTarget == 1) ? relayTimerR1 : relayTimerR2;
      String title = menuEditField == 0 ? "IDŐTARTAM" : "ZÁROLÁS IDEJE";
      int val = menuEditField == 0 ? rt.onDurationSec : rt.cooldownSec;
      String hint = menuEditField == 0
        ? (rt.timerMode == 0 ? "(fix ideig tart)" : "(max ennyi ideig tarthat)")
        : "(a szakasz vége után)";
      drawEditScreen(title, String(val) + " másodperc", hint);
      break;
    }
    case MENU_RELAY_SENSOR_SEL: {
      int n = 0;
      if (currentMode == 2) { rows[n].label = "NTC1 (Hő)"; rows[n].value = ""; rows[n].valColor = UI_WHITE; n++;
                               rows[n].label = "Páratartalom"; rows[n].value = ""; rows[n].valColor = UI_WHITE; n++; }
      else if (currentMode == 1) { rows[n].label = "NTC1"; rows[n].value = ""; rows[n].valColor = UI_WHITE; n++; }
      else { rows[n].label = "NTC1"; rows[n].value = ""; rows[n].valColor = UI_WHITE; n++;
             rows[n].label = "NTC2"; rows[n].value = ""; rows[n].valColor = UI_WHITE; n++; }
      rows[n].label = "Vissza"; rows[n].value = ""; rows[n].valColor = UI_WHITE; n++;
      drawValueList("ÉRZÉKELŐ VÁLASZTÁS", rows, n, menuCursor);
      break;
    }
    case MENU_RELAY_THERM_LIST: {
      ThermRelay &tr = curThermRelay();
      bool isHum = (tr.sensor == 3);
      rows[0] = { "Érzékelő", sensorLabel(tr.sensor), UI_CYAN };
      rows[1] = { "Állapot", tr.enabled ? "BE" : "KI", (uint16_t)(tr.enabled ? UI_GREEN : UI_RED) };
      rows[2] = { "Bekapcsol", String(tr.ton, isHum ? 0 : 1) + (isHum ? "%" : "C"), UI_CYAN };
      rows[3] = { "Kikapcsol", String(tr.toff, isHum ? 0 : 1) + (isHum ? "%" : "C"), UI_CYAN };
      rows[4] = { "Vissza", "", UI_WHITE };
      drawValueList("RELÉ TERMOSZTÁT", rows, 5, menuCursor);
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
      rows[n].label = "Állapot"; rows[n].value = ch.enabled ? "BE" : "KI"; rows[n].valColor = (uint16_t)(ch.enabled ? UI_GREEN : UI_RED); n++;
      if (currentMode == 4) {
        const char* labels[4] = {"K1N-K2N", "K1N-K2C", "K1C-K2N", "K1C-K2C"};
        for (int i = 0; i < 4; i++) { rows[n].label = labels[i]; rows[n].value = logicValLabel(ch.val[i], false); rows[n].valColor = (uint16_t)(ch.val[i] > 0 ? UI_GREEN : UI_CYAN); n++; }
      } else {
        rows[n].label = "K1 Nyitott"; rows[n].value = logicValLabel(ch.val[0], false); rows[n].valColor = (uint16_t)(ch.val[0] < 0 ? UI_YELLOW : (ch.val[0] > 0 ? UI_GREEN : UI_CYAN)); n++;
        rows[n].label = "K1 Zárt";    rows[n].value = logicValLabel(ch.val[2], false); rows[n].valColor = (uint16_t)(ch.val[2] < 0 ? UI_YELLOW : (ch.val[2] > 0 ? UI_GREEN : UI_CYAN)); n++;
      }
      rows[n].label = "Vissza"; rows[n].value = ""; rows[n].valColor = UI_WHITE; n++;
      drawValueList("LOGIKA", rows, n, menuCursor);
      break;
    }
    case MENU_RELAY_LOGIC_EDIT: {
      int v = curLogicRelay().val[menuLogicIdx];
      String val = v < 0 ? "TERMOSZTÁTOT KÖVETI" : (v != 0 ? "BEKAPCSOLVA" : "KIKAPCSOLVA");
      String hint = (currentMode == 3) ? "(Fel/Le: KI/BE/Termosztát)" : "(Fel/Le: KI/BE)";
      drawEditScreen("LOGIKA ÉRTÉK", val, hint);
      break;
    }

    case MENU_WIFI_SCANNING: {
      ui_wifi_scanning();
      break;
    }

    case MENU_WIFI_SCAN_LIST: {
      if (wifiMenuScanCount == 0) {
        rows[0] = { "Nincs található hálózat", "", UI_RED };
        rows[1] = { "Vissza", "", UI_WHITE };
        drawValueList("WIFI HÁLÓZATOK", rows, 2, menuCursor);
      } else {
        int n = 0;
        for (int i = 0; i < wifiMenuScanCount && n < 11; i++) {
          rows[n].label = wifiMenuScanSsids[i]; rows[n].value = ""; rows[n].valColor = UI_WHITE; n++;
        }
        rows[n] = { "Vissza", "", UI_WHITE }; n++;
        drawValueList("WIFI HÁLÓZATOK", rows, n, menuCursor);
      }
      break;
    }

    case MENU_WIFI_PASSWORD_EDIT: {
      // a begépelt jelszó karakterei direktben látszanak (nincs elrejtve,
      // mert ez a kijelző csak helyben, fizikailag hozzáférhető eszközön
      // látszik); a kiválasztás alatt álló karakter külön "chip"-ben van.
      String typed = "";
      for (int i = 0; i < wifiPwLen; i++) typed += wifiPwBuf[i];
      char cur = WIFI_CHARSET[wifiPwCharIdx];
      String chip = (cur == '<') ? String("DEL") : String(cur == ' ' ? '_' : cur);
      ui_wifi_password(wifiMenuSelectedSsid, typed.c_str(), chip.c_str());
      break;
    }

    case MENU_WIFI_CONNECTING: {
      if (wifiConnectPending) ui_wifi_result(0, "");
      else if (WiFi.status() == WL_CONNECTED) ui_wifi_result(1, WiFi.localIP().toString());
      else ui_wifi_result(2, "");
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
          case 0: menuScreen = MENU_SETTINGS_SUB; menuCursor = 0; break;
          case 1: menuScreen = MENU_RGB_LIST; menuCursor = 0; break;
          case 2: menuFanTarget = 1; menuScreen = MENU_FAN_SUB; menuCursor = 0; break;
          case 3: menuFanTarget = 2; menuScreen = MENU_FAN_SUB; menuCursor = 0; break;
          case 4: menuRelayTarget = 1; menuScreen = MENU_RELAY_SUB; menuCursor = 0; break;
          case 5: menuRelayTarget = 2; menuScreen = MENU_RELAY_SUB; menuCursor = 0; break;
        }
      }
      break;

    case MENU_SETTINGS_SUB:
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % 8;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + 8) % 8;
      else if (ev == BTN_BACK) { menuScreen = MENU_MAIN; menuCursor = 0; }
      else if (ev == BTN_SELECT) {
        switch (menuCursor) {
          case 0: menuScreen = MENU_MODE_LIST; menuCursor = currentMode; break;
          case 1: menuScreen = MENU_TIME_LIST; menuCursor = 0; break;
          case 2: menuScreen = MENU_COMP_LIST; menuCursor = 0; break;
          case 3: menuScreen = MENU_DISPLAY_SUB; menuCursor = 0; break;
          case 4:
            // WiFi hálózatok keresése - a tényleges (blokkoló) keresést a
            // loop() indítja, ne itt fusson (BTN_SELECT feldolgozás közben).
            menuScreen = MENU_WIFI_SCANNING;
            wifiMenuScanRequested = true;
            break;
          case 5: menuScreen = MENU_MQTT_LIST; menuCursor = 0; break;
          case 6: menuScreen = MENU_FACTORY_CONFIRM; break;
          case 7: menuScreen = MENU_MAIN; menuCursor = 0; break;
        }
      }
      break;

    case MENU_DISPLAY_SUB:
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % 3;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + 3) % 3;
      else if (ev == BTN_BACK) { menuScreen = MENU_SETTINGS_SUB; menuCursor = 3; }
      else if (ev == BTN_SELECT) {
        if (menuCursor == 0) menuScreen = MENU_BRIGHTNESS_EDIT;
        else if (menuCursor == 1) { menuScreen = MENU_SLEEP_SEL; menuCursor = sleepTimeoutIdx; }
        else { menuScreen = MENU_SETTINGS_SUB; menuCursor = 3; }
      }
      break;

    case MENU_BRIGHTNESS_EDIT:
      if (ev == BTN_UP) { displayBrightness = (uint8_t)constrain((int)displayBrightness + 5, 1, 100); applyBacklight(); markDirty(); }
      else if (ev == BTN_DOWN) { displayBrightness = (uint8_t)constrain((int)displayBrightness - 5, 1, 100); applyBacklight(); markDirty(); }
      else if (ev == BTN_SELECT || ev == BTN_BACK) { menuScreen = MENU_DISPLAY_SUB; menuCursor = 0; }
      break;

    case MENU_SLEEP_SEL:
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % (SLEEP_OPTIONS_COUNT + 1);
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + SLEEP_OPTIONS_COUNT + 1) % (SLEEP_OPTIONS_COUNT + 1);
      else if (ev == BTN_BACK) { menuScreen = MENU_DISPLAY_SUB; menuCursor = 1; }
      else if (ev == BTN_SELECT) {
        if (menuCursor < SLEEP_OPTIONS_COUNT) { sleepTimeoutIdx = (uint8_t)menuCursor; markDirty(); lastUserActivity = millis(); }
        menuScreen = MENU_DISPLAY_SUB; menuCursor = 1;
      }
      break;

    case MENU_MODE_LIST:
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % 6;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + 6) % 6;
      else if (ev == BTN_BACK) { menuScreen = MENU_SETTINGS_SUB; menuCursor = 0; }
      else if (ev == BTN_SELECT) {
        if (menuCursor < 5 && menuCursor != currentMode) {
          currentMode = menuCursor;
          idleNeedsFullClear = true; // mód-váltás -> idle képernyő teljes törlése (nincs ottmaradt korábbi elem)
          markDirty();
        }
        menuScreen = MENU_SETTINGS_SUB; menuCursor = 0;
      }
      break;

    case MENU_COMP_LIST: {
      // Csak azokat a sorokat mutatjuk, amik az aktuális módban valós hőmérőt
      // jelentenek (ugyanaz a logika, mint a rajzoló ágban).
      int n = 0;
      if (currentMode == 0) n = 2;
      else if (currentMode == 1 || currentMode == 3) n = 1;
      int count = (n == 0 ? 1 : n) + 1; // (+"Nincs hőmérő" hely VAGY a sorok) + Vissza
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % count;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + count) % count;
      else if (ev == BTN_BACK) { menuScreen = MENU_SETTINGS_SUB; menuCursor = 2; }
      else if (ev == BTN_SELECT) {
        if (n > 0 && menuCursor < n) { menuSensTarget = menuCursor; menuScreen = MENU_SENS_LIST; menuCursor = 0; }
        else { menuScreen = MENU_SETTINGS_SUB; menuCursor = 2; }
      }
      break;
    }

    case MENU_SENS_LIST: {
      SensField f[9];
      int n = sensBuildFields(menuSensTarget, f);
      if (menuCursor >= n) menuCursor = 0;
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % n;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + n) % n;
      else if (ev == BTN_BACK) { menuScreen = MENU_COMP_LIST; menuCursor = menuSensTarget; }
      else if (ev == BTN_SELECT) {
        SensField sel = f[menuCursor];
        if (sel == SF_TYPE) { menuScreen = MENU_SENS_TYPE_SEL; menuCursor = sensCfg[menuSensTarget].type < 4 ? sensCfg[menuSensTarget].type : 3; }
        else if (sel == SF_BACK) { menuScreen = MENU_COMP_LIST; menuCursor = menuSensTarget; }
        else if (sel != SF_LIVE) { menuSensField = sel; menuScreen = MENU_SENS_EDIT; }
      }
      break;
    }

    case MENU_SENS_EDIT: {
      if (ev == BTN_UP) { sensEditStep(menuSensTarget, (SensField)menuSensField, +1); markDirty(); }
      else if (ev == BTN_DOWN) { sensEditStep(menuSensTarget, (SensField)menuSensField, -1); markDirty(); }
      else if (ev == BTN_SELECT || ev == BTN_BACK) {
        markDirty();
        SensField f[9];
        int n = sensBuildFields(menuSensTarget, f);
        int pos = 0;
        for (int i = 0; i < n; i++) if (f[i] == (SensField)menuSensField) pos = i;
        menuScreen = MENU_SENS_LIST; menuCursor = pos;
      }
      break;
    }

    case MENU_SENS_TYPE_SEL: {
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % 5;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + 5) % 5;
      else if (ev == BTN_BACK) { menuScreen = MENU_SENS_LIST; menuCursor = 0; }
      else if (ev == BTN_SELECT) {
        if (menuCursor < 4) {
          if (menuCursor != 3) applySensorPreset(sensCfg[menuSensTarget], (uint8_t)menuCursor); // az "Egyedi" nem ír felül semmit
          sensCfg[menuSensTarget].type = (uint8_t)menuCursor;
          markDirty();
        }
        menuScreen = MENU_SENS_LIST; menuCursor = 0;
      }
      break;
    }

    case MENU_MQTT_LIST:
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % 5;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + 5) % 5;
      else if (ev == BTN_BACK) { menuScreen = MENU_SETTINGS_SUB; menuCursor = 5; }
      else if (ev == BTN_SELECT) {
        if (menuCursor == 0) { mqttCfg.enabled = !mqttCfg.enabled; mqttReconf = true; markDirty(); } // a bróker adatai az appból állíthatók
        else if (menuCursor == 4) { menuScreen = MENU_SETTINGS_SUB; menuCursor = 5; }
      }
      break;

    case MENU_FACTORY_CONFIRM:
      if (ev == BTN_SELECT) { factoryReset(); /* nem tér vissza - ujraindul */ }
      else if (ev == BTN_BACK) { menuScreen = MENU_SETTINGS_SUB; menuCursor = 6; }
      break;

    case MENU_TIME_LIST:
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % 12;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + 12) % 12;
      else if (ev == BTN_BACK) { menuScreen = MENU_SETTINGS_SUB; menuCursor = 1; }
      else if (ev == BTN_SELECT) {
        if (menuCursor <= 5) { menuEditField = menuCursor; menuScreen = MENU_TIME_EDIT; }
        else if (menuCursor == 6) { menuScreen = MENU_TIMEFORMAT_SEL; menuCursor = timeFormat; }
        else if (menuCursor == 7) { menuScreen = MENU_DATEFORMAT_SEL; menuCursor = dateFormat; }
        else if (menuCursor == 8) { clockPos = (clockPos + 1) % 3; markDirty(); idleNeedsFullClear = true; }
        else if (menuCursor == 9) { datePos = (datePos + 1) % 3; markDirty(); idleNeedsFullClear = true; }
        else if (menuCursor == 10) { dayNamePos = (dayNamePos + 1) % 3; markDirty(); idleNeedsFullClear = true; }
        else { menuScreen = MENU_SETTINGS_SUB; menuCursor = 1; }
      }
      break;

    case MENU_TIME_EDIT: {
      int dir = (ev == BTN_UP) ? 1 : (ev == BTN_DOWN ? -1 : 0);
      if (dir != 0) {
        switch (menuEditField) {
          case 0: yr = (uint16_t)constrain((int)yr + dir, 2000, 2099); if (day > daysInMonth(month, yr)) day = daysInMonth(month, yr); break;
          case 1: month = (uint8_t)(((int)month - 1 + dir + 12) % 12 + 1); if (day > daysInMonth(month, yr)) day = daysInMonth(month, yr); break;
          case 2: day = (uint8_t)(((int)day - 1 + dir + daysInMonth(month, yr)) % daysInMonth(month, yr) + 1); break;
          case 3: hr = (hr + dir + 24) % 24; break;
          case 4: min_t = (min_t + dir + 60) % 60; break;
          case 5: sec = (sec + dir + 60) % 60; break;
        }
      } else if (ev == BTN_SELECT || ev == BTN_BACK) {
        markDirty();
        idleNeedsFullClear = true;
        menuScreen = MENU_TIME_LIST; menuCursor = menuEditField;
      }
      break;
    }

    case MENU_TIMEFORMAT_SEL:
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % 4;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + 4) % 4;
      else if (ev == BTN_BACK) { menuScreen = MENU_TIME_LIST; menuCursor = 6; }
      else if (ev == BTN_SELECT) {
        if (menuCursor < 3) { timeFormat = (uint8_t)menuCursor; markDirty(); }
        menuScreen = MENU_TIME_LIST; menuCursor = 6;
      }
      break;

    case MENU_DATEFORMAT_SEL:
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % 6;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + 6) % 6;
      else if (ev == BTN_BACK) { menuScreen = MENU_TIME_LIST; menuCursor = 7; }
      else if (ev == BTN_SELECT) {
        if (menuCursor < 5) { dateFormat = (uint8_t)menuCursor; markDirty(); idleNeedsFullClear = true; }
        menuScreen = MENU_TIME_LIST; menuCursor = 7;
      }
      break;

    case MENU_RGB_LIST:
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % 5;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + 5) % 5;
      else if (ev == BTN_BACK) { menuScreen = MENU_MAIN; menuCursor = 1; }
      else if (ev == BTN_SELECT) {
        switch (menuCursor) {
          case 0: digitalWrite(RGB_ON, digitalRead(RGB_ON) ? LOW : HIGH); markDirty(); break;
          case 1: menuEditField = 0; menuScreen = MENU_RGB_EDIT; break;
          case 2: menuEditField = 1; menuScreen = MENU_RGB_EDIT; break;
          case 3: menuEditField = 2; menuScreen = MENU_RGB_EDIT; break;
          case 4: menuScreen = MENU_MAIN; menuCursor = 1; break;
        }
      }
      break;

    case MENU_RGB_EDIT: {
      int delta = (ev == BTN_UP) ? 1 : (ev == BTN_DOWN ? -1 : 0);
      if (delta != 0) {
        if (menuEditField == 0) pwmR = constrain(pwmR + delta, 0, 255);
        else if (menuEditField == 1) pwmG = constrain(pwmG + delta, 0, 255);
        else pwmB = constrain(pwmB + delta, 0, 255);
        updatePWMOutputs();
        markDirty();
      }
      else if (ev == BTN_SELECT || ev == BTN_BACK) { menuScreen = MENU_RGB_LIST; menuCursor = 1 + menuEditField; }
      break;
    }

    case MENU_FAN_SUB: {
      // mode3-ban ("1 kapcsoló + 1 hőmérő") MINDKET lehetoseg elerheto.
      bool hasCurve = (currentMode == 3 || !isSwitchMode());
      bool hasLogic = (currentMode == 3 || isSwitchMode());
      int idxCurve = hasCurve ? 4 : -1;
      int idxLogic = hasCurve ? (hasLogic ? 5 : -1) : (hasLogic ? 4 : -1);
      int idxBack = 4 + (hasCurve ? 1 : 0) + (hasLogic ? 1 : 0);
      int count = idxBack + 1;

      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % count;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + count) % count;
      else if (ev == BTN_BACK) { menuScreen = MENU_MAIN; menuCursor = (menuFanTarget == 1) ? 2 : 3; }
      else if (ev == BTN_SELECT) {
        if (menuCursor == 0) { curFanAuto() = !curFanAuto(); markDirty(); }
        else if (menuCursor == 1) {
          // Allapot: CSAK Kézi módban hat - Automatában a görbe/logika dönt.
          if (!curFanAuto()) {
            int &pwm = (menuFanTarget == 1) ? pwmFan : pwmFan2;
            int newPwm = (pwm > 0) ? 0 : 128; // ha KI volt, 50%-ra kapcsol BE; ha BE volt, KI-ra
            setPWM(menuFanTarget == 1 ? 3 : 4, newPwm);
            markDirty();
          }
        }
        else if (menuCursor == 2) { menuScreen = MENU_FAN_MANUAL_PCT; }
        else if (menuCursor == 3) {
          menuFreqTargetType = 0;
          freqValueToDigits(menuFanTarget == 1 ? FAN1_PWM_FREQ : FAN2_PWM_FREQ);
          menuScreen = MENU_FREQ_PRESET_SEL; menuCursor = 0;
        }
        else if (menuCursor == idxCurve) { menuScreen = MENU_FAN_CURVE_LIST; menuCursor = 0; }
        else if (menuCursor == idxLogic) { menuScreen = MENU_FAN_LOGIC_LIST; menuCursor = 0; }
        else { menuScreen = MENU_MAIN; menuCursor = (menuFanTarget == 1) ? 2 : 3; }
      }
      break;
    }

    case MENU_FAN_MANUAL_PCT: {
      // CSAK Kézi módban hat - Automatában a kijelzett % az automatika által
      // beállított tényleges értéket mutatja, de gombnyomásra nem változik.
      if ((ev == BTN_UP || ev == BTN_DOWN) && !curFanAuto()) {
        int &pwm = (menuFanTarget == 1) ? pwmFan : pwmFan2;
        int pct = (int)round(pwm / 2.55); // a jelenlegi % (kerekítve)
        pct = constrain(pct + (ev == BTN_UP ? 1 : -1), 0, 100); // 1%-os lépés
        int newPwm = (int)round(pct * 2.55);
        setPWM(menuFanTarget == 1 ? 3 : 4, newPwm); // real-time alkalmazás minden gombnyomásnál
        markDirty();
      }
      if (ev == BTN_SELECT || ev == BTN_BACK) { menuScreen = MENU_FAN_SUB; menuCursor = 2; }
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
        markDirty();
        menuScreen = MENU_FAN_CURVE_LIST; menuCursor = 0;
      }
      break;
    }

    case MENU_FAN_CURVE_LIST: {
      ThermFan &tf = curThermFan();
      int n = tf.count + 4; // Forras + Allapot + Pontok szama + N pont + Vissza
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % n;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + n) % n;
      else if (ev == BTN_BACK) { menuScreen = MENU_FAN_SUB; menuCursor = 4; }
      else if (ev == BTN_SELECT) {
        if (menuCursor == 0) { menuScreen = MENU_FAN_SENSOR_SEL; menuCursor = 0; }
        else if (menuCursor == 1) { tf.enabled = !tf.enabled; markDirty(); }
        else if (menuCursor == 2) { tf.count = (tf.count % FAN_CURVE_MAX_POINTS) + 1; markDirty(); }
        else if (menuCursor == n - 1) { menuScreen = MENU_FAN_SUB; menuCursor = 4; }
        else { menuEditPoint = menuCursor - 3; menuScreen = MENU_FAN_CURVE_POINT; menuCursor = 0; }
      }
      break;
    }

    case MENU_FAN_CURVE_POINT:
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % 4;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + 4) % 4;
      else if (ev == BTN_BACK) { menuScreen = MENU_FAN_CURVE_LIST; menuCursor = menuEditPoint + 3; }
      else if (ev == BTN_SELECT) {
        if (menuCursor == 0) { menuEditField = 0; menuScreen = MENU_FAN_CURVE_FIELD_EDIT; }
        else if (menuCursor == 1) { menuEditField = 1; menuScreen = MENU_FAN_CURVE_FIELD_EDIT; }
        else if (menuCursor == 2) {
          menuFreqTargetType = 1;
          freqValueToDigits(curThermFan().freq[menuEditPoint]);
          menuScreen = MENU_FREQ_PRESET_SEL; menuCursor = 0;
        }
        else { menuScreen = MENU_FAN_CURVE_LIST; menuCursor = menuEditPoint + 3; }
      }
      break;

    case MENU_FAN_CURVE_FIELD_EDIT: {
      ThermFan &tf = curThermFan();
      int dir = (ev == BTN_UP) ? 1 : (ev == BTN_DOWN ? -1 : 0);
      if (dir != 0) {
        if (menuEditField == 0) tf.temp[menuEditPoint] = constrain(tf.temp[menuEditPoint] + dir, -40, 120);
        else tf.pct[menuEditPoint] = (uint8_t)constrain((int)tf.pct[menuEditPoint] + dir, 0, 100); // 1%-os lepes
        markDirty();
      } else if (ev == BTN_SELECT || ev == BTN_BACK) {
        if (menuEditField == 0) sortFanCurve(curThermFan(), curThermFan().count);
        markDirty();
        menuScreen = MENU_FAN_CURVE_POINT; menuCursor = menuEditField;
      }
      break;
    }

    case MENU_FREQ_PRESET_SEL:
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % 4;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + 4) % 4;
      else if (ev == BTN_BACK) { exitFreqEdit(); }
      else if (ev == BTN_SELECT) {
        if (menuCursor == 0) { freqDigitPos = 0; menuScreen = MENU_FREQ_DIGIT_EDIT; }
        else if (menuCursor == 1) { applyFrequencyValue(30); exitFreqEdit(); }
        else if (menuCursor == 2) { applyFrequencyValue(20000); exitFreqEdit(); }
        else { exitFreqEdit(); }
      }
      break;

    case MENU_FREQ_DIGIT_EDIT:
      if (ev == BTN_LONG_UP) {
        freqDigitPos = (freqDigitPos + 1) % 5;
      } else if (ev == BTN_UP) {
        freqDigits[freqDigitPos] = (freqDigits[freqDigitPos] + 1) % 10;
        applyFrequencyValue(freqDigitsToValue());
      } else if (ev == BTN_DOWN) {
        freqDigits[freqDigitPos] = (freqDigits[freqDigitPos] + 9) % 10;
        applyFrequencyValue(freqDigitsToValue());
      } else if (ev == BTN_SELECT || ev == BTN_BACK) {
        exitFreqEdit();
      }
      break;

    case MENU_FAN_LOGIC_LIST: {
      int n = (currentMode == 4) ? 6 : 4;
      int backCursor = (currentMode == 3) ? 5 : 4; // mode3: Gorbe(4)+Logika(5), mode4: csak Logika(4)
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % n;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + n) % n;
      else if (ev == BTN_BACK) { menuScreen = MENU_FAN_SUB; menuCursor = backCursor; }
      else if (ev == BTN_SELECT) {
        if (menuCursor == 0) { curLogicFan().enabled = !curLogicFan().enabled; markDirty(); }
        else if (menuCursor == n - 1) { menuScreen = MENU_FAN_SUB; menuCursor = backCursor; }
        else {
          int stateIdx = menuCursor - 1;
          menuLogicIdx = (currentMode == 4) ? stateIdx : (stateIdx == 0 ? 0 : 2);
          curLogicFan().axis2Mode = (currentMode == 4) ? 0 : 2;
          menuScreen = MENU_FAN_LOGIC_EDIT; menuCursor = 0;
        }
      }
      break;
    }

    case MENU_FAN_LOGIC_EDIT:
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % 3;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + 3) % 3;
      else if (ev == BTN_BACK) {
        menuScreen = MENU_FAN_LOGIC_LIST;
        menuCursor = ((currentMode == 4) ? menuLogicIdx : (menuLogicIdx == 0 ? 0 : 1)) + 1;
      }
      else if (ev == BTN_SELECT) {
        if (menuCursor == 0) { menuScreen = MENU_FAN_LOGIC_VAL_EDIT; }
        else if (menuCursor == 1) {
          menuFreqTargetType = 2;
          freqValueToDigits(curLogicFan().freq[menuLogicIdx]);
          menuScreen = MENU_FREQ_PRESET_SEL; menuCursor = 0;
        }
        else {
          menuScreen = MENU_FAN_LOGIC_LIST;
          menuCursor = ((currentMode == 4) ? menuLogicIdx : (menuLogicIdx == 0 ? 0 : 1)) + 1;
        }
      }
      break;

    case MENU_FAN_LOGIC_VAL_EDIT: {
      int &v = curLogicFan().val[menuLogicIdx];
      // mode4-ben NINCS görbe (nincs hőmérő) - csak 0-100 közötti fix % állítható.
      // mode3-ban a -1 ("görbe szerint") is elérhető, mert ott van hőmérő/görbe.
      int minVal = (currentMode == 4) ? 0 : -1;
      if (ev == BTN_UP) v = (v >= 100) ? 100 : v + 1;
      else if (ev == BTN_DOWN) v = (v <= minVal) ? minVal : v - 1;
      else if (ev == BTN_SELECT || ev == BTN_BACK) {
        markDirty();
        menuScreen = MENU_FAN_LOGIC_EDIT; menuCursor = 0;
      }
      break;
    }

    case MENU_RELAY_SUB: {
      bool hasTherm = (currentMode == 3 || !isSwitchMode());
      bool hasLogic = (currentMode == 3 || isSwitchMode());
      int idxTherm = hasTherm ? 2 : -1;
      int idxLogic = hasTherm ? (hasLogic ? 3 : -1) : (hasLogic ? 2 : -1);
      int idxHold = 2 + (hasTherm ? 1 : 0) + (hasLogic ? 1 : 0);
      int count = idxHold + 2; // + Kapuzas + Vissza

      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % count;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + count) % count;
      else if (ev == BTN_BACK) { menuScreen = MENU_MAIN; menuCursor = (menuRelayTarget == 1) ? 4 : 5; }
      else if (ev == BTN_SELECT) {
        if (menuCursor == 0) {
          bool wasAuto = curRelayAuto();
          curRelayAuto() = !wasAuto;
          if (!wasAuto) { // most kapcsolt Auto-ra -> friss RelayTimer ciklus
            RelayTimer &rt = (menuRelayTarget == 1) ? relayTimerR1 : relayTimerR2;
            rt.timedActive = false; rt.cooldownActive = false; rt.waitingForReversal = false;
          }
          markDirty();
        }
        else if (menuCursor == 1) {
          // Kezi Be/Ki: CSAK Kézi módban hat - Automatában a termosztát/logika dönt.
          if (!curRelayAuto()) {
            int pin = (menuRelayTarget == 1) ? RELAY1_PIN : RELAY2_PIN;
            digitalWrite(pin, digitalRead(pin) ? LOW : HIGH);
            markDirty();
          }
        }
        else if (menuCursor == idxTherm) { menuScreen = MENU_RELAY_THERM_LIST; menuCursor = 0; }
        else if (menuCursor == idxLogic) { menuScreen = MENU_RELAY_LOGIC_LIST; menuCursor = 0; }
        else if (menuCursor == idxHold) { menuScreen = MENU_RELAY_HOLD_LIST; menuCursor = 0; }
        else { menuScreen = MENU_MAIN; menuCursor = (menuRelayTarget == 1) ? 4 : 5; }
      }
      break;
    }

    case MENU_RELAY_HOLD_LIST: {
      RelayTimer &rt = (menuRelayTarget == 1) ? relayTimerR1 : relayTimerR2;
      // Sor-indexek: 0=Be/Ki,1=Mod,2=Idotartam,3=Zarolas,[4=Zarolas ideje ha lockMode==1],
      // utana Allapot, majd Vissza.
      int idxHoldTime = (rt.lockMode == 1) ? 4 : -1;
      int idxState = (rt.lockMode == 1) ? 5 : 4;
      int idxBack  = idxState + 1;
      int count = idxBack + 1;
      int relaySubBackCursor = (currentMode == 3) ? 4 : 3; // mode3-ban Termosztat+Logika miatt eggyel arrébb van a Kapuzas

      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % count;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + count) % count;
      else if (ev == BTN_BACK) { menuScreen = MENU_RELAY_SUB; menuCursor = relaySubBackCursor; }
      else if (ev == BTN_SELECT) {
        if (menuCursor == 0) {
          bool wasEnabled = rt.enabled;
          rt.enabled = !rt.enabled;
          if (rt.enabled && !wasEnabled) { rt.timedActive = false; rt.cooldownActive = false; rt.waitingForReversal = false; }
          markDirty();
        } else if (menuCursor == 1) {
          rt.timerMode = (rt.timerMode == 0) ? 1 : 0;
          rt.timedActive = false; rt.cooldownActive = false; rt.waitingForReversal = false; // váltáskor friss ciklus
          markDirty();
        } else if (menuCursor == 2) {
          menuEditField = 0; menuScreen = MENU_RELAY_HOLD_EDIT;
        } else if (menuCursor == 3) {
          menuScreen = MENU_RELAY_LOCKMODE_SEL; menuCursor = rt.lockMode;
        } else if (menuCursor == idxHoldTime) {
          menuEditField = 1; menuScreen = MENU_RELAY_HOLD_EDIT;
        } else if (menuCursor == idxState) {
          menuScreen = MENU_RELAY_STATE_SEL; menuCursor = rt.stateFilter;
        } else {
          menuScreen = MENU_RELAY_SUB; menuCursor = relaySubBackCursor;
        }
      }
      break;
    }

    case MENU_RELAY_LOCKMODE_SEL: {
      RelayTimer &rt = (menuRelayTarget == 1) ? relayTimerR1 : relayTimerR2;
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % 4;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + 4) % 4;
      else if (ev == BTN_BACK) { menuScreen = MENU_RELAY_HOLD_LIST; menuCursor = 3; }
      else if (ev == BTN_SELECT) {
        if (menuCursor < 3) {
          rt.lockMode = (uint8_t)menuCursor;
          rt.cooldownActive = false; rt.waitingForReversal = false;
          markDirty();
        }
        menuScreen = MENU_RELAY_HOLD_LIST; menuCursor = 3;
      }
      break;
    }

    case MENU_RELAY_STATE_SEL: {
      RelayTimer &rt = (menuRelayTarget == 1) ? relayTimerR1 : relayTimerR2;
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % 4;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + 4) % 4;
      else if (ev == BTN_BACK) { menuScreen = MENU_RELAY_HOLD_LIST; menuCursor = (rt.lockMode == 1) ? 5 : 4; }
      else if (ev == BTN_SELECT) {
        if (menuCursor < 3) {
          rt.stateFilter = (uint8_t)menuCursor;
          rt.timedActive = false; rt.cooldownActive = false; rt.waitingForReversal = false;
          markDirty();
        }
        menuScreen = MENU_RELAY_HOLD_LIST; menuCursor = (rt.lockMode == 1) ? 5 : 4;
      }
      break;
    }

    case MENU_RELAY_HOLD_EDIT: {
      RelayTimer &rt = (menuRelayTarget == 1) ? relayTimerR1 : relayTimerR2;
      uint16_t &val = (menuEditField == 0) ? rt.onDurationSec : rt.cooldownSec;
      int step = (menuEditField == 0) ? 5 : 30; // időtartam 5mp, zárolás 30mp lépésekben
      if (ev == BTN_UP) { val = (uint16_t)constrain((int)val + step, 1, 36000); markDirty(); }
      else if (ev == BTN_DOWN) { val = (uint16_t)constrain((int)val - step, 1, 36000); markDirty(); }
      else if (ev == BTN_SELECT || ev == BTN_BACK) { menuScreen = MENU_RELAY_HOLD_LIST; menuCursor = (menuEditField == 0) ? 2 : 4; }
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
        markDirty();
        menuScreen = MENU_RELAY_THERM_LIST; menuCursor = 0;
      }
      break;
    }

    case MENU_RELAY_THERM_LIST:
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % 5;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + 5) % 5;
      else if (ev == BTN_BACK) { menuScreen = MENU_RELAY_SUB; menuCursor = 2; }
      else if (ev == BTN_SELECT) {
        switch (menuCursor) {
          case 0: menuScreen = MENU_RELAY_SENSOR_SEL; menuCursor = 0; break;
          case 1: curThermRelay().enabled = !curThermRelay().enabled; markDirty(); break;
          case 2: menuEditField = 0; menuScreen = MENU_RELAY_THERM_EDIT; break;
          case 3: menuEditField = 1; menuScreen = MENU_RELAY_THERM_EDIT; break;
          case 4: menuScreen = MENU_RELAY_SUB; menuCursor = 2; break;
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
        markDirty();
        menuScreen = MENU_RELAY_THERM_LIST; menuCursor = 2 + menuEditField;
      }
      break;
    }

    case MENU_RELAY_LOGIC_LIST: {
      int n = (currentMode == 4) ? 6 : 4;
      int backCursor = (currentMode == 3) ? 3 : 2; // mode3: Termosztat(2)+Logika(3), mode4: csak Logika(2)
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % n;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + n) % n;
      else if (ev == BTN_BACK) { menuScreen = MENU_RELAY_SUB; menuCursor = backCursor; }
      else if (ev == BTN_SELECT) {
        if (menuCursor == 0) { curLogicRelay().enabled = !curLogicRelay().enabled; markDirty(); }
        else if (menuCursor == n - 1) { menuScreen = MENU_RELAY_SUB; menuCursor = backCursor; }
        else {
          int stateIdx = menuCursor - 1;
          menuLogicIdx = (currentMode == 4) ? stateIdx : (stateIdx == 0 ? 0 : 2);
          curLogicRelay().axis2Mode = (currentMode == 4) ? 0 : 2;
          menuScreen = MENU_RELAY_LOGIC_EDIT;
        }
      }
      break;
    }

    case MENU_RELAY_LOGIC_EDIT: {
      SwitchLogicChannel &ch = curLogicRelay();
      int &v = ch.val[menuLogicIdx];
      if (ev == BTN_UP || ev == BTN_DOWN) {
        if (currentMode == 3) {
          // 3 allapot korbe: KI(0) -> BE(1) -> Termosztatot koveti(-1) -> KI...
          if (ev == BTN_UP) v = (v == 0) ? 1 : (v == 1 ? -1 : 0);
          else v = (v == 0) ? -1 : (v == -1 ? 1 : 0);
        } else {
          // mode4: nincs termosztát, csak KI/BE
          v = ch.val[menuLogicIdx] ? 0 : 1;
        }
        markDirty();
      }
      else if (ev == BTN_SELECT || ev == BTN_BACK) {
        markDirty();
        menuScreen = MENU_RELAY_LOGIC_LIST;
        menuCursor = ((currentMode == 4) ? menuLogicIdx : (menuLogicIdx == 0 ? 0 : 1)) + 1;
      }
      break;
    }

    case MENU_WIFI_SCANNING:
      // Ez a képernyő átmeneti - a tényleges keresést a loop() indítja
      // (wifiMenuScanRequested), gombesemény itt nem releváns.
      break;

    case MENU_WIFI_SCAN_LIST: {
      int count = wifiMenuScanCount + 1; // + "Vissza"
      if (wifiMenuScanCount == 0) count = 2; // "Nincs található..." sor + "Vissza"
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % count;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + count) % count;
      else if (ev == BTN_BACK) { menuScreen = MENU_SETTINGS_SUB; menuCursor = 4; }
      else if (ev == BTN_SELECT) {
        if (wifiMenuScanCount == 0 || menuCursor == wifiMenuScanCount) {
          menuScreen = MENU_SETTINGS_SUB; menuCursor = 4;
        } else {
          wifiMenuSelectedSsid = wifiMenuScanSsids[menuCursor];
          wifiPwLen = 0; wifiPwCursor = 0; wifiPwCharIdx = 1; // 1 = szóköz (0='<' torles)
          wifiPwBuf[0] = '\0';
          menuScreen = MENU_WIFI_PASSWORD_EDIT;
          idleNeedsFullClear = true;
        }
      }
      break;
    }

    case MENU_WIFI_PASSWORD_EDIT: {
      if (ev == BTN_UP) wifiPwCharIdx = (wifiPwCharIdx + 1) % WIFI_CHARSET_LEN;
      else if (ev == BTN_DOWN) wifiPwCharIdx = (wifiPwCharIdx - 1 + WIFI_CHARSET_LEN) % WIFI_CHARSET_LEN;
      else if (ev == BTN_SELECT) {
        char c = WIFI_CHARSET[wifiPwCharIdx];
        if (c == '<') {
          // TORLES: az utolsó beírt karakter törlése (ha van)
          if (wifiPwLen > 0) {
            wifiPwLen--;
            wifiPwCursor = wifiPwLen;
            wifiPwBuf[wifiPwLen] = '\0';
          }
        } else if (wifiPwLen < WIFI_PW_MAX_LEN) {
          // Valódi karakter beírása a végéhez
          wifiPwBuf[wifiPwLen] = c;
          wifiPwLen++;
          wifiPwCursor = wifiPwLen;
          wifiPwBuf[wifiPwLen] = '\0';
        }
      }
      else if (ev == BTN_LONG_SELECT) {
        // HOSSZÚ, mindkét-gombos nyomás: jelszó véglegesítése, csatlakozás
        // indítása (üres jelszóval = mégse, vissza a hálózat-listára)
        if (wifiPwLen == 0) {
          menuScreen = MENU_WIFI_SCAN_LIST; menuCursor = 0;
        } else {
          wifiSsid = wifiMenuSelectedSsid;
          wifiPassword = String(wifiPwBuf);
          markDirty();
          wifiConnectRequested = true;
          menuScreen = MENU_WIFI_CONNECTING;
          idleNeedsFullClear = true;
        }
      }
      break;
    }

    case MENU_WIFI_CONNECTING:
      // Bármelyik gomb (rövid nyomás is) vissza a Beallitasok menübe -
      // a kapcsolat állapota a háttérben tovább él, csak a menüből lépünk ki.
      if (ev == BTN_UP || ev == BTN_DOWN || ev == BTN_SELECT || ev == BTN_BACK || ev == BTN_LONG_UP) {
        menuScreen = MENU_SETTINGS_SUB; menuCursor = 4;
        idleNeedsFullClear = true;
      }
      break;

    default: break;
  }
}
