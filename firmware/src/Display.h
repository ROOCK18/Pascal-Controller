// ============================================================
// Display.h - Az idle (fő) képernyő, LVGL-lel
//
// Elrendezés (240x135):
//   Fent:    dátum/idő (bal, közép) | WiFi, BT jel és mód-kód (jobb)
//   Bal sáv: R1/R2 relé állapot
//   Középen: kártya, benne mód-függően 1-2 "csempe"
//            (hőmérséklet / páratartalom / kapcsoló állapot)
//   Lent:    Ventilátor1 és 2 ív-mérő + százalék | RGB színminta
//
// Az objektumok egyszer épülnek fel (ui_idle_build), a displayInfo() csak
// azt frissíti, ami ténylegesen változott - az LVGL csak a változó
// területeket rajzolja újra, így nincs villódzás.
// ============================================================
#pragma once

// Rövid mód-kódok a jobb felső sarokba
const char* MODE_SHORT[] = { "2Ho", "1Ho", "Ho+Pa", "Ho+K1", "2K" };
// Nap nevek (dayOfWeekIdx() sorrendjében: 0=Vasarnap...6=Szombat)
const char* DOW_NAMES[] = { "Vas", "Het", "Ked", "Sze", "Csu", "Pen", "Szo" };

// Változott-e a kijelzett érték? A NaN (pl. szakadt/hiányzó szenzor) is érvényes
// állapot: NaN -> szám és szám -> NaN váltás is változásnak számít.
static bool tempChanged(float cur, float last) {
  if (isnan(cur) != isnan(last)) return true;
  if (isnan(cur)) return false;
  return fabs(cur - last) >= 0.1f;
}

// Hőmérséklet színe a palettából
uint32_t tempToPal(float t) {
  if (isnan(t)) return PAL_TEXT;
  if (t < 15) return PAL_BLUE;
  if (t < 28) return PAL_OK;
  if (t < 38) return PAL_WARN;
  return PAL_BAD;
}

// Sakamoto-algoritmus: a hét napja (0=Vasarnap...6=Szombat)
int dayOfWeekIdx(uint16_t y, uint8_t m, uint8_t d) {
  static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

// Dátum szövege a Beallitasokban kiválasztott formátum szerint
// 0=É/H/N, 1=N/H/É, 2=H/N/É, 3=H/N (év nélkül), 4=N/H (év nélkül)
String buildDateString() {
  char buf[14];
  if (dateFormat == 1) snprintf(buf, sizeof(buf), "%02d/%02d/%04d", day, month, yr);       // N/H/É
  else if (dateFormat == 2) snprintf(buf, sizeof(buf), "%02d/%02d/%04d", month, day, yr);  // H/N/É
  else if (dateFormat == 3) snprintf(buf, sizeof(buf), "%02d/%02d", month, day);           // H/N
  else if (dateFormat == 4) snprintf(buf, sizeof(buf), "%02d/%02d", day, month);           // N/H
  else snprintf(buf, sizeof(buf), "%04d/%02d/%02d", yr, month, day);                       // É/H/N
  return String(buf);
}

// Az óra szövege a Beallitasokban kiválasztott formátum szerint
String buildClockText() {
  char buf[16];
  if (timeFormat == 1) snprintf(buf, sizeof(buf), "%02d:%02d", hr, min_t);
  else if (timeFormat == 2) {
    int h12 = hr % 12; if (h12 == 0) h12 = 12;
    snprintf(buf, sizeof(buf), "%d:%02d%s", h12, min_t, hr < 12 ? "AM" : "PM");
  } else snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hr, min_t, sec);
  return String(buf);
}

// Egy adott pozícióhoz (1=Balra, 2=Kozepen) tartozó teljes szöveg - MINDIG
// rögzített sorrendben: Dátum, majd Óra, majd Nap neve (amelyik oda van
// állítva), pl. "2026/08/21 14:35:22 Csut".
String buildPositionText(uint8_t pos) {
  String s = "";
  if (datePos == pos) s += buildDateString();
  if (clockPos == pos) { if (s.length() > 0) s += " "; s += buildClockText(); }
  if (dayNamePos == pos) { if (s.length() > 0) s += " "; s += String(DOW_NAMES[dayOfWeekIdx(yr, month, day)]); }
  return s;
}

