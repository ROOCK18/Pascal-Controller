// ============================================================
// Alerts.h - Riasztások (értesítés a telefonra) és relé-impulzus
//
// Az eszköz figyeli a hőmérő(k)et, és ha
//   - a szenzor hibás (szakadt / rövidzárt / értelmezhetetlen tartomány),
//   - a hőmérséklet az alsó határ alatt van,
//   - a hőmérséklet a felső határ felett van,
// BLE-n "ALERT:<szenzor 1|2>:<állapot>:<érték>" sort küld az appnak (állapot: 0 = rendben
// (megszűnt), 1 = szenzorhiba, 2 = túl alacsony, 3 = túl magas). Az app ebből Android értesítést csinál.
//
// A riasztás nem fél a villanástól: az új állapotnak legalább ALERT_DEBOUNCE_MS ideig fenn kell állnia,
// és a határoknál 0,5 °C hiszterézis van (nincs "villogó" riasztás a határon).
// Ha az app épp nincs csatlakozva, a riasztás állapota megmarad: csatlakozáskor a GET_ALERT
// újraküldi az aktív riasztásokat.
// ============================================================
#pragma once

#define ALERT_DEBOUNCE_MS 5000UL
#define ALERT_HYST        0.5f

enum AlertState : uint8_t { AL_OK = 0, AL_FAULT = 1, AL_LOW = 2, AL_HIGH = 3 };

static uint8_t alertState[2]   = { AL_OK, AL_OK };   // megerősített állapot szenzoronként
static uint8_t alertPending[2] = { AL_OK, AL_OK };   // várakozó (még nem megerősített) állapot
static unsigned long alertPendingSince[2] = { 0, 0 };

// Mely hőmérők léteznek az aktuális módban (bit0 = 1. szenzor, bit1 = 2. szenzor)
static uint8_t alertSensorMask() {
  switch (currentMode) {
    case 0: return 0x03;           // 2 hőmérő
    case 1: case 2: case 3: return 0x01;   // 1 hőmérő / DHT11 / 1 kapcsoló + 1 hőmérő
    default: return 0x00;          // 2 kapcsoló: nincs hőmérő
  }
}

static float alertValue(int i) { return i == 0 ? ntc1Temperature : ntc2Temperature; }

// Az aktuális mérésből adódó állapot; "cur" a jelenlegi megerősített állapot (a hiszterézishez).
static uint8_t alertClassify(int i, uint8_t cur) {
  float t = alertValue(i);
  if (isnan(t)) return alertCfg.faultOn ? AL_FAULT : AL_OK;
  if (alertCfg.lowOn) {
    float limit = alertCfg.low + (cur == AL_LOW ? ALERT_HYST : 0.0f);
    if (t < limit) return AL_LOW;
  }
  if (alertCfg.highOn) {
    float limit = alertCfg.high - (cur == AL_HIGH ? ALERT_HYST : 0.0f);
    if (t > limit) return AL_HIGH;
  }
  return AL_OK;
}

static void alertSend(int i) {
  float v = alertValue(i);
  notifyLine("ALERT:" + String(i + 1) + ":" + String(alertState[i]) + ":" + (isnan(v) ? String("nan") : String(v, 1)));
}

// Van-e most aktív riasztás (a kijelzőn jelzésre)
bool alertActive() { return alertState[0] != AL_OK || alertState[1] != AL_OK; }

// Másodpercenként egyszer hívandó
void checkAlerts() {
  static unsigned long lastCheck = 0;
  unsigned long now = millis();
  if (now - lastCheck < 1000) return;
  lastCheck = now;

  uint8_t mask = alertSensorMask();
  for (int i = 0; i < 2; i++) {
    uint8_t want = AL_OK;
    if (alertCfg.enabled && (mask & (1 << i))) want = alertClassify(i, alertState[i]);

    if (want == alertState[i]) { alertPending[i] = want; continue; }
    if (want != alertPending[i]) { alertPending[i] = want; alertPendingSince[i] = now; continue; }
    // A kívánt állapot elég ideje stabil -> megerősítjük és jelezzük
    if (now - alertPendingSince[i] >= ALERT_DEBOUNCE_MS) {
      alertState[i] = want;
      alertSend(i);
    }
  }
}

// Parancs: SET_ALERT:<bekapcsolva 0/1>:<alsó határ be 0/1>:<alsó °C>:<felső határ be 0/1>:<felső °C>:<szenzorhiba 0/1>
void handleSetAlert(const String &body) {
  String tok[6]; int n = 0, start = 0;
  while (n < 6) {
    int c = body.indexOf(':', start);
    if (c < 0) { tok[n++] = body.substring(start); break; }
    tok[n++] = body.substring(start, c);
    start = c + 1;
  }
  if (n < 6) return;
  alertCfg.enabled = tok[0].toInt() != 0;
  alertCfg.lowOn = tok[1].toInt() != 0;
  alertCfg.low = constrain(tok[2].toFloat(), -50.0f, 150.0f);
  alertCfg.highOn = tok[3].toInt() != 0;
  alertCfg.high = constrain(tok[4].toFloat(), -50.0f, 150.0f);
  alertCfg.faultOn = tok[5].toInt() != 0;
  // Új beállítás: minden szenzor újraértékelődik (az aktív riasztásokat az app újra megkapja)
  for (int i = 0; i < 2; i++) { alertState[i] = AL_OK; alertPending[i] = AL_OK; }
  markDirty();
  notifyLine("ALERT_OK");
}

// GET_ALERT: a beállítás + az épp aktív riasztások
void handleGetAlert() {
  notifyLine("LOAD_ALERT:" + String(alertCfg.enabled ? 1 : 0) + ":" + String(alertCfg.lowOn ? 1 : 0) + ":" +
             String(alertCfg.low, 1) + ":" + String(alertCfg.highOn ? 1 : 0) + ":" + String(alertCfg.high, 1) + ":" +
             String(alertCfg.faultOn ? 1 : 0));
  for (int i = 0; i < 2; i++)
    if (alertState[i] != AL_OK) alertSend(i);
}
