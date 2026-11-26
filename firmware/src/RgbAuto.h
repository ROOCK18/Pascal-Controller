// ============================================================
// RgbAuto.h - RGB automatikák: Automata/Kézi, időzítő, hőmérséklet-görbe, napszak-görbe, közeledés
//
// Csak Automata üzemmódban (rgbAuto) dolgozik; Kézi állapotban egyik automatika sem ír az RGB-re.
// Kézi állapotban egyik automatika sem működik; Automata állapotban a kézi RGB-vezérlés (BLE, menü, MQTT) nem működik.
// Elsőbbség (felülről lefelé):  közeledés  >  időzítő  >  hőmérséklet-görbe  >  napszak-görbe (önállóan).
// A SZÍNT mindig az éppen író automatika adja. A FÉNYERŐT, ha a napszak-görbe fut, MINDIG a napszak-görbe pillanatnyi értéke adja
// (a görbének nincs színe), kivéve a KI műveleteket, azok kikapcsolnak. Ha a görbe nem fut, az automatika saját fényereje érvényes.
//   - közeledés: X méteren belülre érve a beállított színnel felfut (felfutási idő); a "világítási mód":
//       0 = csak felkapcsol (a felfutás után a többi automatika dönt, kikapcsolhatja),
//       1 = amíg ott vagyok (az app távozáskor PROX_LEAVE:RGB-t küld; ha a kapcsolat 15 mp-ig megszakad, az is távozás),
//       2 = a felfutás után X mp-ig világít, aztán a lefutási idővel kikapcsol.
//   - időzítő: napokra, órára eseményeket állít (be/ki, szín, fényerő, felfutás). Az utolsó, már elmúlt mai esemény
//       érvényes (az időzítő tulajdonában van az állapot a következő eseményig).
//   - napszak-görbe: a napszak szerinti pontok között folyamatosan változik a fényerő (éjfélen át körbeér); önállóan a legutóbbi automatika-színnel világít.
//   - hőmérséklet-görbe: a hőmérséklet szerinti pontok között folyamatosan változik a szín és a fényerő.
// Minden változás átmenet-motoron (rgbGoto) megy: a jelenlegi értékről a célra, a megadott idő alatt, lineárisan.
// ============================================================
#pragma once

enum RgbSrc : uint8_t { RS_NONE = 0, RS_PROX = 1, RS_TIMER = 2, RS_DAY = 3, RS_THERM = 4 };

// ── Átmenet-motor ──
static float rgbFrom[3] = { 0, 0, 0 };
static float rgbTo[3] = { 0, 0, 0 };
static int rgbKeep[3] = { 0, 0, 0 };            // kikapcsoláskor ezt a színt tartjuk meg (BE kézi gombra ugyanez jön vissza)
static unsigned long rgbRampStart = 0, rgbRampMs = 0;
static bool rgbRamping = false, rgbTargetOn = false;

static uint8_t rgbSrc = RS_NONE;
static int32_t rgbSrcKey = -1;
static bool rgbProxActive = false;
static unsigned long rgbProxStart = 0;
static int32_t rgbProxSerial = 1000000;
static unsigned long rgbDiscSince = 0;
static uint8_t rgbLastCol[3] = { 255, 255, 255 };   // az utolsó automatika-szín (ezzel világít a napszak-görbe önállóan)

static void rgbEngineStep() {
  if (!rgbRamping) return;
  unsigned long now = millis();
  float f = (rgbRampMs == 0) ? 1.0f : (float)(now - rgbRampStart) / (float)rgbRampMs;
  if (f > 1.0f) f = 1.0f;
  for (int i = 0; i < 3; i++) setPWM(i, (int)(rgbFrom[i] + (rgbTo[i] - rgbFrom[i]) * f + 0.5f));
  if (f >= 1.0f) {
    rgbRamping = false;
    if (!rgbTargetOn) {
      digitalWrite(RGB_ON, LOW);
      for (int i = 0; i < 3; i++) setPWM(i, rgbKeep[i]);   // a szín megmarad a következő BE-hez
    }
  }
}

