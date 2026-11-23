#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <math.h>
#include <Update.h>

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

// --- LEDC PWM konfiguráció ---
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

// --- BLE beállítások ---
BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic;
BLECharacteristic *pRxCharacteristic;
bool deviceConnected = false;
bool oldDeviceConnected = false;

#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// --- Villámlás paraméterek ---
unsigned long lastLightning = 0;
unsigned long lightningInterval = 0;
bool lightningActive = false;
unsigned long lightningStartTime = 0;
int lightningDuration = 0;
int lightningType = 0;

const int LIGHTNING_COLORS[][3] = {{255, 100, 100}, {100, 255, 100}, {100, 100, 255}, {255, 255, 100}, {255, 100, 255}};
const int NUM_LIGHTNING_COLORS = 5;

// --- Globális állapot változók ---
int pwmR = 0;
int pwmG = 0;
int pwmB = 0;
int pwmFan = 0; // Fan 1
int pwmFan2 = 0; // Fan 2 (ÚJ)

bool manualR = true;
bool manualG = true;
bool manualB = true;
bool manualFan = true;
bool autoMode = false;

float ntc1Temperature = 0;
float ntc2Temperature = 0;
bool relayState = false; // Relay 1 status

int hr = 12, min_t = 0, sec = 0;
int onH = -1, onM = -1, offH = -1, offM = -1;
unsigned long lastClockUpdate = 0;

unsigned long lastSensorRead = 0;
unsigned long lastDisplayDraw = 0;
unsigned long lastSerialOut = 0;

unsigned long fullSize = 0, currentSize = 0; // OTA változók
bool otaInProgress = false;

// ---------- FÜGGVÉNYEK ----------

void updateClock() {
  if (millis() - lastClockUpdate >= 1000) {
    lastClockUpdate = millis();
    sec++;
    if (sec >= 60) { sec = 0; min_t++; }
    if (min_t >= 60) { min_t = 0; hr++; }
    if (hr >= 24) { hr = 0; }
  }
}

