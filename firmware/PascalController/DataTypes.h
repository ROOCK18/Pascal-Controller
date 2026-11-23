// ============================================================
// DataTypes.h - Struktúrák (ThermFan, ThermRelay, TimerSet, SwitchLogicChannel, stb.) és a globális példányaik
// ============================================================
#pragma once

struct ThermRelay;
struct RelayTimer;

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

// Idő alapú relékapcsolás ("impulzus" logika): ha enabled, akkor amikor az
// automatika (termosztát/logika) az AKTUÁLIS kimenettől ELTÉRŐ állapotot
// kér (tehát ténylegesen VÁLTANI akarna), a relé a kért új állapotot
// PONTOSAN onDurationSec másodpercig tartja, majd MAGÁTÓL VISSZAÁLL arra
// az állapotra, ami a váltás ELŐTT volt (nem feltétlenül KI!). Ezután
// cooldownSec másodpercig SEMMILYEN automatika nem válthat újra, a relé a
// visszaállt állapotban marad. Ez MINDEN automatikánál elsőbbséget élvez -
// kivéve a Kézi üzemmódot, ami ezt a mechanizmust teljesen megkerüli
// (lásd setRelayAutomation).
struct RelayTimer {
  bool enabled = false;
  uint8_t timerMode = 0;       // 0=Fix ido (mindig pontosan ennyi ideig tart), 1=Max ido (koveti a jelet, csak felulrol korlatozva)
  // Melyik irányra vonatkozzon a mechanizmus:
  //   0 = BE  - csak akkor lép életbe, ha a relé BE-re kapcsolna
  //   1 = KI  - csak akkor lép életbe, ha a relé KI-re kapcsolna
  //   2 = MIND - mindkét irányú váltásra érvényes
  // A NEM felügyelt irányú váltás azonnal, korlátozás nélkül végrehajtódik.
  uint8_t stateFilter = 2;
  uint16_t onDurationSec = 30;
  // Zárolás a szakasz vége (lejárat/kényszerített visszaállás) UTÁN:
  //   0 = Nincs zárolás - azonnal újra reagálhat
  //   1 = Idő alapú - cooldownSec másodpercig nem reagál
  //   2 = Jelváltásig - addig nem reagál, amíg a forrás (termosztát/logika)
  //       akár egy pillanatra is vissza nem kéri a szakasz ELŐTTI állapotot
  //       (tehát amíg "meg nem akarja valami visszaváltani")
  uint8_t lockMode = 1;
  uint16_t cooldownSec = 300;  // csak lockMode==1 esetén használt
  bool timedActive = false;        // jelenleg az impulzus/kitérés szakaszban van
  bool cooldownActive = false;     // jelenleg az idő alapú zárolásban van (lockMode==1)
  bool waitingForReversal = false; // jelenleg a "jelváltásig" zárolásban van (lockMode==2)
  bool priorState = false;         // az állapot, amire a szakasz végén vissza kell állni
  unsigned long phaseStart = 0;
};

ThermFan   thermF1, thermF2;

ThermRelay thermR1, thermR2;
RelayTimer relayTimerR1, relayTimerR2;

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
  uint16_t freq[4] = {30, 30, 30, 30}; // csak ventilátor-logikánál használt, állapotonkénti PWM frekvencia
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

enum ButtonEvent : uint8_t { BTN_NONE, BTN_DOWN, BTN_UP, BTN_SELECT, BTN_BACK, BTN_LONG_UP, BTN_LONG_SELECT };

struct ButtonState {
  bool stable = false, raw = false;
  unsigned long changeT = 0, pressT = 0;
  bool longFired = false, consumedBySelect = false, wasDown = false;
  unsigned long lastRepeatT = 0; // hosszú nyomás közbeni automatikus ismétléshez
};

ButtonState btnL, btnR;

// Ha true, a MENU_*_EDIT (érték-szerkesztő) képernyőkön vagyunk: ilyenkor a
// LE gomb hosszú nyomása NEM lép ki a menüből (BTN_BACK), hanem 2 mp után
// automatikusan, ismételve csökkenti/növeli az értéket - lásd Buttons.h.
bool menuAllowsHoldRepeat = false;

// Ha true, a mindkét-gomb-együtt (SELECT) esemény csak akkor tüzel rövid
// SELECT-ként, ha RÖVIDEN engedik el - ha HOSSZAN (>~1,2mp) tartják nyomva
// mindkettőt, BTN_LONG_SELECT tüzel helyette. Ezt a WiFi jelszó-szerkesztő
// használja: rövid = karakter beírása, hosszú = csatlakozás indítása.
bool menuUsesLongSelect = false;

// ── Menü állapot ──
enum MenuScreen {
  MENU_NONE, MENU_MAIN,
  MENU_SETTINGS_SUB, MENU_MODE_LIST, MENU_COMP_LIST, MENU_COMP_EDIT,
  MENU_DISPLAY_SUB, MENU_BRIGHTNESS_EDIT, MENU_SLEEP_SEL, MENU_FACTORY_CONFIRM,
  MENU_TIME_LIST, MENU_TIME_EDIT, MENU_TIMEFORMAT_SEL, MENU_DATEFORMAT_SEL,
  MENU_RGB_LIST, MENU_RGB_EDIT,
  MENU_FAN_SUB, MENU_FAN_MANUAL_PCT, MENU_FAN_SENSOR_SEL,
  MENU_FAN_CURVE_LIST, MENU_FAN_CURVE_POINT, MENU_FAN_CURVE_FIELD_EDIT, MENU_FAN_LOGIC_LIST, MENU_FAN_LOGIC_EDIT, MENU_FAN_LOGIC_VAL_EDIT,
  MENU_FREQ_PRESET_SEL, MENU_FREQ_DIGIT_EDIT,
  MENU_RELAY_SUB, MENU_RELAY_SENSOR_SEL,
  MENU_RELAY_THERM_LIST, MENU_RELAY_THERM_EDIT, MENU_RELAY_LOGIC_LIST, MENU_RELAY_LOGIC_EDIT,
  MENU_RELAY_HOLD_LIST, MENU_RELAY_HOLD_EDIT, MENU_RELAY_LOCKMODE_SEL, MENU_RELAY_STATE_SEL,
  MENU_WIFI_SCANNING, MENU_WIFI_SCAN_LIST, MENU_WIFI_PASSWORD_EDIT, MENU_WIFI_CONNECTING
};

// ── Generikus "cimke: ertek" lista sor + rajzoló ──
struct ValueRow { String label; String value; uint16_t valColor; };