// Cél: szín (0-255) fényerővel (%), be/ki, átmeneti idővel (mp)
static void rgbGoto(uint8_t r, uint8_t g, uint8_t b, uint8_t pct, bool on, uint16_t rampSec) {
  bool lit = digitalRead(RGB_ON) == HIGH;
  rgbFrom[0] = lit ? (float)pwmR : 0.0f;
  rgbFrom[1] = lit ? (float)pwmG : 0.0f;
  rgbFrom[2] = lit ? (float)pwmB : 0.0f;
  if (!on && lit && (pwmR | pwmG | pwmB) != 0) { rgbKeep[0] = pwmR; rgbKeep[1] = pwmG; rgbKeep[2] = pwmB; }
  float k = (float)constrain((int)pct, 0, 100) / 100.0f;
  rgbTo[0] = on ? (float)r * k : 0.0f;
  rgbTo[1] = on ? (float)g * k : 0.0f;
  rgbTo[2] = on ? (float)b * k : 0.0f;
  rgbTargetOn = on;
  if (on) digitalWrite(RGB_ON, HIGH);
  rgbRampStart = millis();
  rgbRampMs = (unsigned long)rampSec * 1000UL;
  rgbRamping = true;
  rgbEngineStep();
}

// ── Segédek ──
static bool rgbRunsToday(uint8_t days) {
  int bit = (dayOfWeekIdx(yr, month, day) + 6) % 7;   // Vasárnap(0) -> 6, Hétfő(1) -> 0, ...
  return (days >> bit) & 1;
}

static uint8_t rgbLerp8(uint8_t a, uint8_t b, float f) {
  return (uint8_t)constrain((int)((float)a + ((float)b - (float)a) * f + 0.5f), 0, 255);
}

static void rgbProxLeave() {
  rgbProxActive = false;
  rgbGoto(0, 0, 0, 0, false, rgbProxCfg.rampDownSec);
  rgbSrc = RS_NONE;
  rgbSrcKey = -1;
}

// PROX_TRIGGER:RGB - true, ha végrehajtotta
bool rgbProxTrigger() {
  if (!rgbAuto || !rgbProxCfg.enabled) return false;
  rgbProxActive = true;
  rgbProxStart = millis();
  rgbProxSerial++;
  return true;
}

// PROX_LEAVE:RGB - az app jelzi, hogy a telefon elhagyta a hatókört ("amíg ott vagyok" mód)
void rgbProxLeaveCmd() {
  if (rgbProxActive && rgbProxCfg.mode == 1) rgbProxLeave();
}

// A napszak-görbe pillanatnyi fényereje (%). false, ha a görbe nem fut (ki van kapcsolva, nincs pontja,
// vagy az adott napon nem él). A görbének nincs színe.
static bool rgbDayPct(uint8_t &tp) {
  if (!(rgbDayCfg.enabled && rgbDayCfg.count > 0 && rgbRunsToday(rgbDayCfg.days))) return false;
  int nowMin = hr * 60 + min_t;
  int n = rgbDayCfg.count;
  int prev = -1, next = -1;
  for (int i = 0; i < n; i++) if (rgbDayCfg.pts[i].minute <= nowMin) prev = i;       // a pontok növekvő sorrendben vannak
  for (int i = n - 1; i >= 0; i--) if (rgbDayCfg.pts[i].minute > nowMin) next = i;
  int prevMin, nextMin;
  const RgbDayPt *a, *b;
  if (prev < 0) { a = &rgbDayCfg.pts[n - 1]; prevMin = (int)a->minute - 1440; } else { a = &rgbDayCfg.pts[prev]; prevMin = (int)a->minute; }
  if (next < 0) { b = &rgbDayCfg.pts[0]; nextMin = (int)b->minute + 1440; } else { b = &rgbDayCfg.pts[next]; nextMin = (int)b->minute; }
  float span = (float)(nextMin - prevMin);
  float f = span <= 0.0f ? 0.0f : (float)(nowMin - prevMin) / span;
  f = constrain(f, 0.0f, 1.0f);
  tp = rgbLerp8(a->pct, b->pct, f);
  return true;
}

