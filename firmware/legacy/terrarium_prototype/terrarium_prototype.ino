#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <math.h>

// --- TFT kijelző lábkiosztás (TTGO T-Display) ---
#define TFT_MOSI 19
#define TFT_SCLK 18
#define TFT_CS   5
#define TFT_DC   16
#define TFT_RST  23
#define TFT_BL   4

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// --- PIN definíciók ---
const int NTC1_PIN    = 27;
const int NTC2_PIN    = 26;
const int RGB_R_PIN   = 33;  // Piros
const int RGB_G_PIN   = 32;  // Zöld
const int RGB_B_PIN   = 25;  // Kék
const int FAN_PWM_PIN = 39;
const int RELAY_PIN   = 38;  // Relé vezérlő pin

// --- LEDC PWM konfiguráció ---
const int PWM_FREQ = 250;        // 250 Hz
const int PWM_RESOLUTION = 8;    // 8 bit (0-255)

// --- NTC paraméterek ---
const float VCC = 3.3;
const float ADC_MAX = 4095.0;
const float R_FIXED = 10000.0;
const float NTC_R0 = 10000.0;
const float NTC_T0 = 298.15;
const float NTC_BETA = 3950.0;

// --- BLE beállítások ---
BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic;
BLECharacteristic *pRxCharacteristic;
bool deviceConnected = false;
bool oldDeviceConnected = false;

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// --- Villámlás paraméterek ---
unsigned long lastLightning = 0;
unsigned long lightningInterval = 0;
bool lightningActive = false;
unsigned long lightningStartTime = 0;
int lightningDuration = 0;
int lightningType = 0;

// Villám színek (R, G, B)
const int LIGHTNING_COLORS[][3] = {
  {255, 100, 100},  // Pirosas
  {100, 255, 100},  // Zöldes
  {100, 100, 255},  // Kékes
  {255, 255, 100},  // Sárga
  {255, 100, 255}   // Rózsaszín
};
const int NUM_LIGHTNING_COLORS = 5;

// --- Globális PWM állapot ---
int pwmR   = 0;
int pwmG   = 0;
int pwmB   = 0;
int pwmFan = 128;

// Kézi vezérlés flag-ek
bool manualR   = false;
bool manualG   = false;
bool manualB   = false;
bool manualFan = false;
bool autoMode  = true;

// --- Szenzorok / relé ---
float ntc1Temperature = 0;
float ntc2Temperature = 0;
bool relayState   = false;  // Relé állapota: false = OFF, true = ON

// --- Időzítések a loop-ban ---
unsigned long lastSensorRead  = 0;
unsigned long lastDisplayDraw = 0;
unsigned long lastSerialOut   = 0;

