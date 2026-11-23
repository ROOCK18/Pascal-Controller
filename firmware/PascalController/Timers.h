// ============================================================
// Timers.h - Idő szerinti időzítők (FTIMER/RTIMER)
// ============================================================
#pragma once

void parseSlots(const String &slotData, TimerSet &ts) {
  ts.count = 0;
  int start = 0;
  while (start < (int)slotData.length() && ts.count < MAX_SLOTS) {
    int bar = slotData.indexOf('|', start);
    String piece = (bar < 0) ? slotData.substring(start) : slotData.substring(start, bar);
    int c1 = piece.indexOf(':');
    int c2 = piece.indexOf(':', c1 + 1);
    if (c1 > 0 && c2 > c1) {
      ts.slots[ts.count].h = piece.substring(0, c1).toInt();
      ts.slots[ts.count].m = piece.substring(c1 + 1, c2).toInt();
      ts.slots[ts.count].v = piece.substring(c2 + 1).toInt();
      ts.slots[ts.count].used = true;
      ts.count++;
    }
    if (bar < 0) break;
    start = bar + 1;
  }
}

// Parancs: SET_FTIMER:<F1|F2>:<days>:<H>:<M>:<pct>|...
void handleSetFTimer(const String &body) {
  int p1 = body.indexOf(':');
  int p2 = body.indexOf(':', p1 + 1);
  if (p1 < 0) return;
  String target = body.substring(0, p1);
  int rest2 = (p2 < 0) ? body.length() : p2;
  int days = body.substring(p1 + 1, rest2).toInt();
  String slotData = (p2 < 0) ? "" : body.substring(p2 + 1);

  TimerSet &ts = (target == "F1") ? ftimerF1 : ftimerF2;
  ts.days = days;
  parseSlots(slotData, ts);
  markDirty();
  notifyLine("FTIMER_OK");
}

// Parancs: SET_RTIMER:<R1|R2>:<days>:<H>:<M>:<0/1>|...
void handleSetRTimer(const String &body) {
  int p1 = body.indexOf(':');
  int p2 = body.indexOf(':', p1 + 1);
  if (p1 < 0) return;
  String target = body.substring(0, p1);
  int rest2 = (p2 < 0) ? body.length() : p2;
  int days = body.substring(p1 + 1, rest2).toInt();
  String slotData = (p2 < 0) ? "" : body.substring(p2 + 1);

  TimerSet &ts = (target == "R1") ? rtimerR1 : rtimerR2;
  ts.days = days;
  parseSlots(slotData, ts);
  markDirty();
  notifyLine("RTIMER_OK");
}

// Parancs: DEL_FTIMER:<F1|F2> - törli a slotokat és alaphelyzetbe áll
void handleDelFTimer(const String &target) {
  TimerSet &ts = (target == "F1") ? ftimerF1 : ftimerF2;
  ts = TimerSet();
  markDirty();
  notifyLine("FTIMER_OK");
}

// Parancs: DEL_RTIMER:<R1|R2> - törli a slotokat és alaphelyzetbe áll
void handleDelRTimer(const String &target) {
  TimerSet &ts = (target == "R1") ? rtimerR1 : rtimerR2;
  ts = TimerSet();
  markDirty();
  notifyLine("RTIMER_OK");
}

String buildLoadTimer(const String &prefix, const String &target, TimerSet &ts) {
  String out = prefix + target + ":" + String(ts.days);
  for (int i = 0; i < ts.count; i++)
    out += ":" + String(ts.slots[i].h) + ":" + String(ts.slots[i].m) + ":" + String(ts.slots[i].v);
  return out;
}