// Az átmenet-motor lépése + az automatikák kiértékelése. Minden loop()-ban hívandó.
void applyRgbAutomation() {
  if (!rgbAuto) {
    rgbRamping = false; rgbProxActive = false; rgbSrc = RS_NONE; rgbSrcKey = -1;
    return;
  }
  rgbEngineStep();

  static unsigned long lastEval = 0;
  unsigned long now = millis();
  if (now - lastEval < 500) return;
  lastEval = now;

  // A BLE-kapcsolat megszakadása ("amíg ott vagyok" mód)
  if (deviceConnected) rgbDiscSince = 0; else if (rgbDiscSince == 0) rgbDiscSince = now;

  if (rgbProxActive) {
    unsigned long upMs = (unsigned long)rgbProxCfg.rampUpSec * 1000UL;
    if (rgbProxCfg.mode == 0) {
      if (now - rgbProxStart >= upMs) rgbProxActive = false;   // csak felkapcsol: a felfutás után a többi automatika dönt
    } else if (rgbProxCfg.mode == 1) {
      if (rgbDiscSince != 0 && now - rgbDiscSince >= 15000UL) rgbProxLeave();
    } else {
      if (now - rgbProxStart >= upMs + (unsigned long)rgbProxCfg.holdSec * 1000UL) rgbProxLeave();
    }
  }

  // A napszak-görbe pillanatnyi fényereje: ha fut, minden automatika fényerejét ez adja
  uint8_t dp = 0;
  bool dayOn = rgbDayPct(dp);
  int32_t dayBits = dayOn ? ((int32_t)(dp + 1) << 24) : 0;   // a kulcsba is bekerül, így a fény követi a görbét

  // 1) közeledés
  if (rgbProxActive) {
    int32_t key = rgbProxSerial + dayBits;
    if (rgbSrc != RS_PROX || rgbSrcKey != key) {
      bool fresh = (rgbSrc != RS_PROX) || ((rgbSrcKey & 0xFFFFFF) != rgbProxSerial);
      uint8_t pct = dayOn ? dp : rgbProxCfg.pct;
      rgbLastCol[0] = rgbProxCfg.r; rgbLastCol[1] = rgbProxCfg.g; rgbLastCol[2] = rgbProxCfg.b;
      rgbGoto(rgbProxCfg.r, rgbProxCfg.g, rgbProxCfg.b, pct, pct > 0, fresh ? rgbProxCfg.rampUpSec : 2);
      rgbSrc = RS_PROX; rgbSrcKey = key;
    }
    return;
  }

  // 2) időzítő: a legutóbbi, már elmúlt mai esemény
  if (rgbTimerCfg.enabled && rgbTimerCfg.count > 0 && rgbRunsToday(rgbTimerCfg.days)) {
    int nowMin = hr * 60 + min_t;
    int best = -1, bestMin = -1;
    for (int i = 0; i < rgbTimerCfg.count; i++) {
      int m = rgbTimerCfg.slots[i].h * 60 + rgbTimerCfg.slots[i].m;
      if (m <= nowMin && m >= bestMin) { best = i; bestMin = m; }
    }
    if (best >= 0) {
      const RgbSlot &s = rgbTimerCfg.slots[best];
      int32_t base = 100000 + bestMin * 8 + best;
      int32_t key = base + (s.on ? dayBits : 0);
      if (rgbSrc != RS_TIMER || rgbSrcKey != key) {
        bool fresh = (rgbSrc != RS_TIMER) || ((rgbSrcKey & 0xFFFFFF) != base);
        uint8_t pct = (s.on && dayOn) ? dp : s.pct;
        if (s.on) { rgbLastCol[0] = s.r; rgbLastCol[1] = s.g; rgbLastCol[2] = s.b; }
        rgbGoto(s.r, s.g, s.b, pct, s.on != 0 && pct > 0, fresh ? s.rampSec : 2);
        rgbSrc = RS_TIMER; rgbSrcKey = key;
      }
      return;
    }
  }

  // 3) hőmérséklet-görbe (szín a görbéből) és 4) napszak-görbe önállóan (szín: az utolsó automatika-szín)
  bool have = false;
  uint8_t tr = 0, tg = 0, tb = 0, tp = 0;
  uint8_t src = RS_NONE;

  if (rgbThermCfg.enabled && rgbThermCfg.count > 0) {
    float t = getSensorTemp(rgbThermCfg.sensor);
    if (!isnan(t)) {
      int n = rgbThermCfg.count;
      const RgbThermPt *p = rgbThermCfg.pts;
      if (t <= p[0].temp) { tr = p[0].r; tg = p[0].g; tb = p[0].b; tp = p[0].pct; }
      else if (t >= p[n - 1].temp) { tr = p[n - 1].r; tg = p[n - 1].g; tb = p[n - 1].b; tp = p[n - 1].pct; }
      else {
        for (int i = 0; i < n - 1; i++) {
          if (t >= p[i].temp && t <= p[i + 1].temp) {
            float span = p[i + 1].temp - p[i].temp;
            float f = span <= 0.0f ? 0.0f : (t - p[i].temp) / span;
            tr = rgbLerp8(p[i].r, p[i + 1].r, f); tg = rgbLerp8(p[i].g, p[i + 1].g, f); tb = rgbLerp8(p[i].b, p[i + 1].b, f);
            tp = rgbLerp8(p[i].pct, p[i + 1].pct, f);
            break;
          }
        }
      }
      rgbLastCol[0] = tr; rgbLastCol[1] = tg; rgbLastCol[2] = tb;
      have = true; src = RS_THERM;
    }
  }
  if (!have && dayOn) {
    tr = rgbLastCol[0]; tg = rgbLastCol[1]; tb = rgbLastCol[2]; tp = dp;
    have = true; src = RS_DAY;
  }

  if (!have) { rgbSrc = RS_NONE; return; }
  if (dayOn) tp = dp;   // a fényerőt a napszak-görbe adja
  // a fényerő a színbe számolva; 0 fényerő = kikapcsolt állapot
  uint8_t sr = (uint8_t)((int)tr * tp / 100), sg = (uint8_t)((int)tg * tp / 100), sb = (uint8_t)((int)tb * tp / 100);
  int32_t key = ((int32_t)sr << 16) | ((int32_t)sg << 8) | (int32_t)sb;
  if (rgbSrc != src || rgbSrcKey != key) {
    rgbGoto(sr, sg, sb, 100, (sr | sg | sb) != 0, 2);
    rgbSrc = src; rgbSrcKey = key;
  }
}

