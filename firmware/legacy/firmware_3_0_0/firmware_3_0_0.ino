// ============================================================
// Pascal-Controller firmware v1.2.0
//
// v1.2.0 VÁLTOZÁS (OTA HIBA / Update.end() javítás - lásd base
// firmware fejlécét a részletes indoklásért):
//   A flash írás (Update.write()) KIKERÜLT a BLE onWrite()
//   callback-ből egy queue-ba - a tényleges írás a loop()-ban
//   történik. Ez szünteti meg a callback-blokkolás miatti
//   csomagvesztést/torzulást, ami korábban Update.end()-nél
//   "nem bootolható image" hibát okozott.
//   Emellett: valós Update.errorString() visszaküldése hiba
//   esetén (ERROR:<ok>), és VER: automatikus küldése GET_STATE-re
//   is (az app soha nem küldött külön GET_VER-t, így a Settings
//   oldal firmware-verzió címkéje korábban sosem frissült).
//
// Funkciók: RGB, 2x ventilátor (PWM + frekvencia), 2x relé,
// termosztát (SET_THERM), alap idő szerinti időzítők
// (SET_FTIMER/SET_RTIMER), mód váltás (AUTO/MANUAL), BLE OTA.
// ============================================================

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <math.h>
#include <Update.h>
#include <queue>

#define FW_VERSION "3.0.0"

// --- TFT kijelző lábkiosztás (TTGO T-Display) ---
#define TFT_MOSI 19
#define TFT_SCLK 18
#define TFT_CS 5
#define TFT_DC 16
#define TFT_RST 23
#define TFT_BL 4
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// --- PIN definíciók ---
const int NTC1_PIN = 37;
const int NTC2_PIN = 38;
const int RGB_R_PIN = 2;
const int RGB_G_PIN = 17;
const int RGB_B_PIN = 15;
const int FAN1_PWM_PIN = 13;
const int FAN2_PWM_PIN = 12;
const int RELAY1_PIN = 33;
const int RELAY2_PIN = 32;

// "2 kapcsoló" mód fizikai bemenetei (kapcsolási logikához). Az ESP32
// 34-39 lábai csak bemenetek, NINCS belső pull-up rajtuk -> KÜLSŐ pull-up
// ellenállás szükséges (3.3V-ra), a kapcsoló GND-re köt: nyitva = HIGH,
// zárva (megnyomva/összezárva) = LOW.
// "1 kapcsoló+1 hőmérő" / "2 kapcsoló" mód fizikai bemenetei (kapcsolási
// logikához). ÁTHELYEZVE normál GPIO-kra (21, 14), mert a GPIO35-öt a
// beépített jobb gomb foglalja (lásd BTN_RIGHT lejjebb) - ezek a lábak
// támogatják a belső pull-up-ot, így KÜLSŐ ellenállás sem kell többé:
// nyitva = HIGH (belső pull-up húzza fel), zárva (GND-re köt) = LOW.
const int SWITCH1_PIN = 21;
const int SWITCH2_PIN = 14;

// Beépített fizikai gombok (menürendszerhez). Active-low.
// GPIO0: BAL gomb (= "lefelé" navigáció) - egyben boot strapping láb is,
// futás közben szabadon használható, csak flashelés alatt nem szabad
// lenyomva tartani. Belső pull-up szükséges.
// GPIO35: JOBB gomb (= "felfelé" navigáció) - csak bemenet, nincs belső
// pull-up, de a legtöbb TTGO T-Display NYÁK-on gyárilag van rajta külső
// pull-up (mivel ez az egyik beépített gomb lába).
const int BTN_LEFT  = 0;
const int BTN_RIGHT = 35;
const int FAN1_ON = 27;
const int FAN2_ON = 26;
const int RGB_ON = 25;

const int LED_PWM_FREQ = 250;
int FAN1_PWM_FREQ = 30;

// Auto/Kézi mód csatornánként: ha true (alapértelmezett), a termosztát/görbe/
// kapcsoló-logika vezérli az adott kimenetet; ha false, a felhasználó direkt
// parancsai (Ctrl oldal csúszka/kapcsoló) döntenek, az automatika szünetel.
bool fan1Auto = true, fan2Auto = true, relay1Auto = true, relay2Auto = true;
int FAN2_PWM_FREQ = 30;
const int PWM_RESOLUTION = 8;

// --- NTC paraméterek ---
const float VCC = 5;
const float ADC_MAX = 4095.0;
const float R_FIXED = 10000.0;
const float NTC_R0 = 10000.0;
const float NTC_T0 = 298.15;
const float NTC_BETA = 3950.0;

// --- BLE ---
BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic;
BLECharacteristic *pRxCharacteristic;
bool deviceConnected = false;
bool oldDeviceConnected = false;

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// --- Állapot ---
int pwmR = 0, pwmG = 0, pwmB = 0;
int pwmFan = 0, pwmFan2 = 0;
// Az app "STATE:<n>" / "SET_MODE:<n>" parancsa NEM auto/manual kapcsoló,
// hanem a fizikai szenzor-elrendezést választja (Data.xaml.cs ApplyLayoutMode):
// 0=2 hőmérő, 1=1 hőmérő, 2=DHT11 hő+pára, 3=1 kapcsoló+1 hőmérő, 4=2 kapcsoló.
// Ez a hardver 2 db NTC szenzorral szerelt -> alapértelmezett és fix: 0.
int currentMode = 0;

float ntc1Temperature = 0;
float ntc2Temperature = 0;

int hr = 12, min_t = 0, sec = 0;
unsigned long lastClockUpdate = 0;
unsigned long lastSensorRead = 0;
unsigned long lastDisplayDraw = 0;

// --- OTA ---
unsigned long fullSize = 0, currentSize = 0;
bool otaInProgress = false;
int lastOtaPct = -1;

#define MAX_OTA_CHUNK 128 // az app mindig max 128 byte-os chunkokat küld (lásd Settings.xaml.cs _otaMtu)
struct OtaPacket {
  uint8_t data[MAX_OTA_CHUNK];
  size_t len;
};
std::queue<OtaPacket> otaQueue;
portMUX_TYPE otaMux = portMUX_INITIALIZER_UNLOCKED;

// Előre deklarálás: az Arduino IDE a fájl elejére generál automatikus
// function prototípusokat, MÉG a lenti struct definíciók előtt. Ezek
// nélkül a "TimerSet"/"ThermFan"/"ThermRelay" típus ismeretlen lenne
// a generált prototípusokban -> fordítási hiba.
struct ThermFan;
struct ThermRelay;
struct TimerSlot;
struct TimerSet;
struct SwitchLogicChannel;
struct ButtonState;
enum ButtonEvent : uint8_t;

void clearOtaQueue() {
  portENTER_CRITICAL(&otaMux);
  while (!otaQueue.empty()) otaQueue.pop();
  portEXIT_CRITICAL(&otaMux);
}

void notifyLine(const String &s) {
  if (!deviceConnected) return;
  String withNl = s;
  if (!withNl.endsWith("\n")) withNl += "\n";
  pTxCharacteristic->setValue((uint8_t*)withNl.c_str(), withNl.length());
  pTxCharacteristic->notify();
}

// ============================================================
// TERMOSZTÁT (SET_THERM)
// ============================================================
#define FAN_CURVE_MAX_POINTS 5
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

// Páratartalom - TODO: ha lesz DHT11/DHT22 szenzor a NYÁK-on, itt kell
// ténylegesen kiolvasni (pl. dht.readHumidity()). Amíg nincs bekötve
// fizikai szenzor, ez a keret készen áll, de a forrás nem ad valós adatot.
float humidity = NAN;

