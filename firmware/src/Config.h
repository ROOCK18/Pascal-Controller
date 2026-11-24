// ============================================================
// Config.h - Pin-kiosztás, BLE UUID-k és egyéb konstansok
// ============================================================
#pragma once

#define FW_VERSION "5.0.0"

// --- Régi RGB565 színkonstansok (ValueRow.valColor); az ui.h a palettára képezi le ---
#define UI_BLACK   0x0000
#define UI_WHITE   0xFFFF
#define UI_RED     0xF800
#define UI_GREEN   0x07E0
#define UI_BLUE    0x001F
#define UI_CYAN    0x07FF
#define UI_MAGENTA 0xF81F
#define UI_YELLOW  0xFFE0
#define UI_ORANGE  0xFC00

// --- TFT kijelző lábkiosztás (TTGO T-Display) ---
#define TFT_MOSI 19

#define TFT_SCLK 18

#define TFT_CS 5

#define TFT_DC 16

#define TFT_RST 23

#define TFT_BL 4

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

// FONTOS: nincs külön "kapcsoló" GPIO! A fizikai kapcsolók (K1/K2, "1
// kapcsoló+1 hőmérő" / "2 kapcsoló" módban) az NTC1_PIN/NTC2_PIN lábakat
// használják DUÁL FUNKCIÓBAN: a láb 10k ellenállással 5V-ra van felhúzva -
// nyitott kapcsoló = magas ADC érték, rövidre zárt (zárt) kapcsoló = ~0 ADC
// érték. Lásd isSwitchClosed() lejjebb. Hasonlóan, "páratartalom" módban az
// NTC2_PIN-re egy DHT11 modul van kötve (digitális protokoll) - lásd
// readDHT11() lejjebb.

// Beépített fizikai gombok (menürendszerhez). Active-low.
// GPIO0: BAL gomb (= "lefelé" navigáció) - egyben boot strapping láb is,
// futás közben szabadon használható, csak flashelés alatt nem szabad
// lenyomva tartani. Belső pull-up szükséges.
// GPIO35: JOBB gomb (= "felfelé" navigáció) - csak bemenet, nincs belső
// pull-up, de a legtöbb TTGO T-Display NYÁK-on gyárilag van rajta külső
// pull-up (mivel ez az egyik beépített gomb lába).
const int BTN_LEFT  = 0;

const int BTN_RIGHT = 35;

// DHT11 (páratartalom mód): a csatlakozó 3-as és 4-es lába (GPIO21, GPIO22) 47 Ω-on át az NTC1, ill. NTC2
// csatlakozó jelcsomópontjára megy. A DHT11 tápja az NTC1 csatlakozó 1-es lábán (GPIO21 hajtja),
// az adatvonala az NTC2 csatlakozó 1-es lábán (GPIO22), a GND a csatlakozók 2-es lába.
// Más módban mindkét láb nagyimpedanciás BEMENET kell legyen, hogy az NTC analóg mérését ne zavarja.
const int DHT_VCC_PIN = 21;
const int DHT_DATA_PIN = 22;

const int FAN1_ON = 27;

const int FAN2_ON = 26;

const int RGB_ON = 25;

const int LED_PWM_FREQ = 250;

// Kijelző háttérvilágítás PWM frekvenciája - magasabb, mint a RGB/ventilátor
// csatornáké, hogy ne legyen látható villódzás a háttérvilágításon.
const int BACKLIGHT_PWM_FREQ = 5000;

const int PWM_RESOLUTION = 8;

// --- NTC paraméterek ---
const float VCC = 5;
const float ADC_MAX = 4095.0;
const float R_FIXED = 10000.0;
const float NTC_R0 = 10000.0;
const float NTC_T0 = 298.15;
const float NTC_BETA = 3950.0;

// --- BLE ---
NimBLEServer *pServer = NULL;

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"

#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#define MAX_OTA_CHUNK 128 // az app mindig max 128 byte-os chunkokat küld (lásd Settings.xaml.cs _otaMtu)

// ============================================================
// TERMOSZTÁT (SET_THERM)
// ============================================================
#define FAN_CURVE_MAX_POINTS 5

// ============================================================
// EGYSZERŰ IDŐZÍTŐK (FTIMER / RTIMER) - tárolás + visszaigazolás
// ============================================================
#define MAX_SLOTS 8

// ============================================================
// KIJELZŐ
// ============================================================
// ============================================================
#define BTN_DEBOUNCE_MS 30

#define BTN_LONG_MS     600
#define BTN_HOLD_REPEAT_START_MS 2000 // ennyi ideig kell nyomva tartani, mire elkezd automatikusan ismételni
#define BTN_LONG_SELECT_MS 1200 // ennyi ideig kell EGYÜTT nyomva tartani mindkét gombot a "hosszú SELECT"-hez (pl. WiFi csatlakozás indítása)
#define BTN_HOLD_REPEAT_INTERVAL_MS 120 // ilyen gyakran ismétel, amíg nyomva van

#define MENU_TIMEOUT_MS 30000

const int MAIN_COUNT = 6;

// Kijelző elalvási idő előre definiált lehetőségei (ms-ben). Az utolsó elem
// (0) jelenti a "Soha" opciót - ekkor a háttérvilágítás sosem kapcsol ki.
const unsigned long SLEEP_TIMES_MS[] = { 10000UL, 30000UL, 60000UL, 180000UL, 300000UL, 600000UL, 1800000UL, 3600000UL, 0UL };
const char* SLEEP_NAMES[] = { "10mp", "30mp", "1p", "3p", "5p", "10p", "30p", "1o", "Soha" };
const int SLEEP_OPTIONS_COUNT = 9;