// ---------- ÚJ LEDC PWM FÜGGVÉNYEK ----------
void setupPWM() {
  // Új LEDC API - minden pin-hez külön attach
  ledcAttach(RGB_R_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(RGB_G_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(RGB_B_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(FAN_PWM_PIN, PWM_FREQ, PWM_RESOLUTION);
  
  // Kezdeti értékek beállítása
  ledcWrite(RGB_R_PIN, 0);
  ledcWrite(RGB_G_PIN, 0);
  ledcWrite(RGB_B_PIN, 0);
  ledcWrite(FAN_PWM_PIN, 128);
  
  Serial.println("Új LEDC PWM inicializálva 250Hz-re");
  Serial.println("Pin-ek: R=33, G=32, B=25, Fan=38");
}

void setPWM(int channel, int duty) {
  duty = constrain(duty, 0, 255);
  
  switch(channel) {
    case 0: // R
      ledcWrite(RGB_R_PIN, duty);
      pwmR = duty;
      break;
    case 1: // G
      ledcWrite(RGB_G_PIN, duty);
      pwmG = duty;
      break;
    case 2: // B
      ledcWrite(RGB_B_PIN, duty);
      pwmB = duty;
      break;
    case 3: // FAN
      ledcWrite(FAN_PWM_PIN, duty);
      pwmFan = duty;
      break;
  }
}

void updatePWMOutputs() {
  setPWM(0, pwmR);
  setPWM(1, pwmG);
  setPWM(2, pwmB);
  setPWM(3, pwmFan);
}

// ---------- RELÉ VEZÉRLŐ FÜGGVÉNYEK ----------
void setRelay(bool state) {
  relayState = state;
  if (state) {
    digitalWrite(RELAY_PIN, HIGH);  // RELAY ON - 3.3V
    Serial.println("✅ RELAY: ON (PIN 39 HIGH)");
  } else {
    digitalWrite(RELAY_PIN, LOW);   // RELAY OFF - 0V
    Serial.println("❌ RELAY: OFF (PIN 39 LOW)");
  }
}

// ---------- VILLÁMLÁS FÜGGVÉNYEK ----------
void startLightning() {
  lightningActive = true;
  lightningStartTime = millis();
  
  lightningType = random(0, 2);
  
  if (lightningType == 0) {
    lightningDuration = random(50, 150);
    int colorIndex = random(0, NUM_LIGHTNING_COLORS);
    pwmR = LIGHTNING_COLORS[colorIndex][0];
    pwmG = LIGHTNING_COLORS[colorIndex][1];
    pwmB = LIGHTNING_COLORS[colorIndex][2];
  } else {
    lightningDuration = random(300, 600);
    pwmR = random(20, 50);
    pwmG = random(20, 50);
    pwmB = random(20, 50);
  }
  
  updatePWMOutputs();
}

void updateLightningEffect() {
  unsigned long now = millis();
  unsigned long elapsed = now - lightningStartTime;
  
  if (autoMode && !manualR && !manualG && !manualB) {
    if (lightningActive) {
      if (lightningType == 1 && elapsed < lightningDuration) {
        if (elapsed < 100) {
          int progress = map(elapsed, 0, 100, 0, 255);
          pwmR = min(pwmR + progress, 255);
          pwmG = min(pwmG + progress, 255);
          pwmB = min(pwmB + progress, 255);
        } else if (elapsed < 150) {
          int colorIndex = random(0, NUM_LIGHTNING_COLORS);
          pwmR = LIGHTNING_COLORS[colorIndex][0];
          pwmG = LIGHTNING_COLORS[colorIndex][1];
          pwmB = LIGHTNING_COLORS[colorIndex][2];
        } else {
          int fadeOut = map(elapsed - 150, 0, lightningDuration - 150, 255, 0);
          pwmR = constrain(fadeOut, 0, 255);
          pwmG = constrain(fadeOut, 0, 255);
          pwmB = constrain(fadeOut, 0, 255);
        }
        updatePWMOutputs();
      }
      
      if (elapsed >= lightningDuration) {
        lightningActive = false;
        pwmR = 0; pwmG = 0; pwmB = 0;
        updatePWMOutputs();
      }
    } else {
      if (now - lastLightning >= lightningInterval) {
        startLightning();
        lastLightning = now;
        lightningInterval = random(5000, 30000);
      }
    }
  } else if (lightningActive) {
    lightningActive = false;
    pwmR = 0; pwmG = 0; pwmB = 0;
    updatePWMOutputs();
  }
}

// ---------- NTC FÜGGVÉNYEK ----------
float readNTCTemperature(int ntcPin) {
  int adcValue = analogRead(ntcPin);
  if (adcValue <= 0) adcValue = 1;
  if (adcValue >= ADC_MAX) adcValue = ADC_MAX - 1;
  
  float voltage = adcValue * VCC / ADC_MAX;
  float rNTC = (voltage * R_FIXED) / (VCC - voltage);
  
  float steinhart = log(rNTC / NTC_R0) / NTC_BETA;
  steinhart += 1.0 / NTC_T0;
  float temperatureK = 1.0 / steinhart;
  float temperatureC = temperatureK - 273.15;
  
  return temperatureC;
}

// ---------- BLE FÜGGVÉNYEK ----------
void sendSensorData() {
  if (deviceConnected) {
    String data = "SENSORS," + String(ntc1Temperature, 1) + "," + 
                  String(ntc2Temperature, 1) + "," + 
                  (relayState ? "1" : "0") + "," +
                  String(pwmR) + "," + String(pwmG) + "," + 
                  String(pwmB) + "," + String(pwmFan);
    
    pTxCharacteristic->setValue(data.c_str());
    pTxCharacteristic->notify();
  }
}

// ---------- KIJELZŐ FÜGGVÉNYEK ----------
void displayInfo() {
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);

  tft.setCursor(0, 50);
  tft.printf("R:%3d G:%3d B:%3d\n", pwmR, pwmG, pwmB);

  tft.setCursor(0, 70);
  tft.printf("Fan:%3d NTC1:%4.1fC\n", pwmFan, ntc1Temperature);

  tft.setCursor(0, 90);
  tft.printf("NTC2:%4.1fC Relay:", ntc2Temperature);
  if (relayState) {
    tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
    tft.println("ON ");
  } else {
    tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
    tft.println("OFF");
  }

  tft.setCursor(0, 110);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.print("Mode: ");
  if (autoMode) {
    tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    tft.println("AUTO     ");
  } else {
    tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
    tft.println("MANUAL   ");
  }

  tft.setCursor(0, 130);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.print("BLE: ");
  if (deviceConnected) {
    tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
    tft.println("CONNECTED ");
  } else {
    tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
    tft.println("DISCONNECT");
  }
}

void serialOutput() {
  Serial.printf("RGB: R=%d G=%d B=%d Fan=%d | ", pwmR, pwmG, pwmB, pwmFan);
  Serial.printf("NTC1:%.1fC NTC2:%.1fC | Relay:%s (PIN39:%s) | %s\n",
                ntc1Temperature, ntc2Temperature,
                relayState ? "ON" : "OFF",
                digitalRead(RELAY_PIN) ? "HIGH" : "LOW",
                autoMode ? "AUTO" : "MANUAL");
}

// --- BLE Callback osztály ---
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("📱 BLE eszköz csatlakozott");
    };
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("📱 BLE eszköz szétkapcsolódott");
      pServer->getAdvertising()->start();
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String rxValue = pCharacteristic->getValue();
      if (rxValue.length() > 0) {
        String receivedString = String(rxValue.c_str());
        receivedString.trim();
        
        Serial.println("📲 BLE parancs érkezett: '" + receivedString + "'");
        Serial.println("📲 Parancs hossza: " + String(receivedString.length()));
        
        // Debug: kiírjuk az összes karaktert
        Serial.print("📲 Karakterek: ");
        for (int i = 0; i < receivedString.length(); i++) {
          Serial.print("0x");
          Serial.print(receivedString[i], HEX);
          Serial.print(" ");
        }
        Serial.println();
        
        // ELŐSZÖR a teljes parancsokat kezeljük, UTÁNA az egybetűs prefixeket
        if (receivedString == "RELAY1") {
          Serial.println("⚡ RELAY1 parancs feldolgozva - relé BEKAPCSOL");
          setRelay(true);
        }
        else if (receivedString == "RELAY0") {
          Serial.println("⚡ RELAY0 parancs feldolgozva - relé KIKAPCSOL");
          setRelay(false);
        }
        else if (receivedString == "GET") {
          Serial.println("📊 GET parancs - adatok küldése");
          sendSensorData();
        }
        else if (receivedString == "AUTO") {
          manualR = manualG = manualB = manualFan = false;
          autoMode = true;
          Serial.println("🤖 Automatikus mód aktiválva");
        }
        else if (receivedString == "MANUAL") {
          manualR = manualG = manualB = manualFan = true;
          autoMode = false;
          Serial.println("👨‍💼 Kézi mód aktiválva");
        }
        else if (receivedString == "RELAY_STATUS") {
          String status = relayState ? "RELAY_ON" : "RELAY_OFF";
          pTxCharacteristic->setValue(status.c_str());
          pTxCharacteristic->notify();
          Serial.println("📊 Relé státusz lekérdezve: " + status);
        }
        // Csak akkor nézzük az egybetűs parancsokat, ha nem találtunk teljes parancsot
        else if (receivedString.startsWith("R") && receivedString.length() > 1) {
          pwmR = receivedString.substring(1).toInt();
          manualR = true;
          setPWM(0, pwmR);
          Serial.println("🔴 Piros beállítva: " + String(pwmR));
        }
        else if (receivedString.startsWith("G") && receivedString.length() > 1) {
          pwmG = receivedString.substring(1).toInt();
          manualG = true;
          setPWM(1, pwmG);
          Serial.println("🟢 Zöld beállítva: " + String(pwmG));
        }
        else if (receivedString.startsWith("B") && receivedString.length() > 1) {
          pwmB = receivedString.substring(1).toInt();
          manualB = true;
          setPWM(2, pwmB);
          Serial.println("🔵 Kék beállítva: " + String(pwmB));
        }
        else if (receivedString.startsWith("F") && receivedString.length() > 1) {
          pwmFan = receivedString.substring(1).toInt();
          manualFan = true;
          setPWM(3, pwmFan);
          Serial.println("💨 Ventilátor beállítva: " + String(pwmFan));
        }
        else {
          Serial.println("❌ Ismeretlen parancs: '" + receivedString + "'");
        }
        
        // Válasz küldése, hogy a parancs feldolgozva lett
        String response = "CMD_OK:" + receivedString;
        pTxCharacteristic->setValue(response.c_str());
        pTxCharacteristic->notify();
      }
    }
};

