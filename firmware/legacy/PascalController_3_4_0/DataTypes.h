// ============================================================
// DataTypes.h - Struktúrák (ThermFan, ThermRelay, TimerSet, SwitchLogicChannel, stb.) és a globális példányaik
// ============================================================
#pragma once

struct ThermRelay;

struct TimerSlot;

struct TimerSet;

struct SwitchLogicChannel;

struct ButtonState;

struct ValueRow;

enum ButtonEvent : uint8_t;

struct ThermFan {
  bool enabled = false;
  uint8_t count = 5; // ténylegesen használt pontok száma (1..5)
  float temp[FAN_CURVE_MAX_POINTS] = {10, 20, 30, 40, 50};
  uint8_t pct[FAN_CURVE_MAX_POINTS]  = {0, 25, 50, 75, 100};
  uint16_t freq[FAN_CURVE_MAX_POINTS] = {30, 30, 30, 30, 30}; // Hz, pontonként egyedi
  uint8_t sensor = 1; // 1=NTC1, 2=NTC2
};

struct ThermRelay {
  bool enabled = false;
  float ton = 30.0, toff = 25.0; // ton<toff: fűtés (BE hidegben), ton>=toff: hűtés (BE melegben)
  uint8_t sensor = 1;
  bool state = false;
};

ThermFan   thermF1, thermF2;

ThermRelay thermR1, thermR2;

// ============================================================
// KAPCSOLÓ(+HŐMÉRSÉKLET)-LOGIKA (mode=3 vagy mode=4), CSATORNÁNKÉNT
// axis2Mode: 0 = a 2. tengely K2 kapcsoló (mode 4, "2 kapcsoló")
//            2 = nincs 2. tengely, csak K1 számít (mode 3, "1 kapcsoló+1 hőmérő")
// index: bit1=K1 zárva, bit0=axis2 (K2 zárva, vagy axis2Mode=2 esetén mindig 0).
// Ventilátornál a val[] 0-100 (%), VAGY -1 = "az 5 pontos görbe eredménye
// érvényesüljön" (lásd interpolateFanCurve). Relénél val[] 1=mindig húzzon,
// 0=a normál hőmérséklet-termosztát dönt (nem íródik felül). Ha egy csatorna
// logikája aktív, a loop()-ban applySwitchLogic() az applyThermostat() UTÁN
// fut, tehát felülbírálja azt (kivéve a "kövesse a termosztátot" eseteket).
// ============================================================
struct SwitchLogicChannel {
  bool enabled = false;
  int val[4] = {-1, -1, -1, -1};
  uint8_t axis2Mode = 0;
};

SwitchLogicChannel logicF1, logicF2, logicR1, logicR2;

struct TimerSlot { uint8_t h = 0, m = 0; int v = 0; bool used = false; };

struct TimerSet {
  int days = 0;
  TimerSlot slots[MAX_SLOTS];
  int count = 0;
};

TimerSet ftimerF1, ftimerF2, rtimerR1, rtimerR2;

int lastAppliedMinute = -1;

enum ButtonEvent : uint8_t { BTN_NONE, BTN_DOWN, BTN_UP, BTN_SELECT, BTN_BACK };

struct ButtonState {
  bool stable = false, raw = false;
  unsigned long changeT = 0, pressT = 0;
  bool longFired = false, consumedBySelect = false, wasDown = false;
};

ButtonState btnL, btnR;

// ── Menü állapot ──
enum MenuScreen {
  MENU_NONE, MENU_MAIN, MENU_MODE_SET, MENU_TIME_SET,
  MENU_RGB_LIST, MENU_RGB_EDIT,
  MENU_FAN_SUB, MENU_FAN_MANUAL_PCT, MENU_FAN_SENSOR_SEL,
  MENU_FAN_CURVE_LIST, MENU_FAN_CURVE_POINT, MENU_FAN_LOGIC_LIST, MENU_FAN_LOGIC_EDIT,
  MENU_RELAY_SUB, MENU_RELAY_MANUAL, MENU_RELAY_SENSOR_SEL,
  MENU_RELAY_THERM_LIST, MENU_RELAY_THERM_EDIT, MENU_RELAY_LOGIC_LIST, MENU_RELAY_LOGIC_EDIT
};

// ── Generikus "cimke: ertek" lista sor + rajzoló ──
struct ValueRow { String label; String value; uint16_t valColor; };
