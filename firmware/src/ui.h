// ============================================================
// ui.h - Kijelző-réteg: LovyanGFX (SPI/DMA meghajtó) + LVGL 9 (felület)
//
// Ez a fájl a "közös" rész: inicializálás, színpaletta, a menü oldalai
// (lista, érték-szerkesztő, frekvencia-számjegyek, WiFi képernyők) és az
// OTA képernyő. Az idle (fő) képernyő a Display.h-ban van.
//
// A menü LOGIKÁJA (Menu.h állapotgép, gombkezelés) nem változott - ez a
// réteg csak megjeleníti az állapotot. A szövegek szándékosan ékezet
// nélküliek (a beépített LVGL betűtípusok csak ASCII-t tartalmaznak).
//
// SZÁLBIZTONSÁG: az LVGL nem szálbiztos. A BLE callback (másik task) is
// hív ui_ota_*() függvényeket, ezért minden LVGL-hívás a ui_lock()/
// ui_unlock() (rekurzív mutex) közé kerül, a main loop pedig ui_tick()-et hív.
// ============================================================
#pragma once

#include <lvgl.h>
#include "lgfx_setup.h"

// ---------------- Paletta ----------------
#define PAL_BG     0x0B0F14
#define PAL_CARD   0x161C24
#define PAL_LINE   0x2A3441
#define PAL_TEXT   0xE6EDF3
#define PAL_MUTED  0x8B98A5
#define PAL_ACCENT 0x22D3EE
#define PAL_OK     0x34D399
#define PAL_BAD    0xF87171
#define PAL_WARN   0xFB923C
#define PAL_YELLOW 0xFACC15
#define PAL_BLUE   0x60A5FA

// ---------------- Betűtípusok ----------------
// HU_FONTS=1 esetén a saját, magyar ékezetes betűtípusok (src/fonts/lv_font_hu_*.c,
// generálás: tools/gen_fonts.sh). Nélkülük az LVGL beépített, ASCII-s Montserrat betűi
// futnak, és az ékezetes szövegeket a ui_fold() automatikusan ékezet nélkülire képezi le.
#ifdef HU_FONTS
LV_FONT_DECLARE(lv_font_hu_12);
LV_FONT_DECLARE(lv_font_hu_14);
LV_FONT_DECLARE(lv_font_hu_16);
LV_FONT_DECLARE(lv_font_hu_20);
LV_FONT_DECLARE(lv_font_hu_28);
#define FONT_S  (&lv_font_hu_12)
#define FONT_M  (&lv_font_hu_14)
#define FONT_ML (&lv_font_hu_16)
#define FONT_L  (&lv_font_hu_20)
#define FONT_XL (&lv_font_hu_28)
#else
#define FONT_S  (&lv_font_montserrat_12)
#define FONT_M  (&lv_font_montserrat_14)
#define FONT_ML (&lv_font_montserrat_16)
#define FONT_L  (&lv_font_montserrat_20)
#define FONT_XL (&lv_font_montserrat_28)

// Ékezetes UTF-8 karakterek -> ASCII (csak a beépített betűtípusokhoz kell).
// Az LVGL szimbólumok (FontAwesome, 0xEF kezdőbájt) érintetlenül átmennek.
static void ui_fold(const char *in, char *out, size_t cap) {
  struct Map { uint8_t a, b; const char *r; };
  static const Map map[] = {
    {0xC3,0x81,"A"},{0xC3,0x89,"E"},{0xC3,0x8D,"I"},{0xC3,0x93,"O"},{0xC3,0x96,"O"},{0xC3,0x9A,"U"},{0xC3,0x9C,"U"},
    {0xC3,0xA1,"a"},{0xC3,0xA9,"e"},{0xC3,0xAD,"i"},{0xC3,0xB3,"o"},{0xC3,0xB6,"o"},{0xC3,0xBA,"u"},{0xC3,0xBC,"u"},
    {0xC5,0x90,"O"},{0xC5,0x91,"o"},{0xC5,0xB0,"U"},{0xC5,0xB1,"u"},
    {0xCE,0xA9,"Ohm"},{0xCE,0xB2,"beta"}
  };
  size_t o = 0;
  for (size_t i = 0; in[i] && o + 5 < cap; ) {
    const char *rep = nullptr;
    for (const Map &m : map) {
      if ((uint8_t)in[i] == m.a && (uint8_t)in[i + 1] == m.b) { rep = m.r; break; }
    }
    if (rep) { while (*rep && o + 1 < cap) out[o++] = *rep++; i += 2; }
    else out[o++] = in[i++];
  }
  out[o] = 0;
}
#endif

