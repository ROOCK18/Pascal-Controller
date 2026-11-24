// ============================================================
// Mqtt.h - MQTT + Home Assistant (MQTT discovery)
//
// Az eszköz a WiFi-n át csatlakozik egy MQTT-brókerhez (pl. a Home Assistant
// Mosquitto-ja), és magát automatikusan bejelenti a Home Assistantnak:
//   - hőmérők / páratartalom / kapcsoló-állapot (szenzorok, a mód szerint)
//   - Relé 1-2 (kapcsoló), Ventilátor 1-2 (fan, százalékkal), RGB (fény)
//   - "automata" kapcsolók csatornánként (Auto/Kézi)
// A Google Home / Alexa / Apple Home a Home Assistanton keresztül éri el ezeket.
//
// A hálózati rész KÜLÖN FreeRTOS taskban fut (a csatlakozás blokkolhat néhány
// másodpercig, ez nem állíthatja meg a kijelzőt és a vezérlést).
// A beállítások (host, port, felhasználó, jelszó, név) NVS-ben vannak, BLE-n
// (SET_MQTT / GET_MQTT) és az appból állíthatók.
//
// Témák: pascal/<id>/...   (id = "pascal_" + a MAC utolsó 3 bájtja)
//   állapot:  pascal/<id>/temp1 | temp2 | hum | k1 | k2 | relay1 | relay2 |
//             fan1/state | fan1/pct | fan2/state | fan2/pct | rgb/state | rgb/rgb |
//             fan1_auto | fan2_auto | relay1_auto | relay2_auto | status
//   parancs:  ugyanezek "/set" végződéssel (fan: /pct/set, rgb: /rgb/set)
//
// FIGYELEM: egy csatornára érkező kézi parancs (Home Assistant) a csatornát KÉZI
// módba kapcsolja (különben a termosztát azonnal visszaírná). Az "automata"
// kapcsolóval lehet visszaállítani.
// ============================================================
#pragma once

#include <PubSubClient.h>
#include <esp_mac.h>

static WiFiClient mqttNet;
static PubSubClient mqttClient(mqttNet);
static String mqttId, mqttBase;
static unsigned long mqttLastAttempt = 0;
static int mqttDiscoveredMode = -1;
static volatile bool mqttForcePublish = false;
static const int MQTT_CACHE = 24;
static String mqttCache[MQTT_CACHE];
static unsigned long mqttLastFull = 0;

static String jsonEsc(const String &s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

static void mqttInitIds() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char buf[24];
  snprintf(buf, sizeof(buf), "pascal_%02x%02x%02x", mac[3], mac[4], mac[5]);
  mqttId = buf;
  mqttBase = "pascal/" + mqttId;
}

// ---------------- Discovery ----------------
static String mqttDeviceBlock() {
  String name = (mqttCfg.name[0] != 0) ? String(mqttCfg.name) : String("Pascal-Controller");
  return String("\"dev\":{\"ids\":[\"") + mqttId + "\"],\"name\":\"" + jsonEsc(name) +
         "\",\"mf\":\"Amicro\",\"mdl\":\"Pascal-Controller\",\"sw\":\"" FW_VERSION "\"}";
}

static String mqttCfgHead(const char *obj, const char *name) {
  return String("{\"~\":\"") + mqttBase + "\",\"name\":\"" + name + "\",\"uniq_id\":\"" + mqttId + "_" + obj +
         "\",\"avty_t\":\"~/status\"," + mqttDeviceBlock();
}

static void mqttPubConfig(const char *component, const char *obj, const String &payload) {
  String topic = "homeassistant/" + String(component) + "/" + mqttId + "/" + obj + "/config";
  mqttClient.publish(topic.c_str(), payload.c_str(), true); // üres payload = entitás törlése
}

