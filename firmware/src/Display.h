// ============================================================
// Display.h - Az idle (fő) képernyő, LVGL-lel
//
// Elrendezés (240x135), az app Data oldalának kártyás kinézete szerint:
//   Fent:    dátum/idő (bal, közép) | WiFi, BT jel és mód-kód (jobb)
//   Középen: 1-2 szenzor kártya (hőmérséklet / páratartalom / kapcsoló állapot)
//   Lent:    V1, V2 (pörgő 5 lapátos propeller + %), R1, R2 (ON/OFF), RGB
//
// Az objektumok egyszer épülnek fel (ui_idle_build), a displayInfo() csak
// azt frissíti, ami ténylegesen változott - az LVGL csak a változó
// területeket rajzolja újra, így nincs villódzás.
// ============================================================
#pragma once

// Rövid mód-kódok a jobb felső sarokba
const char* MODE_SHORT[] = { "2Hő", "1Hő", "Hő+Pá", "Hő+K1", "2K" };
// Nap nevek (dayOfWeekIdx() sorrendjében: 0=Vasarnap...6=Szombat)
const char* DOW_NAMES[] = { "Vas", "Hét", "Ked", "Sze", "Csü", "Pén", "Szo" };

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
// Az app (Data oldal) kártyás kinézete: sötét kártyák 1px kerettel, halvány felirat,
// piros érték; aktív relé/ventilátor/kapcsoló = piros kártya, inaktív = #3A3A3C.
static lv_obj_t *idLeft, *idCenter, *idRight, *idWifi, *idBt, *idMode;
static lv_obj_t *idTile[2], *idTileTitle[2], *idTileVal[2];
static lv_obj_t *idFanCard[2], *idFanName[2], *idFanSpin[2], *idFanVal[2];
static lv_obj_t *idRelCard[2], *idRelName[2], *idRelTxt[2];
static lv_obj_t *idRgbCard, *idRgbName, *idRgbDot, *idRgbTxt;
static int  fanAngle[2] = { 0, 0 };
static bool fanSpin[2]  = { false, false };

#define IDLE_BOT_Y   73
#define IDLE_BOT_H   58
#define IDLE_BOT_W   44

static lv_obj_t *idle_card(lv_obj_t *parent, int x, int y, int w, int h, uint32_t fill) {
  lv_obj_t *c = ui_box(parent, x, y, w, h);
  ui_fill(c, fill, 12);
  lv_obj_set_style_border_width(c, 1, 0);
  lv_obj_set_style_border_color(c, pal(PAL_LINE), 0);
  return c;
}

static void idle_make_tile(int k) {
  idTile[k] = idle_card(scrIdle, 4 + k * 118, 21, 114, 48, PAL_CARD);
  idTileTitle[k] = ui_label(idTile[k], FONT_S, PAL_MUTED, "");
  lv_obj_set_pos(idTileTitle[k], 8, 1);
  idTileVal[k] = ui_label(idTile[k], FONT_L, PAL_ACCENT, "");
  lv_obj_set_pos(idTileVal[k], 8, 15);
}

// Az 5 lapátos propeller kirajzolása; a szöget a fan_timer_cb() lépteti
static void fan_draw_cb(lv_event_t *e) {
  lv_obj_t *o = lv_event_get_target_obj(e);
  lv_layer_t *layer = lv_event_get_layer(e);
  int k = (int)(intptr_t)lv_obj_get_user_data(o);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  float cx = (a.x1 + a.x2) / 2.0f, cy = (a.y1 + a.y2) / 2.0f;
  const float R = 12.0f, DEG = 3.14159265f / 180.0f;

  lv_draw_triangle_dsc_t t;
  lv_draw_triangle_dsc_init(&t);
  t.bg_color = pal(PAL_TEXT);
  t.bg_opa = LV_OPA_COVER;
  for (int b = 0; b < 5; b++) {
    float a0 = (fanAngle[k] + b * 72) * DEG;
    float a1 = a0 + 34.0f * DEG;   // elcsavart lapát: a két külső pont nem szimmetrikus
    t.p[0].x = cx;                 t.p[0].y = cy;
    t.p[1].x = cx + R * cosf(a0);  t.p[1].y = cy + R * sinf(a0);
    t.p[2].x = cx + R * cosf(a1);  t.p[2].y = cy + R * sinf(a1);
    lv_draw_triangle(layer, &t);
  }
  lv_draw_rect_dsc_t r;
  lv_draw_rect_dsc_init(&r);
  r.bg_color = pal(fanSpin[k] ? PAL_ACCENT : PAL_LINE);
  r.radius = LV_RADIUS_CIRCLE;
  lv_area_t hub = { (int32_t)cx - 3, (int32_t)cy - 3, (int32_t)cx + 3, (int32_t)cy + 3 };
  lv_draw_rect(layer, &r, &hub);
}

