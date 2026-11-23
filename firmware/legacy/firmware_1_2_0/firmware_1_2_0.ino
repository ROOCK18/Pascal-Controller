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

#define FW_VERSION "1.2.0"

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
const int FAN1_ON = 27;
const int FAN2_ON = 26;
const int RGB_ON = 25;

const int LED_PWM_FREQ = 250;
int FAN1_PWM_FREQ = 30;
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
bool autoMode = false; // false=MANUAL(0), true=AUTO(1)  -> STATE:0 / STATE:1

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
struct ThermFan {
  bool enabled = false;
  float tmin = 20.0, tmax = 40.0;
  uint8_t sensor = 1; // 1=NTC1, 2=NTC2
};
struct ThermRelay {
  bool enabled = false;
  float ton = 30.0, toff = 25.0; // BE >= ton, KI <= toff (hiszterézis)
  uint8_t sensor = 1;
  bool state = false;
};

ThermFan   thermF1, thermF2;
ThermRelay thermR1, thermR2;

float getSensorTemp(uint8_t sensor) {
  return (sensor == 2) ? ntc2Temperature : ntc1Temperature;
}

void applyThermostat() {
  if (!autoMode) return;

  // --- Ventilátorok: lineáris fűtés/hűtés arány tmin..tmax között ---
  if (thermF1.enabled) {
    float t = getSensorTemp(thermF1.sensor);
    int pct;
    if (t <= thermF1.tmin) pct = 0;
    else if (t >= thermF1.tmax) pct = 255;
    else pct = (int)((t - thermF1.tmin) / (thermF1.tmax - thermF1.tmin) * 255.0);
    pwmFan = pct;
    setPWM(3, pwmFan);
  }
  if (thermF2.enabled) {
    float t = getSensorTemp(thermF2.sensor);
    int pct;
    if (t <= thermF2.tmin) pct = 0;
    else if (t >= thermF2.tmax) pct = 255;
    else pct = (int)((t - thermF2.tmin) / (thermF2.tmax - thermF2.tmin) * 255.0);
    pwmFan2 = pct;
    setPWM(4, pwmFan2);
  }

  // --- Relék: hiszterézises BE/KI kapcsolás ---
  if (thermR1.enabled) {
    float t = getSensorTemp(thermR1.sensor);
    if (!thermR1.state && t >= thermR1.ton)  { thermR1.state = true;  digitalWrite(RELAY1_PIN, HIGH); }
    if (thermR1.state  && t <= thermR1.toff) { thermR1.state = false; digitalWrite(RELAY1_PIN, LOW); }
  }
  if (thermR2.enabled) {
    float t = getSensorTemp(thermR2.sensor);
    if (!thermR2.state && t >= thermR2.ton)  { thermR2.state = true;  digitalWrite(RELAY2_PIN, HIGH); }
    if (thermR2.state  && t <= thermR2.toff) { thermR2.state = false; digitalWrite(RELAY2_PIN, LOW); }
  }
}

// Parancs: SET_THERM:<F1|F2|R1|R2>:<0/1>:<v1>:<v2>:<sensor>
void handleSetTherm(const String &body) {
  int p1 = body.indexOf(':');
  int p2 = body.indexOf(':', p1 + 1);
  int p3 = body.indexOf(':', p2 + 1);
  int p4 = body.indexOf(':', p3 + 1);
  if (p1 < 0 || p2 < 0 || p3 < 0 || p4 < 0) return;

  String target = body.substring(0, p1);
  bool en = body.substring(p1 + 1, p2).toInt() != 0;
  float v1 = body.substring(p2 + 1, p3).toFloat();
  float v2 = body.substring(p3 + 1, p4).toFloat();
  uint8_t sensor = body.substring(p4 + 1).toInt();
  if (sensor != 2) sensor = 1;

  if (target == "F1") { thermF1.enabled = en; thermF1.tmin = v1; thermF1.tmax = v2; thermF1.sensor = sensor; }
  else if (target == "F2") { thermF2.enabled = en; thermF2.tmin = v1; thermF2.tmax = v2; thermF2.sensor = sensor; }
  else if (target == "R1") { thermR1.enabled = en; thermR1.ton = v1; thermR1.toff = v2; thermR1.sensor = sensor; }
  else if (target == "R2") { thermR2.enabled = en; thermR2.ton = v1; thermR2.toff = v2; thermR2.sensor = sensor; }
  else return;

  notifyLine("THERM_OK");
}