// sensor: 1=NTC1, 2=NTC2, 3=Páratartalom (%)
float getSensorTemp(uint8_t sensor) {
  if (sensor == 3) return humidity;
  return (sensor == 2) ? ntc2Temperature : ntc1Temperature;
}

// Lineáris interpoláció a görbe pontjai között (a pontok hőmérséklet
// szerint növekvő sorrendben vannak tárolva - lásd handleSetTherm).
// count==1 esetén a görbe egy konstans %-ot ad, hőmérséklettől függetlenül.
int interpolateFanCurve(ThermFan &tf, float t) {
  uint8_t n = (tf.count < 1) ? 1 : (tf.count > FAN_CURVE_MAX_POINTS ? FAN_CURVE_MAX_POINTS : tf.count);
  if (n == 1) return tf.pct[0];
  if (t <= tf.temp[0]) return tf.pct[0];
  for (int i = 0; i < n - 1; i++) {
    if (t <= tf.temp[i + 1]) {
      float span = tf.temp[i + 1] - tf.temp[i];
      if (span <= 0) return tf.pct[i];
      float frac = (t - tf.temp[i]) / span;
      return (int)(tf.pct[i] + frac * ((int)tf.pct[i + 1] - (int)tf.pct[i]));
    }
  }
  return tf.pct[n - 1];
}

// A PWM frekvenciát NEM interpoláljuk folyamatosan (minden ledcDetach/
// ledcAttach hívás megszakítja/újraindítja a PWM-et - ha ez másodpercenként
// többször történne, az instabil/villódzó fordulatszámot adna). Ehelyett a
// hőmérséklethez LEGKÖZELEBBI pont frekvenciáját vesszük át.
uint16_t nearestFreqForCurve(ThermFan &tf, float t) {
  uint8_t n = (tf.count < 1) ? 1 : (tf.count > FAN_CURVE_MAX_POINTS ? FAN_CURVE_MAX_POINTS : tf.count);
  int nearest = 0;
  float best = fabs(t - tf.temp[0]);
  for (int i = 1; i < n; i++) {
    float d = fabs(t - tf.temp[i]);
    if (d < best) { best = d; nearest = i; }
  }
  return tf.freq[nearest];
}

// Közös segédfüggvény: a görbe alapján beállítja egy ventilátor %-át ÉS
// (ha szükséges) PWM frekvenciáját. channelNum: 1=Fan1, 2=Fan2.
// Ezt használja mind az applyThermostat(), mind a kapcsoló-logika "görbe
// szerint" (-1) esete.
void applyFanFromCurve(ThermFan &tf, int channelNum) {
  float t = getSensorTemp(tf.sensor);
  int pct = interpolateFanCurve(tf, t);
  uint16_t freq = nearestFreqForCurve(tf, t);
  int pwm = constrain((int)(pct * 2.55), 0, 255);

  if (channelNum == 1) {
    if (freq != FAN1_PWM_FREQ && freq >= 10 && freq <= 25000) {
      ledcDetach(FAN1_PWM_PIN);
      FAN1_PWM_FREQ = freq;
      ledcAttach(FAN1_PWM_PIN, FAN1_PWM_FREQ, PWM_RESOLUTION);
    }
    pwmFan = pwm;
    setPWM(3, pwmFan);
  } else {
    if (freq != FAN2_PWM_FREQ && freq >= 10 && freq <= 25000) {
      ledcDetach(FAN2_PWM_PIN);
      FAN2_PWM_FREQ = freq;
      ledcAttach(FAN2_PWM_PIN, FAN2_PWM_FREQ, PWM_RESOLUTION);
    }
    pwmFan2 = pwm;
    setPWM(4, pwmFan2);
  }
}

void applyThermostat() {
  // Minden csatorna a saját SET_THERM "enabled" flag-je ÉS a saját Auto/Kézi
  // (fan1Auto/fan2Auto/relay1Auto/relay2Auto) állapota alapján fut. Kézi
  // módban a Ctrl oldal direkt parancsai döntenek, az automatika szünetel.

  // --- Ventilátorok: 1-5 pontos, szakaszonként lineáris görbe (% és Hz is) ---
  if (thermF1.enabled && fan1Auto) applyFanFromCurve(thermF1, 1);
  if (thermF2.enabled && fan2Auto) applyFanFromCurve(thermF2, 2);

  // --- Relék: hiszterézises BE/KI kapcsolás ---
  // Ha ton < toff: FŰTÉS logika (BE ha hideg, KI ha túl meleg).
  // Ha ton >= toff: HŰTÉS logika (BE ha meleg, KI ha hideg).
  if (thermR1.enabled && relay1Auto) {
    float t = getSensorTemp(thermR1.sensor);
    if (thermR1.ton < thermR1.toff) { // fűtés
      if (!thermR1.state && t <= thermR1.ton)  { thermR1.state = true;  digitalWrite(RELAY1_PIN, HIGH); }
      if (thermR1.state  && t >= thermR1.toff) { thermR1.state = false; digitalWrite(RELAY1_PIN, LOW); }
    } else { // hűtés
      if (!thermR1.state && t >= thermR1.ton)  { thermR1.state = true;  digitalWrite(RELAY1_PIN, HIGH); }
      if (thermR1.state  && t <= thermR1.toff) { thermR1.state = false; digitalWrite(RELAY1_PIN, LOW); }
    }
  }
  if (thermR2.enabled && relay2Auto) {
    float t = getSensorTemp(thermR2.sensor);
    if (thermR2.ton < thermR2.toff) { // fűtés
      if (!thermR2.state && t <= thermR2.ton)  { thermR2.state = true;  digitalWrite(RELAY2_PIN, HIGH); }
      if (thermR2.state  && t >= thermR2.toff) { thermR2.state = false; digitalWrite(RELAY2_PIN, LOW); }
    } else { // hűtés
      if (!thermR2.state && t >= thermR2.ton)  { thermR2.state = true;  digitalWrite(RELAY2_PIN, HIGH); }
      if (thermR2.state  && t <= thermR2.toff) { thermR2.state = false; digitalWrite(RELAY2_PIN, LOW); }
    }
  }
}

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

void applyLogicChannel(SwitchLogicChannel &ch, bool isFan, int channelNum) {
  if (!ch.enabled) return;
  bool sw1closed = digitalRead(SWITCH1_PIN) == LOW;
  int axis2 = (ch.axis2Mode == 2) ? 0 : ((digitalRead(SWITCH2_PIN) == LOW) ? 1 : 0);
  int idx = (sw1closed ? 2 : 0) | axis2;
  int v = ch.val[idx];

  if (isFan) {
    ThermFan &tf = (channelNum == 1) ? thermF1 : thermF2;
    if (v < 0) {
      // "Görbe szerint" - a % ÉS a PWM frekvencia is az 5 pontos görbéből jön
      applyFanFromCurve(tf, channelNum);
    } else {
      int pwm = constrain((int)(v * 2.55), 0, 255);
      if (channelNum == 1) { pwmFan = pwm; setPWM(3, pwmFan); }
      else                 { pwmFan2 = pwm; setPWM(4, pwmFan2); }
    }
  } else {
    // v==0: "kövesse a hőmérséklet-termosztátot" -> nem nyúlunk a relé lábhoz
    // (azt már beállította applyThermostat(), ha az a csatorna is aktív).
    if (v != 0) {
      int pin = (channelNum == 1) ? RELAY1_PIN : RELAY2_PIN;
      digitalWrite(pin, HIGH);
    }
  }
}