void setupBLE() {
  BLEDevice::init("TTGO-Terrarium");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
  pTxCharacteristic->addDescriptor(new BLE2902());
  
  pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
  pRxCharacteristic->setCallbacks(new MyCallbacks());
  
  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  
  Serial.println("📱 BLE szolgáltatás elindítva - 'TTGO-Terrarium'");
}

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);
  Serial.println("🚀 TTGO Terrarium Controller - RELAY DEBUG");
  randomSeed(analogRead(0));

  // TFT kijelző inicializálása
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI);
  tft.init(135, 240);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);

  tft.setCursor(10, 10);
  tft.println("TTGO T-Display");
  tft.println("RELAY DEBUG");

  // Analóg beolvasás beállítása
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  // Pin módok beállítása
  pinMode(NTC1_PIN, INPUT);
  pinMode(NTC2_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  
  // Relé kezdeti állapota: KI (LOW)
  setRelay(false);

  setupPWM();
  setupBLE();
  
  lightningInterval = random(5000, 30000);
  delay(1000);
  
  Serial.println("✅ Setup befejezve - Relé inicializálva OFF állapotban");
  Serial.println("🔍 Várakozás BLE parancsokra...");
}

// ---------- LOOP ----------
void loop() {
  unsigned long now = millis();

  // BLE kapcsolat kezelése
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println("📱 BLE hirdetés újraindítva");
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }

  // Villámlás effekt frissítése
  updateLightningEffect();

  // Szenzorok olvasása
  if (now - lastSensorRead >= 100) {
    lastSensorRead = now;
    ntc1Temperature = readNTCTemperature(NTC1_PIN);
    ntc2Temperature = readNTCTemperature(NTC2_PIN);
  }

  // Kijelző frissítése
  if (now - lastDisplayDraw >= 100) {
    lastDisplayDraw = now;
    displayInfo();
  }

  // Szerial és BLE adatküldés
  if (now - lastSerialOut >= 1000) {
    lastSerialOut = now;
    serialOutput();
    if (deviceConnected) sendSensorData();
  }

  delay(1);
}