static void mqttPublishDiscovery() {
  // --- mindig jelen lévők ---
  for (int i = 1; i <= 2; i++) {
    String o = "relay" + String(i);
    mqttPubConfig("switch", o.c_str(), mqttCfgHead(o.c_str(), i == 1 ? "Relé 1" : "Relé 2") +
      ",\"stat_t\":\"~/" + o + "\",\"cmd_t\":\"~/" + o + "/set\"}");

    String f = "fan" + String(i);
    mqttPubConfig("fan", f.c_str(), mqttCfgHead(f.c_str(), i == 1 ? "Ventilátor 1" : "Ventilátor 2") +
      ",\"stat_t\":\"~/" + f + "/state\",\"cmd_t\":\"~/" + f + "/set\"," +
      "\"pct_stat_t\":\"~/" + f + "/pct\",\"pct_cmd_t\":\"~/" + f + "/pct/set\"," +
      "\"spd_rng_min\":1,\"spd_rng_max\":100}");

    String fa = f + "_auto";
    mqttPubConfig("switch", fa.c_str(), mqttCfgHead(fa.c_str(), i == 1 ? "Ventilátor 1 automata" : "Ventilátor 2 automata") +
      ",\"stat_t\":\"~/" + fa + "\",\"cmd_t\":\"~/" + fa + "/set\",\"ent_cat\":\"config\"}");

    String ra = o + "_auto";
    mqttPubConfig("switch", ra.c_str(), mqttCfgHead(ra.c_str(), i == 1 ? "Relé 1 automata" : "Relé 2 automata") +
      ",\"stat_t\":\"~/" + ra + "\",\"cmd_t\":\"~/" + ra + "/set\",\"ent_cat\":\"config\"}");
  }
  mqttPubConfig("light", "rgb", mqttCfgHead("rgb", "RGB") +
    ",\"stat_t\":\"~/rgb/state\",\"cmd_t\":\"~/rgb/set\",\"rgb_stat_t\":\"~/rgb/rgb\",\"rgb_cmd_t\":\"~/rgb/rgb/set\"}");

  // --- a módtól függő szenzorok ---
  bool t1 = (currentMode == 0 || currentMode == 1 || currentMode == 2 || currentMode == 3);
  bool t2 = (currentMode == 0);
  bool hum = (currentMode == 2);
  bool k1 = (currentMode == 3 || currentMode == 4);
  bool k2 = (currentMode == 4);

  auto tempCfg = [&](const char *obj, const char *name) {
    return mqttCfgHead(obj, name) + ",\"stat_t\":\"~/" + obj +
           "\",\"unit_of_meas\":\"°C\",\"dev_cla\":\"temperature\",\"stat_cla\":\"measurement\"}";
  };
  mqttPubConfig("sensor", "temp1", t1 ? tempCfg("temp1", currentMode == 0 ? "Hőmérő 1" : "Hőmérséklet") : String(""));
  mqttPubConfig("sensor", "temp2", t2 ? tempCfg("temp2", "Hőmérő 2") : String(""));
  mqttPubConfig("sensor", "hum", hum ? (mqttCfgHead("hum", "Páratartalom") +
      ",\"stat_t\":\"~/hum\",\"unit_of_meas\":\"%\",\"dev_cla\":\"humidity\",\"stat_cla\":\"measurement\"}") : String(""));
  mqttPubConfig("binary_sensor", "k1", k1 ? (mqttCfgHead("k1", currentMode == 4 ? "K1 kapcsoló" : "Kapcsoló") +
      ",\"stat_t\":\"~/k1\",\"pl_on\":\"ON\",\"pl_off\":\"OFF\"}") : String(""));
  mqttPubConfig("binary_sensor", "k2", k2 ? (mqttCfgHead("k2", "K2 kapcsoló") +
      ",\"stat_t\":\"~/k2\",\"pl_on\":\"ON\",\"pl_off\":\"OFF\"}") : String(""));
  mqttDiscoveredMode = currentMode;
}

// ---------------- Állapot-közzététel ----------------
// Csak akkor küld, ha az érték változott (vagy a teljes frissítés ideje jött el).
static void mqttPubIf(int idx, const String &suffix, const String &value, bool full) {
  if (!full && mqttCache[idx] == value) return;
  mqttCache[idx] = value;
  String topic = mqttBase + "/" + suffix;
  mqttClient.publish(topic.c_str(), value.c_str(), true);
}

