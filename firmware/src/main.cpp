#include <Arduino.h>
// ============================================================
// Pascal-Controller firmware v3.5.0
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
//   Storage.h       - beállítások mentése/betöltése (NVS), factory reset
//   Buttons.h       - fizikai gombok kezelése
//   Menu.h          - on-device menürendszer
//   Display.h       - alap (idle) kijelző
//   OTA.h           - BLE OTA frissítés
//   BLECommands.h   - BLE parancsok fogadása + szerver indítás
//
// FONTOS: mind a 16 fájlnak UGYANABBAN a mappában kell lennie,
// mint ennek a .ino fájlnak (Arduino IDE ezt automatikusan
// kezeli, ha a mappa neve megegyezik a .ino fájl nevével).
// ============================================================

#include <SPI.h>
#include <NimBLEDevice.h>
#include <math.h>
#include <string.h>
#include <Update.h>
#include <queue>
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>


#include "Config.h"
#include "Globals.h"
#include "DataTypes.h"
#include "Prototypes.h"
#include "ui.h"
#include "Comm.h"
#include "Sensors.h"
#include "RgbCal.h"
#include "PWMControl.h"
#include "Thermostat.h"
#include "SwitchLogic.h"
#include "Timers.h"
#include "Storage.h"
#include "Alerts.h"
#include "RgbAuto.h"
#include "Prox.h"
#include "Buttons.h"
#include "Menu.h"
#include "Display.h"
#include "OTA.h"
#include "WifiOTA.h"
#include "Mqtt.h"
#include "BLECommands.h"

// ============================================================
// SETUP / LOOP
// ============================================================
void setup() {
  // ELSŐ lépés: a kimeneti lábak definiált (KI) szintre. Reset után a lábak lebegnek
  // (bemenet, felhúzás nélkül); minél tovább tart, mire a firmware kimenetté teszi és
  // alacsonyra húzza őket, annál nagyobb az esély, hogy a relé/MOSFET meghajtó
  // "megérzi", és a relé kattan. Ezért ez megelőz minden mást (Serial, kijelző, LVGL).
  digitalWrite(RELAY1_PIN, LOW); pinMode(RELAY1_PIN, OUTPUT);
  digitalWrite(RELAY2_PIN, LOW); pinMode(RELAY2_PIN, OUTPUT);
  digitalWrite(FAN1_ON, LOW);    pinMode(FAN1_ON, OUTPUT);
  digitalWrite(FAN2_ON, LOW);    pinMode(FAN2_ON, OUTPUT);
  digitalWrite(RGB_ON, LOW);     pinMode(RGB_ON, OUTPUT);
  pinMode(DHT_VCC_PIN, INPUT); pinMode(DHT_DATA_PIN, INPUT); // DHT11 lábak: nagyimpedanciás, amíg páratartalom módban nincs rájuk szükség

  Serial.begin(115200);
  randomSeed(analogRead(0));

  ui_init(); // LovyanGFX + LVGL, indító képernyő

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  pinMode(NTC1_PIN, INPUT);
  pinMode(NTC2_PIN, INPUT);
  // NTC1_PIN/NTC2_PIN: nincs szükség pinMode-ra itt, az analogRead/readDHT11
  // dinamikusan kezeli a lábakat mód szerint (lásd isSwitchClosed/readDHT11).
  pinMode(BTN_LEFT, INPUT_PULLUP);   // GPIO0 - belső pull-up szükséges (lásd fent)
  pinMode(BTN_RIGHT, INPUT);         // GPIO35 - csak bemenet, gyári/külső pull-up-ra támaszkodik

  setupPWM();
  loadAllSettings(); // mentett beállítások visszatöltése (áramszünet utáni állapot-visszaállítás)
  ensureBlePin();    // első induláskor véletlen BLE PIN
  if (wifiSsid.length() > 0) {
    // Ha van mentett WiFi hálózat, induláskor automatikusan próbáljon
    // csatlakozni hozzá (a loop() indítja el, ugyanúgy, mint SET_WIFI után).
    wifiConnectRequested = true;
  }
  // A setupPWM() még az alapértelmezett (30Hz) frekvenciával csatolta a
  // ventilátor PWM csatornákat - most, hogy a mentett érték betöltődött,
  // újra kell csatolni a TÉNYLEGES frekvenciával.
  ledcDetach(FAN1_PWM_PIN); ledcAttach(FAN1_PWM_PIN, FAN1_PWM_FREQ, PWM_RESOLUTION);
  ledcDetach(FAN2_PWM_PIN); ledcAttach(FAN2_PWM_PIN, FAN2_PWM_FREQ, PWM_RESOLUTION);
  updatePWMOutputs(); // a betöltött RGB/ventilátor értékek ténylegesen ki is íródjanak
  applyBacklight();   // a betöltött fényerő ténylegesen alkalmazva
  lastUserActivity = millis();
  WiFi.onEvent(onWifiEvent); // valós idejű WiFi csatlakozás-visszajelzésekhez (jelszó-hiba stb.)
  setupBLE();
  mqttStart(); // MQTT / Home Assistant (háttér-task; csak akkor csatlakozik, ha be van kapcsolva és van WiFi)

  Serial.printf("Pascal-Controller firmware v%s kesz.\n", FW_VERSION);
}