void applyTimers() {
  if (hr * 60 + min_t == lastAppliedMinute) return; // csak percenként egyszer
  lastAppliedMinute = hr * 60 + min_t;

  for (int i = 0; i < ftimerF1.count; i++)
    if (ftimerF1.slots[i].h == hr && ftimerF1.slots[i].m == min_t) { pwmFan = constrain(ftimerF1.slots[i].v * 255 / 100, 0, 255); setPWM(3, pwmFan); }
  for (int i = 0; i < ftimerF2.count; i++)
    if (ftimerF2.slots[i].h == hr && ftimerF2.slots[i].m == min_t) { pwmFan2 = constrain(ftimerF2.slots[i].v * 255 / 100, 0, 255); setPWM(4, pwmFan2); }
  for (int i = 0; i < rtimerR1.count; i++)
    if (rtimerR1.slots[i].h == hr && rtimerR1.slots[i].m == min_t) digitalWrite(RELAY1_PIN, rtimerR1.slots[i].v ? HIGH : LOW);
  for (int i = 0; i < rtimerR2.count; i++)
    if (rtimerR2.slots[i].h == hr && rtimerR2.slots[i].m == min_t) digitalWrite(RELAY2_PIN, rtimerR2.slots[i].v ? HIGH : LOW);
}

// ============================================================
// IDŐ ALAPÚ RELÉKAPCSOLÁS (RelayTimer) - BLE parancsok
// Parancs: SET_RHOLD:<R1|R2>:<0/1>:<onDurationSec>:<cooldownSec>
// ============================================================
RelayTimer &getRelayTimer(const String &target) {
  return (target == "R1") ? relayTimerR1 : relayTimerR2;
}

// Parancs: SET_RHOLD:<R1|R2>:<0/1>:<onDurationSec>:<cooldownSec>:<mode 0=Fix/1=Max>
// Parancs: SET_RHOLD:<R1|R2>:<0/1>:<onDurationSec>:<cooldownSec>:<mode 0=Fix/1=Max>:<lockMode 0=Nincs/1=Ido/2=Jelvaltasig>
// Parancs: SET_RHOLD:<R1|R2>:<0/1>:<onDurationSec>:<cooldownSec>:<mode 0=Fix/1=Max>:<lockMode 0=Nincs/1=Ido/2=Jelvaltasig>:<stateFilter 0=BE/1=KI/2=MIND>
void handleSetRHold(const String &body) {
  String tok[7]; int n = 0, start = 0;
  while (n < 7) {
    int c = body.indexOf(':', start);
    if (c < 0) { tok[n++] = body.substring(start); break; }
    tok[n++] = body.substring(start, c);
    start = c + 1;
  }
  if (n < 4) return;
  String target = tok[0];
  if (target != "R1" && target != "R2") return;
  RelayTimer &rt = getRelayTimer(target);
  bool wasEnabled = rt.enabled;
  rt.enabled = tok[1].toInt() != 0;
  rt.onDurationSec = (uint16_t)constrain(tok[2].toInt(), 1, 36000);
  rt.cooldownSec = (uint16_t)constrain(tok[3].toInt(), 0, 36000);
  if (n >= 5) rt.timerMode = (uint8_t)constrain(tok[4].toInt(), 0, 1);
  if (n >= 6) rt.lockMode = (uint8_t)constrain(tok[5].toInt(), 0, 2);
  if (n >= 7) rt.stateFilter = (uint8_t)constrain(tok[6].toInt(), 0, 2);
  // Ha most kapcsolták be (vagy módosították bekapcsolt állapotban), friss
  // ciklussal induljon - ne ragadjon egy régi zárolási/időzített szakaszban.
  if (rt.enabled && !wasEnabled) { rt.timedActive = false; rt.cooldownActive = false; rt.waitingForReversal = false; }
  markDirty();
  notifyLine("RHOLD_OK");
}

void handleGetRHold(const String &target) {
  if (target != "R1" && target != "R2") return;
  RelayTimer &rt = getRelayTimer(target);
  notifyLine("LOAD_RHOLD:" + target + ":" + String(rt.enabled ? 1 : 0) + ":" +
             String(rt.onDurationSec) + ":" + String(rt.cooldownSec) + ":" +
             String(rt.timerMode) + ":" + String(rt.lockMode) + ":" + String(rt.stateFilter));
}

