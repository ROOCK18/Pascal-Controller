// ============================================================
// Pascal-Controller ALAP (üres) firmware v1.0.1-base
// Ez az első firmware, amit USB-n keresztül kell feltölteni.
// Egyetlen feladata: stabil BLE OTA fogadás, hogy utána MINDEN
// további frissítés (pl. a teljes funkciós firmware) már
// Bluetooth-on keresztül telepíthető legyen az appból.
//
// v1.0.1 VÁLTOZÁS (OTA HIBA / Update.end() javítás):
//   A korábbi verzió a bejövő BLE csomagot KÖZVETLENÜL a BLE
//   callback-ből írta a flash-be (Update.write()). Egy flash
//   szektor törlés/írás akár több tíz ms-ig is tarthat, ez alatt
//   viszont a BLE stack callback taskja blokkolva volt -> a BLE
//   kapcsolat időzítése összecsúszhatott, csomagok veszhettek/
//   torzulhattak, ami a végén Update.end()-nél buktatta az
//   ellenőrzést ("nem bootolható image" hiba).
//   MOST: az onWrite() csak egy queue-ba másolja a bejövő adatot
//   (gyors, nem blokkol), a TÉNYLEGES Update.write() hívás pedig
//   a loop()-ban történik. Ez pontosan az a minta, amit az eredeti
//   "BT updater.ino"/"gekko.ino" is használt - stabil, tesztelt
//   megoldás ESP32 BLE OTA-hoz.
//
// Protokoll (megegyezik a Controll app BluetoothManager.cs /
// Settings.xaml.cs OTA logikájával):
//   App -> "UPDATE:<total>:<chunkSize>"
//   ESP -> "READY"
//   App -> bináris chunkok (WithoutResponse, fire-and-forget, max 128B)
//   ESP -> (siker) "OK_RESTART" majd újraindul
//   ESP -> (hiba)  "ERROR:<ok>"
// ============================================================

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Update.h>
#include <queue>

#define FW_VERSION "1.0.1-base"

// --- Kijelző (TTGO T-Display) ---
#define TFT_MOSI 19
#define TFT_SCLK 18
#define TFT_CS     5
#define TFT_DC    16
#define TFT_RST   23
#define TFT_BL     4
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// --- Bluetooth UUID-k (meg KELL egyezniük minden jövőbeli firmware-ben) ---
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic;
bool deviceConnected = false;

unsigned long fullSize = 0, currentSize = 0;
bool otaInProgress = false;
int lastOtaPct = -1;

// --- OTA queue: a BLE callback csak ide másol, a flash írás a loop()-ban fut ---
#define MAX_OTA_CHUNK 128 // az app mindig max 128 byte-os chunkokat küld (lásd Settings.xaml.cs _otaMtu)
struct OtaPacket {
  uint8_t data[MAX_OTA_CHUNK];
  size_t len;
};
std::queue<OtaPacket> otaQueue;
portMUX_TYPE otaMux = portMUX_INITIALIZER_UNLOCKED;

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

void drawStatus(const char* line1, const char* line2 = "") {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(10, 10);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.println(line1);
  if (strlen(line2) > 0) {
    tft.setCursor(10, 40);
    tft.println(line2);
  }
}

void otaFail(const String &reason) {
  Serial.printf("OTA hiba: %s\n", reason.c_str());
  Update.abort();
  otaInProgress = false;
  clearOtaQueue();
  notifyLine("ERROR:" + reason);
  drawStatus("OTA HIBA!", reason.c_str());
}

