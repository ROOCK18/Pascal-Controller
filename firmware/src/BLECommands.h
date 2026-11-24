// ============================================================
// BLECommands.h - BLE parancsok fogadása/szétosztása és a BLE szerver indítása
// ============================================================
#pragma once

// ============================================================
// SZENZOR-PARAMÉTEREK (PT1000 / NTC)
// Parancs: SET_SENS:<1|2>:<type 0-3>:<r0>:<t0>:<beta>:<rSeries>:<vSupply>
//   type: 0=PT1000, 1=NTC 10k, 2=NTC 100k, 3=Egyedi. Választ: SENS_OK
// Parancs: GET_SENS:<1|2>  ->  LOAD_SENS:<n>:<type>:<r0>:<t0>:<beta>:<rSeries>:<vSupply>:<komp>
// Parancs: GET_SENSRAW     ->  SENSRAW:<mV1>:<R1>:<mV2>:<R2>  (R="-" ha nincs érvényes mérés)
// ============================================================
void handleGetSens(const String &which) {
  int n = which.toInt();
  if (n < 1 || n > 2) return;
  const SensorCfg &c = sensCfg[n - 1];
  float comp = (n == 1) ? tempCompNTC1 : tempCompNTC2;
  notifyLine("LOAD_SENS:" + String(n) + ":" + String(c.type) + ":" + String(c.r0, 1) + ":" + String(c.t0, 1) + ":" +
             String(c.beta, 0) + ":" + String(c.rSeries, 1) + ":" + String(c.vSupply, 2) + ":" + String(comp, 1));
}

void handleSetSens(const String &body) {
  String tok[7]; int n = 0, start = 0;
  while (n < 7) {
    int c = body.indexOf(':', start);
    if (c < 0) { tok[n++] = body.substring(start); break; }
    tok[n++] = body.substring(start, c);
    start = c + 1;
  }
  if (n < 7) { notifyLine("ERROR:SET_SENS hianyos parameter"); return; }
  int idx = tok[0].toInt();
  int type = tok[1].toInt();
  float r0 = tok[2].toFloat(), t0 = tok[3].toFloat(), beta = tok[4].toFloat();
  float rs = tok[5].toFloat(), vs = tok[6].toFloat();
  if (idx < 1 || idx > 2 || type < 0 || type > 3 ||
      !(r0 >= 1 && r0 <= 1000000) || !(t0 >= -50 && t0 <= 150) || !(beta >= 500 && beta <= 10000) ||
      !(rs >= 10 && rs <= 1000000) || !(vs >= 1.0f && vs <= 6.0f)) {
    notifyLine("ERROR:SET_SENS ervenytelen ertek");
    return;
  }
  SensorCfg &c = sensCfg[idx - 1];
  c.type = (uint8_t)type; c.r0 = r0; c.t0 = t0; c.beta = beta; c.rSeries = rs; c.vSupply = vs;
  markDirty();
  notifyLine("SENS_OK");
}

void handleGetSensRaw() {
  String out = "SENSRAW";
  for (int i = 0; i < 2; i++) {
    out += ":" + String(sensLastMv[i], 0) + ":" + (isnan(sensLastR[i]) ? String("-") : String(sensLastR[i], 1));
  }
  notifyLine(out);
}

// ============================================================
// MQTT / HOME ASSISTANT beállítások
// Parancs: SET_MQTT:<enabled 0/1>|<host>|<port>|<user>|<pass>|<name>   (a "|" az elválasztó)
//   üres jelszó = a mentett jelszó nem változik. Válasz: MQTT_OK
// Parancs: GET_MQTT -> LOAD_MQTT:<enabled>|<host>|<port>|<user>|<passSet 0/1>|<name>|<state>
//   state: 0 kikapcsolva, 1 nincs WiFi, 2 csatlakozik, 3 csatlakozva (a jelszó sosem megy vissza)
// ============================================================
void handleGetMqtt() {
  notifyLine("LOAD_MQTT:" + String(mqttCfg.enabled ? 1 : 0) + "|" + String(mqttCfg.host) + "|" + String(mqttCfg.port) + "|" +
             String(mqttCfg.user) + "|" + String(mqttCfg.pass[0] ? 1 : 0) + "|" + String(mqttCfg.name) + "|" + String((int)mqttState));
}