// ============================================================
// BEALLITASOK - Kompenzáció / Kijelző / Dátum-Idő pozíciók / Factory reset
// Ezeket a Beállítások menü vezette be a kijelzőn - itt tesszük elérhetővé
// BLE-n keresztül is, hogy az app Settings oldala is tudja állítani/olvasni.
// ============================================================

// Parancs: SET_COMP:<ntc1>:<ntc2>  (előjeles tizedes fokok, pl. -1.5)
void handleSetComp(const String &body) {
  int c = body.indexOf(':');
  if (c < 0) return;
  tempCompNTC1 = body.substring(0, c).toFloat();
  tempCompNTC2 = body.substring(c + 1).toFloat();
  markDirty();
  notifyLine("COMP_OK");
}

void handleGetComp() {
  notifyLine("LOAD_COMP:" + String(tempCompNTC1, 1) + ":" + String(tempCompNTC2, 1));
}

// Parancs: SET_DISPLAY:<brightness 0-100>:<sleepIdx 0-8>
void handleSetDisplay(const String &body) {
  int c = body.indexOf(':');
  if (c < 0) return;
  displayBrightness = (uint8_t)constrain(body.substring(0, c).toInt(), 1, 100);
  int idx = body.substring(c + 1).toInt();
  if (idx >= 0 && idx < SLEEP_OPTIONS_COUNT) sleepTimeoutIdx = (uint8_t)idx;
  applyBacklight();
  markDirty();
  notifyLine("DISPLAY_OK");
}

void handleGetDisplay() {
  notifyLine("LOAD_DISPLAY:" + String(displayBrightness) + ":" + String(sleepTimeoutIdx));
}

// Parancs: SET_DATEPOS:<clockPos 0-2>:<datePos 0-2>:<dayNamePos 0-2>:<timeFormat 0-2>
// Parancs: SET_DATEPOS:<clockPos>:<datePos>:<dayNamePos>:<timeFormat>:<dateFormat>
void handleSetDatePos(const String &body) {
  String tok[5]; int n = 0, start = 0;
  while (n < 5) {
    int c = body.indexOf(':', start);
    if (c < 0) { tok[n++] = body.substring(start); break; }
    tok[n++] = body.substring(start, c);
    start = c + 1;
  }
  if (n < 4) return;
  clockPos = (uint8_t)constrain(tok[0].toInt(), 0, 2);
  datePos = (uint8_t)constrain(tok[1].toInt(), 0, 2);
  dayNamePos = (uint8_t)constrain(tok[2].toInt(), 0, 2);
  timeFormat = (uint8_t)constrain(tok[3].toInt(), 0, 2);
  if (n >= 5) dateFormat = (uint8_t)constrain(tok[4].toInt(), 0, 4);
  idleNeedsFullClear = true;
  markDirty();
  notifyLine("DATEPOS_OK");
}

void handleGetDatePos() {
  notifyLine("LOAD_DATEPOS:" + String(clockPos) + ":" + String(datePos) + ":" +
             String(dayNamePos) + ":" + String(timeFormat) + ":" + String(dateFormat));
}

// Parancs: SET_DATE:<ev>:<honap>:<nap>  (a TIME: parancs kezeli az óra:perc:mp-et)
void handleSetDate(const String &body) {
  String tok[3]; int n = 0, start = 0;
  while (n < 3) {
    int c = body.indexOf(':', start);
    if (c < 0) { tok[n++] = body.substring(start); break; }
    tok[n++] = body.substring(start, c);
    start = c + 1;
  }
  if (n < 3) return;
  yr = (uint16_t)constrain(tok[0].toInt(), 2000, 2099);
  month = (uint8_t)constrain(tok[1].toInt(), 1, 12);
  day = (uint8_t)constrain(tok[2].toInt(), 1, daysInMonth(month, yr));
  markDirty();
  notifyLine("DATE_OK");
}
