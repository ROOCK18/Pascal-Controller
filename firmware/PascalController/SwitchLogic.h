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
      uint16_t freq = ch.freq[idx];
      int pin = (channelNum == 1) ? FAN1_PWM_PIN : FAN2_PWM_PIN;
      int &curFreq = (channelNum == 1) ? FAN1_PWM_FREQ : FAN2_PWM_FREQ;
      if (freq != curFreq && freq >= 10 && freq <= 25000) {
        ledcDetach(pin);
        curFreq = freq;
        ledcAttach(pin, curFreq, PWM_RESOLUTION);
      }
      if (channelNum == 1) { pwmFan = pwm; setPWM(3, pwmFan); }
      else                 { pwmFan2 = pwm; setPWM(4, pwmFan2); }
    }
  } else {
    // Relé-logika HÁROM lehetséges érték: v==-1 -> "Termosztátot követi"
    // (nem nyúlunk a relé lábhoz, azt applyThermostat() már eldöntötte -
    // ez csak mode3-ban értelmezhető, ahol VAN is termosztát); v==0 -> mindig
    // KI; v>0 -> mindig BE. A tényleges lábírás setRelayAutomation()-on át
    // megy, hogy az idő alapú BE-tartás/tiltás (ha be van kapcsolva) itt is
    // érvényesüljön.
    if (v < 0) {
      // "Termosztátot követi" - csak mode3-ban értelmezhető (ott van
      // termosztát is). Mode4-ben (nincs termosztát) biztonsági okból KI-t
      // jelent, nehogy határozatlan (előző, esetleg véletlenszerű) állapotban
      // maradjon a relé.
      if (currentMode != 3) setRelayAutomation(channelNum, false);
      // mode3-ban szándékosan nem írunk semmit - a relé állapotát az
      // applyThermostat() hiszterézise már beállította.
    } else {
      setRelayAutomation(channelNum, v != 0);
    }
  }
}

void applySwitchLogic() {
  // Mód-guard: a kapcsoló-alapú logika CSAK mode 3/4-ben fusson (ott van
  // ténylegesen kapcsoló bekötve). Mode 0/1/2-ben az NTC1/NTC2 lábak
  // hőmérőként vannak használva - egy esetlegesen "ittmaradt" bekapcsolt
  // logika ne tudjon a hőmérséklet analóg értékéből "kapcsoló-állapotot"
  // értelmezve beavatkozni.
  if (currentMode != 3 && currentMode != 4) return;

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

// Parancs: SET_LOGIC:<F1|F2|R1|R2>:<0/1>:<v_OO>:<v_OC>:<v_CO>:<v_CC>:<axis2Mode>[:<f_OO>:<f_OC>:<f_CO>:<f_CC>]
// A freki mezők csak ventilátor (F1/F2) célnál értelmezettek. Relénél a v
// mostantól -1 is lehet: "Termosztátot követi" (csak mode3-ban értelmes).
void handleSetLogic(const String &body) {
  String tok[11]; int n = 0, start = 0;
  while (n < 11) {
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
    ch.val[i] = isFan ? (v < 0 ? -1 : constrain(v, 0, 100)) : (v < 0 ? -1 : (v != 0 ? 1 : 0));
  }
  ch.axis2Mode = (n >= 7) ? (uint8_t)tok[6].toInt() : 0;
  if (isFan && n >= 11) {
    for (int i = 0; i < 4; i++) ch.freq[i] = (uint16_t)constrain(tok[7 + i].toInt(), 10, 25000);
  }
  markDirty();
  notifyLine("LOGIC_OK");
}

void handleGetLogic(const String &target) {
  if (target != "F1" && target != "F2" && target != "R1" && target != "R2") return;
  SwitchLogicChannel &ch = getLogicChannel(target);
  bool isFan = (target == "F1" || target == "F2");
  String out = "LOAD_LOGIC:" + target + ":" + String(ch.enabled ? 1 : 0);
  for (int i = 0; i < 4; i++) out += ":" + String(ch.val[i]);
  out += ":" + String(ch.axis2Mode);
  if (isFan) for (int i = 0; i < 4; i++) out += ":" + String(ch.freq[i]);
  notifyLine(out);
}

void handleDelLogic(const String &target) {
  if (target != "F1" && target != "F2" && target != "R1" && target != "R2") return;
  getLogicChannel(target) = SwitchLogicChannel();
  markDirty();
  notifyLine("LOGIC_OK");
}