void setupPWM() {
  ledcAttach(RGB_R_PIN, LED_PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(RGB_G_PIN, LED_PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(RGB_B_PIN, LED_PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(FAN1_PWM_PIN, FAN1_PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(FAN2_PWM_PIN, FAN2_PWM_FREQ, PWM_RESOLUTION);

  ledcWrite(RGB_R_PIN, 0);
  ledcWrite(RGB_G_PIN, 0);
  ledcWrite(RGB_B_PIN, 0);
  ledcWrite(FAN1_PWM_PIN, 0);
  ledcWrite(FAN2_PWM_PIN, 0);
}

void setPWM(int channel, int duty) {
   duty = constrain(duty, 0, 255);
    
    switch(channel) {
        case 0: // R
            pwmR = duty;
            ledcWrite(RGB_R_PIN, duty);
            break;
        case 1: // G
            pwmG = duty;
            ledcWrite(RGB_G_PIN, duty);
            break;
        case 2: // B
            pwmB = duty;
            ledcWrite(RGB_B_PIN, duty);
            break;
        case 3: // FAN 1
            pwmFan = duty;
            ledcWrite(FAN1_PWM_PIN, duty);
            // Ha a PWM 0, lekapcsoljuk a tápot is
            break;
        case 4: // FAN 2
            pwmFan2 = duty;
            ledcWrite(FAN2_PWM_PIN, duty);
            // Ha a PWM 0, lekapcsoljuk a tápot is
            break;
    }
}

void updatePWMOutputs() {
  setPWM(0, pwmR);
  setPWM(1, pwmG);
  setPWM(2, pwmB);
  setPWM(3, pwmFan);
  setPWM(4, pwmFan2);
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
  float voltage = adcValue * VCC / ADC_MAX;
  float rNTC = (voltage * R_FIXED) / (VCC - voltage);
  float steinhart = log(rNTC / NTC_R0) / NTC_BETA + 1.0 / NTC_T0;
  return (1.0 / steinhart) - 273.15;
}

void sendSensorData() {
  if (deviceConnected) {
    int r1 = digitalRead(RELAY1_PIN);
    int r2 = digitalRead(RELAY2_PIN);
    String data = "SENSORS," + String(ntc1Temperature, 1) + "," + String(ntc2Temperature, 1) + "," + 
                  String(r1) + "," + String(r2) + "," + String(pwmR) + "," + String(pwmG) + "," + 
                  String(pwmB) + "," + String(pwmFan) + "," + String(pwmFan2) + "," +
                  String(hr) + ":" + (min_t < 10 ? "0" : "") + String(min_t);
    pTxCharacteristic->setValue(data.c_str());
    pTxCharacteristic->notify();
  }
}

void displayInfo() {
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setCursor(0, 0);
  tft.printf("%02d:%02d:%02d\n\n", hr, min_t, sec);

  tft.setTextSize(2);
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

void serialOutput() {
  Serial.printf("Time: %02d:%02d:%02d | RGB: R=%d G=%d B=%d Fan=%d | ", hr, min_t, sec, pwmR, pwmG, pwmB, pwmFan);
  Serial.printf("NTC1:%.1fC NTC2:%.1fC | Relay:%s | %s\n",
                ntc1Temperature, ntc2Temperature,
                relayState ? "ON" : "OFF",
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
        uint8_t* data = pCharacteristic->getData();
        size_t len = pCharacteristic->getLength();
        String receivedString = "";
        for (int i = 0; i < len; i++) receivedString += (char)data[i];
        receivedString.trim();

        if (!otaInProgress) {
            if (receivedString.startsWith("UPDATE:")) {
                fullSize = receivedString.substring(7).toInt();
                if (fullSize > 0 && Update.begin(fullSize)) {
                    otaInProgress = true;
                    currentSize = 0;
                    pTxCharacteristic->setValue("READY");
                    pTxCharacteristic->notify();
                    tft.fillScreen(ST77XX_BLUE);
                    tft.setTextColor(ST77XX_WHITE);
                    tft.setCursor(10, 40); tft.print("FIRMWARE UPDATE...");
                    return; // Kilépünk, hogy ne dolgozza fel parancsként
                }
            }
        //  KOMBINÁLT RGB (R...G...B...)
        // --- HEX RGB FELDOLGOZÁS (#RRGGBB) ---
            if (receivedString.startsWith("#") && receivedString.length() == 7) {
                // Kivágjuk a hexadecimális részeket (0-tól indulva: # az 0., R az 1-2., stb.)
                String rHex = receivedString.substring(1, 3);
                String gHex = receivedString.substring(3, 5);
                String bHex = receivedString.substring(5, 7);

                // Átalakítás 16-os számrendszerből integerré (0-255)
                pwmR = (int)strtol(rHex.c_str(), NULL, 16);
                pwmG = (int)strtol(gHex.c_str(), NULL, 16);
                pwmB = (int)strtol(bHex.c_str(), NULL, 16);
                
                // Mivel az RGB_ON lábat a setPWM kezeli, hívjuk meg az update-et
                updatePWMOutputs();
                
                Serial.printf("🎨 HEX RGB beállítva: R:%d G:%d B:%d\n", pwmR, pwmG, pwmB);
            }

        // 2. KOMBINÁLT VENTILÁTOR (FxxxSyyy)
        // --- HEX VENTILÁTOR FELDOLGOZÁS (F{XX}{YY}) ---
        if (receivedString.startsWith("F") && receivedString.length() == 5) {
            
            // Kivágjuk a hexadecimális részeket a fix pozíciókból
            String fan1Hex = receivedString.substring(1, 3); // 1. és 2. karakter
            String fan2Hex = receivedString.substring(3, 5); // 3. és 4. karakter

            // Átalakítás 16-os számrendszerből (0-255)
            pwmFan = (int)strtol(fan1Hex.c_str(), NULL, 16);
            pwmFan2 = (int)strtol(fan2Hex.c_str(), NULL, 16);

            // Kimenetek frissítése
            setPWM(3, pwmFan);
            setPWM(4, pwmFan2);

            Serial.printf("🌬 HEX FAN beállítva: F1:%d F2:%d\n", pwmFan, pwmFan2);
        }
        // VENTILÁTOR PWM ÉRTÉK
        // --- HEX VENTILÁTOR PWM FELDOLGOZÁS (FXP{XXXX}) ---
        if (receivedString.startsWith("F1P") && receivedString.length() == 7) {
          String hexVal = receivedString.substring(3, 7);
          long frequency = strtol(hexVal.c_str(), NULL, 16);
          if (frequency >= 10 && frequency <= 25000) {
            ledcDetach(FAN1_PWM_PIN);
            FAN1_PWM_FREQ = frequency;
            ledcAttach(FAN1_PWM_PIN, FAN1_PWM_FREQ, PWM_RESOLUTION);
            setPWM(FAN1_PWM_PIN, pwmFan);
            Serial.printf("🌬 PWM FAN1 beállítva: P:%d\n", FAN1_PWM_FREQ);
          }
        }
        if (receivedString.startsWith("F2P") && receivedString.length() == 7) {
          String hexVal = receivedString.substring(3, 7);
          long frequency = strtol(hexVal.c_str(), NULL, 16);
          if (frequency >= 10 && frequency <= 25000) {
            ledcDetach(FAN2_PWM_PIN);
            FAN2_PWM_FREQ = frequency;
            ledcAttach(FAN2_PWM_PIN, FAN2_PWM_FREQ, PWM_RESOLUTION);
            setPWM(FAN2_PWM_PIN, pwmFan);
            Serial.printf("🌬 PWM FAN2 beállítva: P:%d\n", FAN1_PWM_FREQ);
          }
        }
        // --- DIREKT TÁP KAPCSOLÓK (ON/OFF LÁBAK) ---
        if (receivedString == "RGB_1") digitalWrite(RGB_ON, HIGH);
        if (receivedString == "RGB_0") digitalWrite(RGB_ON, LOW);
        if (receivedString == "F1_1")  digitalWrite(FAN1_ON, HIGH);
        if (receivedString == "F1_0")  digitalWrite(FAN1_ON, LOW);
        if (receivedString == "F2_1")  digitalWrite(FAN2_ON, HIGH);
        if (receivedString == "F2_0")  digitalWrite(FAN2_ON, LOW);
        if (receivedString == "R1_1")  digitalWrite(RELAY1_PIN, HIGH);
        if (receivedString == "R1_0")  digitalWrite(RELAY1_PIN, LOW);
        if (receivedString == "R2_1")  digitalWrite(RELAY2_PIN, HIGH);
        if (receivedString == "R2_0")  digitalWrite(RELAY2_PIN, LOW);
        // ----------------------------------------------

        // 3. IDŐ ÉS IDŐZÍTÉS
        else if (receivedString.startsWith("TIME:")) {
          hr = receivedString.substring(5, 7).toInt();
          min_t = receivedString.substring(8, 10).toInt();
          sec = receivedString.substring(11, 13).toInt();
          Serial.printf("🕒 Óra szinkronizálva: %02d:%02d:%02d\n", hr, min_t, sec);
        }
        // 4. MÓDOK
        if (receivedString == "AUTO") { manualR = manualG = manualB = manualFan = false; autoMode = true; }
        if (receivedString == "MANUAL") { manualR = manualG = manualB = manualFan = true; autoMode = false; }
        
        if (receivedString == "GET_RGB") {
          char buffer[100];
          char currentHex[8];
          sprintf(currentHex, "#%02X%02X%02X", pwmR, pwmG, pwmB);
          int rgbon = digitalRead(RGB_ON);

          // ADJUNK HOZZÁ EGY \n-t A VÉGÉRE:
          snprintf(buffer, sizeof(buffer), "RGB|%s|%d\n", currentHex, rgbon);
                      
          pTxCharacteristic->setValue(buffer);
          pTxCharacteristic->notify(); 
          
          Serial.print("📋 RGB állapot visszaküldve (BLE): ");
          Serial.println(buffer); // Itt látni fogod a Serialon, hogy van egy üres sor utána
        }
        if (receivedString == "GET_CTR") {
            char buffer[32]; 
            int f1_on = digitalRead(FAN1_ON);
            int f2_on = digitalRead(FAN2_ON);
            int r1_on = digitalRead(RELAY1_PIN);
            int r2_on = digitalRead(RELAY2_PIN);

            // Bitek összeállítása: Fan1On(bit3), Fan2On(bit2), Relay1(bit1), Relay2(bit0)
            uint8_t states = (f1_on << 3) | (f2_on << 2) | (r1_on << 1) | r2_on;

            // Formátum: CTR|XX|XXXX|XX|XXXX|X
            // XX (Fan1 PWM) | XXXX (Fan1 Freq) | XX (Fan2 PWM) | XXXX (Fan2 Freq) | X (4 bit állapot)
            snprintf(buffer, sizeof(buffer), "CTR%02X%04X%02X%04X%X\n", 
                    (uint8_t)pwmFan, 
                    (uint16_t)FAN1_PWM_FREQ, 
                    (uint8_t)pwmFan2, 
                    (uint16_t)FAN2_PWM_FREQ, 
                    states
            );
            
            pTxCharacteristic->setValue(buffer);
            pTxCharacteristic->notify(); 
            
            Serial.print("📋 ULTRA-COMPACT CTR: ");
            Serial.print(buffer); 
        }
      } else {
            // 2. KRITIKUS RÉSZ: OTA ADATFOGADÁS
            Update.write(data, len);
            currentSize += len;
            int progress = (currentSize * 220) / fullSize;
            tft.fillRect(10, 80, progress, 20, ST77XX_GREEN);

            if (currentSize >= fullSize) {
                if (Update.end(true)) {
                    pTxCharacteristic->setValue("OK_RESTART");
                    pTxCharacteristic->notify();
                    delay(1500);
                    ESP.restart();
                }
            }
        }
    }
};

void setupBLE() {
  BLEDevice::init("Pascal-Controller");
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
}

// ---------- SETUP ----------
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
  
  lightningInterval = random(5000, 30000);
}

// ---------- LOOP ----------
void loop() {
  updateClock(); // Óra léptetése és időzítő figyelése
  unsigned long now = millis();

  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }

  updateLightningEffect();

  if (now - lastSensorRead >= 500) {
    lastSensorRead = now;
    ntc1Temperature = readNTCTemperature(NTC1_PIN);
    ntc2Temperature = readNTCTemperature(NTC2_PIN);
  }

  if (now - lastDisplayDraw >= 200) {
    lastDisplayDraw = now;
    displayInfo();
  }
/*
  if (now - lastSerialOut >= 1000) {
    lastSerialOut = now;
    serialOutput();
    if (deviceConnected) sendSensorData();
  }
*/
  delay(1);
}