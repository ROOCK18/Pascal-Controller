// ============================================================
// Globals.h - Globális állapot-változók (kijelző objektum, BLE, PWM, óra, OTA)
// ============================================================
#pragma once

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

int FAN1_PWM_FREQ = 30;

// Auto/Kézi mód csatornánként: ha true (alapértelmezett), a termosztát/görbe/
// kapcsoló-logika vezérli az adott kimenetet; ha false, a felhasználó direkt
// parancsai (Ctrl oldal csúszka/kapcsoló) döntenek, az automatika szünetel.
bool fan1Auto = false, fan2Auto = false, relay1Auto = false, relay2Auto = false;

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
uint8_t day = 1, month = 1;
uint16_t yr = 2025;

// Idő kijelzés formátuma: 0=Ó:P:MP, 1=Ó:P, 2=AM/PM (angol)
uint8_t timeFormat = 0;
// Dátum formátuma a fejlécben: 0=É/H/N, 1=N/H/É, 2=H/N/É
uint8_t dateFormat = 0;
// Mit mutasson a fejléc a mód-kód helyett: külön-külön be/ki kapcsolható,
// hogy a hónap/nap/óra megjelenjen-e - ezek KOMBINÁLVA jelennek meg.
// Ha mindegyik ki van kapcsolva, a mód rövid kódja látszik (mint alapból).
// Óra / Dátum / Nap neve megjelenítési helye a fejlécben: 0=Kikapcsolva,
// 1=Baloldalt, 2=Középen. Egy oldalon belül MINDIG rögzített sorrendben
// jelennek meg: Dátum, Óra, Nap neve (pl. "2026/08/21 14:35:22 Csütörtök").
uint8_t clockPos = 2;    // alapból középen (mint eddig)
uint8_t datePos = 0;     // alapból kikapcsolva
uint8_t dayNamePos = 0;  // alapból kikapcsolva

// Kijelző fényerő (0-100%, PWM háttérvilágítás) és elalvási időzítő.
// Minél alacsonyabb a fényerő, annál "feketébbek" a színek (a háttérvilágítás
// erőssége korlátozza az elérhető kontrasztot/fényességet).
uint8_t displayBrightness = 100; // %
uint8_t sleepTimeoutIdx = 8;     // index a SLEEP_TIMES_MS[] tömbbe - alapból "Soha" (utolsó elem)
bool displayAsleep = false;
unsigned long lastUserActivity = 0;

// Hőmérő kalibrációs eltolás - CSAK az NTC1/NTC2 analóg hőmérőkre vonatkozik
// (nem a DHT11-re, annak nincs kompenzációja). Lehet negatív is.
float tempCompNTC1 = 0.0, tempCompNTC2 = 0.0;

// Mentés-állapot (lásd Storage.h): ha valami beállítás változott, ezt
// jelezzük, és a loop() időnként (nem minden váltáskor!) elmenti a flash-be,
// hogy ne kopjon feleslegesen a memória.
bool settingsDirty = false;
unsigned long lastSettingsSave = 0;

Preferences prefs;

bool idleNeedsFullClear = true; // egyszeri teljes törlés kell, mielőtt az idle képernyőt rajzoljuk - itt deklarálva, mert a Timers.h (BLE parancsok) korábban hivatkozik rá, mint a Menu.h

// "Utoljára kirajzolt" idle-képernyő értékek - a Display.h használja a
// villódzásmentes, csak-változáskor-frissítő rajzoláshoz, de a Menu.h is
// hivatkozik rájuk (pl. idő/dátum szerkesztésnél kényszerített frissítés),
// ezért itt, korán deklarálva vannak (Menu.h előbb települ, mint Display.h).
int lastShownSec = -1, lastShownMin = -1, lastShownHr = -1;
bool lastShownBt = false;
bool lastShownWifi = false;
uint16_t lastShownRgb = 0xFFFF;
int lastShownMode = -1;
int lastShownDay = -1;
float lastShownT1 = -999, lastShownT2 = -999, lastShownHum = -999;
bool lastShownSw1 = false, lastShownSw2 = false;
int lastShownFan1Pct = -1, lastShownFan2Pct = -1;
// A bal/közép fejléc-terület "utoljára kirajzolt" szövege - String, mert a
// tartalom (dátum+idő+napnév kombináció) tetszőleges összeállítású lehet.
// Kezdőértékük egy sosem előforduló karakter, hogy az ELSŐ rajzolás garantált legyen.
String lastLeftText = "\x01";
String lastCenterText = "\x01";
bool lastShownR1 = false, lastShownR2 = false;

