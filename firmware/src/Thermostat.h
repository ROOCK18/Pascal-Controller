// ============================================================
// Thermostat.h - Ventilátor-görbe és relé-termosztát logika
// ============================================================
#pragma once

// Lineáris interpoláció a görbe pontjai között (a pontok hőmérséklet
// szerint növekvő sorrendben vannak tárolva - lásd handleSetTherm).
// count==1 esetén a görbe egy konstans %-ot ad, hőmérséklettől függetlenül.
int interpolateFanCurve(ThermFan &tf, float t) {
  uint8_t n = (tf.count < 1) ? 1 : (tf.count > FAN_CURVE_MAX_POINTS ? FAN_CURVE_MAX_POINTS : tf.count);
  if (n == 1) return tf.pct[0];
  if (t <= tf.temp[0]) return tf.pct[0];
  for (int i = 0; i < n - 1; i++) {
    if (t <= tf.temp[i + 1]) {
      float span = tf.temp[i + 1] - tf.temp[i];
      if (span <= 0) return tf.pct[i];
      float frac = (t - tf.temp[i]) / span;
      return (int)(tf.pct[i] + frac * ((int)tf.pct[i + 1] - (int)tf.pct[i]));
    }
  }
  return tf.pct[n - 1];
}

// A PWM frekvenciát NEM interpoláljuk folyamatosan (minden ledcDetach/
// ledcAttach hívás megszakítja/újraindítja a PWM-et - ha ez másodpercenként
// többször történne, az instabil/villódzó fordulatszámot adna). Ehelyett a
// hőmérséklethez LEGKÖZELEBBI pont frekvenciáját vesszük át.
uint16_t nearestFreqForCurve(ThermFan &tf, float t) {
  uint8_t n = (tf.count < 1) ? 1 : (tf.count > FAN_CURVE_MAX_POINTS ? FAN_CURVE_MAX_POINTS : tf.count);
  int nearest = 0;
  float best = fabs(t - tf.temp[0]);
  for (int i = 1; i < n; i++) {
    float d = fabs(t - tf.temp[i]);
    if (d < best) { best = d; nearest = i; }
  }
  return tf.freq[nearest];
}

// Közös segédfüggvény: a görbe alapján beállítja egy ventilátor %-át ÉS
// (ha szükséges) PWM frekvenciáját. channelNum: 1=Fan1, 2=Fan2.
// Ezt használja mind az applyThermostat(), mind a kapcsoló-logika "görbe
// szerint" (-1) esete.
void applyFanFromCurve(ThermFan &tf, int channelNum) {
  float t = getSensorTemp(tf.sensor);
  int pct = interpolateFanCurve(tf, t);
  uint16_t freq = nearestFreqForCurve(tf, t);
  int pwm = constrain((int)(pct * 2.55), 0, 255);

  if (channelNum == 1) {
    if (freq != FAN1_PWM_FREQ && freq >= 10 && freq <= 25000) {
      ledcDetach(FAN1_PWM_PIN);
      FAN1_PWM_FREQ = freq;
      ledcAttach(FAN1_PWM_PIN, FAN1_PWM_FREQ, PWM_RESOLUTION);
    }
    pwmFan = pwm;
    setPWM(3, pwmFan);
  } else {
    if (freq != FAN2_PWM_FREQ && freq >= 10 && freq <= 25000) {
      ledcDetach(FAN2_PWM_PIN);
      FAN2_PWM_FREQ = freq;
      ledcAttach(FAN2_PWM_PIN, FAN2_PWM_FREQ, PWM_RESOLUTION);
    }
    pwmFan2 = pwm;
    setPWM(4, pwmFan2);
  }
}