void applySwitchLogic() {
  if (fan1Auto)    applyLogicChannel(logicF1, true, 1);
  if (fan2Auto)    applyLogicChannel(logicF2, true, 2);
  if (relay1Auto)  applyLogicChannel(logicR1, false, 1);
  if (relay2Auto)  applyLogicChannel(logicR2, false, 2);
}

SwitchLogicChannel &getLogicChannel(const String &target) {
  if (target == "F1") return logicF1;
  if (target == "F2") return logicF2;
  if (target == "R2") return logicR2;
  return logicR1; // "R1" és minden más esetben (védelem)
}

// Parancs: SET_LOGIC:<F1|F2|R1|R2>:<0/1>:<v_OO>:<v_OC>:<v_CO>:<v_CC>:<axis2Mode>
void handleSetLogic(const String &body) {
  String tok[7]; int n = 0, start = 0;
  while (n < 7) {
    int c = body.indexOf(':', start);
    if (c < 0) { tok[n++] = body.substring(start); break; }
    tok[n++] = body.substring(start, c);
    start = c + 1;
  }
  if (n < 6) return;
  String target = tok[0];
  if (target != "F1" && target != "F2" && target != "R1" && target != "R2") return;

  SwitchLogicChannel &ch = getLogicChannel(target);
  bool isFan = (target == "F1" || target == "F2");
  ch.enabled = tok[1].toInt() != 0;
  for (int i = 0; i < 4; i++) {
    int v = tok[2 + i].toInt();
    ch.val[i] = isFan ? (v < 0 ? -1 : constrain(v, 0, 100)) : (v != 0 ? 1 : 0);
  }
  ch.axis2Mode = (n >= 7) ? (uint8_t)tok[6].toInt() : 0;
  notifyLine("LOGIC_OK");
}

void handleGetLogic(const String &target) {
  if (target != "F1" && target != "F2" && target != "R1" && target != "R2") return;
  SwitchLogicChannel &ch = getLogicChannel(target);
  String out = "LOAD_LOGIC:" + target + ":" + String(ch.enabled ? 1 : 0);
  for (int i = 0; i < 4; i++) out += ":" + String(ch.val[i]);
  out += ":" + String(ch.axis2Mode);
  notifyLine(out);
}

void handleDelLogic(const String &target) {
  if (target != "F1" && target != "F2" && target != "R1" && target != "R2") return;
  getLogicChannel(target) = SwitchLogicChannel();
  notifyLine("LOGIC_OK");
}

// Egyszerű beszúrásos rendezés hőmérséklet szerint növekvő sorrendbe -
// a pct-értékeket a temp-tel együtt mozgatja, hogy a párosítás megmaradjon.
// Csak az első "n" (aktív) pontot rendezi.
void sortFanCurve(ThermFan &tf, uint8_t n) {
  for (int i = 1; i < n; i++) {
    float tKey = tf.temp[i]; uint8_t pKey = tf.pct[i];
    int j = i - 1;
    while (j >= 0 && tf.temp[j] > tKey) {
      tf.temp[j + 1] = tf.temp[j];
      tf.pct[j + 1]  = tf.pct[j];
      j--;
    }
    tf.temp[j + 1] = tKey;
    tf.pct[j + 1]  = pKey;
  }
}

// Parancs (ventilátor): SET_THERM:<F1|F2>:<0/1>:<sensor>:<n>:<t1>:<p1>:<f1>:...:<tn>:<pn>:<fn>
//   n = 1..5 aktív pont, minden pontnak SAJÁT PWM frekvenciája (Hz) is van.
// Parancs (relé):       SET_THERM:<R1|R2>:<0/1>:<ton>:<toff>:<sensor>
void handleSetTherm(const String &body) {
  // Tokenizálás ':' mentén (max 20 mező elég mindkét formátumhoz: 4 fejléc + 5x3 pont)
  String tok[20]; int n = 0;
  int start = 0;
  while (n < 20) {
    int c = body.indexOf(':', start);
    if (c < 0) { tok[n++] = body.substring(start); break; }
    tok[n++] = body.substring(start, c);
    start = c + 1;
  }
  if (n < 1) return;
  String target = tok[0];

  if (target == "F1" || target == "F2") {
    if (n < 5) return; // target,en,sensor,count,min 1x(t,p,f) = 5
    bool en = tok[1].toInt() != 0;
    uint8_t sensor = tok[2].toInt(); if (sensor < 1 || sensor > 3) sensor = 1;
    uint8_t cnt = tok[3].toInt();
    if (cnt < 1) cnt = 1;
    if (cnt > FAN_CURVE_MAX_POINTS) cnt = FAN_CURVE_MAX_POINTS;

    int needed = 4 + cnt * 3; // pontonként (t,p,f) = 3 mező
    if (n < needed) return;

    ThermFan &tf = (target == "F1") ? thermF1 : thermF2;
    tf.enabled = en;
    tf.sensor = sensor;
    tf.count = cnt;
    for (int i = 0; i < cnt; i++) {
      tf.temp[i] = tok[4 + i * 3].toFloat();
      tf.pct[i]  = (uint8_t)constrain(tok[5 + i * 3].toInt(), 0, 100);
      long f = tok[6 + i * 3].toInt();
      tf.freq[i] = (uint16_t)constrain(f, 10, 25000);
    }
    sortFanCurve(tf, cnt);
    notifyLine("THERM_OK");
  }
  else if (target == "R1" || target == "R2") {
    if (n < 5) return;
    bool en = tok[1].toInt() != 0;
    float v1 = tok[2].toFloat();
    float v2 = tok[3].toFloat();
    uint8_t sensor = tok[4].toInt(); if (sensor < 1 || sensor > 3) sensor = 1;
    ThermRelay &tr = (target == "R1") ? thermR1 : thermR2;
    tr.enabled = en; tr.ton = v1; tr.toff = v2; tr.sensor = sensor;
    notifyLine("THERM_OK");
  }
}

void handleGetTherm(const String &target) {
  String out = "LOAD_THERM:" + target + ":";
  if (target == "F1" || target == "F2") {
    ThermFan &tf = (target == "F1") ? thermF1 : thermF2;
    out += String(tf.enabled ? 1 : 0) + ":" + String(tf.sensor) + ":" + String(tf.count);
    for (int i = 0; i < tf.count; i++)
      out += ":" + String(tf.temp[i], 1) + ":" + String(tf.pct[i]) + ":" + String(tf.freq[i]);
  }
  else if (target == "R1" || target == "R2") {
    ThermRelay &tr = (target == "R1") ? thermR1 : thermR2;
    out += String(tr.enabled ? 1 : 0) + ":" + String(tr.ton, 1) + ":" + String(tr.toff, 1) + ":" + String(tr.sensor);
  }
  else return;
  notifyLine(out);
}

// Parancs: DEL_THERM:<F1|F2|R1|R2> - alapállapotba állítja és kikapcsolja
void handleDelTherm(const String &target) {
  if (target == "F1") thermF1 = ThermFan();
  else if (target == "F2") thermF2 = ThermFan();
  else if (target == "R1") thermR1 = ThermRelay();
  else if (target == "R2") thermR2 = ThermRelay();
  else return;
  notifyLine("THERM_OK");
}

// ============================================================
// EGYSZERŰ IDŐZÍTŐK (FTIMER / RTIMER) - tárolás + visszaigazolás
// ============================================================
#define MAX_SLOTS 8
struct TimerSlot { uint8_t h = 0, m = 0; int v = 0; bool used = false; };
struct TimerSet {
  int days = 0;
  TimerSlot slots[MAX_SLOTS];
  int count = 0;
};
TimerSet ftimerF1, ftimerF2, rtimerR1, rtimerR2;
int lastAppliedMinute = -1;

