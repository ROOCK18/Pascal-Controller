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
  notifyLine("RTIMER_OK");
}

// Parancs: DEL_FTIMER:<F1|F2> - törli a slotokat és alaphelyzetbe áll
void handleDelFTimer(const String &target) {
  TimerSet &ts = (target == "F1") ? ftimerF1 : ftimerF2;
  ts = TimerSet();
  notifyLine("FTIMER_OK");
}

// Parancs: DEL_RTIMER:<R1|R2> - törli a slotokat és alaphelyzetbe áll
void handleDelRTimer(const String &target) {
  TimerSet &ts = (target == "R1") ? rtimerR1 : rtimerR2;
  ts = TimerSet();
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