// ---------------- Objektumok ----------------
static lv_obj_t *idLeft, *idCenter, *idRight, *idWifi, *idBt, *idMode;
static lv_obj_t *idRelCap[2], *idRelPill[2], *idRelTxt[2];
static lv_obj_t *idCard, *idDiv;
static lv_obj_t *idTile[2], *idTileTitle[2], *idTileVal[2], *idTileBar[2];
static lv_obj_t *idFanArc[2], *idFanName[2], *idFanVal[2];
static lv_obj_t *idRgbDot, *idRgbName, *idRgbSub;

static void idle_make_tile(int k) {
  idTile[k] = ui_box(idCard, 0, 0, 98, 57);
  idTileTitle[k] = ui_label(idTile[k], FONT_S, PAL_MUTED, "");
  lv_obj_set_pos(idTileTitle[k], 8, 5);
  idTileVal[k] = ui_label(idTile[k], FONT_L, PAL_TEXT, "");
  idTileBar[k] = lv_bar_create(idTile[k]);
  lv_obj_set_size(idTileBar[k], 82, 4);
  lv_obj_set_pos(idTileBar[k], 8, 47);
  lv_bar_set_range(idTileBar[k], 0, 100);
  lv_obj_set_style_bg_color(idTileBar[k], pal(PAL_LINE), LV_PART_MAIN);
  lv_obj_set_style_bg_color(idTileBar[k], pal(PAL_OK), LV_PART_INDICATOR);
  lv_obj_set_style_radius(idTileBar[k], 2, LV_PART_MAIN);
  lv_obj_set_style_radius(idTileBar[k], 2, LV_PART_INDICATOR);
}

