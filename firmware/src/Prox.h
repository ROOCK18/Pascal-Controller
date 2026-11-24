// ============================================================
// Prox.h - Közeledés-automatika csatornánként (ventilátor 1-2, relé 1-2, RGB)
//
// A telefon (app) közeledéskor "PROX_TRIGGER" parancsot küld. Az eszköz ilyenkor azokat a csatornákat
// kapcsolja, amelyeknél a közeledés-automatika be van kapcsolva ÉS az automatika is be van kapcsolva
// (a ventilátor/relé "Automata" állapotban van) - ugyanúgy, ahogy a többi automatikus szabály is csak
// Automata módban dolgozik. Az RGB-nek nincs Automata/Kézi módja, ott a közeledés-kapcsoló önmagában dönt.
//
// Csatornánként: be/ki, érték (ventilátor: % ; relé/RGB: nincs) és időtartam (mp):
//   ventilátor: a megadott % -ra áll az időtartam alatt, utána visszaáll az előző értékre (0 mp = marad)
//   relé:       az időtartamra bekapcsol (impulzus), 0 mp = a relé "Kapuzás" beállított ideje
//   RGB:        az időtartamra bekapcsol, utána kikapcsol (0 mp = bekapcsolva marad)
// ============================================================
#pragma once

#define PROX_CH 5   // 0=F1, 1=F2, 2=R1, 3=R2, 4=RGB
static const char *PROX_NAMES[PROX_CH] = { "F1", "F2", "R1", "R2", "RGB" };
static unsigned long proxEnd[PROX_CH] = { 0, 0, 0, 0, 0 };
static bool proxActive[PROX_CH] = { false, false, false, false, false };
static int proxPrior[2] = { 0, 0 };   // a ventilátorok közeledés előtti értéke

static bool proxChannelAuto(int i) {
  switch (i) {
    case 0: return fan1Auto;
    case 1: return fan2Auto;
    case 2: return relay1Auto;
    case 3: return relay2Auto;
    default: return true;   // RGB: nincs Automata/Kézi mód
  }
}

static int proxFanDuty(int i) { return constrain((int)proxCh[i].value * 255 / 100, 0, 255); }

// PROX_TRIGGER: a közeledés-automatikák végrehajtása
void proxTrigger() {
  int applied = 0;
  unsigned long now = millis();
  for (int i = 0; i < PROX_CH; i++) {
    ProxCh &p = proxCh[i];
    if (!p.enabled || !proxChannelAuto(i)) continue;
    applied++;
    if (i < 2) {
      int &pwm = (i == 0) ? pwmFan : pwmFan2;
      if (!proxActive[i]) proxPrior[i] = pwm;
      setPWM(i == 0 ? 3 : 4, proxFanDuty(i));
      proxActive[i] = p.durSec > 0;
      proxEnd[i] = now + (unsigned long)p.durSec * 1000UL;
    } else if (i < 4) {
      int relay = i - 1;   // 2 -> 1. relé, 3 -> 2. relé
      uint32_t sec = p.durSec > 0 ? p.durSec : (relay == 1 ? relayTimerR1 : relayTimerR2).onDurationSec;
      startRelayPulse(relay, sec);
    } else {
      digitalWrite(RGB_ON, HIGH);
      proxActive[4] = p.durSec > 0;
      proxEnd[4] = now + (unsigned long)p.durSec * 1000UL;
    }
  }
  notifyLine("PROX_OK:" + String(applied));
}

// Az automatika (termosztát/logika/időzítő) UTÁN hívandó: az aktív közeledés-művelet felülírja, lejáratkor visszaáll
void applyProxOverrides() {
  for (int i = 0; i < PROX_CH; i++) {
    if (!proxActive[i]) continue;
    bool expired = (long)(millis() - proxEnd[i]) >= 0;
    if (i < 2) {
      int &pwm = (i == 0) ? pwmFan : pwmFan2;
      int want = expired ? proxPrior[i] : proxFanDuty(i);
      if (expired) proxActive[i] = false;
      if (pwm != want) setPWM(i == 0 ? 3 : 4, want);
    } else if (i == 4) {
      if (expired) { proxActive[4] = false; digitalWrite(RGB_ON, LOW); }
      else digitalWrite(RGB_ON, HIGH);
    }
  }
}

// SET_PROX:<F1|F2|R1|R2|RGB>:<be 0/1>:<érték 0-100>:<időtartam mp>
void handleSetProx(const String &body) {
  String tok[4]; int n = 0, start = 0;
  while (n < 4) {
    int c = body.indexOf(':', start);
    if (c < 0) { tok[n++] = body.substring(start); break; }
    tok[n++] = body.substring(start, c);
    start = c + 1;
  }
  if (n < 4) return;
  int idx = -1;
  for (int i = 0; i < PROX_CH; i++) if (tok[0] == PROX_NAMES[i]) idx = i;
  if (idx < 0) return;
  proxCh[idx].enabled = tok[1].toInt() != 0;
  proxCh[idx].value = (uint8_t)constrain(tok[2].toInt(), 0, 100);
  proxCh[idx].durSec = (uint16_t)constrain(tok[3].toInt(), 0, 65535);
  markDirty();
  notifyLine("PROXCFG_OK");
}

// GET_PROX: minden csatorna beállítása
void handleGetProx() {
  for (int i = 0; i < PROX_CH; i++)
    notifyLine("LOAD_PROX:" + String(PROX_NAMES[i]) + ":" + String(proxCh[i].enabled ? 1 : 0) + ":" +
               String(proxCh[i].value) + ":" + String(proxCh[i].durSec));
}
