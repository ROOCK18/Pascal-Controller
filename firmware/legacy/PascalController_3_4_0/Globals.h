// ============================================================
// Globals.h - Globális állapot-változók (kijelző objektum, BLE, PWM, óra, OTA)
// ============================================================
#pragma once

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

int FAN1_PWM_FREQ = 30;

// Auto/Kézi mód csatornánként: ha true (alapértelmezett), a termosztát/görbe/
// kapcsoló-logika vezérli az adott kimenetet; ha false, a felhasználó direkt
// parancsai (Ctrl oldal csúszka/kapcsoló) döntenek, az automatika szünetel.
bool fan1Auto = true, fan2Auto = true, relay1Auto = true, relay2Auto = true;

int FAN2_PWM_FREQ = 30;

BLECharacteristic *pTxCharacteristic;

BLECharacteristic *pRxCharacteristic;

bool deviceConnected = false;

bool oldDeviceConnected = false;

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

// Páratartalom - a readDHT11() tölti fel "páratartalom" módban (lásd loop()).
float humidity = NAN;