void parseSlots(const String &slotData, TimerSet &ts) {
  ts.count = 0;
  int start = 0;
  while (start < (int)slotData.length() && ts.count < MAX_SLOTS) {
    int bar = slotData.indexOf('|', start);
    String piece = (bar < 0) ? slotData.substring(start) : slotData.substring(start, bar);
    int c1 = piece.indexOf(':');
    int c2 = piece.indexOf(':', c1 + 1);
    if (c1 > 0 && c2 > c1) {
      ts.slots[ts.count].h = piece.substring(0, c1).toInt();
      ts.slots[ts.count].m = piece.substring(c1 + 1, c2).toInt();
      ts.slots[ts.count].v = piece.substring(c2 + 1).toInt();
      ts.slots[ts.count].used = true;
      ts.count++;
    }
    if (bar < 0) break;
    start = bar + 1;
  }
}

// Parancs: SET_FTIMER:<F1|F2>:<days>:<H>:<M>:<pct>|...
void handleSetFTimer(const String &body) {
  int p1 = body.indexOf(':');
  int p2 = body.indexOf(':', p1 + 1);
  if (p1 < 0) return;
  String target = body.substring(0, p1);
  int rest2 = (p2 < 0) ? body.length() : p2;
  int days = body.substring(p1 + 1, rest2).toInt();
  String slotData = (p2 < 0) ? "" : body.substring(p2 + 1);

  TimerSet &ts = (target == "F1") ? ftimerF1 : ftimerF2;
  ts.days = days;
  parseSlots(slotData, ts);
  notifyLine("FTIMER_OK");
}

// Parancs: SET_RTIMER:<R1|R2>:<days>:<H>:<M>:<0/1>|...
void handleSetRTimer(const String &body) {
  int p1 = body.indexOf(':');
  int p2 = body.indexOf(':', p1 + 1);
  if (p1 < 0) return;
  String target = body.substring(0, p1);
  int rest2 = (p2 < 0) ? body.length() : p2;
  int days = body.substring(p1 + 1, rest2).toInt();
  String slotData = (p2 < 0) ? "" : body.substring(p2 + 1);

  TimerSet &ts = (target == "R1") ? rtimerR1 : rtimerR2;
  ts.days = days;
  parseSlots(slotData, ts);
  notifyLine("RTIMER_OK");
}

// Parancs: DEL_FTIMER:<F1|F2> - törli a slotokat és alaphelyzetbe áll
void handleDelFTimer(const String &target) {
  TimerSet &ts = (target == "F1") ? ftimerF1 : ftimerF2;
  ts = TimerSet();
  notifyLine("FTIMER_OK");
}

// Parancs: DEL_RTIMER:<R1|R2> - törli a slotokat és alaphelyzetbe áll
void handleDelRTimer(const String &target) {
  TimerSet &ts = (target == "R1") ? rtimerR1 : rtimerR2;
  ts = TimerSet();
  notifyLine("RTIMER_OK");
}

String buildLoadTimer(const String &prefix, const String &target, TimerSet &ts) {
  String out = prefix + target + ":" + String(ts.days);
  for (int i = 0; i < ts.count; i++)
    out += ":" + String(ts.slots[i].h) + ":" + String(ts.slots[i].m) + ":" + String(ts.slots[i].v);
  return out;
}

void applyTimers() {
  if (hr * 60 + min_t == lastAppliedMinute) return; // csak percenként egyszer
  lastAppliedMinute = hr * 60 + min_t;

  for (int i = 0; i < ftimerF1.count; i++)
    if (ftimerF1.slots[i].h == hr && ftimerF1.slots[i].m == min_t) { pwmFan = constrain(ftimerF1.slots[i].v * 255 / 100, 0, 255); setPWM(3, pwmFan); }
  for (int i = 0; i < ftimerF2.count; i++)
    if (ftimerF2.slots[i].h == hr && ftimerF2.slots[i].m == min_t) { pwmFan2 = constrain(ftimerF2.slots[i].v * 255 / 100, 0, 255); setPWM(4, pwmFan2); }
  for (int i = 0; i < rtimerR1.count; i++)
    if (rtimerR1.slots[i].h == hr && rtimerR1.slots[i].m == min_t) digitalWrite(RELAY1_PIN, rtimerR1.slots[i].v ? HIGH : LOW);
  for (int i = 0; i < rtimerR2.count; i++)
    if (rtimerR2.slots[i].h == hr && rtimerR2.slots[i].m == min_t) digitalWrite(RELAY2_PIN, rtimerR2.slots[i].v ? HIGH : LOW);
}

// ============================================================
// ÓRA / PWM
// ============================================================
void updateClock() {
  if (millis() - lastClockUpdate >= 1000) {
    lastClockUpdate = millis();
    sec++;
    if (sec >= 60) { sec = 0; min_t++; }
    if (min_t >= 60) { min_t = 0; hr++; }
    if (hr >= 24) hr = 0;
  }
}

