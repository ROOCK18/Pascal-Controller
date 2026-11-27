// ============================================================
// PWMControl.h - PWM kimenetek beállítása és az óra léptetése
// ============================================================
#pragma once

// ============================================================
// ÓRA / PWM
// ============================================================
// Az adott hónap napjainak száma (szökőév-tudatos február)
uint8_t daysInMonth(uint8_t m, uint16_t y) {
  static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (m < 1 || m > 12) return 30;
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return days[m - 1];
}

void updateClock() {
  if (millis() - lastClockUpdate >= 1000) {
    lastClockUpdate = millis();
    sec++;
    if (sec >= 60) { sec = 0; min_t++; }
    if (min_t >= 60) { min_t = 0; hr++; }
    if (hr >= 24) {
      hr = 0;
      day++;
      if (day > daysInMonth(month, yr)) {
        day = 1;
        month++;
        if (month > 12) { month = 1; yr++; }
      }
    }
  }
}

void setupPWM() {
  ledcAttach(RGB_R_PIN, LED_PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(RGB_G_PIN, LED_PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(RGB_B_PIN, LED_PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(FAN1_PWM_PIN, FAN1_PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(FAN2_PWM_PIN, FAN2_PWM_FREQ, PWM_RESOLUTION);
  ledcWrite(RGB_R_PIN, 0); ledcWrite(RGB_G_PIN, 0); ledcWrite(RGB_B_PIN, 0);
  ledcWrite(FAN1_PWM_PIN, 0); ledcWrite(FAN2_PWM_PIN, 0);

  ledcAttach(TFT_BL, BACKLIGHT_PWM_FREQ, PWM_RESOLUTION);
  ledcWrite(TFT_BL, 255); // teljes fényerő induláskor (a betöltött érték a loadAllSettings() után kerül alkalmazásra)
}

// A jelenlegi displayBrightness (%) alkalmazása a háttérvilágításra - de csak
// ha a kijelző éppen nincs elalva (elalvás alatt mindig 0 a tényleges PWM).
void applyBacklight() {
  if (displayAsleep) return;
  ledcWrite(TFT_BL, map(displayBrightness, 0, 100, 0, 255));
}

// Bármilyen gombesemény hívja - felébreszti a kijelzőt (ha aludt) és
// nullázza az inaktivitás-számlálót.
void wakeDisplay() {
  lastUserActivity = millis();
  if (displayAsleep) {
    displayAsleep = false;
    applyBacklight();
  }
}

// A loop()-ból hívva: ha letelt a beállított elalvási idő, elalszik a
// kijelző (háttérvilágítás KI) - "Soha" esetén (SLEEP_TIMES_MS==0) sosem.
void checkDisplaySleep() {
  unsigned long timeout = SLEEP_TIMES_MS[sleepTimeoutIdx];
  if (timeout == 0 || displayAsleep) return;
  if (millis() - lastUserActivity >= timeout) {
    displayAsleep = true;
    ledcWrite(TFT_BL, 0);
  }
}

// FONTOS: a ventilátorok engedélyező lába (FAN1_ON/FAN2_ON) mostantól
// MINDIG automatikusan követi a PWM értéket - akár kézi szerkesztésből, akár
// automatikából (görbe/logika) jön a hívás. Ha a PWM 0, az engedélyező láb
// lekapcsolja a tápot; ha a PWM>0, átengedi. Enélkül korábban az automata
// üzemmód sosem kapcsolta be ténylegesen a ventilátort (a PWM láb önmagában
// csak a fordulatszámot szaggatja, a tápot az engedélyező láb adja rá).
void setPWM(int channel, int duty) {
  duty = constrain(duty, 0, 255);
  switch (channel) {
    // Az RGB csatornák a kalibrációs táblázaton át kerülnek a kimenetre (RgbCal.h); kalibrációs módban az app írja a kimenetet
    case 0: pwmR = duty; if (!calMode) ledcWrite(RGB_R_PIN, rgbCalMap(0, duty)); break;
    case 1: pwmG = duty; if (!calMode) ledcWrite(RGB_G_PIN, rgbCalMap(1, duty)); break;
    case 2: pwmB = duty; if (!calMode) ledcWrite(RGB_B_PIN, rgbCalMap(2, duty)); break;
    case 3:
      pwmFan = duty;
      ledcWrite(FAN1_PWM_PIN, duty);
      digitalWrite(FAN1_ON, duty > 0 ? HIGH : LOW);
      break;
    case 4:
      pwmFan2 = duty;
      ledcWrite(FAN2_PWM_PIN, duty);
      digitalWrite(FAN2_ON, duty > 0 ? HIGH : LOW);
      break;
  }
}

void updatePWMOutputs() {
  setPWM(0, pwmR); setPWM(1, pwmG); setPWM(2, pwmB);
  setPWM(3, pwmFan); setPWM(4, pwmFan2);
}

// ============================================================
// IDŐ ALAPÚ RELÉKAPCSOLÁS - két mód (Fix/Max) + választható zárolás
//
// Az automatika (termosztát VAGY kapcsoló-logika) SOHA nem írja közvetlenül
// a relé lábat - mindig ezen a függvényen keresztül kéri a kívánt állapotot.
// Kézi üzemmódban (relay1Auto/relay2Auto == false) ezek a függvények nem is
// futnak le, tehát a Kézi vezérlés ezt a mechanizmust teljesen megkerülve
// közvetlenül írja a relé lábat.
//
// MOD 0 - "Fix ido": amikor az automatika VÁLTani akar (a jelenlegitől eltérő
//   állapotot kér), a relé az ÚJ állapotot PONTOSAN onDurationSec másodpercig
//   tartja, FÜGGETLENÜL attól, hogy közben mit kér az automatika. Lejáratkor
//   visszaáll a váltás előtti állapotra, majd a zárolás lép életbe.
//
// MOD 1 - "Max ido": a relé VALÓS IDŐBEN követi az automatika kérését (pl.
//   egy pillanatnyi kapcsolójelre azonnal reagál, és ha a jel hamarabb eltűnik,
//   azonnal magától vissza is áll - EKKOR NINCS zárolás). DE ha a kérés a
//   váltás előtti állapottól eltérve onDurationSec másodpercnél tovább
//   tartana, a relé KÉNYSZERŰEN visszaáll - és EKKOR lép életbe a zárolás.
//
// ZÁROLÁS (mindkét módban ugyanaz a beállítás, a szakasz vége után fut le):
//   lockMode 0 - Nincs: azonnal újra reagálhat egy új váltásra.
//   lockMode 1 - Idő alapú: cooldownSec másodpercig semmi nem válthat újra.
//   lockMode 2 - Jelváltásig: addig nem reagál semmilyen új váltásra, amíg a
//     forrás (termosztát/logika) akár egy pillanatra is vissza NEM kéri a
//     szakasz ELŐTTI állapotot - tehát amíg "meg nem akarja valami
//     visszaváltani" a kezdeti (induló) állapotra.
// ============================================================
void applyRelayLockout(RelayTimer &rt, unsigned long now) {
  if (rt.lockMode == 1) { rt.cooldownActive = true; rt.phaseStart = now; }
  else if (rt.lockMode == 2) { rt.waitingForReversal = true; }
  // lockMode 0: nincs teendő, azonnal újra reagálhat
}

// A stateFilter alapján eldönti, hogy a MOSTANI váltás (targetState felé)
// a mechanizmus által felügyelt irány-e. A NEM felügyelt irányú váltás
// azonnal, korlátozás/időzítés nélkül végrehajtódik.
bool relayDirectionGoverned(RelayTimer &rt, bool targetState) {
  if (rt.stateFilter == 2) return true;       // MIND: mindkét irány felügyelt
  if (rt.stateFilter == 0) return targetState;  // BE: csak a BE-re kapcsolás felügyelt
  return !targetState;                          // KI: csak a KI-re kapcsolás felügyelt
}

// ── Relé-impulzus (pl. kapunyitás a telefonról): a relé N másodpercre bekapcsol, majd kikapcsol. ──
// Az impulzus alatt az automatika (termosztát/logika/időzítő) nem tudja kikapcsolni; a végén a relé KI lesz,
// utána az automatika (ha aktív) a következő ciklusban újra átveszi.
static bool relayPulseActive[2] = { false, false };
static unsigned long relayPulseEnd[2] = { 0, 0 };

void startRelayPulse(int relayNum, uint32_t seconds) {
  int i = (relayNum == 2) ? 1 : 0;
  relayPulseEnd[i] = millis() + seconds * 1000UL;
  relayPulseActive[i] = true;
  digitalWrite(i == 0 ? RELAY1_PIN : RELAY2_PIN, HIGH);
}

void applyRelayPulses() {
  for (int i = 0; i < 2; i++) {
    if (!relayPulseActive[i]) continue;
    int pin = (i == 0) ? RELAY1_PIN : RELAY2_PIN;
    if ((long)(millis() - relayPulseEnd[i]) >= 0) {
      relayPulseActive[i] = false;
      digitalWrite(pin, LOW);
    } else {
      digitalWrite(pin, HIGH);
    }
  }
}

void setRelayAutomation(int relayNum, bool wantOn) {
  int pin = (relayNum == 1) ? RELAY1_PIN : RELAY2_PIN;
  RelayTimer &rt = (relayNum == 1) ? relayTimerR1 : relayTimerR2;

  if (!rt.enabled) {
    digitalWrite(pin, wantOn ? HIGH : LOW);
    return;
  }

  unsigned long now = millis();

  // "Jelváltásig" zárolás: amíg a forrás nem kéri (akár pillanatra is) a
  // szakasz előtti (bázis) állapotot, a relé ott marad, minden mást figyelmen
  // kívül hagyunk.
  if (rt.waitingForReversal) {
    if (wantOn == rt.priorState) {
      rt.waitingForReversal = false; // megérkezett a "visszaváltás" - feloldva
    } else {
      digitalWrite(pin, rt.priorState ? HIGH : LOW);
      return;
    }
  }

  // Idő alapú zárolás
  if (rt.cooldownActive) {
    if (now - rt.phaseStart >= (unsigned long)rt.cooldownSec * 1000UL) {
      rt.cooldownActive = false;
    } else {
      digitalWrite(pin, rt.priorState ? HIGH : LOW);
      return;
    }
  }

  if (rt.timerMode == 1) {
    // ── MAX IDŐ MÓD ──
    bool curOut = digitalRead(pin);
    if (!rt.timedActive) {
      if (wantOn != curOut) {
        if (!relayDirectionGoverned(rt, wantOn)) {
          digitalWrite(pin, wantOn ? HIGH : LOW); // nem felügyelt irány: azonnal, korlátozás nélkül
          return;
        }
        rt.priorState = curOut;
        rt.timedActive = true;
        rt.phaseStart = now;
        digitalWrite(pin, wantOn ? HIGH : LOW);
      }
      // ha wantOn == curOut: nincs eltérés, nincs teendő
    } else {
      bool expired = (now - rt.phaseStart) >= (unsigned long)rt.onDurationSec * 1000UL;
      if (wantOn == rt.priorState) {
        // a kérés magától visszatért az alapállapotra - TERMÉSZETES
        // visszaállás, nincs zárolás (a forrás már úgyis a bázist kéri).
        rt.timedActive = false;
        digitalWrite(pin, rt.priorState ? HIGH : LOW);
      } else if (expired) {
        // letelt a max idő, de a kérés MÉG MINDIG eltér - KÉNYSZERÍTETT
        // visszaállás, ITT lép életbe a zárolás.
        rt.timedActive = false;
        digitalWrite(pin, rt.priorState ? HIGH : LOW);
        applyRelayLockout(rt, now);
      } else {
        digitalWrite(pin, wantOn ? HIGH : LOW); // követi a kérést (még a max időn belül)
      }
    }
    return;
  }

  // ── FIX IDŐ MÓD (alapértelmezett) ──
  if (rt.timedActive) {
    if (now - rt.phaseStart >= (unsigned long)rt.onDurationSec * 1000UL) {
      rt.timedActive = false;
      digitalWrite(pin, rt.priorState ? HIGH : LOW); // vissza a váltás előtti állapotra
      applyRelayLockout(rt, now);
    }
    return; // amíg tart az impulzus, nem reagálunk új kérésre
  }

  bool curOut = digitalRead(pin);
  if (wantOn != curOut) {
    if (!relayDirectionGoverned(rt, wantOn)) {
      digitalWrite(pin, wantOn ? HIGH : LOW); // nem felügyelt irány: azonnal, korlátozás nélkül
      return;
    }
    rt.priorState = curOut;
    rt.timedActive = true;
    rt.phaseStart = now;
    digitalWrite(pin, wantOn ? HIGH : LOW);
  }
}