// ── Parancsok ──
static int rgbSplit(const String &s, char sep, String *out, int maxN) {
  int n = 0, start = 0;
  while (n < maxN) {
    int c = s.indexOf(sep, start);
    if (c < 0) { out[n++] = s.substring(start); break; }
    out[n++] = s.substring(start, c);
    start = c + 1;
  }
  return n;
}

static void rgbParseHex(const String &s, uint8_t &r, uint8_t &g, uint8_t &b) {
  String h = s;
  if (h.startsWith("#")) h = h.substring(1);
  if (h.length() < 6) return;
  r = (uint8_t)strtol(h.substring(0, 2).c_str(), NULL, 16);
  g = (uint8_t)strtol(h.substring(2, 4).c_str(), NULL, 16);
  b = (uint8_t)strtol(h.substring(4, 6).c_str(), NULL, 16);
}

static String rgbHex(uint8_t r, uint8_t g, uint8_t b) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02X%02X%02X", r, g, b);
  return String(buf);
}

static void rgbResetEval() { rgbSrc = RS_NONE; rgbSrcKey = -1; }

// SET_RGBTIMER:<be>:<napok>|<óra>:<perc>:<be/ki>:<RRGGBB>:<fényerő>:<átmenet mp>|...
void handleSetRgbTimer(const String &body) {
  String part[7];
  int np = rgbSplit(body, '|', part, 7);
  String h[2];
  if (np < 1 || rgbSplit(part[0], ':', h, 2) < 2) return;
  RgbTimerCfg c;
  c.enabled = h[0].toInt() != 0;
  c.days = (uint8_t)constrain(h[1].toInt(), 0, 127);
  for (int i = 1; i < np && c.count < 5; i++) {
    String t[6];
    if (rgbSplit(part[i], ':', t, 6) < 6) continue;
    RgbSlot &s = c.slots[c.count];
    s.h = (uint8_t)constrain(t[0].toInt(), 0, 23);
    s.m = (uint8_t)constrain(t[1].toInt(), 0, 59);
    s.on = t[2].toInt() != 0 ? 1 : 0;
    rgbParseHex(t[3], s.r, s.g, s.b);
    s.pct = (uint8_t)constrain(t[4].toInt(), 0, 100);
    s.rampSec = (uint16_t)constrain(t[5].toInt(), 0, 3600);
    c.count++;
  }
  rgbTimerCfg = c;
  rgbResetEval();
  markDirty();
  notifyLine("RGBAUTO_OK");
}