// ---------------- Állapot ----------------
LGFX lcd;
lv_display_t *ui_disp = nullptr;
alignas(4) static uint8_t ui_buf1[240 * 24 * 2];
alignas(4) static uint8_t ui_buf2[240 * 24 * 2];

static SemaphoreHandle_t ui_mutex = nullptr;

lv_obj_t *scrBoot = nullptr, *scrIdle = nullptr, *scrMenu = nullptr, *scrOta = nullptr;

// Menü
static lv_obj_t *mTitle, *mCount;
static lv_obj_t *pageList, *pageEdit, *pageFreq, *pageScan, *pagePw, *pageConn;
static lv_obj_t *ui_pages[6];
static lv_obj_t *ui_lastPage = nullptr;
static String ui_lastTitle = "";

// Lista
#define LIST_ROWS 5
#define LIST_ROW_H 22
static lv_obj_t *hlBar, *rowCont[LIST_ROWS], *rowLbl[LIST_ROWS], *rowVal[LIST_ROWS];

// Szerkesztő
static lv_obj_t *eUp, *eDn, *eVal, *eSub, *eHint;

// Frekvencia
static lv_obj_t *fBox[5], *fDig[5], *fHint;

// WiFi
static lv_obj_t *scanSpin, *scanLbl;
static lv_obj_t *pwSsid, *pwCard, *pwText, *pwChip, *pwChipTxt, *pwHint;
static lv_obj_t *connSpin, *connIcon, *connMsg, *connSub, *connHint;

// OTA
static lv_obj_t *otaTitle, *otaSub, *otaBar, *otaPct;

// ---------------- Segédek ----------------
static inline lv_color_t pal(uint32_t c) { return lv_color_hex(c); }

void ui_lock()   { if (ui_mutex) xSemaphoreTakeRecursive(ui_mutex, portMAX_DELAY); }
void ui_unlock() { if (ui_mutex) xSemaphoreGiveRecursive(ui_mutex); }

// A régi RGB565 színkonstansok (ValueRow.valColor) leképezése a palettára
static lv_color_t ui_color565(uint16_t c) {
  switch (c) {
    case UI_GREEN:  return pal(PAL_OK);
    case UI_RED:    return pal(PAL_BAD);
    case UI_CYAN:   return pal(PAL_ACCENT);
    case UI_YELLOW: return pal(PAL_YELLOW);
    case UI_ORANGE: return pal(PAL_WARN);
    case UI_BLUE:   return pal(PAL_BLUE);
    case UI_WHITE:  return pal(PAL_TEXT);
    case UI_BLACK:  return pal(PAL_BG);
    default: break;
  }
  uint8_t r = ((c >> 11) & 31) * 255 / 31;
  uint8_t g = ((c >> 5) & 63) * 255 / 63;
  uint8_t b = (c & 31) * 255 / 31;
  return lv_color_make(r, g, b);
}

static void ui_set_text(lv_obj_t *o, const char *s) {
#ifndef HU_FONTS
  char buf[240];
  ui_fold(s, buf, sizeof(buf));
  s = buf;
#endif
  if (strcmp(lv_label_get_text(o), s) != 0) lv_label_set_text(o, s);
}

static lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *f, uint32_t col, const char *txt = "") {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, pal(col), 0);
  ui_set_text(l, txt);
  return l;
}