// A loop()-ból hívva: kiüríti az OTA queue-t, ténylegesen írja a flash-t.
void processOtaQueue() {
  int processed = 0;
  while (processed < 8) { // egy loop() ciklusban max ennyi csomagot dolgozunk fel
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
      tft.fillRect(10, 105, 220, 20, ST77XX_BLACK);
      tft.setCursor(10, 105);
      tft.printf("%d%%", pct);
    }

    if (currentSize >= fullSize) {
      if (Update.end(true)) {
        notifyLine("OK_RESTART");
        drawStatus("OTA KESZ!", "Ujrainditas...");
        delay(1500);
        ESP.restart();
      } else {
        otaFail(String(Update.errorString()));
      }
      return;
    }
  }
}

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s) {
    deviceConnected = true;
    drawStatus("BLE csatlakozva", "Varakozas UPDATE-re");
    Serial.println("BLE eszkoz csatlakozott");
  }
  void onDisconnect(BLEServer* s) {
    deviceConnected = false;
    if (otaInProgress) {
      // Ha OTA kozben szakadt meg a kapcsolat, tiszta allapotba
      // allitjuk vissza, hogy a kovetkezo probalkozas ujrakezdheto legyen.
      Update.abort();
      otaInProgress = false;
      clearOtaQueue();
      Serial.println("OTA megszakadt: BLE lecsatlakozott");
    }
    Serial.println("BLE eszkoz szetkapcsolodott");
    drawStatus("Varakozas BT-re...", FW_VERSION);
    s->getAdvertising()->start();
  }
};

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    uint8_t* data = pCharacteristic->getData();
    size_t len = pCharacteristic->getLength();
    if (len == 0) return;

    // ---------------- OTA bináris adat: csak queue-ba másolás ----------------
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
    String cmd;
    cmd.reserve(len + 1);
    for (size_t i = 0; i < len; i++) cmd += (char)data[i];
    cmd.trim();

    if (cmd.startsWith("UPDATE:")) {
      // "UPDATE:<total>:<chunkSize>" - a toInt() a ':'-nál megall,
      // igy csak a total meretet olvassuk ki.
      fullSize = cmd.substring(7).toInt();
      clearOtaQueue();
      if (fullSize > 0 && Update.begin(fullSize)) {
        otaInProgress = true;
        currentSize = 0;
        lastOtaPct = -1;
        notifyLine("READY");
        drawStatus("OTA: ADATFOGADAS", String(fullSize / 1024).c_str());
        tft.fillRect(10, 80, 220, 20, ST77XX_BLACK);
      } else {
        notifyLine("ERROR:" + String(Update.errorString()));
        drawStatus("OTA HIBA!", "Update.begin()");
      }
    }
    else if (cmd == "GET_VER" || cmd == "GET_STATE") {
      notifyLine("VER:" + String(FW_VERSION));
      if (cmd == "GET_STATE") notifyLine("STATE:0");
    }
    // Az alap firmware ezen kívül semmilyen más parancsot nem ismer -
    // a végleges (teljes funkciós) firmware ezt a fájlt bővíti ki.
  }
};

void setupBLE() {
  BLEDevice::init("Pascal-Controller");
  BLEDevice::setMTU(517); // engedjük a nagy MTU-t az app RequestMtuAsync(517) hivasahoz

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
  pTxCharacteristic->addDescriptor(new BLE2902());

  // FONTOS: WRITE_NR (Write Without Response) is kell, mert az app
  // OTA alatt fire-and-forget irast hasznal - enelkul a chunkok
  // elvesznek vagy nagyon lelassul a feltoltes.
  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  pRxCharacteristic->setCallbacks(new MyCallbacks());

  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();
}

void setup() {
  Serial.begin(115200);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  SPI.begin(TFT_SCLK, -1, TFT_MOSI);
  tft.init(135, 240);
  tft.setRotation(3);

  drawStatus("Varakozas BT-re...", FW_VERSION);

  setupBLE();
  Serial.printf("Alap firmware v%s kesz, varjuk a csatlakozast...\n", FW_VERSION);
}

void loop() {
  if (otaInProgress) {
    // OTA alatt kizárólag a queue ürítésére koncentrálunk, hogy a
    // flash írás sebessége tartsa a lépést a bejövő BLE adattal.
    processOtaQueue();
    return;
  }
  delay(10);
}