void applyThermostat() {
  // Mód-guard: a hőmérő-alapú görbe/termosztát CSAK akkor fusson, ha az
  // aktuális módban ténylegesen van hőmérő (mode 0/1/2/3). Mode 4-ben
  // ("2 kapcsoló") NINCS hőmérő - ott mindkét láb kapcsolóként van bekötve,
  // ezért egy esetlegesen korábbi módból "ittmaradt" bekapcsolt görbe/
  // termosztát ne tudjon értelmetlen (kapcsoló-jelből számolt "hőfok")
  // adat alapján beavatkozni.
  if (currentMode == 4) return;

  // Minden csatorna a saját SET_THERM "enabled" flag-je ÉS a saját Auto/Kézi
  // (fan1Auto/fan2Auto/relay1Auto/relay2Auto) állapota alapján fut. Kézi
  // módban a Ctrl oldal direkt parancsai döntenek, az automatika szünetel.

  // --- Ventilátorok: 1-5 pontos, szakaszonként lineáris görbe (% és Hz is) ---
  if (thermF1.enabled && fan1Auto) applyFanFromCurve(thermF1, 1);
  if (thermF2.enabled && fan2Auto) applyFanFromCurve(thermF2, 2);

  // --- Relék: hiszterézises BE/KI kapcsolás ---
  // Ha ton < toff: FŰTÉS logika (BE ha hideg, KI ha túl meleg).
  // Ha ton >= toff: HŰTÉS logika (BE ha meleg, KI ha hideg).
  // FONTOS: a relé lábat SOSEM írjuk közvetlenül - mindig setRelayAutomation()-
  // on keresztül, MINDEN ciklusban (nemcsak állapotváltáskor), hogy az idő
  // alapú BE-tartás/tiltás (RelayTimer) folyamatosan érvényesülhessen akkor
  // is, ha a hiszterézis "gondolata" időközben nem változik.
  if (thermR1.enabled && relay1Auto) {
    float t = getSensorTemp(thermR1.sensor);
    if (isnan(t)) {
      // Szenzorhiba: biztonságos állapot = KI (különben egy szakadt szenzornál
      // a fűtés tartósan bekapcsolva maradhatna).
      thermR1.state = false;
      setRelayAutomation(1, false);
    } else {
    if (thermR1.ton < thermR1.toff) { // fűtés
      if (!thermR1.state && t <= thermR1.ton)  thermR1.state = true;
      if (thermR1.state  && t >= thermR1.toff) thermR1.state = false;
    } else { // hűtés
      if (!thermR1.state && t >= thermR1.ton)  thermR1.state = true;
      if (thermR1.state  && t <= thermR1.toff) thermR1.state = false;
    }
    setRelayAutomation(1, thermR1.state);
    }
  }
  if (thermR2.enabled && relay2Auto) {
    float t = getSensorTemp(thermR2.sensor);
    if (isnan(t)) {
      // Szenzorhiba: biztonságos állapot = KI (különben egy szakadt szenzornál
      // a fűtés tartósan bekapcsolva maradhatna).
      thermR2.state = false;
      setRelayAutomation(2, false);
    } else {
    if (thermR2.ton < thermR2.toff) { // fűtés
      if (!thermR2.state && t <= thermR2.ton)  thermR2.state = true;
      if (thermR2.state  && t >= thermR2.toff) thermR2.state = false;
    } else { // hűtés
      if (!thermR2.state && t >= thermR2.ton)  thermR2.state = true;
      if (thermR2.state  && t <= thermR2.toff) thermR2.state = false;
    }
    setRelayAutomation(2, thermR2.state);
    }
  }
}

// Egyszerű beszúrásos rendezés hőmérséklet szerint növekvő sorrendbe -
// a pct-értékeket a temp-tel együtt mozgatja, hogy a párosítás megmaradjon.
// Csak az első "n" (aktív) pontot rendezi.
void sortFanCurve(ThermFan &tf, uint8_t n) {
  for (int i = 1; i < n; i++) {
    float tKey = tf.temp[i]; uint8_t pKey = tf.pct[i];
    int j = i - 1;
    while (j >= 0 && tf.temp[j] > tKey) {
      tf.temp[j + 1] = tf.temp[j];
      tf.pct[j + 1]  = tf.pct[j];
      j--;
    }
    tf.temp[j + 1] = tKey;
    tf.pct[j + 1]  = pKey;
  }
}