// Forgatás: csak a 0%-nál nagyobb sebességű ventilátor propellere pörög (állandó sebességgel)
static void fan_timer_cb(lv_timer_t *tm) {
  for (int k = 0; k < 2; k++) {
    if (!fanSpin[k]) continue;
    fanAngle[k] = (fanAngle[k] + 15) % 360;
    lv_obj_invalidate(idFanSpin[k]);
  }
}

static void idle_make_fan(int k, int x) {
  idFanCard[k] = idle_card(scrIdle, x, IDLE_BOT_Y, IDLE_BOT_W, IDLE_BOT_H, PAL_LINE);
  idFanName[k] = ui_label(idFanCard[k], FONT_S, PAL_MUTED, k == 0 ? "V1" : "V2");
  lv_obj_align(idFanName[k], LV_ALIGN_TOP_MID, 0, 0);
  idFanSpin[k] = ui_box(idFanCard[k], 0, 0, 26, 26);
  lv_obj_align(idFanSpin[k], LV_ALIGN_TOP_MID, 0, 15);
  lv_obj_set_user_data(idFanSpin[k], (void *)(intptr_t)k);
  lv_obj_add_event_cb(idFanSpin[k], fan_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
  idFanVal[k] = ui_label(idFanCard[k], FONT_S, PAL_TEXT, "0%");
  lv_obj_align(idFanVal[k], LV_ALIGN_BOTTOM_MID, 0, -1);
}

static void idle_make_relay(int k, int x) {
  idRelCard[k] = idle_card(scrIdle, x, IDLE_BOT_Y, IDLE_BOT_W, IDLE_BOT_H, PAL_LINE);
  idRelName[k] = ui_label(idRelCard[k], FONT_S, PAL_MUTED, k == 0 ? "R1" : "R2");
  lv_obj_align(idRelName[k], LV_ALIGN_TOP_MID, 0, 0);
  idRelTxt[k] = ui_label(idRelCard[k], FONT_M, PAL_TEXT, "OFF");
  lv_obj_align(idRelTxt[k], LV_ALIGN_CENTER, 0, 6);
}

void ui_idle_build() {
  scrIdle = ui_screen();

  // Felső sáv
  idLeft = ui_label(scrIdle, FONT_S, PAL_MUTED, "");
  lv_obj_set_pos(idLeft, 6, 1);
  idCenter = ui_label(scrIdle, FONT_S, PAL_TEXT, "");
  lv_obj_align(idCenter, LV_ALIGN_TOP_MID, 0, 1);
  idRight = ui_box(scrIdle, 0, 0, 10, 18);
  lv_obj_set_size(idRight, LV_SIZE_CONTENT, 18);
  lv_obj_set_flex_flow(idRight, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(idRight, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(idRight, 6, 0);
  lv_obj_align(idRight, LV_ALIGN_TOP_RIGHT, -6, 0);
  idWifi = ui_label(idRight, FONT_S, PAL_TEXT, LV_SYMBOL_WIFI);
  idBt = ui_label(idRight, FONT_S, PAL_BLUE, LV_SYMBOL_BLUETOOTH);
  idMode = ui_label(idRight, FONT_S, PAL_ACCENT, "");
  lv_obj_add_flag(idWifi, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(idBt, LV_OBJ_FLAG_HIDDEN);

  // Szenzor kártyák
  idle_make_tile(0);
  idle_make_tile(1);

  // Alsó sor: V1, V2, R1, R2, RGB
  idle_make_fan(0, 4);
  idle_make_fan(1, 51);
  idle_make_relay(0, 98);
  idle_make_relay(1, 145);
  idRgbCard = idle_card(scrIdle, 192, IDLE_BOT_Y, IDLE_BOT_W, IDLE_BOT_H, PAL_LINE);
  idRgbName = ui_label(idRgbCard, FONT_S, PAL_MUTED, "RGB");
  lv_obj_align(idRgbName, LV_ALIGN_TOP_MID, 0, 0);
  idRgbDot = ui_box(idRgbCard, 0, 0, 22, 22);
  ui_fill(idRgbDot, PAL_BG, LV_RADIUS_CIRCLE);
  lv_obj_set_style_border_width(idRgbDot, 1, 0);
  lv_obj_set_style_border_color(idRgbDot, pal(PAL_TEXT), 0);
  lv_obj_align(idRgbDot, LV_ALIGN_TOP_MID, 0, 17);
  idRgbTxt = ui_label(idRgbCard, FONT_S, PAL_TEXT, "OFF");
  lv_obj_align(idRgbTxt, LV_ALIGN_BOTTOM_MID, 0, -1);

  lv_timer_create(fan_timer_cb, 60, nullptr);
}

// ---------------- Frissítés ----------------
// Egy kártya beállítása; az érték színe és a kártya háttere a tartalomtól függ.
static void tile_set(int k, const char *title, const char *val, uint32_t valCol, uint32_t bgCol) {
  ui_set_text(idTileTitle[k], title);
  ui_set_text(idTileVal[k], val);
  lv_obj_set_style_text_color(idTileVal[k], pal(valCol), 0);
  lv_obj_set_style_bg_color(idTile[k], pal(bgCol), 0);
  lv_obj_set_style_text_color(idTileTitle[k], pal(bgCol == PAL_CARD ? PAL_MUTED : PAL_TEXT), 0);
}

static void tile_temp(int k, const char *title, float t) {
  char b[16];
  if (isnan(t)) snprintf(b, sizeof(b), "-- " "\xC2\xB0" "C");
  else snprintf(b, sizeof(b), "%.1f " "\xC2\xB0" "C", t);
  tile_set(k, title, b, PAL_ACCENT, PAL_CARD);
}

static void tile_hum(int k, float h) {
  char b[16];
  if (isnan(h)) snprintf(b, sizeof(b), "--%%");
  else snprintf(b, sizeof(b), "%.0f%%", h);
  tile_set(k, "Páratartalom", b, PAL_ACCENT, PAL_CARD);
}

// Kapcsoló mód: mint az appban - zárt = piros kártya, nyitott = szürke kártya, fehér felirat
static void tile_switch(int k, const char *name, bool closed) {
  tile_set(k, name, closed ? "ZÁRT" : "NYITOTT", PAL_TEXT, closed ? PAL_ACCENT : PAL_LINE);
}

// A kártyák elrendezése módonként: 1 hőmérő módban egy széles kártya,
// egyébként két egyforma.
static void idle_layout(int mode) {
  if (mode == 1) {
    lv_obj_set_width(idTile[0], 232);
    lv_obj_add_flag(idTile[1], LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_set_width(idTile[0], 114);
    lv_obj_remove_flag(idTile[1], LV_OBJ_FLAG_HIDDEN);
  }
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

  // Szenzor kártyák: mód-függő tartalom
  switch (currentMode) {
    case 0:
      if (tempChanged(ntc1Temperature, lastT1)) { lastT1 = ntc1Temperature; tile_temp(0, "Hőmérséklet 1", ntc1Temperature); }
      if (tempChanged(ntc2Temperature, lastT2)) { lastT2 = ntc2Temperature; tile_temp(1, "Hőmérséklet 2", ntc2Temperature); }
      break;
    case 1:
      if (tempChanged(ntc1Temperature, lastT1)) { lastT1 = ntc1Temperature; tile_temp(0, "Hőmérséklet", ntc1Temperature); }
      break;
    case 2: {
      if (tempChanged(ntc1Temperature, lastT1)) { lastT1 = ntc1Temperature; tile_temp(0, "Hőmérséklet", ntc1Temperature); }
      bool humChanged = (isnan(humidity) != isnan(lastHum)) || (!isnan(humidity) && fabs(humidity - lastHum) >= 0.5);
      if (humChanged) { lastHum = humidity; tile_hum(1, humidity); }
      break;
    }
    case 3: {
      int sw = isSwitchClosed(NTC2_PIN) ? 1 : 0;
      if (tempChanged(ntc1Temperature, lastT1)) { lastT1 = ntc1Temperature; tile_temp(0, "Hőmérséklet", ntc1Temperature); }
      if (sw != lastSw2) { lastSw2 = sw; tile_switch(1, "Kapcsoló", sw); }
      break;
    }
    case 4: {
      int s1 = isSwitchClosed(NTC1_PIN) ? 1 : 0, s2 = isSwitchClosed(NTC2_PIN) ? 1 : 0;
      if (s1 != lastSw1) { lastSw1 = s1; tile_switch(0, "Kapcsoló 1", s1); }
      if (s2 != lastSw2) { lastSw2 = s2; tile_switch(1, "Kapcsoló 2", s2); }
      break;
    }
  }

  // Ventilátorok (V1, V2): aktív = piros kártya, a propeller pörög
  int fanPct[2] = { (int)round(pwmFan / 2.55), (int)round(pwmFan2 / 2.55) };
  for (int i = 0; i < 2; i++) {
    if (fanPct[i] != lastFan[i]) {
      lastFan[i] = fanPct[i];
      fanSpin[i] = fanPct[i] > 0;
      lv_obj_set_style_bg_color(idFanCard[i], pal(fanSpin[i] ? PAL_ACCENT : PAL_LINE), 0);
      lv_obj_set_style_text_color(idFanName[i], pal(fanSpin[i] ? PAL_TEXT : PAL_MUTED), 0);
      lv_obj_invalidate(idFanSpin[i]);
      char b[8]; snprintf(b, sizeof(b), "%d%%", fanPct[i]);
      ui_set_text(idFanVal[i], b);
    }
  }

  // Relék: ON piros, OFF szürke
  for (int i = 0; i < 2; i++) {
    int on = digitalRead(i == 0 ? RELAY1_PIN : RELAY2_PIN) ? 1 : 0;
    if (on != lastR[i]) {
      lastR[i] = on;
      lv_obj_set_style_bg_color(idRelCard[i], pal(on ? PAL_ACCENT : PAL_LINE), 0);
      lv_obj_set_style_text_color(idRelName[i], pal(on ? PAL_TEXT : PAL_MUTED), 0);
      ui_set_text(idRelTxt[i], on ? "ON" : "OFF");
      lv_obj_align(idRelTxt[i], LV_ALIGN_CENTER, 0, 6);
    }
  }

  // RGB
  bool rgbOn = digitalRead(RGB_ON);
  uint32_t rgbKey = rgbOn ? (((uint32_t)pwmR << 16) | ((uint32_t)pwmG << 8) | pwmB | 0x1000000) : 0;
  if (rgbKey != lastRgb) {
    lastRgb = rgbKey;
    lv_obj_set_style_bg_color(idRgbCard, pal(rgbOn ? PAL_ACCENT : PAL_LINE), 0);
    lv_obj_set_style_text_color(idRgbName, pal(rgbOn ? PAL_TEXT : PAL_MUTED), 0);
    lv_obj_set_style_bg_color(idRgbDot, rgbOn ? lv_color_make(pwmR, pwmG, pwmB) : pal(PAL_BG), 0);
    ui_set_text(idRgbTxt, rgbOn ? "ON" : "OFF");
  }
  ui_unlock();
}