static lv_obj_t *ui_box(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

static void ui_fill(lv_obj_t *o, uint32_t col, int radius) {
  lv_obj_set_style_bg_color(o, pal(col), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, radius, 0);
}

static lv_obj_t *ui_screen() {
  lv_obj_t *s = lv_obj_create(nullptr);
  lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s, pal(PAL_BG), 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(s, 0, 0);
  lv_obj_set_style_border_width(s, 0, 0);
  return s;
}

static void ui_flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  uint32_t w = a->x2 - a->x1 + 1;
  uint32_t h = a->y2 - a->y1 + 1;
  lcd.startWrite();
  lcd.setAddrWindow(a->x1, a->y1, w, h);
  lcd.writePixels((lgfx::rgb565_t *)px, w * h);
  lcd.endWrite();
  lv_display_flush_ready(d);
}

static void ui_anim_y(lv_obj_t *o, int toY) {
  lv_anim_delete(o, nullptr);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
  lv_anim_set_values(&a, lv_obj_get_y(o), toY);
  lv_anim_set_duration(&a, 110);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

// A menü-képernyő megjelenítése, a megadott oldallal. Visszaadja, hogy ez
// MÁSIK oldal/cím-e, mint legutóbb (akkor nincs animáció, ugrik a kijelölés).
static bool ui_menu_page(lv_obj_t *page, const String &title) {
  if (lv_screen_active() != scrMenu) lv_screen_load(scrMenu);
  for (int i = 0; i < 6; i++) {
    if (ui_pages[i] == page) lv_obj_remove_flag(ui_pages[i], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(ui_pages[i], LV_OBJ_FLAG_HIDDEN);
  }
  ui_set_text(mTitle, title.c_str());
  bool changed = (page != ui_lastPage) || (title != ui_lastTitle);
  ui_lastPage = page;
  ui_lastTitle = title;
  return changed;
}

// ---------------- Építés ----------------
static void ui_build_menu() {
  scrMenu = ui_screen();

  lv_obj_t *hdr = ui_box(scrMenu, 0, 0, 240, 22);
  ui_fill(hdr, PAL_CARD, 0);
  lv_obj_t *accent = ui_box(hdr, 0, 0, 3, 22);
  ui_fill(accent, PAL_ACCENT, 0);
  mTitle = ui_label(hdr, FONT_M, PAL_ACCENT, "");
  lv_obj_align(mTitle, LV_ALIGN_LEFT_MID, 10, 0);
  mCount = ui_label(hdr, FONT_S, PAL_MUTED, "");
  lv_obj_align(mCount, LV_ALIGN_RIGHT_MID, -8, 0);

  // ---- Lista ----
  pageList = ui_box(scrMenu, 0, 22, 240, 113);
  hlBar = ui_box(pageList, 3, 1, 234, LIST_ROW_H - 2);
  ui_fill(hlBar, PAL_ACCENT, 6);
  for (int i = 0; i < LIST_ROWS; i++) {
    rowCont[i] = ui_box(pageList, 0, i * LIST_ROW_H, 240, LIST_ROW_H);
    lv_obj_set_flex_flow(rowCont[i], LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rowCont[i], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(rowCont[i], 12, 0);
    lv_obj_set_style_pad_right(rowCont[i], 12, 0);
    lv_obj_set_style_pad_column(rowCont[i], 6, 0);
    rowLbl[i] = ui_label(rowCont[i], FONT_M, PAL_TEXT, "");
    lv_label_set_long_mode(rowLbl[i], LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(rowLbl[i], 1);
    rowVal[i] = ui_label(rowCont[i], FONT_M, PAL_TEXT, "");
  }

  // ---- Érték-szerkesztő ----
  pageEdit = ui_box(scrMenu, 0, 22, 240, 113);
  eUp = ui_label(pageEdit, FONT_M, PAL_MUTED, LV_SYMBOL_UP);
  lv_obj_align(eUp, LV_ALIGN_TOP_MID, 0, 2);
  eVal = ui_label(pageEdit, FONT_XL, PAL_TEXT, "");
  lv_obj_set_width(eVal, 228);
  lv_label_set_long_mode(eVal, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(eVal, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(eVal, LV_ALIGN_TOP_MID, 0, 20);
  eDn = ui_label(pageEdit, FONT_M, PAL_MUTED, LV_SYMBOL_DOWN);
  lv_obj_align(eDn, LV_ALIGN_TOP_MID, 0, 58);
  eSub = ui_label(pageEdit, FONT_S, PAL_MUTED, "");
  lv_obj_set_width(eSub, 228);
  lv_label_set_long_mode(eSub, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(eSub, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(eSub, LV_ALIGN_TOP_MID, 0, 72);
  eHint = ui_label(pageEdit, FONT_S, PAL_ACCENT, "Fel/Le: érték     Dupla: OK");
  lv_obj_align(eHint, LV_ALIGN_BOTTOM_MID, 0, -3);

  // ---- Frekvencia ----
  pageFreq = ui_box(scrMenu, 0, 22, 240, 113);
  for (int i = 0; i < 5; i++) {
    fBox[i] = ui_box(pageFreq, 20 + i * 40, 12, 36, 48);
    ui_fill(fBox[i], PAL_CARD, 8);
    lv_obj_set_style_border_width(fBox[i], 2, 0);
    lv_obj_set_style_border_color(fBox[i], pal(PAL_LINE), 0);
    fDig[i] = ui_label(fBox[i], FONT_XL, PAL_TEXT, "0");
    lv_obj_center(fDig[i]);
  }
  fHint = ui_label(pageFreq, FONT_S, PAL_ACCENT, "Fel/Le: érték 0-9\nHosszú Fel: következő számjegy");
  lv_obj_set_style_text_align(fHint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(fHint, LV_ALIGN_BOTTOM_MID, 0, -4);

  // ---- WiFi: keresés ----
  pageScan = ui_box(scrMenu, 0, 22, 240, 113);
  scanSpin = lv_spinner_create(pageScan);
  lv_spinner_set_anim_params(scanSpin, 1000, 220);
  lv_obj_set_size(scanSpin, 44, 44);
  lv_obj_set_style_arc_width(scanSpin, 5, LV_PART_MAIN);
  lv_obj_set_style_arc_width(scanSpin, 5, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(scanSpin, pal(PAL_LINE), LV_PART_MAIN);
  lv_obj_set_style_arc_color(scanSpin, pal(PAL_ACCENT), LV_PART_INDICATOR);
  lv_obj_align(scanSpin, LV_ALIGN_TOP_MID, 0, 14);
  scanLbl = ui_label(pageScan, FONT_ML, PAL_TEXT, "Hálózatok keresése...");
  lv_obj_align(scanLbl, LV_ALIGN_TOP_MID, 0, 72);

  // ---- WiFi: jelszó ----
  pagePw = ui_box(scrMenu, 0, 22, 240, 113);
  pwSsid = ui_label(pagePw, FONT_S, PAL_MUTED, "");
  lv_obj_set_width(pwSsid, 228);
  lv_label_set_long_mode(pwSsid, LV_LABEL_LONG_DOT);
  lv_obj_align(pwSsid, LV_ALIGN_TOP_LEFT, 6, 2);
  pwCard = ui_box(pagePw, 6, 17, 228, 30);
  ui_fill(pwCard, PAL_CARD, 8);
  pwText = ui_label(pwCard, FONT_L, PAL_TEXT, "");
  lv_obj_set_width(pwText, 214);
  lv_label_set_long_mode(pwText, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(pwText, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(pwText, LV_ALIGN_RIGHT_MID, -7, 0);
  pwChip = ui_box(pagePw, 0, 52, 64, 34);
  ui_fill(pwChip, PAL_OK, 8);
  lv_obj_align(pwChip, LV_ALIGN_TOP_MID, 0, 52);
  pwChipTxt = ui_label(pwChip, FONT_L, PAL_BG, "a");
  lv_obj_center(pwChipTxt);
  pwHint = ui_label(pagePw, FONT_S, PAL_ACCENT, "Fel/Le: karakter     Mindkét gomb: beír\nHosszan mindkét gomb: csatlakozás");
  lv_obj_set_style_text_align(pwHint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(pwHint, LV_ALIGN_BOTTOM_MID, 0, -2);

  // ---- WiFi: csatlakozás eredménye ----
  pageConn = ui_box(scrMenu, 0, 22, 240, 113);
  connSpin = lv_spinner_create(pageConn);
  lv_spinner_set_anim_params(connSpin, 1000, 220);
  lv_obj_set_size(connSpin, 44, 44);
  lv_obj_set_style_arc_width(connSpin, 5, LV_PART_MAIN);
  lv_obj_set_style_arc_width(connSpin, 5, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(connSpin, pal(PAL_LINE), LV_PART_MAIN);
  lv_obj_set_style_arc_color(connSpin, pal(PAL_ACCENT), LV_PART_INDICATOR);
  lv_obj_align(connSpin, LV_ALIGN_TOP_MID, 0, 8);
  connIcon = ui_label(pageConn, FONT_XL, PAL_OK, LV_SYMBOL_OK);
  lv_obj_align(connIcon, LV_ALIGN_TOP_MID, 0, 8);
  connMsg = ui_label(pageConn, FONT_L, PAL_TEXT, "");
  lv_obj_align(connMsg, LV_ALIGN_TOP_MID, 0, 56);
  connSub = ui_label(pageConn, FONT_M, PAL_MUTED, "");
  lv_obj_align(connSub, LV_ALIGN_TOP_MID, 0, 82);
  connHint = ui_label(pageConn, FONT_S, PAL_ACCENT, "Bármelyik gomb: vissza");
  lv_obj_align(connHint, LV_ALIGN_BOTTOM_MID, 0, -2);

  ui_pages[0] = pageList; ui_pages[1] = pageEdit; ui_pages[2] = pageFreq;
  ui_pages[3] = pageScan; ui_pages[4] = pagePw;   ui_pages[5] = pageConn;
}

static void ui_build_ota() {
  scrOta = ui_screen();
  otaTitle = ui_label(scrOta, FONT_L, PAL_TEXT, "");
  lv_obj_set_width(otaTitle, 224);
  lv_label_set_long_mode(otaTitle, LV_LABEL_LONG_DOT);
  lv_obj_align(otaTitle, LV_ALIGN_TOP_LEFT, 8, 8);
  otaSub = ui_label(scrOta, FONT_M, PAL_MUTED, "");
  lv_obj_set_width(otaSub, 224);
  lv_label_set_long_mode(otaSub, LV_LABEL_LONG_WRAP);
  lv_obj_align(otaSub, LV_ALIGN_TOP_LEFT, 8, 40);
  otaBar = lv_bar_create(scrOta);
  lv_obj_set_size(otaBar, 224, 14);
  lv_obj_align(otaBar, LV_ALIGN_BOTTOM_MID, 0, -30);
  lv_bar_set_range(otaBar, 0, 100);
  lv_obj_set_style_bg_color(otaBar, pal(PAL_LINE), LV_PART_MAIN);
  lv_obj_set_style_bg_color(otaBar, pal(PAL_OK), LV_PART_INDICATOR);
  lv_obj_set_style_radius(otaBar, 7, LV_PART_MAIN);
  lv_obj_set_style_radius(otaBar, 7, LV_PART_INDICATOR);
  otaPct = ui_label(scrOta, FONT_ML, PAL_TEXT, "");
  lv_obj_align(otaPct, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_add_flag(otaBar, LV_OBJ_FLAG_HIDDEN);
}

void ui_idle_build();   // Display.h

void ui_init() {
  ui_mutex = xSemaphoreCreateRecursiveMutex();

  lcd.init();
  lcd.setRotation(3);
  lcd.fillScreen(0x0000);

  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });
  ui_disp = lv_display_create(240, 135);
  lv_display_set_flush_cb(ui_disp, ui_flush_cb);
  lv_display_set_buffers(ui_disp, ui_buf1, ui_buf2, sizeof(ui_buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);

  ui_lock();
  // indító képernyő
  scrBoot = ui_screen();
  lv_obj_t *t = ui_label(scrBoot, FONT_L, PAL_TEXT, "Pascal-Controller");
  lv_obj_align(t, LV_ALIGN_CENTER, 0, -14);
  lv_obj_t *v = ui_label(scrBoot, FONT_M, PAL_ACCENT, "v" FW_VERSION);
  lv_obj_align(v, LV_ALIGN_CENTER, 0, 14);
  lv_screen_load(scrBoot);

  ui_build_menu();
  ui_build_ota();
  ui_idle_build();
  lv_refr_now(ui_disp);
  ui_unlock();
}

// A main loop minden körben hívja
void ui_tick() {
  ui_lock();
  lv_timer_handler();
  ui_unlock();
}

// ---------------- Menü oldalak ----------------
void ui_list(const String &title, ValueRow rows[], int count, int cursor) {
  ui_lock();
  bool jump = ui_menu_page(pageList, title);

  int start = 0;
  if (count > LIST_ROWS) {
    start = cursor - LIST_ROWS / 2;
    if (start < 0) start = 0;
    if (start > count - LIST_ROWS) start = count - LIST_ROWS;
  }

  for (int i = 0; i < LIST_ROWS; i++) {
    int idx = start + i;
    if (idx < count) {
      bool sel = (idx == cursor);
      lv_obj_remove_flag(rowCont[i], LV_OBJ_FLAG_HIDDEN);
      ui_set_text(rowLbl[i], rows[idx].label.c_str());
      ui_set_text(rowVal[i], rows[idx].value.c_str());
      lv_obj_set_style_text_color(rowLbl[i], pal(sel ? PAL_BG : PAL_TEXT), 0);
      lv_obj_set_style_text_color(rowVal[i], sel ? pal(PAL_BG) : ui_color565(rows[idx].valColor), 0);
    } else {
      lv_obj_add_flag(rowCont[i], LV_OBJ_FLAG_HIDDEN);
    }
  }

  int targetY = (cursor - start) * LIST_ROW_H + 1;
  if (jump) {
    lv_anim_delete(hlBar, nullptr);
    lv_obj_set_y(hlBar, targetY);
  } else if (lv_obj_get_y(hlBar) != targetY) {
    ui_anim_y(hlBar, targetY);
  }

  char buf[24]; // két int és a "/" jel is biztosan elfér
  snprintf(buf, sizeof(buf), "%d/%d", cursor + 1, count);
  ui_set_text(mCount, buf);
  ui_unlock();
}

void ui_edit(const String &title, const String &line1, const String &line2) {
  ui_lock();
  ui_menu_page(pageEdit, title);
  ui_set_text(mCount, "");

  bool shortVal = line1.length() <= 10;
  lv_obj_set_style_text_font(eVal, shortVal ? FONT_XL : FONT_M, 0);
  lv_obj_align(eVal, LV_ALIGN_TOP_MID, 0, shortVal ? 20 : 26);
  ui_set_text(eVal, line1.c_str());
  if (shortVal) { lv_obj_remove_flag(eUp, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(eDn, LV_OBJ_FLAG_HIDDEN); }
  else { lv_obj_add_flag(eUp, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(eDn, LV_OBJ_FLAG_HIDDEN); }
  lv_obj_align(eSub, LV_ALIGN_TOP_MID, 0, shortVal ? 74 : 62);
  ui_set_text(eSub, line2.c_str());
  ui_unlock();
}

void ui_freq(const uint8_t *digits, int sel) {
  ui_lock();
  ui_menu_page(pageFreq, "FREKVENCIA (Hz)");
  ui_set_text(mCount, "");
  for (int i = 0; i < 5; i++) {
    char b[2] = { (char)('0' + digits[i]), 0 };
    ui_set_text(fDig[i], b);
    bool s = (i == sel);
    lv_obj_set_style_border_color(fBox[i], pal(s ? PAL_OK : PAL_LINE), 0);
    lv_obj_set_style_text_color(fDig[i], pal(s ? PAL_OK : PAL_TEXT), 0);
  }
  ui_unlock();
}

void ui_wifi_scanning() {
  ui_lock();
  ui_menu_page(pageScan, "WIFI");
  ui_set_text(mCount, "");
  lv_refr_now(ui_disp); // a keresés blokkol: a képernyőnek előbb ki kell rajzolódnia
  ui_unlock();
}

void ui_wifi_password(const String &ssid, const char *typed, const char *chip) {
  ui_lock();
  // Hosszú jelszónál a VÉGE látszik (az éppen gépelt rész), nem az eleje.
  String tail = String(typed);
  const int maxChars = 15;
  if ((int)tail.length() > maxChars) tail = "~" + tail.substring(tail.length() - maxChars);
  ui_menu_page(pagePw, "WIFI JELSZÓ");
  ui_set_text(mCount, "");
  ui_set_text(pwSsid, ssid.c_str());
  ui_set_text(pwText, tail.c_str());
  ui_set_text(pwChipTxt, chip);
  lv_obj_center(pwChipTxt);
  ui_unlock();
}

// state: 0 = csatlakozás folyamatban, 1 = siker, 2 = sikertelen
void ui_wifi_result(int state, const String &ip) {
  ui_lock();
  ui_menu_page(pageConn, "WIFI");
  ui_set_text(mCount, "");
  if (state == 0) {
    lv_obj_remove_flag(connSpin, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(connIcon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(connMsg, pal(PAL_TEXT), 0);
    ui_set_text(connMsg, "Csatlakozás...");
    ui_set_text(connSub, "");
    lv_obj_add_flag(connHint, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(connSpin, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(connIcon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(connHint, LV_OBJ_FLAG_HIDDEN);
    bool ok = (state == 1);
    ui_set_text(connIcon, ok ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(connIcon, pal(ok ? PAL_OK : PAL_BAD), 0);
    lv_obj_set_style_text_color(connMsg, pal(ok ? PAL_OK : PAL_BAD), 0);
    ui_set_text(connMsg, ok ? "Sikeres!" : "Sikertelen");
    ui_set_text(connSub, ok ? ip.c_str() : "Ellenőrizd a jelszót");
  }
  lv_obj_align(connMsg, LV_ALIGN_TOP_MID, 0, 56);
  lv_obj_align(connSub, LV_ALIGN_TOP_MID, 0, 82);
  ui_unlock();
}

// Azonnali kirajzolás: olyan helyeken kell, ahol a kód közvetlenül utána
// blokkol/újraindul (pl. gyári visszaállítás), így a loop() nem jut ui_tick()-hez.
void ui_refresh_now() {
  ui_lock();
  lv_refr_now(ui_disp);
  ui_unlock();
}

// ---------------- OTA képernyő ----------------
// A BLE callback és a loop() is hívhatja - lásd a fájl eleji megjegyzést.
void ui_ota_status(const char *line1, const String &line2) {
  ui_lock();
  if (lv_screen_active() != scrOta) lv_screen_load(scrOta);
  bool err = strstr(line1, "HIBA") != nullptr;
  bool done = strstr(line1, "KESZ") != nullptr || strstr(line1, "KÉSZ") != nullptr;
  lv_obj_set_style_text_color(otaTitle, pal(err ? PAL_BAD : (done ? PAL_OK : PAL_TEXT)), 0);
  ui_set_text(otaTitle, line1);
  ui_set_text(otaSub, line2.c_str());
  if (err || done) { lv_obj_add_flag(otaBar, LV_OBJ_FLAG_HIDDEN); ui_set_text(otaPct, ""); }
  lv_refr_now(ui_disp);
  ui_unlock();
}

void ui_ota_progress(int pct) {
  ui_lock();
  if (lv_screen_active() != scrOta) lv_screen_load(scrOta);
  lv_obj_remove_flag(otaBar, LV_OBJ_FLAG_HIDDEN);
  lv_bar_set_value(otaBar, pct, LV_ANIM_OFF);
  char b[8];
  snprintf(b, sizeof(b), "%d%%", pct);
  ui_set_text(otaPct, b);
  lv_refr_now(ui_disp);
  ui_unlock();
}