void handleSetMqtt(const String &body) {
  String tok[6]; int n = 0, start = 0;
  while (n < 6) {
    int c = body.indexOf('|', start);
    if (c < 0) { tok[n++] = body.substring(start); break; }
    tok[n++] = body.substring(start, c);
    start = c + 1;
  }
  if (n < 6) { notifyLine("ERROR:SET_MQTT hianyos parameter"); return; }
  long port = tok[2].toInt();
  if (port < 1 || port > 65535 || tok[1].length() >= sizeof(mqttCfg.host) || tok[3].length() >= sizeof(mqttCfg.user) ||
      tok[4].length() >= sizeof(mqttCfg.pass) || tok[5].length() >= sizeof(mqttCfg.name)) {
    notifyLine("ERROR:SET_MQTT ervenytelen ertek");
    return;
  }
  MqttCfg c = mqttCfg;
  c.enabled = tok[0].toInt() != 0;
  strlcpy(c.host, tok[1].c_str(), sizeof(c.host));
  c.port = (uint16_t)port;
  strlcpy(c.user, tok[3].c_str(), sizeof(c.user));
  if (tok[4].length() > 0) strlcpy(c.pass, tok[4].c_str(), sizeof(c.pass)); // üres = marad a régi
  strlcpy(c.name, tok[5].length() > 0 ? tok[5].c_str() : "Pascal-Controller", sizeof(c.name));
  mqttCfg = c;
  mqttDiscoveredMode = -1;   // a (megváltozott nevű) entitások újra bejelentése
  mqttReconf = true;
  markDirty();
  notifyLine("MQTT_OK");
}

// ============================================================
// BLE CALLBACKS
// ============================================================
class MyServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& connInfo) override {
    deviceConnected = true;
    Serial.println("BLE eszkoz csatlakozott");
  }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& connInfo, int reason) override {
    deviceConnected = false;
    if (otaInProgress) {
      Update.abort();
      otaInProgress = false;
      clearOtaQueue();
      Serial.println("OTA megszakadt: BLE lecsatlakozott - ujrainditas");
      // Ugyanaz az elv, mint minden más OTA-hibánál: tiszta újraindulás,
      // ne ragadjon be az eszköz részleges/összezavarodott állapotban.
      delay(1000);
      ESP.restart();
    }
    Serial.println("BLE eszkoz szetkapcsolodott");
    NimBLEDevice::startAdvertising();
  }
};

class MyCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo& connInfo) override {
    NimBLEAttValue rxValue = pCharacteristic->getValue();
    const uint8_t* data = rxValue.data();
    size_t len = rxValue.size();
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
      markDirty();
    }
    else if (receivedString.startsWith("F1P") && receivedString.length() == 7) {
      long f = strtol(receivedString.substring(3, 7).c_str(), NULL, 16);
      if (f >= 10 && f <= 25000) {
        ledcDetach(FAN1_PWM_PIN);
        FAN1_PWM_FREQ = f;
        ledcAttach(FAN1_PWM_PIN, FAN1_PWM_FREQ, PWM_RESOLUTION);
        setPWM(3, pwmFan);
        markDirty();
      }
    }
    else if (receivedString.startsWith("F2P") && receivedString.length() == 7) {
      long f = strtol(receivedString.substring(3, 7).c_str(), NULL, 16);
      if (f >= 10 && f <= 25000) {
        ledcDetach(FAN2_PWM_PIN);
        FAN2_PWM_FREQ = f;
        ledcAttach(FAN2_PWM_PIN, FAN2_PWM_FREQ, PWM_RESOLUTION);
        setPWM(4, pwmFan2);
        markDirty();
      }
    }
    else if (receivedString.startsWith("F") && receivedString.length() == 5) {
      pwmFan = (int)strtol(receivedString.substring(1, 3).c_str(), NULL, 16);
      pwmFan2 = (int)strtol(receivedString.substring(3, 5).c_str(), NULL, 16);
      setPWM(3, pwmFan);
      setPWM(4, pwmFan2);
      markDirty();
    }
    else if (receivedString == "RGB_1") { digitalWrite(RGB_ON, HIGH); markDirty(); }
    else if (receivedString == "RGB_0") { digitalWrite(RGB_ON, LOW); markDirty(); }
    else if (receivedString == "F1_1")  { digitalWrite(FAN1_ON, HIGH); markDirty(); }
    else if (receivedString == "F1_0")  { digitalWrite(FAN1_ON, LOW); markDirty(); }
    else if (receivedString == "F2_1")  { digitalWrite(FAN2_ON, HIGH); markDirty(); }
    else if (receivedString == "F2_0")  { digitalWrite(FAN2_ON, LOW); markDirty(); }
    else if (receivedString == "R1_1")  { digitalWrite(RELAY1_PIN, HIGH); markDirty(); }
    else if (receivedString == "R1_0")  { digitalWrite(RELAY1_PIN, LOW); markDirty(); }
    else if (receivedString == "R2_1")  { digitalWrite(RELAY2_PIN, HIGH); markDirty(); }
    else if (receivedString == "R2_0")  { digitalWrite(RELAY2_PIN, LOW); markDirty(); }
    else if (receivedString.startsWith("TIME:")) {
      hr    = receivedString.substring(5, 7).toInt();
      min_t = receivedString.substring(8, 10).toInt();
      sec   = receivedString.substring(11, 13).toInt();
      markDirty();
    }
    else if (receivedString.startsWith("SET_MODE:")) {
      // Szenzor-elrendezés választó (Settings ModePicker) - ezen a hardveren
      // fixen 2 NTC van, de tároljuk/visszaigazoljuk, ha mégis állítanák.
      int newMode = receivedString.substring(9).toInt();
      if (newMode < 0 || newMode > 4) { notifyLine("ERROR:ervenytelen mod"); return; }
      currentMode = newMode;
      idleNeedsFullClear = true; // ne maradjon az előző mód kijelzés-maradványa
      markDirty();
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
      handleGetRHold("R1");
      handleGetRHold("R2");
      handleGetComp();
      handleGetSens("1");
      handleGetSens("2");
      handleGetDisplay();
      handleGetDatePos();
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
    else if (receivedString.startsWith("SET_RHOLD:")) {
      handleSetRHold(receivedString.substring(10));
    }
    else if (receivedString.startsWith("GET_RHOLD:")) {
      handleGetRHold(receivedString.substring(10));
    }
    else if (receivedString.startsWith("SET_COMP:")) {
      handleSetComp(receivedString.substring(9));
    }
    else if (receivedString.startsWith("SET_SENS:")) {
      handleSetSens(receivedString.substring(9));
    }
    else if (receivedString.startsWith("GET_SENS:")) {
      handleGetSens(receivedString.substring(9));
    }
    else if (receivedString == "GET_SENSRAW") {
      handleGetSensRaw();
    }
    else if (receivedString.startsWith("SET_MQTT:")) {
      handleSetMqtt(receivedString.substring(9));
    }
    else if (receivedString == "GET_MQTT") {
      handleGetMqtt();
    }
    else if (receivedString == "GET_COMP") {
      handleGetComp();
    }
    else if (receivedString.startsWith("SET_DISPLAY:")) {
      handleSetDisplay(receivedString.substring(12));
    }
    else if (receivedString == "GET_DISPLAY") {
      handleGetDisplay();
    }
    else if (receivedString.startsWith("SET_DATEPOS:")) {
      handleSetDatePos(receivedString.substring(12));
    }
    else if (receivedString == "GET_DATEPOS") {
      handleGetDatePos();
    }
    else if (receivedString.startsWith("SET_DATE:")) {
      handleSetDate(receivedString.substring(9));
    }
    else if (receivedString == "FACTORY_RESET") {
      notifyLine("FACTORY_OK");
      delay(100);
      factoryReset(); // ujraindul - nem ter vissza
    }
    else if (receivedString.startsWith("SET_WIFI:")) {
      handleSetWifi(receivedString.substring(9));
    }
    else if (receivedString == "GET_WIFI_STATUS") {
      handleGetWifiStatus();
    }
    else if (receivedString == "WIFI_SCAN") {
      handleWifiScan();
    }
    else if (receivedString == "WIFI_DISCONNECT") {
      handleWifiDisconnect();
    }
    else if (receivedString == "WIFI_FORGET") {
      handleWifiForget();
    }
    else if (receivedString.startsWith("OTA_URL:")) {
      // A tényleges letöltés/flash-írás blokkoló művelet - NEM itt, a BLE
      // callback taskban indítjuk (az watchdog-reset-et okozhatna), hanem
      // csak jelezzük, és a loop() indítja el a következő ciklusban.
      pendingOtaUrl = receivedString.substring(8);
      otaUrlPending = true;
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
        else if (target == "R1") { if (en && !relay1Auto) { relayTimerR1.timedActive = false; relayTimerR1.cooldownActive = false; relayTimerR1.waitingForReversal = false; } relay1Auto = en; }
        else if (target == "R2") { if (en && !relay2Auto) { relayTimerR2.timedActive = false; relayTimerR2.cooldownActive = false; relayTimerR2.waitingForReversal = false; } relay2Auto = en; }
        markDirty();
        notifyLine("AUTO_OK:" + target + ":" + String(en ? 1 : 0));
      }
    }
  }
};