static String mqttTemp(float t) { return isnan(t) ? String("None") : String(t, 1); }
static String mqttOnOff(bool b) { return b ? String("ON") : String("OFF"); }

static void mqttPublishStates(bool full) {
  int i = 0;
  if (currentMode <= 3) mqttPubIf(i, "temp1", mqttTemp(ntc1Temperature), full); i++;
  if (currentMode == 0) mqttPubIf(i, "temp2", mqttTemp(ntc2Temperature), full); i++;
  if (currentMode == 2) mqttPubIf(i, "hum", mqttTemp(humidity), full); i++;
  if (currentMode == 3) mqttPubIf(i, "k1", mqttOnOff(isSwitchClosed(NTC2_PIN)), full);
  else if (currentMode == 4) mqttPubIf(i, "k1", mqttOnOff(isSwitchClosed(NTC1_PIN)), full);
  i++;
  if (currentMode == 4) mqttPubIf(i, "k2", mqttOnOff(isSwitchClosed(NTC2_PIN)), full); i++;
  mqttPubIf(i++, "relay1", mqttOnOff(digitalRead(RELAY1_PIN)), full);
  mqttPubIf(i++, "relay2", mqttOnOff(digitalRead(RELAY2_PIN)), full);
  mqttPubIf(i++, "fan1/state", mqttOnOff(pwmFan > 0), full);
  mqttPubIf(i++, "fan1/pct", String((int)round(pwmFan / 2.55)), full);
  mqttPubIf(i++, "fan2/state", mqttOnOff(pwmFan2 > 0), full);
  mqttPubIf(i++, "fan2/pct", String((int)round(pwmFan2 / 2.55)), full);
  mqttPubIf(i++, "rgb/state", mqttOnOff(digitalRead(RGB_ON)), full);
  mqttPubIf(i++, "rgb/rgb", String(pwmR) + "," + String(pwmG) + "," + String(pwmB), full);
  mqttPubIf(i++, "fan1_auto", mqttOnOff(fan1Auto), full);
  mqttPubIf(i++, "fan2_auto", mqttOnOff(fan2Auto), full);
  mqttPubIf(i++, "relay1_auto", mqttOnOff(relay1Auto), full);
  mqttPubIf(i++, "relay2_auto", mqttOnOff(relay2Auto), full);
}

// ---------------- Parancsok ----------------
static void mqttSetAuto(const String &target, bool en) {
  if (target == "fan1") fan1Auto = en;
  else if (target == "fan2") fan2Auto = en;
  else if (target == "relay1") {
    if (en && !relay1Auto) { relayTimerR1.timedActive = false; relayTimerR1.cooldownActive = false; relayTimerR1.waitingForReversal = false; }
    relay1Auto = en;
  } else if (target == "relay2") {
    if (en && !relay2Auto) { relayTimerR2.timedActive = false; relayTimerR2.cooldownActive = false; relayTimerR2.waitingForReversal = false; }
    relay2Auto = en;
  }
}

static void mqttOnMessage(char *topic, byte *payload, unsigned int len) {
  String t(topic), p;
  for (unsigned int i = 0; i < len && i < 64; i++) p += (char)payload[i];
  p.trim();
  String pre = mqttBase + "/";
  if (!t.startsWith(pre)) return;
  String s = t.substring(pre.length());
  bool on = p.equalsIgnoreCase("ON") || p == "1";

  if (s == "relay1/set" || s == "relay2/set") {
    int n = (s == "relay1/set") ? 1 : 2;
    mqttSetAuto("relay" + String(n), false);                 // kézi parancs -> kézi mód
    digitalWrite(n == 1 ? RELAY1_PIN : RELAY2_PIN, on ? HIGH : LOW);
  }
  else if (s == "fan1/set" || s == "fan2/set") {
    int n = (s == "fan1/set") ? 1 : 2;
    mqttSetAuto("fan" + String(n), false);
    int &cur = (n == 1) ? pwmFan : pwmFan2;
    setPWM(n == 1 ? 3 : 4, on ? (cur > 0 ? cur : 255) : 0);
  }
  else if (s == "fan1/pct/set" || s == "fan2/pct/set") {
    int n = (s == "fan1/pct/set") ? 1 : 2;
    mqttSetAuto("fan" + String(n), false);
    int pct = constrain(p.toInt(), 0, 100);
    setPWM(n == 1 ? 3 : 4, (int)round(pct * 2.55));
  }
  else if (s == "rgb/set") {
    digitalWrite(RGB_ON, on ? HIGH : LOW);
  }
  else if (s == "rgb/rgb/set") {
    int r = 0, g = 0, b = 0;
    if (sscanf(p.c_str(), "%d,%d,%d", &r, &g, &b) == 3) {
      pwmR = constrain(r, 0, 255); pwmG = constrain(g, 0, 255); pwmB = constrain(b, 0, 255);
      updatePWMOutputs();
      digitalWrite(RGB_ON, HIGH);
    }
  }
  else if (s.endsWith("_auto/set")) {
    mqttSetAuto(s.substring(0, s.length() - 9), on);
  }
  else return;

  markDirty();
  mqttForcePublish = true;
}