void loop() {
  processBleCommands(); // a BLE parancsok ITT, a fő szálon futnak (a callback csak sorba állítja őket)
  applyRelayPulses();   // relé-impulzus (PULSE) lejáratának figyelése - OTA alatt is, hogy a relé ne ragadjon be
  applyProxOverrides(); // közeledés-automatika lejárata / felülírása

  if (otaInProgress) {
    // OTA alatt kizárólag a queue ürítésére koncentrálunk - semmi mást
    // nem csinálunk (kijelző/szenzor/időzítő), hogy a flash írás
    // sebessége biztosan tartsa a lépést a bejövő BLE adattal.
    processOtaQueue();
    return;
  }

  if (otaUrlPending) {
    // A WiFi-OTA indítása itt, a fő loop()-ban történik (nem a BLE callback
    // taskban) - ez egy hosszú, blokkoló művelet (WiFi csatlakozás + letöltés),
    // amit a BLE stack callback taskjában futtatva watchdog-reset-et okozna.
    otaUrlPending = false;
    handleOtaUrl(pendingOtaUrl);
    return;
  }

  if (wifiConnectRequested) {
    wifiConnectRequested = false;
    startWifiConnect();
  }
  if (wifiConnectPending && millis() - wifiConnectStartTime > 15000) {
    wifiConnectPending = false;
    WiFi.disconnect(true, false); // ne próbálkozzon tovább a háttérben, hogy a scan újra tisztán működjön
    notifyLine("WIFI_FAILED:Idotullepes - router nem elerheto vagy tavol van");
  }

  if (wifiScanRequested) {
    wifiScanRequested = false;
    doWifiScan();
    return;
  }

  if (wifiMenuScanRequested) {
    wifiMenuScanRequested = false;
    drawMenu(); // "Keresés..." felirat előbb, hogy ne fagyottnak tűnjön a keresés alatt
    doWifiScanForMenu();
    return;
  }

  updateClock();
  unsigned long now = millis();

  // A WiFi-csatlakozás állapota gombnyomás nélkül, aszinkron módon
  // változhat (WiFi.onEvent) - ezért ezen a képernyőn gombesemény nélkül is
  // rendszeresen újra kell rajzolni, hogy lásd a valós idejű állapotot.
  static unsigned long lastWifiConnectingRedraw = 0;
  if ((menuScreen == MENU_WIFI_CONNECTING || menuScreen == MENU_SENS_LIST || menuScreen == MENU_MQTT_LIST) && now - lastWifiConnectingRedraw > 300) {
    lastWifiConnectingRedraw = now;
    drawMenu();
  }

  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    NimBLEDevice::startAdvertising();
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }

  if (now - lastSensorRead >= 500) {
    lastSensorRead = now;

    // DHT11: páratartalom módban a GPIO21 adja a 3,3 V-ot (47 Ω-on át), a GPIO22 az adatvonal. Más módban
    // mindkét láb nagyimpedanciás bemenet (különben az NTC analóg csomópontját zavarnák).
    static int dhLastMode = -1;
    static unsigned long dhOnSince = 0;
    if (currentMode != dhLastMode) {
      if (currentMode == 2) {
        pinMode(DHT_VCC_PIN, OUTPUT);
        digitalWrite(DHT_VCC_PIN, HIGH);
        dhOnSince = now;                       // a DHT11-nek ~1,5 mp felfutás kell
      } else {
        pinMode(DHT_VCC_PIN, INPUT);
        pinMode(DHT_DATA_PIN, INPUT);
      }
      dhLastMode = currentMode;
    }

    // Mód-függő szenzorolvasás: az NTC1/NTC2 lábak jelentése módonként más
    // (analóg hőmérő / digitális kapcsoló / DHT11) - csak azt olvassuk ki,
    // ami az aktuális módban ténylegesen NTC-ként van bekötve.
    switch (currentMode) {
      case 0: // 2 hőmérő
        ntc1Temperature = readNTCTemperature(NTC1_PIN) + tempCompNTC1;
        ntc2Temperature = readNTCTemperature(NTC2_PIN) + tempCompNTC2;
        break;
      case 1: // 1 hőmérő
        ntc1Temperature = readNTCTemperature(NTC1_PIN) + tempCompNTC1;
        break;
      case 2: { // páratartalom - DHT11 az NTC2_PIN lábon (min. 1mp a mérések közt)
        static unsigned long lastDhtRead = 0, lastDhtOk = 0;
        if (now - dhOnSince >= 1500 && now - lastDhtRead >= 1000) {
          lastDhtRead = now;
          float dhtT, dhtH;
          if (readDHT11(DHT_DATA_PIN, dhtT, dhtH)) {
            ntc1Temperature = dhtT; // a DHT11-hez nincs kompenzáció (nem NTC)
            humidity = dhtH;
            lastDhtOk = now;
          } else if (now - lastDhtOk > 15000) {
            // 15 mp-e nincs érvényes mérés: ne maradjon régi érték, a termosztát ne
            // elavult adat alapján dolgozzon (a szenzorhiba kezelése NaN-nal történik).
            ntc1Temperature = NAN;
            humidity = NAN;
          }
        }
        break;
      }
      case 3: // 1 kapcsoló+1 hőmérő - csak NTC1 analóg, NTC2 a K1 kapcsoló
        ntc1Temperature = readNTCTemperature(NTC1_PIN) + tempCompNTC1;
        break;
      case 4: // 2 kapcsoló - egyik láb sem analóg hőmérő
        break;
    }

    applyThermostat();
    applySwitchLogic();
    applyTimers();
    applyRelayPulses();   // az automatika után: az aktív impulzus felülírja
    applyProxOverrides(); // ... és az aktív közeledés-művelet is
  }
  checkAlerts();          // riasztások (szenzorhiba / hőmérséklet-határ) -> értesítés az appnak
  rgbCalTick();           // kalibrációs mód időtúllépése
  applyRgbAutomation();   // RGB: átmenet-motor + időzítő / görbék / közeledés (csak Automata üzemmódban)

  // ── Gombok: menü navigáció, vagy idle képernyőn belépés a menübe ──
  ButtonEvent btnEv = pollButtons();
  if (btnEv != BTN_NONE) {
    bool wasAsleep = displayAsleep;
    wakeDisplay(); // bármilyen gombnyomás felébreszti a kijelzőt és nullázza az inaktivitás-időzítőt
    if (wasAsleep) btnEv = BTN_NONE; // az első gombnyomás alvó kijelzőnél CSAK ébreszt, nem hajt végre semmit
  }
  checkDisplaySleep(); // ha letelt a beállított elalvási idő, kikapcsolja a háttérvilágítást

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

  // Beállítások mentése flash-be - ÖSSZEVONVA, legfeljebb 5 másodpercenként
  // (nem minden apró változásnál azonnal), hogy ne kopjon feleslegesen a
  // flash, de áramkimaradás esetén se vesszen el a legutóbbi állapot.
  if (settingsDirty && now - lastSettingsSave >= 5000) {
    saveAllSettings();
    settingsDirty = false;
    lastSettingsSave = now;
  }

  ui_tick(); // LVGL: animációk és változott területek kirajzolása
  delay(1);
}