// SET_RGBTHERM:<be>:<szenzor 1/2>|<hőmérséklet>:<RRGGBB>:<fényerő>|...   (a pontok hőmérséklet szerint rendezve tárolódnak)
void handleSetRgbTherm(const String &body) {
  String part[6];
  int np = rgbSplit(body, '|', part, 6);
  String h[2];
  if (np < 1 || rgbSplit(part[0], ':', h, 2) < 2) return;
  RgbThermCfg c;
  c.enabled = h[0].toInt() != 0;
  c.sensor = (h[1].toInt() == 2) ? 2 : 1;
  for (int i = 1; i < np && c.count < 5; i++) {
    String t[3];
    if (rgbSplit(part[i], ':', t, 3) < 3) continue;
    RgbThermPt &p = c.pts[c.count];
    p.temp = constrain(t[0].toFloat(), -50.0f, 150.0f);
    rgbParseHex(t[1], p.r, p.g, p.b);
    p.pct = (uint8_t)constrain(t[2].toInt(), 0, 100);
    c.count++;
  }
  for (int i = 1; i < c.count; i++) {           // beszúró rendezés hőmérséklet szerint
    RgbThermPt x = c.pts[i];
    int j = i - 1;
    while (j >= 0 && c.pts[j].temp > x.temp) { c.pts[j + 1] = c.pts[j]; j--; }
    c.pts[j + 1] = x;
  }
  rgbThermCfg = c;
  rgbResetEval();
  markDirty();
  notifyLine("RGBAUTO_OK");
}

// SET_RGBDAY:<be>:<napok>|<óra>:<perc>:<fényerő>|...   (max. 10 pont, napszak szerint rendezve tárolódnak)
void handleSetRgbDay(const String &body) {
  String part[11];
  int np = rgbSplit(body, '|', part, 11);
  String h[2];
  if (np < 1 || rgbSplit(part[0], ':', h, 2) < 2) return;
  RgbDayCfg c;
  c.enabled = h[0].toInt() != 0;
  c.days = (uint8_t)constrain(h[1].toInt(), 0, 127);
  for (int i = 1; i < np && c.count < 10; i++) {
    String t[4];
    int nt = rgbSplit(part[i], ':', t, 4);   // új alak: óra:perc:fényerő (a régi óra:perc:szín:fényerő is jó, a színt eldobjuk)
    if (nt < 3) continue;
    RgbDayPt &p = c.pts[c.count];
    p.minute = (uint16_t)(constrain(t[0].toInt(), 0, 23) * 60 + constrain(t[1].toInt(), 0, 59));
    p.r = p.g = p.b = 255;
    p.pct = (uint8_t)constrain(t[nt - 1].toInt(), 0, 100);
    c.count++;
  }
  for (int i = 1; i < c.count; i++) {
    RgbDayPt x = c.pts[i];
    int j = i - 1;
    while (j >= 0 && c.pts[j].minute > x.minute) { c.pts[j + 1] = c.pts[j]; j--; }
    c.pts[j + 1] = x;
  }
  rgbDayCfg = c;
  rgbResetEval();
  markDirty();
  notifyLine("RGBAUTO_OK");
}