unsigned long lastClockUpdate = 0;
unsigned long lastSensorRead = 0;
unsigned long lastDisplayDraw = 0;

// --- OTA ---
unsigned long fullSize = 0, currentSize = 0;
bool otaInProgress = false;

// WiFi-alapú OTA (a BLE-s csomagolt frissítés MELLETT, azt nem helyettesítve).
// Ha van elmentett SSID/jelszó és a router elérhető, az ESP32 SAJÁT WiFi
// rádióján tölti le a firmware-t közvetlenül a megadott URL-ről (pl. GitHub
// release), miközben a telefon végig BLE-n marad kapcsolódva - a BLE-n csak
// az URL-parancs megy át, nem a több száz KB-os fájl.
String wifiSsid = "";
String wifiPassword = "";
bool wifiOtaInProgress = false;
String pendingOtaUrl = "";
bool otaUrlPending = false;

// Valós idejű WiFi csatlakozás/keresés állapota
bool wifiConnectRequested = false; // SET_WIFI után a loop() indítja a tényleges kapcsolódást
bool wifiConnectPending = false;   // éppen folyamatban van egy csatlakozási kísérlet
unsigned long wifiConnectStartTime = 0;
bool wifiScanRequested = false;    // WIFI_SCAN után a loop() indítja a keresést

// ── Kijelző-menüs WiFi beállítás állapota ──
#define WIFI_MENU_MAX_NETWORKS 15
#define WIFI_PW_MAX_LEN 32
String wifiMenuScanSsids[WIFI_MENU_MAX_NETWORKS];
int wifiMenuScanCount = 0;
bool wifiMenuScanRequested = false; // a menüből indított keresés jelzője (külön a BLE-s wifiScanRequested-től)
String wifiMenuSelectedSsid = "";

// A jelszó-karakterkerék: karakterenként szerkeszthető puffer. A kurzor a
// JELENLEG szerkesztett pozíción áll, ott az UP/FEL és DOWN/LE gomb az adott
// karaktert lépteti a WIFI_CHARSET-ben körbe - hosszan nyomva (2mp) gyorsan
// pörgeti, ugyanaz a mechanizmus, mint a %-os értékeknél.
// index0 = '<' TÖRLÉS (utolsó karakter törlése) - nem valódi jelszó-karakter,
// hanem speciális parancs a karakterkeréken belül. A SELECT gomb (rövid,
// mindkét gomb egyszerre) "beírja" az aktuálisan kiválasztott karaktert/
// parancsot. A HOSSZÚ, mindkét-gombos nyomás (LONG_SELECT) indítja a
// csatlakozást - lásd Menu.h MENU_WIFI_PASSWORD_EDIT esete.
const char WIFI_CHARSET[] = "< abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()-_=+.,";
const int WIFI_CHARSET_LEN = sizeof(WIFI_CHARSET) - 1; // a záró '\0' nélkül
char wifiPwBuf[WIFI_PW_MAX_LEN + 1] = "";
int wifiPwLen = 0;      // eddig ténylegesen begépelt karakterek száma
int wifiPwCursor = 0;   // a jelenleg szerkesztett pozíció (0..wifiPwLen, ahol wifiPwLen = "új karakter hozzáadása")
int wifiPwCharIdx = 0;  // a WIFI_CHARSET-ben az aktuális pozíción álló karakter indexe

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