void setupBLE() {
  NimBLEDevice::init("Pascal-Controller");
  NimBLEDevice::setMTU(517); // engedjük a nagy MTU-t az app RequestMtuAsync(517) híváshoz

  // FONTOS: itt SZÁNDÉKOSAN nincs BLESecurity/bonding beállítva, ÉS nincs
  // külön klasszikus Bluetooth (BluetoothSerial) réteg sem. Mindkettőt
  // kipróbáltuk:
  //   - BLESecurity (Just Works bonding): párosítható lett, de utána a
  //     BLE GATT csatlakozás megbízhatatlanná vált (Android GATT error 133).
  //   - BluetoothSerial (klasszikus BT láthatóság a BLE mellett): ha a
  //     felhasználó ezen keresztül párosította az eszközt, az Android a
  //     közös azonosító miatt a BLE kapcsolatot is "bond-oltnak" tekintette,
  //     és titkosítást várt - amit a BLE szerver nem biztosított -> ugyanaz
  //     a GATT error 133 hiba, csatlakozáskor.
  // A megbízható működés érdekében az eszköz TISZTÁN BLE-only marad, nyitott
  // (titkosítás nélküli) GATT kapcsolattal - az app saját BLE-keresésével
  // (nem a telefon általános Bluetooth-listájából) érhető el és csatlakoztatható.

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  NimBLEService *pService = pServer->createService(SERVICE_UUID);

  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX, NIMBLE_PROPERTY::NOTIFY); // a CCCD (0x2902) leírót a NimBLE automatikusan létrehozza

  // FONTOS: WRITE_NR is kell, mert az app OTA alatt WithoutResponse
  // (fire-and-forget) írást használ - enélkül a chunkok elvesznek/lassulnak.
  pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pRxCharacteristic->setCallbacks(new MyCallbacks());

  pServer->start(); // NimBLE 2.x: a szolgáltatások indítása a szerverrel együtt
  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->enableScanResponse(true);
  NimBLEDevice::startAdvertising();
}