void setupPWM() {
  ledcAttach(RGB_R_PIN, LED_PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(RGB_G_PIN, LED_PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(RGB_B_PIN, LED_PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(FAN1_PWM_PIN, FAN1_PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(FAN2_PWM_PIN, FAN2_PWM_FREQ, PWM_RESOLUTION);
  ledcWrite(RGB_R_PIN, 0); ledcWrite(RGB_G_PIN, 0); ledcWrite(RGB_B_PIN, 0);
  ledcWrite(FAN1_PWM_PIN, 0); ledcWrite(FAN2_PWM_PIN, 0);
}

void setPWM(int channel, int duty) {
  duty = constrain(duty, 0, 255);
  switch (channel) {
    case 0: pwmR = duty; ledcWrite(RGB_R_PIN, duty); break;
    case 1: pwmG = duty; ledcWrite(RGB_G_PIN, duty); break;
    case 2: pwmB = duty; ledcWrite(RGB_B_PIN, duty); break;
    case 3: pwmFan = duty; ledcWrite(FAN1_PWM_PIN, duty); break;
    case 4: pwmFan2 = duty; ledcWrite(FAN2_PWM_PIN, duty); break;
  }
}

void updatePWMOutputs() {
  setPWM(0, pwmR); setPWM(1, pwmG); setPWM(2, pwmB);
  setPWM(3, pwmFan); setPWM(4, pwmFan2);
}

// ============================================================
// NTC
// ============================================================
float readNTCTemperature(int ntcPin) {
  int adcValue = analogRead(ntcPin);
  if (adcValue <= 0) adcValue = 1;
  float voltage = adcValue * VCC / ADC_MAX;
  float rNTC = (voltage * R_FIXED) / (VCC - voltage);
  float steinhart = log(rNTC / NTC_R0) / NTC_BETA + 1.0 / NTC_T0;
  return (1.0 / steinhart) - 273.15;
}

// ============================================================
// KIJELZŐ
// ============================================================
// ============================================================
// BEÉPÍTETT GOMBOK (BAL=GPIO0="lefelé", JOBB=GPIO35="felfelé")
// és ON-DEVICE MENÜRENDSZER (alkalmazás nélkül is minden beállítható)
// ============================================================
#define BTN_DEBOUNCE_MS 30
#define BTN_LONG_MS     600
#define MENU_TIMEOUT_MS 30000

enum ButtonEvent : uint8_t { BTN_NONE, BTN_DOWN, BTN_UP, BTN_SELECT, BTN_BACK };

struct ButtonState {
  bool stable = false, raw = false;
  unsigned long changeT = 0, pressT = 0;
  bool longFired = false, consumedBySelect = false, wasDown = false;
};
ButtonState btnL, btnR;
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

// ── Menü állapot ──
enum MenuScreen {
  MENU_NONE, MENU_MAIN, MENU_MODE_SET, MENU_TIME_SET, MENU_RGB,
  MENU_FAN_SUB, MENU_FAN_MANUAL_PCT, MENU_FAN_CURVE_COUNT, MENU_FAN_CURVE_POINT,
  MENU_RELAY_SUB, MENU_RELAY_MANUAL, MENU_RELAY_THERM
};
MenuScreen menuScreen = MENU_NONE;
int menuCursor = 0;
int menuFanTarget = 1;     // 1 vagy 2
int menuRelayTarget = 1;   // 1 vagy 2
int menuEditPoint = 0;     // görbe-pont szerkesztésénél az aktuális pont indexe
int menuEditField = 0;     // 0=T,1=%,2=Hz (görbe) VAGY 0=BE,1=KI,2=szenzor (relé)
int menuTimeField = 0;     // 0=óra,1=perc
unsigned long lastMenuActivity = 0;

void menuTouch() { lastMenuActivity = millis(); }

const char* MAIN_ITEMS[] = { "Mod", "Ido", "RGB szin", "Ventilator 1", "Ventilator 2", "Rele 1", "Rele 2" };
const int MAIN_COUNT = 7;
const char* FAN_ITEMS[] = { "Auto/Kezi", "Kezi %", "Gorbe pontjai", "Vissza" };
const int FAN_COUNT = 4;
const char* RELAY_ITEMS[] = { "Auto/Kezi", "Kezi Be/Ki", "Termosztat", "Vissza" };
const int RELAY_COUNT = 4;

void drawMenuList(const char* title, const char* items[], int count, int cursor) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setCursor(2, 0);
  tft.println(title);
  tft.drawFastHLine(0, 17, 240, ST77XX_WHITE);
  int y = 20;
  for (int i = 0; i < count; i++) {
    tft.setCursor(2, y);
    if (i == cursor) tft.setTextColor(ST77XX_BLACK, ST77XX_YELLOW);
    else             tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.print(i == cursor ? ">" : " ");
    tft.println(items[i]);
    y += 16;
  }
}

void drawEditScreen(const char* title, const String &line1, const String &line2 = "") {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setCursor(2, 0);
  tft.println(title);
  tft.drawFastHLine(0, 17, 240, ST77XX_WHITE);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setCursor(2, 30);
  tft.println(line1);
  if (line2.length() > 0) { tft.setCursor(2, 55); tft.println(line2); }
  tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
  tft.setCursor(2, 110);
  tft.println("Fel/Le=ertek  Dupla=OK");
}

// Aktuálisan szerkesztett ThermFan/ThermRelay referenciák a menuFanTarget/
// menuRelayTarget alapján.
ThermFan &curThermFan()   { return (menuFanTarget == 1) ? thermF1 : thermF2; }
ThermRelay &curThermRelay() { return (menuRelayTarget == 1) ? thermR1 : thermR2; }
bool &curFanAuto()   { return (menuFanTarget == 1) ? fan1Auto : fan2Auto; }
bool &curRelayAuto() { return (menuRelayTarget == 1) ? relay1Auto : relay2Auto; }

void enterMenu() {
  menuScreen = MENU_MAIN;
  menuCursor = 0;
  menuTouch();
}

void exitMenuToIdle() {
  menuScreen = MENU_NONE;
}

void drawMenu() {
  switch (menuScreen) {
    case MENU_MAIN:
      drawMenuList("FOMENU", MAIN_ITEMS, MAIN_COUNT, menuCursor);
      break;
    case MENU_MODE_SET: {
      const char* modeNames[] = {"2 homero", "1 homero", "Paratartalom", "1 kapcs+1 homero", "2 kapcsolo"};
      drawEditScreen("MOD VALASZTAS", String(currentMode) + ": " + modeNames[currentMode]);
      break;
    }
    case MENU_TIME_SET:
      drawEditScreen("IDO BEALLITAS",
        String(menuTimeField == 0 ? "[" : "") + (hr < 10 ? "0" : "") + String(hr) + String(menuTimeField == 0 ? "]" : "") + ":" +
        String(menuTimeField == 1 ? "[" : "") + (min_t < 10 ? "0" : "") + String(min_t) + String(menuTimeField == 1 ? "]" : ""));
      break;
    case MENU_RGB:
      drawEditScreen("RGB SZIN",
        String(menuEditField == 0 ? "[R:" : "R:") + String(pwmR) + String(menuEditField == 0 ? "]" : ""),
        String(menuEditField == 1 ? "[G:" : "G:") + String(pwmG) + String(menuEditField == 1 ? "]" : "") + "  " +
        String(menuEditField == 2 ? "[B:" : "B:") + String(pwmB) + String(menuEditField == 2 ? "]" : ""));
      break;
    case MENU_FAN_SUB:
      drawMenuList(menuFanTarget == 1 ? "VENTILATOR 1" : "VENTILATOR 2", FAN_ITEMS, FAN_COUNT, menuCursor);
      break;
    case MENU_FAN_MANUAL_PCT:
      drawEditScreen("KEZI FORDULATSZAM", String(menuFanTarget == 1 ? pwmFan : pwmFan2) + " (nyers 0-255)");
      break;
    case MENU_FAN_CURVE_COUNT:
      drawEditScreen("GORBE PONTOK SZAMA", String(curThermFan().count) + " pont");
      break;
    case MENU_FAN_CURVE_POINT: {
      ThermFan &tf = curThermFan();
      String fieldName = menuEditField == 0 ? "Homerseklet (C)" : (menuEditField == 1 ? "Fordulatszam (%)" : "Frekvencia (Hz)");
      String val = menuEditField == 0 ? String(tf.temp[menuEditPoint], 1) :
                   (menuEditField == 1 ? String(tf.pct[menuEditPoint]) : String(tf.freq[menuEditPoint]));
      drawEditScreen("PONT " + String(menuEditPoint + 1) + "/" + String(tf.count), fieldName, val);
      break;
    }
    case MENU_RELAY_SUB:
      drawMenuList(menuRelayTarget == 1 ? "RELE 1" : "RELE 2", RELAY_ITEMS, RELAY_COUNT, menuCursor);
      break;
    case MENU_RELAY_MANUAL: {
      int pin = (menuRelayTarget == 1) ? RELAY1_PIN : RELAY2_PIN;
      drawEditScreen("KEZI RELE", digitalRead(pin) ? "BEKAPCSOLVA" : "KIKAPCSOLVA");
      break;
    }
    case MENU_RELAY_THERM: {
      ThermRelay &tr = curThermRelay();
      String fieldName = menuEditField == 0 ? "Bekapcsol (C)" : (menuEditField == 1 ? "Kikapcsol (C)" : "Erzekelo");
      String val = menuEditField == 0 ? String(tr.ton, 1) :
                   (menuEditField == 1 ? String(tr.toff, 1) : (tr.sensor == 2 ? "NTC2" : "NTC1"));
      drawEditScreen("RELE TERMOSZTAT", fieldName, val);
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
          case 2: menuScreen = MENU_RGB; menuEditField = 0; break;
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

    case MENU_RGB:
      if (ev == BTN_UP || ev == BTN_DOWN) {
        int delta = (ev == BTN_UP ? 5 : -5);
        if (menuEditField == 0) pwmR = constrain(pwmR + delta, 0, 255);
        else if (menuEditField == 1) pwmG = constrain(pwmG + delta, 0, 255);
        else pwmB = constrain(pwmB + delta, 0, 255);
        updatePWMOutputs();
      }
      else if (ev == BTN_SELECT) {
        if (menuEditField < 2) menuEditField++;
        else { menuScreen = MENU_MAIN; menuCursor = 2; }
      }
      else if (ev == BTN_BACK) { menuScreen = MENU_MAIN; menuCursor = 2; }
      break;

    case MENU_FAN_SUB:
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % FAN_COUNT;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + FAN_COUNT) % FAN_COUNT;
      else if (ev == BTN_BACK) { menuScreen = MENU_MAIN; menuCursor = (menuFanTarget == 1) ? 3 : 4; }
      else if (ev == BTN_SELECT) {
        switch (menuCursor) {
          case 0: curFanAuto() = !curFanAuto(); break; // Auto/Kezi váltás azonnal
          case 1: menuScreen = MENU_FAN_MANUAL_PCT; break;
          case 2: menuScreen = MENU_FAN_CURVE_COUNT; break;
          case 3: menuScreen = MENU_MAIN; menuCursor = (menuFanTarget == 1) ? 3 : 4; break;
        }
      }
      break;

    case MENU_FAN_MANUAL_PCT: {
      int &pwm = (menuFanTarget == 1) ? pwmFan : pwmFan2;
      if (ev == BTN_UP) pwm = constrain(pwm + 5, 0, 255);
      else if (ev == BTN_DOWN) pwm = constrain(pwm - 5, 0, 255);
      if (ev == BTN_UP || ev == BTN_DOWN) setPWM(menuFanTarget == 1 ? 3 : 4, pwm);
      if (ev == BTN_SELECT || ev == BTN_BACK) { menuScreen = MENU_FAN_SUB; menuCursor = 1; }
      break;
    }

    case MENU_FAN_CURVE_COUNT: {
      ThermFan &tf = curThermFan();
      if (ev == BTN_UP) tf.count = (tf.count % FAN_CURVE_MAX_POINTS) + 1;
      else if (ev == BTN_DOWN) tf.count = ((tf.count + FAN_CURVE_MAX_POINTS - 2) % FAN_CURVE_MAX_POINTS) + 1;
      else if (ev == BTN_SELECT) { menuEditPoint = 0; menuEditField = 0; menuScreen = MENU_FAN_CURVE_POINT; }
      else if (ev == BTN_BACK) { menuScreen = MENU_FAN_SUB; menuCursor = 2; }
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
          menuEditField = 0;
          menuEditPoint++;
          if (menuEditPoint >= tf.count) {
            sortFanCurve(tf, tf.count);
            tf.enabled = true;
            menuScreen = MENU_FAN_SUB; menuCursor = 2;
          }
        }
      }
      else if (ev == BTN_BACK) { menuScreen = MENU_FAN_SUB; menuCursor = 2; }
      break;
    }

    case MENU_RELAY_SUB:
      if (ev == BTN_DOWN) menuCursor = (menuCursor + 1) % RELAY_COUNT;
      else if (ev == BTN_UP) menuCursor = (menuCursor - 1 + RELAY_COUNT) % RELAY_COUNT;
      else if (ev == BTN_BACK) { menuScreen = MENU_MAIN; menuCursor = (menuRelayTarget == 1) ? 5 : 6; }
      else if (ev == BTN_SELECT) {
        switch (menuCursor) {
          case 0: curRelayAuto() = !curRelayAuto(); break;
          case 1: menuScreen = MENU_RELAY_MANUAL; break;
          case 2: menuEditField = 0; menuScreen = MENU_RELAY_THERM; break;
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

    case MENU_RELAY_THERM: {
      ThermRelay &tr = curThermRelay();
      if (ev == BTN_UP || ev == BTN_DOWN) {
        float dir = (ev == BTN_UP) ? 1.0 : -1.0;
        if (menuEditField == 0) tr.ton = constrain(tr.ton + dir, -40, 120);
        else if (menuEditField == 1) tr.toff = constrain(tr.toff + dir, -40, 120);
        else tr.sensor = (tr.sensor == 1) ? 2 : 1;
      }
      else if (ev == BTN_SELECT) {
        if (menuEditField < 2) menuEditField++;
        else { tr.enabled = true; menuScreen = MENU_RELAY_SUB; menuCursor = 2; }
      }
      else if (ev == BTN_BACK) { menuScreen = MENU_RELAY_SUB; menuCursor = 2; }
      break;
    }
    default: break;
  }
}

void displayInfo() {
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setCursor(0, 0);
  tft.printf("%02d:%02d:%02d\n\n", hr, min_t, sec);

  tft.setTextColor(digitalRead(RGB_ON) ? ST77XX_GREEN : ST77XX_RED, ST77XX_BLACK);
  tft.printf("R:%03d G:%03d B:%03d\n", pwmR, pwmG, pwmB);
  tft.setTextColor(digitalRead(FAN1_ON) ? ST77XX_GREEN : ST77XX_RED, ST77XX_BLACK);
  tft.printf("F1:%03d", pwmFan);
  tft.setTextColor(digitalRead(FAN2_ON) ? ST77XX_GREEN : ST77XX_RED, ST77XX_BLACK);
  tft.printf("F2:%03d\n", pwmFan2);

  // Mód-függő sor: a szenzor-elrendezésnek megfelelő adatokat mutatja
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  switch (currentMode) {
    case 0: // 2 hőmérő
      tft.printf("T1:%2.1fC T2:%2.1fC\n", ntc1Temperature, ntc2Temperature);
      break;
    case 1: // 1 hőmérő
      tft.printf("T1:%2.1fC\n", ntc1Temperature);
      break;
    case 2: // páratartalom (nincs fizikai szenzor - lásd humidity TODO)
      tft.printf("T1:%2.1fC H:--%%\n", ntc1Temperature);
      break;
    case 3: // 1 kapcsoló+1 hőmérő
      tft.printf("T1:%2.1fC K1:%s\n", ntc1Temperature, digitalRead(SWITCH1_PIN) == LOW ? "Zart" : "Nyit");
      break;
    case 4: // 2 kapcsoló
      tft.printf("K1:%s K2:%s\n", digitalRead(SWITCH1_PIN) == LOW ? "Zart" : "Nyit",
                                    digitalRead(SWITCH2_PIN) == LOW ? "Zart" : "Nyit");
      break;
  }

  tft.print("Relay: ");
  tft.setTextColor(digitalRead(RELAY1_PIN) ? ST77XX_GREEN : ST77XX_RED, ST77XX_BLACK);
  tft.print("R1 ");
  tft.setTextColor(digitalRead(RELAY2_PIN) ? ST77XX_GREEN : ST77XX_RED, ST77XX_BLACK);
  tft.println("R2");
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.printf("Mode:%d BLE:%s", currentMode, deviceConnected ? "CON" : "DISC");
}

void drawOtaStatus(const char* line1, const String &line2) {
  tft.fillScreen(ST77XX_BLUE);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println(line1);
  tft.setCursor(10, 40);
  tft.println(line2);
}

// ============================================================
// OTA feldolgozás - a queue-t a loop() üríti, NEM a BLE callback!
// ============================================================
void otaFail(const String &reason) {
  Serial.printf("OTA hiba: %s\n", reason.c_str());
  Update.abort();
  otaInProgress = false;
  clearOtaQueue();
  notifyLine("ERROR:" + reason);
  drawOtaStatus("OTA HIBA!", reason);
}

void processOtaQueue() {
  int processed = 0;
  while (processed < 8) {
    OtaPacket packet;
    bool has = false;
    portENTER_CRITICAL(&otaMux);
    if (!otaQueue.empty()) { packet = otaQueue.front(); otaQueue.pop(); has = true; }
    portEXIT_CRITICAL(&otaMux);
    if (!has) break;

    if (Update.write(packet.data, packet.len) != packet.len) {
      otaFail(String(Update.errorString()));
      return;
    }
    currentSize += packet.len;
    processed++;

    int pct = fullSize > 0 ? (int)((currentSize * 100) / fullSize) : 0;
    if (pct != lastOtaPct) {
      lastOtaPct = pct;
      int barWidth = (pct * 220) / 100;
      tft.fillRect(10, 80, 220, 20, ST77XX_BLACK);
      tft.fillRect(10, 80, barWidth, 20, ST77XX_GREEN);
      tft.fillRect(10, 105, 220, 20, ST77XX_BLUE);
      tft.setCursor(10, 105);
      tft.printf("%d%%", pct);
    }

    if (currentSize >= fullSize) {
      if (Update.end(true)) {
        notifyLine("OK_RESTART");
        drawOtaStatus("OTA KESZ!", "Ujrainditas...");
        delay(1500);
        ESP.restart();
      } else {
        otaFail(String(Update.errorString()));
      }
      return;
    }
  }
}

// ============================================================
// BLE CALLBACKS
// ============================================================
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s) {
    deviceConnected = true;
    Serial.println("BLE eszkoz csatlakozott");
  }
  void onDisconnect(BLEServer* s) {
    deviceConnected = false;
    if (otaInProgress) {
      Update.abort();
      otaInProgress = false;
      clearOtaQueue();
      Serial.println("OTA megszakadt: BLE lecsatlakozott");
    }
    Serial.println("BLE eszkoz szetkapcsolodott");
    s->getAdvertising()->start();
  }
};

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    uint8_t* data = pCharacteristic->getData();
    size_t len = pCharacteristic->getLength();
    if (len == 0) return;

    // ---------------- OTA bináris adat: csak queue-ba másolás ----------------
    // A tényleges flash írás a loop()-ban (processOtaQueue) történik, hogy a
    // BLE stack callback taskja soha ne blokkolódjon flash művelet miatt.
    if (otaInProgress) {
      if (len > MAX_OTA_CHUNK) len = MAX_OTA_CHUNK; // védelem, elméletileg nem fordulhat elő
      OtaPacket packet;
      packet.len = len;
      memcpy(packet.data, data, len);
      portENTER_CRITICAL(&otaMux);
      otaQueue.push(packet);
      portEXIT_CRITICAL(&otaMux);
      return;
    }

    // ---------------- Szöveges parancsok ----------------
    String receivedString;
    receivedString.reserve(len + 1);
    for (size_t i = 0; i < len; i++) receivedString += (char)data[i];
    receivedString.trim();

    if (receivedString.startsWith("UPDATE:")) {
      // "UPDATE:<total>:<chunkSize>" - a toInt() a ':'-nál megall,
      // igy csak a total meretet olvassuk ki.
      fullSize = receivedString.substring(7).toInt();
      clearOtaQueue();
      if (fullSize > 0 && Update.begin(fullSize)) {
        otaInProgress = true;
        currentSize = 0;
        lastOtaPct = -1;
        notifyLine("READY");
        drawOtaStatus("FIRMWARE UPDATE...", String(fullSize / 1024) + " KB");
      } else {
        notifyLine("ERROR:" + String(Update.errorString()));
        drawOtaStatus("OTA HIBA!", "Update.begin()");
      }
      return;
    }

    if (receivedString.startsWith("#") && receivedString.length() == 7) {
      pwmR = (int)strtol(receivedString.substring(1, 3).c_str(), NULL, 16);
      pwmG = (int)strtol(receivedString.substring(3, 5).c_str(), NULL, 16);
      pwmB = (int)strtol(receivedString.substring(5, 7).c_str(), NULL, 16);
      updatePWMOutputs();
    }
    else if (receivedString.startsWith("F1P") && receivedString.length() == 7) {
      long f = strtol(receivedString.substring(3, 7).c_str(), NULL, 16);
      if (f >= 10 && f <= 25000) {
        ledcDetach(FAN1_PWM_PIN);
        FAN1_PWM_FREQ = f;
        ledcAttach(FAN1_PWM_PIN, FAN1_PWM_FREQ, PWM_RESOLUTION);
        setPWM(3, pwmFan);
      }
    }
    else if (receivedString.startsWith("F2P") && receivedString.length() == 7) {
      long f = strtol(receivedString.substring(3, 7).c_str(), NULL, 16);
      if (f >= 10 && f <= 25000) {
        ledcDetach(FAN2_PWM_PIN);
        FAN2_PWM_FREQ = f;
        ledcAttach(FAN2_PWM_PIN, FAN2_PWM_FREQ, PWM_RESOLUTION);
        setPWM(4, pwmFan2);
      }
    }
    else if (receivedString.startsWith("F") && receivedString.length() == 5) {
      pwmFan = (int)strtol(receivedString.substring(1, 3).c_str(), NULL, 16);
      pwmFan2 = (int)strtol(receivedString.substring(3, 5).c_str(), NULL, 16);
      setPWM(3, pwmFan);
      setPWM(4, pwmFan2);
    }
    else if (receivedString == "RGB_1") digitalWrite(RGB_ON, HIGH);
    else if (receivedString == "RGB_0") digitalWrite(RGB_ON, LOW);
    else if (receivedString == "F1_1")  digitalWrite(FAN1_ON, HIGH);
    else if (receivedString == "F1_0")  digitalWrite(FAN1_ON, LOW);
    else if (receivedString == "F2_1")  digitalWrite(FAN2_ON, HIGH);
    else if (receivedString == "F2_0")  digitalWrite(FAN2_ON, LOW);
    else if (receivedString == "R1_1")  digitalWrite(RELAY1_PIN, HIGH);
    else if (receivedString == "R1_0")  digitalWrite(RELAY1_PIN, LOW);
    else if (receivedString == "R2_1")  digitalWrite(RELAY2_PIN, HIGH);
    else if (receivedString == "R2_0")  digitalWrite(RELAY2_PIN, LOW);
    else if (receivedString.startsWith("TIME:")) {
      hr    = receivedString.substring(5, 7).toInt();
      min_t = receivedString.substring(8, 10).toInt();
      sec   = receivedString.substring(11, 13).toInt();
    }
    else if (receivedString.startsWith("SET_MODE:")) {
      // Szenzor-elrendezés választó (Settings ModePicker) - ezen a hardveren
      // fixen 2 NTC van, de tároljuk/visszaigazoljuk, ha mégis állítanák.
      currentMode = receivedString.substring(9).toInt();
      notifyLine("MODE_OK");
    }
    else if (receivedString == "GET_STATE") {
      // Az app csatlakozáskor mindig GET_STATE-et küld, de sosem GET_VER-t -
      // ezért itt együtt küldjük a verziót is, hogy a Settings oldal
      // firmware-verzió címkéje ténylegesen frissüljön.
      notifyLine("VER:" + String(FW_VERSION));
      notifyLine("STATE:" + String(currentMode));
    }
    else if (receivedString == "GET_VER") {
      notifyLine("VER:" + String(FW_VERSION));
    }
    else if (receivedString == "GET_TMP") {
      // Data oldal 1mp-es lekérdezése - 2 NTC hőmérséklet.
      notifyLine("TMP:" + String(ntc1Temperature, 1) + ":" + String(ntc2Temperature, 1));
    }
    else if (receivedString == "GET_ALL") {
      // Timing oldal megnyitásakor: minden mentett időzítő+termosztát+logika
      // beállítás egyszerre visszaküldve.
      notifyLine(buildLoadTimer("LOAD_FTIMER:", "F1", ftimerF1));
      notifyLine(buildLoadTimer("LOAD_FTIMER:", "F2", ftimerF2));
      notifyLine(buildLoadTimer("LOAD_RTIMER:", "R1", rtimerR1));
      notifyLine(buildLoadTimer("LOAD_RTIMER:", "R2", rtimerR2));
      handleGetTherm("F1");
      handleGetTherm("F2");
      handleGetTherm("R1");
      handleGetTherm("R2");
      handleGetLogic("F1");
      handleGetLogic("F2");
      handleGetLogic("R1");
      handleGetLogic("R2");
    }
    else if (receivedString.startsWith("SET_THERM:")) {
      handleSetTherm(receivedString.substring(10));
    }
    else if (receivedString.startsWith("GET_THERM:")) {
      handleGetTherm(receivedString.substring(10));
    }
    else if (receivedString.startsWith("DEL_THERM:")) {
      handleDelTherm(receivedString.substring(10));
    }
    else if (receivedString.startsWith("SET_FTIMER:")) {
      handleSetFTimer(receivedString.substring(11));
    }
    else if (receivedString.startsWith("SET_RTIMER:")) {
      handleSetRTimer(receivedString.substring(11));
    }
    else if (receivedString.startsWith("DEL_FTIMER:")) {
      handleDelFTimer(receivedString.substring(11));
    }
    else if (receivedString.startsWith("DEL_RTIMER:")) {
      handleDelRTimer(receivedString.substring(11));
    }
    else if (receivedString.startsWith("SET_LOGIC:")) {
      handleSetLogic(receivedString.substring(10));
    }
    else if (receivedString.startsWith("GET_LOGIC:")) {
      handleGetLogic(receivedString.substring(10));
    }
    else if (receivedString.startsWith("DEL_LOGIC:")) {
      handleDelLogic(receivedString.substring(10));
    }
    else if (receivedString.startsWith("GET_FTIMER:")) {
      String t = receivedString.substring(11);
      notifyLine(buildLoadTimer("LOAD_FTIMER:", t, (t == "F1") ? ftimerF1 : ftimerF2));
    }
    else if (receivedString.startsWith("GET_RTIMER:")) {
      String t = receivedString.substring(11);
      notifyLine(buildLoadTimer("LOAD_RTIMER:", t, (t == "R1") ? rtimerR1 : rtimerR2));
    }
    else if (receivedString == "GET_RGB") {
      char buf[32];
      snprintf(buf, sizeof(buf), "RGB|#%02X%02X%02X|%d", pwmR, pwmG, pwmB, digitalRead(RGB_ON));
      notifyLine(String(buf));
    }
    else if (receivedString == "GET_CTR") {
      int f1_on = digitalRead(FAN1_ON), f2_on = digitalRead(FAN2_ON);
      int r1_on = digitalRead(RELAY1_PIN), r2_on = digitalRead(RELAY2_PIN);
      uint8_t states = (f1_on << 3) | (f2_on << 2) | (r1_on << 1) | r2_on;
      // Auto flag-ek: bit3=Fan1, bit2=Fan2, bit1=Relay1, bit0=Relay2 (1=Automata, 0=Kézi)
      uint8_t autoFlags = (fan1Auto << 3) | (fan2Auto << 2) | (relay1Auto << 1) | relay2Auto;
      char buf[32];
      snprintf(buf, sizeof(buf), "CTR%02X%04X%02X%04X%X%X",
               (uint8_t)pwmFan, (uint16_t)FAN1_PWM_FREQ,
               (uint8_t)pwmFan2, (uint16_t)FAN2_PWM_FREQ, states, autoFlags);
      notifyLine(String(buf));
    }
    else if (receivedString.startsWith("SET_AUTO:")) {
      // Parancs: SET_AUTO:<F1|F2|R1|R2>:<0/1>  (1=Automata, 0=Kézi felülbírálás)
      String body = receivedString.substring(9);
      int c = body.indexOf(':');
      if (c > 0) {
        String target = body.substring(0, c);
        bool en = body.substring(c + 1).toInt() != 0;
        if (target == "F1") fan1Auto = en;
        else if (target == "F2") fan2Auto = en;
        else if (target == "R1") relay1Auto = en;
        else if (target == "R2") relay2Auto = en;
        notifyLine("AUTO_OK:" + target + ":" + String(en ? 1 : 0));
      }
    }
  }
};