// SET_RGBPROX:<be>:<RRGGBB>:<fényerő>:<felfutás mp>:<mód 0/1/2>:<világítás mp>:<lefutás mp>[:<régi, figyelmen kívül marad>]
void handleSetRgbProx(const String &body) {
  String t[8];
  int n = rgbSplit(body, ':', t, 8);
  if (n < 7) return;
  RgbProxCfg c;
  c.capByDay = 0;   // már nem használt: a napszak-görbe fényereje mindig érvényes, ha fut
  c.enabled = t[0].toInt() != 0;
  rgbParseHex(t[1], c.r, c.g, c.b);
  c.pct = (uint8_t)constrain(t[2].toInt(), 0, 100);
  c.rampUpSec = (uint16_t)constrain(t[3].toInt(), 0, 3600);
  c.mode = (uint8_t)constrain(t[4].toInt(), 0, 2);
  c.holdSec = (uint16_t)constrain(t[5].toInt(), 0, 65535);
  c.rampDownSec = (uint16_t)constrain(t[6].toInt(), 0, 3600);
  rgbProxCfg = c;
  markDirty();
  notifyLine("RGBAUTO_OK");
}

// GET_RGBAUTO: Automata/Kézi + az összes RGB-automatika beállítása
void handleGetRgbAuto() {
  notifyLine("LOAD_RGBAUTO:" + String(rgbAuto ? 1 : 0));

  String s = "LOAD_RGBTIMER:" + String(rgbTimerCfg.enabled ? 1 : 0) + ":" + String(rgbTimerCfg.days);
  for (int i = 0; i < rgbTimerCfg.count; i++) {
    const RgbSlot &x = rgbTimerCfg.slots[i];
    s += "|" + String(x.h) + ":" + String(x.m) + ":" + String(x.on) + ":" + rgbHex(x.r, x.g, x.b) + ":" + String(x.pct) + ":" + String(x.rampSec);
  }
  notifyLine(s);

  s = "LOAD_RGBTHERM:" + String(rgbThermCfg.enabled ? 1 : 0) + ":" + String(rgbThermCfg.sensor);
  for (int i = 0; i < rgbThermCfg.count; i++) {
    const RgbThermPt &x = rgbThermCfg.pts[i];
    s += "|" + String(x.temp, 1) + ":" + rgbHex(x.r, x.g, x.b) + ":" + String(x.pct);
  }
  notifyLine(s);

  s = "LOAD_RGBDAY:" + String(rgbDayCfg.enabled ? 1 : 0) + ":" + String(rgbDayCfg.days);
  for (int i = 0; i < rgbDayCfg.count; i++) {
    const RgbDayPt &x = rgbDayCfg.pts[i];
    s += "|" + String(x.minute / 60) + ":" + String(x.minute % 60) + ":" + String(x.pct);
  }
  notifyLine(s);

  notifyLine("LOAD_RGBPROX:" + String(rgbProxCfg.enabled ? 1 : 0) + ":" + rgbHex(rgbProxCfg.r, rgbProxCfg.g, rgbProxCfg.b) + ":" +
             String(rgbProxCfg.pct) + ":" + String(rgbProxCfg.rampUpSec) + ":" + String(rgbProxCfg.mode) + ":" +
             String(rgbProxCfg.holdSec) + ":" + String(rgbProxCfg.rampDownSec));
}

// SET_AUTO:RGB:<0/1>
void rgbSetAuto(bool en) {
  rgbAuto = en;
  rgbResetEval();
  rgbProxActive = false;
  markDirty();
}