// Parancs (ventilátor): SET_THERM:<F1|F2>:<0/1>:<sensor>:<n>:<t1>:<p1>:<f1>:...:<tn>:<pn>:<fn>
//   n = 1..5 aktív pont, minden pontnak SAJÁT PWM frekvenciája (Hz) is van.
// Parancs (relé):       SET_THERM:<R1|R2>:<0/1>:<ton>:<toff>:<sensor>
void handleSetTherm(const String &body) {
  // Tokenizálás ':' mentén (max 20 mező elég mindkét formátumhoz: 4 fejléc + 5x3 pont)
  String tok[20]; int n = 0;
  int start = 0;
  while (n < 20) {
    int c = body.indexOf(':', start);
    if (c < 0) { tok[n++] = body.substring(start); break; }
    tok[n++] = body.substring(start, c);
    start = c + 1;
  }
  if (n < 1) return;
  String target = tok[0];

  if (target == "F1" || target == "F2") {
    if (n < 5) return; // target,en,sensor,count,min 1x(t,p,f) = 5
    bool en = tok[1].toInt() != 0;
    uint8_t sensor = tok[2].toInt(); if (sensor < 1 || sensor > 3) sensor = 1;
    uint8_t cnt = tok[3].toInt();
    if (cnt < 1) cnt = 1;
    if (cnt > FAN_CURVE_MAX_POINTS) cnt = FAN_CURVE_MAX_POINTS;

    int needed = 4 + cnt * 3; // pontonként (t,p,f) = 3 mező
    if (n < needed) return;

    ThermFan &tf = (target == "F1") ? thermF1 : thermF2;
    tf.enabled = en;
    tf.sensor = sensor;
    tf.count = cnt;
    for (int i = 0; i < cnt; i++) {
      tf.temp[i] = tok[4 + i * 3].toFloat();
      tf.pct[i]  = (uint8_t)constrain(tok[5 + i * 3].toInt(), 0, 100);
      long f = tok[6 + i * 3].toInt();
      tf.freq[i] = (uint16_t)constrain(f, 10, 25000);
    }
    sortFanCurve(tf, cnt);
    markDirty();
  notifyLine("THERM_OK");
  }
  else if (target == "R1" || target == "R2") {
    if (n < 5) return;
    bool en = tok[1].toInt() != 0;
    float v1 = tok[2].toFloat();
    float v2 = tok[3].toFloat();
    uint8_t sensor = tok[4].toInt(); if (sensor < 1 || sensor > 3) sensor = 1;
    ThermRelay &tr = (target == "R1") ? thermR1 : thermR2;
    tr.enabled = en; tr.ton = v1; tr.toff = v2; tr.sensor = sensor;
    markDirty();
  notifyLine("THERM_OK");
  }
}

void handleGetTherm(const String &target) {
  String out = "LOAD_THERM:" + target + ":";
  if (target == "F1" || target == "F2") {
    ThermFan &tf = (target == "F1") ? thermF1 : thermF2;
    out += String(tf.enabled ? 1 : 0) + ":" + String(tf.sensor) + ":" + String(tf.count);
    for (int i = 0; i < tf.count; i++)
      out += ":" + String(tf.temp[i], 1) + ":" + String(tf.pct[i]) + ":" + String(tf.freq[i]);
  }
  else if (target == "R1" || target == "R2") {
    ThermRelay &tr = (target == "R1") ? thermR1 : thermR2;
    out += String(tr.enabled ? 1 : 0) + ":" + String(tr.ton, 1) + ":" + String(tr.toff, 1) + ":" + String(tr.sensor);
  }
  else return;
  notifyLine(out);
}

// Parancs: DEL_THERM:<F1|F2|R1|R2> - alapállapotba állítja és kikapcsolja
void handleDelTherm(const String &target) {
  if (target == "F1") thermF1 = ThermFan();
  else if (target == "F2") thermF2 = ThermFan();
  else if (target == "R1") thermR1 = ThermRelay();
  else if (target == "R2") thermR2 = ThermRelay();
  else return;
  markDirty();
  notifyLine("THERM_OK");
}