static void idle_make_fan(int k, int x) {
  idFanArc[k] = lv_arc_create(scrIdle);
  lv_obj_set_size(idFanArc[k], 36, 36);
  lv_obj_set_pos(idFanArc[k], x, 88);
  lv_arc_set_rotation(idFanArc[k], 135);
  lv_arc_set_bg_angles(idFanArc[k], 0, 270);
  lv_arc_set_range(idFanArc[k], 0, 100);
  lv_arc_set_value(idFanArc[k], 0);
  lv_obj_remove_style(idFanArc[k], nullptr, LV_PART_KNOB);
  lv_obj_remove_flag(idFanArc[k], LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(idFanArc[k], 5, LV_PART_MAIN);
  lv_obj_set_style_arc_width(idFanArc[k], 5, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(idFanArc[k], pal(PAL_LINE), LV_PART_MAIN);
  lv_obj_set_style_arc_color(idFanArc[k], pal(PAL_OK), LV_PART_INDICATOR);
  idFanName[k] = ui_label(scrIdle, FONT_S, PAL_MUTED, k == 0 ? "VENT 1" : "VENT 2");
  lv_obj_set_pos(idFanName[k], x + 42, 89);
  idFanVal[k] = ui_label(scrIdle, FONT_M, PAL_TEXT, "0%");
  lv_obj_set_pos(idFanVal[k], x + 42, 106);
}

void ui_idle_build() {
  scrIdle = ui_screen();

  // Felső sáv
  idLeft = ui_label(scrIdle, FONT_S, PAL_ACCENT, "");
  lv_obj_set_pos(idLeft, 4, 3);
  idCenter = ui_label(scrIdle, FONT_S, PAL_TEXT, "");
  lv_obj_align(idCenter, LV_ALIGN_TOP_MID, 0, 3);
  idRight = ui_box(scrIdle, 0, 0, 10, 18);
  lv_obj_set_size(idRight, LV_SIZE_CONTENT, 18);
  lv_obj_set_flex_flow(idRight, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(idRight, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(idRight, 6, 0);
  lv_obj_align(idRight, LV_ALIGN_TOP_RIGHT, -4, 0);
  idWifi = ui_label(idRight, FONT_S, PAL_TEXT, LV_SYMBOL_WIFI);
  idBt = ui_label(idRight, FONT_S, PAL_BLUE, LV_SYMBOL_BLUETOOTH);
  idMode = ui_label(idRight, FONT_S, PAL_ACCENT, "");
  lv_obj_add_flag(idWifi, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(idBt, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t *line = ui_box(scrIdle, 0, 18, 240, 1);
  ui_fill(line, PAL_LINE, 0);

  // Relé sáv
  for (int i = 0; i < 2; i++) {
    int y0 = 24 + i * 28;
    idRelCap[i] = ui_label(scrIdle, FONT_S, PAL_MUTED, i == 0 ? "R1" : "R2");
    lv_obj_set_pos(idRelCap[i], 6, y0);
    idRelPill[i] = ui_box(scrIdle, 3, y0 + 13, 27, 14);
    ui_fill(idRelPill[i], PAL_BAD, 7);
    idRelTxt[i] = ui_label(idRelPill[i], FONT_S, PAL_BG, "KI");
    lv_obj_center(idRelTxt[i]);
  }

  // Középső kártya
  idCard = ui_box(scrIdle, 34, 22, 204, 57);
  ui_fill(idCard, PAL_CARD, 10);
  idle_make_tile(0);
  idle_make_tile(1);
  lv_obj_set_x(idTile[1], 102);
  idDiv = ui_box(idCard, 101, 10, 1, 37);
  ui_fill(idDiv, PAL_LINE, 0);

  // Alsó sor
  idle_make_fan(0, 4);
  idle_make_fan(1, 84);
  idRgbDot = ui_box(scrIdle, 164, 90, 30, 30);
  ui_fill(idRgbDot, PAL_BG, LV_RADIUS_CIRCLE);
  lv_obj_set_style_border_width(idRgbDot, 2, 0);
  lv_obj_set_style_border_color(idRgbDot, pal(PAL_LINE), 0);
  idRgbName = ui_label(scrIdle, FONT_S, PAL_MUTED, "RGB");
  lv_obj_set_pos(idRgbName, 202, 89);
  idRgbSub = ui_label(scrIdle, FONT_M, PAL_TEXT, "KI");
  lv_obj_set_pos(idRgbSub, 202, 106);
}

// ---------------- Frissítés ----------------
// Egy csempe beállítása; a "key" a legutóbb kirajzolt tartalom azonosítója,
// hogy csak változáskor írjunk az LVGL-be.
static void tile_set(int k, const char *title, const char *val, uint32_t valCol, int barPct, uint32_t barCol, bool showBar) {
  ui_set_text(idTileTitle[k], title);
  ui_set_text(idTileVal[k], val);
  lv_obj_set_style_text_color(idTileVal[k], pal(valCol), 0);
  if (showBar) {
    lv_obj_remove_flag(idTileBar[k], LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_value(idTileBar[k], barPct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(idTileBar[k], pal(barCol), LV_PART_INDICATOR);
  } else {
    lv_obj_add_flag(idTileBar[k], LV_OBJ_FLAG_HIDDEN);
  }
}

static void tile_temp(int k, const char *title, float t) {
  char b[16];
  if (isnan(t)) snprintf(b, sizeof(b), "--");
  else snprintf(b, sizeof(b), "%.1f" "\xC2\xB0" "C", t);
  float c = isnan(t) ? 0 : constrain(t, -10.0f, 50.0f);
  tile_set(k, title, b, PAL_TEXT, (int)((c + 10.0f) * 100.0f / 60.0f), tempToPal(t), true);
}

static void tile_hum(int k, float h) {
  char b[16];
  if (isnan(h)) snprintf(b, sizeof(b), "--%%");
  else snprintf(b, sizeof(b), "%.0f%%", h);
  tile_set(k, "Paratartalom", b, PAL_ACCENT, isnan(h) ? 0 : (int)constrain(h, 0.0f, 100.0f), PAL_ACCENT, true);
}

static void tile_switch(int k, const char *name, bool closed) {
  tile_set(k, name, closed ? "Zart" : "Nyit", closed ? PAL_OK : PAL_BAD, 0, PAL_OK, false);
}

// A csempék elrendezése módonként: 1 hőmérő módban egy széles csempe nagy
// számmal, egyébként két keskeny csempe.
static void idle_layout(int mode) {
  bool single = (mode == 1);
  if (single) {
    lv_obj_set_width(idTile[0], 200);
    lv_obj_set_width(idTileBar[0], 184);
    lv_obj_set_style_text_font(idTileVal[0], FONT_XL, 0);
    lv_obj_set_pos(idTileVal[0], 8, 14);
    lv_obj_add_flag(idTile[1], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(idDiv, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_set_width(idTile[0], 98);
    lv_obj_set_width(idTileBar[0], 82);
    lv_obj_set_style_text_font(idTileVal[0], FONT_L, 0);
    lv_obj_set_pos(idTileVal[0], 8, 19);
    lv_obj_remove_flag(idTile[1], LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(idDiv, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_set_style_text_font(idTileVal[1], FONT_L, 0);
  lv_obj_set_pos(idTileVal[1], 8, 19);
}

void displayInfo() {
  ui_lock();
  static bool force = true;
  static int lastLayoutMode = -1;
  static String lastLeft = "\x01", lastCenter = "\x01";
  static int lastMode = -1;
  static int lastBt = -1, lastWifi = -1;
  static int lastR[2] = { -1, -1 };
  static float lastT1 = -999, lastT2 = -999, lastHum = -999;
  static int lastSw1 = -1, lastSw2 = -1;
  static int lastFan[2] = { -1, -1 };
  static uint32_t lastRgb = 0xFFFFFFFF;

  if (lv_screen_active() != scrIdle) { lv_screen_load(scrIdle); force = true; }
  if (idleNeedsFullClear) { idleNeedsFullClear = false; force = true; }
  if (force) {
    force = false;
    lastLayoutMode = -1; lastLeft = "\x01"; lastCenter = "\x01"; lastMode = -1;
    lastBt = lastWifi = lastSw1 = lastSw2 = -1; lastR[0] = lastR[1] = -1;
    lastT1 = lastT2 = lastHum = -999; lastFan[0] = lastFan[1] = -1; lastRgb = 0xFFFFFFFF;
  }

  // Elrendezés (csak módváltáskor)
  if (currentMode != lastLayoutMode) {
    lastLayoutMode = currentMode;
    idle_layout(currentMode);
    lastT1 = lastT2 = lastHum = -999; lastSw1 = lastSw2 = -1;
  }

  // Fejléc: dátum / idő
  String leftText = buildPositionText(1);
  String centerText = buildPositionText(2);
  if (leftText != lastLeft) { lastLeft = leftText; ui_set_text(idLeft, leftText.c_str()); }
  if (centerText != lastCenter) { lastCenter = centerText; ui_set_text(idCenter, centerText.c_str()); }

  // Jobb felső sarok: WiFi, BT, mód
  int wifiNow = (WiFi.status() == WL_CONNECTED) ? 1 : 0;
  int btNow = deviceConnected ? 1 : 0;
  if (wifiNow != lastWifi) {
    lastWifi = wifiNow;
    if (wifiNow) lv_obj_remove_flag(idWifi, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(idWifi, LV_OBJ_FLAG_HIDDEN);
  }
  if (btNow != lastBt) {
    lastBt = btNow;
    if (btNow) lv_obj_remove_flag(idBt, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(idBt, LV_OBJ_FLAG_HIDDEN);
  }
  if (currentMode != lastMode) { lastMode = currentMode; ui_set_text(idMode, MODE_SHORT[currentMode]); }

  // Relék
  for (int i = 0; i < 2; i++) {
    int on = digitalRead(i == 0 ? RELAY1_PIN : RELAY2_PIN) ? 1 : 0;
    if (on != lastR[i]) {
      lastR[i] = on;
      lv_obj_set_style_bg_color(idRelPill[i], pal(on ? PAL_OK : PAL_BAD), 0);
      ui_set_text(idRelTxt[i], on ? "BE" : "KI");
      lv_obj_center(idRelTxt[i]);
    }
  }

  // Középső kártya: mód-függő tartalom
  switch (currentMode) {
    case 0:
      if (tempChanged(ntc1Temperature, lastT1)) { lastT1 = ntc1Temperature; tile_temp(0, "Homero 1", ntc1Temperature); }
      if (tempChanged(ntc2Temperature, lastT2)) { lastT2 = ntc2Temperature; tile_temp(1, "Homero 2", ntc2Temperature); }
      break;
    case 1:
      if (tempChanged(ntc1Temperature, lastT1)) { lastT1 = ntc1Temperature; tile_temp(0, "Homerseklet", ntc1Temperature); }
      break;
    case 2: {
      if (tempChanged(ntc1Temperature, lastT1)) { lastT1 = ntc1Temperature; tile_temp(0, "Homero", ntc1Temperature); }
      bool humChanged = (isnan(humidity) != isnan(lastHum)) || (!isnan(humidity) && fabs(humidity - lastHum) >= 0.5);
      if (humChanged) { lastHum = humidity; tile_hum(1, humidity); }
      break;
    }
    case 3: {
      if (tempChanged(ntc1Temperature, lastT1)) { lastT1 = ntc1Temperature; tile_temp(0, "Homero", ntc1Temperature); }
      int sw = isSwitchClosed(NTC2_PIN) ? 1 : 0;
      if (sw != lastSw2) { lastSw2 = sw; tile_switch(1, "Kapcsolo", sw); }
      break;
    }
    case 4: {
      int s1 = isSwitchClosed(NTC1_PIN) ? 1 : 0, s2 = isSwitchClosed(NTC2_PIN) ? 1 : 0;
      if (s1 != lastSw1) { lastSw1 = s1; tile_switch(0, "K1", s1); }
      if (s2 != lastSw2) { lastSw2 = s2; tile_switch(1, "K2", s2); }
      break;
    }
  }

  // Alsó sor: ventilátorok
  int fanPct[2] = { (int)round(pwmFan / 2.55), (int)round(pwmFan2 / 2.55) };
  for (int i = 0; i < 2; i++) {
    if (fanPct[i] != lastFan[i]) {
      lastFan[i] = fanPct[i];
      lv_arc_set_value(idFanArc[i], constrain(fanPct[i], 0, 100));
      lv_obj_set_style_arc_color(idFanArc[i], pal(fanPct[i] > 0 ? PAL_OK : PAL_LINE), LV_PART_INDICATOR);
      char b[8]; snprintf(b, sizeof(b), "%d%%", fanPct[i]);
      ui_set_text(idFanVal[i], b);
      lv_obj_set_style_text_color(idFanVal[i], pal(fanPct[i] > 0 ? PAL_TEXT : PAL_MUTED), 0);
    }
  }

  // RGB
  bool rgbOn = digitalRead(RGB_ON);
  uint32_t rgbKey = rgbOn ? (((uint32_t)pwmR << 16) | ((uint32_t)pwmG << 8) | pwmB | 0x1000000) : 0;
  if (rgbKey != lastRgb) {
    lastRgb = rgbKey;
    lv_obj_set_style_bg_color(idRgbDot, rgbOn ? lv_color_make(pwmR, pwmG, pwmB) : pal(PAL_BG), 0);
    ui_set_text(idRgbSub, rgbOn ? "BE" : "KI");
    lv_obj_set_style_text_color(idRgbSub, pal(rgbOn ? PAL_TEXT : PAL_MUTED), 0);
  }
  ui_unlock();
}