// ---------------- Task ----------------
static void mqttTask(void *) {
  mqttInitIds();
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(100));

    if (mqttReconf) {
      mqttReconf = false;
      if (mqttClient.connected()) mqttClient.disconnect();
      mqttLastAttempt = 0;
    }

    bool wifiOk = (WiFi.status() == WL_CONNECTED);
    bool busy = otaInProgress || wifiOtaInProgress || otaUrlPending; // frissítés alatt a hálózat az OTA-é
    if (!mqttCfg.enabled || mqttCfg.host[0] == 0) {
      if (mqttClient.connected()) mqttClient.disconnect();
      mqttState = 0;
      continue;
    }
    if (!wifiOk || busy) {
      if (mqttClient.connected()) mqttClient.disconnect();
      mqttState = 1;
      continue;
    }

    if (!mqttClient.connected()) {
      mqttState = 2;
      if (mqttLastAttempt != 0 && millis() - mqttLastAttempt < 10000) continue; // 10 mp-es újrapróbálás
      mqttLastAttempt = millis();
      mqttClient.setServer(mqttCfg.host, mqttCfg.port);
      mqttClient.setBufferSize(1200);
      mqttClient.setKeepAlive(30);
      mqttClient.setSocketTimeout(5);
      mqttClient.setCallback(mqttOnMessage);
      String lwt = mqttBase + "/status";
      bool ok;
      if (mqttCfg.user[0])
        ok = mqttClient.connect(mqttId.c_str(), mqttCfg.user, mqttCfg.pass, lwt.c_str(), 0, true, "offline");
      else
        ok = mqttClient.connect(mqttId.c_str(), nullptr, nullptr, lwt.c_str(), 0, true, "offline");
      if (!ok) continue;

      mqttClient.publish(lwt.c_str(), "online", true);
      String sub1 = mqttBase + "/+/set", sub2 = mqttBase + "/+/pct/set", sub3 = mqttBase + "/rgb/rgb/set";
      mqttClient.subscribe(sub1.c_str());
      mqttClient.subscribe(sub2.c_str());
      mqttClient.subscribe(sub3.c_str());
      mqttDiscoveredMode = -1;
      for (int i = 0; i < MQTT_CACHE; i++) mqttCache[i] = "\x01";
      mqttLastFull = 0;
    }

    mqttState = 3;
    mqttClient.loop();
    if (!mqttClient.connected()) continue;

    if (mqttDiscoveredMode != currentMode) { mqttPublishDiscovery(); mqttLastFull = 0; } // az új entitások kapjanak állapotot

    bool full = (millis() - mqttLastFull > 60000) || mqttLastFull == 0;
    if (full) mqttLastFull = millis();
    static unsigned long lastPub = 0;
    if (full || mqttForcePublish || millis() - lastPub > 2000) {
      mqttForcePublish = false;
      lastPub = millis();
      mqttPublishStates(full);
    }
  }
}

void mqttStart() {
  xTaskCreatePinnedToCore(mqttTask, "mqtt", 8192, nullptr, 1, nullptr, 0);
}