void setupBLE() {
  BLEDevice::init("Pascal-Controller");
  BLEDevice::setMTU(517); // engedjük a nagy MTU-t az app RequestMtuAsync(517) híváshoz

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
  pTxCharacteristic->addDescriptor(new BLE2902());

  // FONTOS: WRITE_NR is kell, mert az app OTA alatt WithoutResponse
  // (fire-and-forget) írást használ - enélkül a chunkok elvesznek/lassulnak.
  pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  pRxCharacteristic->setCallbacks(new MyCallbacks());

  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();
}

// ============================================================
// SETUP / LOOP
// ============================================================
void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(0));

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI);
  tft.init(135, 240);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  pinMode(NTC1_PIN, INPUT);
  pinMode(NTC2_PIN, INPUT);
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(SWITCH1_PIN, INPUT_PULLUP); // ezeken már van belső pull-up, nem kell külső ellenállás
  pinMode(SWITCH2_PIN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);   // GPIO0 - belső pull-up szükséges (lásd fent)
  pinMode(BTN_RIGHT, INPUT);         // GPIO35 - csak bemenet, gyári/külső pull-up-ra támaszkodik
  pinMode(FAN1_ON, OUTPUT);
  pinMode(FAN2_ON, OUTPUT);
  pinMode(RGB_ON, OUTPUT);

  digitalWrite(FAN1_ON, LOW);
  digitalWrite(FAN2_ON, LOW);
  digitalWrite(RGB_ON, LOW);
  digitalWrite(RELAY1_PIN, LOW);
  digitalWrite(RELAY2_PIN, LOW);

  setupPWM();
  setupBLE();

  Serial.printf("Pascal-Controller firmware v%s kesz.\n", FW_VERSION);
}

