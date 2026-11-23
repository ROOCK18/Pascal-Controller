// ============================================================
// SwitchLogic.h - Kapcsoló-alapú logika (1/2 kapcsoló mód)
// ============================================================
#pragma once

void applyLogicChannel(SwitchLogicChannel &ch, bool isFan, int channelNum) {
  if (!ch.enabled) return;

  // FONTOS: a K1 kapcsoló lába MÓDTÓL FÜGG!
  //  - mode 4 ("2 kapcsoló"): K1=NTC1_PIN, K2=NTC2_PIN (egyik láb sem hőmérő).
  //  - mode 3 ("1 kapcsoló+1 hőmérő"): a hőmérő az NTC1_PIN-en van, ezért az
  //    EGYETLEN kapcsoló (K1) a NTC2_PIN-en van - nincs K2 tengely.
  // Ha ezt összekevernénk, mode 3-ban a hőmérséklet analóg értéke (NTC1)
  // véletlenszerűen "kapcsoló"-ként viselkedne a hőfok függvényében.
  bool sw1closed;
  int axis2;
  if (currentMode == 4) {
    sw1closed = isSwitchClosed(NTC1_PIN);
    axis2 = (ch.axis2Mode == 2) ? 0 : (isSwitchClosed(NTC2_PIN) ? 1 : 0);
  } else {
    sw1closed = isSwitchClosed(NTC2_PIN);
    axis2 = 0; // mode 3-ban nincs K2, csak K1 számít
  }
  int idx = (sw1closed ? 2 : 0) | axis2;
  int v = ch.val[idx];

  if (isFan) {
    ThermFan &tf = (channelNum == 1) ? thermF1 : thermF2;
    if (v < 0) {
      // "Görbe szerint" - a % ÉS a PWM frekvencia is az 5 pontos görbéből jön
      applyFanFromCurve(tf, channelNum);
    } else {
      int pwm = constrain((int)(v * 2.55), 0, 255);
      if (channelNum == 1) { pwmFan = pwm; setPWM(3, pwmFan); }
      else                 { pwmFan2 = pwm; setPWM(4, pwmFan2); }
    }
  } else {
    // Kapcsolós módban a relé-logika TISZTÁN BE/KI: v!=0 -> mindig húz,
    // v==0 -> mindig elenged. (Nincs "termosztát dönt" köztes állapot -
    // 2 kapcsoló módban nincs is hőmérséklet-alapú termosztát koncepció.)
    int pin = (channelNum == 1) ? RELAY1_PIN : RELAY2_PIN;
    digitalWrite(pin, v != 0 ? HIGH : LOW);
  }
}

void applySwitchLogic() {
  if (fan1Auto)    applyLogicChannel(logicF1, true, 1);
  if (fan2Auto)    applyLogicChannel(logicF2, true, 2);
  if (relay1Auto)  applyLogicChannel(logicR1, false, 1);
  if (relay2Auto)  applyLogicChannel(logicR2, false, 2);
}

SwitchLogicChannel &getLogicChannel(const String &target) {
  if (target == "F1") return logicF1;
  if (target == "F2") return logicF2;
  if (target == "R2") return logicR2;
  return logicR1; // "R1" és minden más esetben (védelem)
}

// Parancs: SET_LOGIC:<F1|F2|R1|R2>:<0/1>:<v_OO>:<v_OC>:<v_CO>:<v_CC>:<axis2Mode>
void handleSetLogic(const String &body) {
  String tok[7]; int n = 0, start = 0;
  while (n < 7) {
    int c = body.indexOf(':', start);
    if (c < 0) { tok[n++] = body.substring(start); break; }
    tok[n++] = body.substring(start, c);
    start = c + 1;
  }
  if (n < 6) return;
  String target = tok[0];
  if (target != "F1" && target != "F2" && target != "R1" && target != "R2") return;

  SwitchLogicChannel &ch = getLogicChannel(target);
  bool isFan = (target == "F1" || target == "F2");
  ch.enabled = tok[1].toInt() != 0;
  for (int i = 0; i < 4; i++) {
    int v = tok[2 + i].toInt();
    ch.val[i] = isFan ? (v < 0 ? -1 : constrain(v, 0, 100)) : (v != 0 ? 1 : 0);
  }
  ch.axis2Mode = (n >= 7) ? (uint8_t)tok[6].toInt() : 0;
  notifyLine("LOGIC_OK");
}

void handleGetLogic(const String &target) {
  if (target != "F1" && target != "F2" && target != "R1" && target != "R2") return;
  SwitchLogicChannel &ch = getLogicChannel(target);
  String out = "LOAD_LOGIC:" + target + ":" + String(ch.enabled ? 1 : 0);
  for (int i = 0; i < 4; i++) out += ":" + String(ch.val[i]);
  out += ":" + String(ch.axis2Mode);
  notifyLine(out);
}

void handleDelLogic(const String &target) {
  if (target != "F1" && target != "F2" && target != "R1" && target != "R2") return;
  getLogicChannel(target) = SwitchLogicChannel();
  notifyLine("LOGIC_OK");
}
