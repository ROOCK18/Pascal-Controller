// ============================================================
// Pascal-Controller firmware v3.4.0
//
// Ez a fájl a fő .ino, ami csak az #include-okat és a setup()/
// loop() függvényeket tartalmazza. A tényleges logika külön
// header (.h) fájlokban van, funkció szerint csoportosítva:
//   Config.h        - pin-kiosztás, BLE UUID-k, konstansok
//   Globals.h       - globális állapot-változók
//   DataTypes.h     - struktúrák + globális példányaik
//   Prototypes.h    - minden függvény előre-deklarálva
//   Comm.h          - BLE üzenetküldés
//   Sensors.h       - NTC/kapcsoló/DHT11 kiolvasás
//   PWMControl.h    - PWM kimenetek, óra léptetése
//   Thermostat.h    - ventilátor-görbe + relé-termosztát
//   SwitchLogic.h   - kapcsoló-alapú logika
//   Timers.h        - napi időzítők
//   Buttons.h       - fizikai gombok kezelése
//   Menu.h          - on-device menürendszer
//   Display.h       - alap (idle) kijelző
//   OTA.h           - BLE OTA frissítés
//   BLECommands.h   - BLE parancsok fogadása + szerver indítás
//
// FONTOS: mind a 15 fájlnak UGYANABBAN a mappában kell lennie,
// mint ennek a .ino fájlnak (Arduino IDE ezt automatikusan
// kezeli, ha a mappa neve megegyezik a .ino fájl nevével).
// ============================================================

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Fonts/FreeSans9pt7b.h>
#include <SPI.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <math.h>
#include <string.h>
#include <Update.h>
#include <queue>


#include "Config.h"
#include "Globals.h"
#include "DataTypes.h"
#include "Prototypes.h"
#include "Comm.h"
#include "Sensors.h"
#include "PWMControl.h"
#include "Thermostat.h"
#include "SwitchLogic.h"
#include "Timers.h"
#include "Buttons.h"
#include "Menu.h"
#include "Display.h"
#include "OTA.h"
#include "BLECommands.h"

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
  // NTC1_PIN/NTC2_PIN: nincs szükség pinMode-ra itt, az analogRead/readDHT11
  // dinamikusan kezeli a lábakat mód szerint (lásd isSwitchClosed/readDHT11).
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

    // Mód-függő szenzorolvasás: az NTC1/NTC2 lábak jelentése módonként más
    // (analóg hőmérő / digitális kapcsoló / DHT11) - csak azt olvassuk ki,
    // ami az aktuális módban ténylegesen NTC-ként van bekötve.
    switch (currentMode) {
      case 0: // 2 hőmérő
        ntc1Temperature = readNTCTemperature(NTC1_PIN);
        ntc2Temperature = readNTCTemperature(NTC2_PIN);
        break;
      case 1: // 1 hőmérő
        ntc1Temperature = readNTCTemperature(NTC1_PIN);
        break;
      case 2: { // páratartalom - DHT11 az NTC2_PIN lábon (min. 1mp a mérések közt)
        static unsigned long lastDhtRead = 0;
        if (now - lastDhtRead >= 1000) {
          lastDhtRead = now;
          float dhtT, dhtH;
          if (readDHT11(NTC2_PIN, dhtT, dhtH)) {
            ntc1Temperature = dhtT; // a DHT11 saját hőmérséklete lesz a "Hőmérséklet (NTC1)" forrás
            humidity = dhtH;
          }
        }
        break;
      }
      case 3: // 1 kapcsoló+1 hőmérő - csak NTC1 analóg, NTC2 a K1 kapcsoló
        ntc1Temperature = readNTCTemperature(NTC1_PIN);
        break;
      case 4: // 2 kapcsoló - egyik láb sem analóg hőmérő
        break;
    }

    applyThermostat();
    applySwitchLogic();
    applyTimers();
  }

  // ── Gombok: menü navigáció, vagy idle képernyőn belépés a menübe ──
  ButtonEvent btnEv = pollButtons();
  if (menuScreen == MENU_NONE) {
    if (btnEv == BTN_SELECT) enterMenu(); // dupla gombnyomás = belépés a menübe
  } else {
    if (btnEv != BTN_NONE) {
      handleMenuInput(btnEv);
      menuDirty = true; // csak TÉNYLEGES gombesemény után kell újrarajzolni
    }
    // Inaktivitás után automatikusan visszalép az idle kijelzőre
    if (millis() - lastMenuActivity > MENU_TIMEOUT_MS) exitMenuToIdle();
  }

  if (menuScreen == MENU_NONE) {
    // Idle képernyő: élő adatok (óra, hőfok, stb.) -> továbbra is
    // időzítve frissül, gombnyomástól függetlenül.
    if (now - lastDisplayDraw >= 200) {
      lastDisplayDraw = now;
      displayInfo();
    }
  } else if (menuDirty) {
    // Menü: KIZÁRÓLAG akkor rajzolunk, ha valóban történt gombesemény
    // (navigáció/érték-változás/belépés) - így nincs felesleges,
    // időzítő-vezérelt villódzás, amikor a felhasználó csak néz egy képernyőt.
    drawMenu();
    menuDirty = false;
  }

  delay(1);
}