void handleGetTherm(const String &target) {
  String out = "LOAD_THERM:" + target + ":";
  if (target == "F1") out += String(thermF1.enabled ? 1 : 0) + ":" + String(thermF1.tmin, 1) + ":" + String(thermF1.tmax, 1) + ":" + String(thermF1.sensor);
  else if (target == "F2") out += String(thermF2.enabled ? 1 : 0) + ":" + String(thermF2.tmin, 1) + ":" + String(thermF2.tmax, 1) + ":" + String(thermF2.sensor);
  else if (target == "R1") out += String(thermR1.enabled ? 1 : 0) + ":" + String(thermR1.ton, 1) + ":" + String(thermR1.toff, 1) + ":" + String(thermR1.sensor);
  else if (target == "R2") out += String(thermR2.enabled ? 1 : 0) + ":" + String(thermR2.ton, 1) + ":" + String(thermR2.toff, 1) + ":" + String(thermR2.sensor);
  else return;
  notifyLine(out);
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
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.printf("T1:%2.1fC\nT2:%2.1fC\n", ntc1Temperature, ntc2Temperature);
  tft.print("Relay: ");
  tft.setTextColor(digitalRead(RELAY1_PIN) ? ST77XX_GREEN : ST77XX_RED, ST77XX_BLACK);
  tft.print("R1 ");
  tft.setTextColor(digitalRead(RELAY2_PIN) ? ST77XX_GREEN : ST77XX_RED, ST77XX_BLACK);
  tft.println("R2");
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.printf("Mode:%s BLE:%s", autoMode ? "AUTO" : "MAN", deviceConnected ? "CON" : "DISC");
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
    else if (receivedString == "AUTO")   autoMode = true;
    else if (receivedString == "MANUAL") autoMode = false;
    else if (receivedString.startsWith("SET_MODE:")) {
      autoMode = receivedString.substring(9).toInt() != 0;
      notifyLine("MODE_OK");
    }
    else if (receivedString == "GET_STATE") {
      // Az app csatlakozáskor mindig GET_STATE-et küld, de sosem GET_VER-t -
      // ezért itt együtt küldjük a verziót is, hogy a Settings oldal
      // firmware-verzió címkéje ténylegesen frissüljön.
      notifyLine("VER:" + String(FW_VERSION));
      notifyLine("STATE:" + String(autoMode ? 1 : 0));
    }
    else if (receivedString == "GET_VER") {
      notifyLine("VER:" + String(FW_VERSION));
    }
    else if (receivedString.startsWith("SET_THERM:")) {
      handleSetTherm(receivedString.substring(10));
    }
    else if (receivedString.startsWith("GET_THERM:")) {
      handleGetTherm(receivedString.substring(10));
    }
    else if (receivedString.startsWith("SET_FTIMER:")) {
      handleSetFTimer(receivedString.substring(11));
    }
    else if (receivedString.startsWith("SET_RTIMER:")) {
      handleSetRTimer(receivedString.substring(11));
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
      char buf[32];
      snprintf(buf, sizeof(buf), "CTR%02X%04X%02X%04X%X",
               (uint8_t)pwmFan, (uint16_t)FAN1_PWM_FREQ,
               (uint8_t)pwmFan2, (uint16_t)FAN2_PWM_FREQ, states);
      notifyLine(String(buf));
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
    applyTimers();
  }

  if (now - lastDisplayDraw >= 200) {
    lastDisplayDraw = now;
    displayInfo();
  }

  delay(1);
}