void loop() {
  if (otaInProgress) {
    // OTA alatt kizárólag a queue ürítésére koncentrálunk - semmi mást
    // nem csinálunk (kijelző/szenzor/időzítő), hogy a flash írás
    // sebessége biztosan tartsa a lépést a bejövő BLE adattal.
    processOtaQueue();
    return;
  }

  updateClock();
  unsigned long now = millis();

  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }

  if (now - lastSensorRead >= 500) {
    lastSensorRead = now;
    ntc1Temperature = readNTCTemperature(NTC1_PIN);
    ntc2Temperature = readNTCTemperature(NTC2_PIN);
    applyThermostat();
    applySwitchLogic();
    applyTimers();
  }

  // ── Gombok: menü navigáció, vagy idle képernyőn belépés a menübe ──
  ButtonEvent btnEv = pollButtons();
  if (menuScreen == MENU_NONE) {
    if (btnEv == BTN_SELECT) enterMenu(); // dupla gombnyomás = belépés a menübe
  } else {
    handleMenuInput(btnEv);
    // Inaktivitás után automatikusan visszalép az idle kijelzőre
    if (millis() - lastMenuActivity > MENU_TIMEOUT_MS) exitMenuToIdle();
  }

  if (now - lastDisplayDraw >= 200) {
    lastDisplayDraw = now;
    if (menuScreen == MENU_NONE) displayInfo();
    else drawMenu();
  }

  delay(1);
}
