// ============================================================
// Storage.h - Beállítások mentése/betöltése (ESP32 NVS flash), factory reset
//
// A struktúrák (ThermFan, ThermRelay, SwitchLogicChannel, TimerSet,
// RelayTimer) csak egyszerű típusokat (bool/uint/float/tömb) tartalmaznak,
// ezért biztonságosan menthetők/olvashatók egyben, "nyers bájtokként".
//
// FONTOS a flash-kopás miatt: NEM mentünk minden apró változásnál azonnal -
// markDirty() csak jelzi, hogy "van mit menteni", a tényleges írás a
// loop()-ban, néhány másodpercenként, összevonva történik (lásd PascalController.ino).
// ============================================================
#pragma once

#define PREFS_NAMESPACE "pascalctl"

void markDirty() { settingsDirty = true; }

void saveAllSettings() {
  if (!prefs.begin(PREFS_NAMESPACE, false)) return;

  prefs.putUChar("mode", (uint8_t)currentMode);
  prefs.putUChar("timeFmt", timeFormat);
  prefs.putUChar("clockPos", clockPos);
  prefs.putUChar("datePos", datePos);
  prefs.putUChar("dayNmPos", dayNamePos);
  prefs.putUChar("dateFmt", dateFormat);
  prefs.putString("wifiSsid", wifiSsid);
  prefs.putString("wifiPass", wifiPassword);
  prefs.putFloat("compNTC1", tempCompNTC1);
  prefs.putFloat("compNTC2", tempCompNTC2);
  prefs.putBytes("sensCfg1", &sensCfg[0], sizeof(SensorCfg));
  prefs.putBytes("sensCfg2", &sensCfg[1], sizeof(SensorCfg));
  prefs.putUChar("brightness", displayBrightness);
  prefs.putUChar("sleepIdx", sleepTimeoutIdx);

  prefs.putUChar("hr", (uint8_t)hr);
  prefs.putUChar("min", (uint8_t)min_t);
  prefs.putUChar("sec", (uint8_t)sec);
  prefs.putUChar("day", day);
  prefs.putUChar("month", month);
  prefs.putUShort("yr", yr);

  prefs.putUChar("pwmR", (uint8_t)pwmR);
  prefs.putUChar("pwmG", (uint8_t)pwmG);
  prefs.putUChar("pwmB", (uint8_t)pwmB);
  prefs.putBool("rgbOn", digitalRead(RGB_ON));
  prefs.putUChar("pwmFan", (uint8_t)pwmFan);
  prefs.putUChar("pwmFan2", (uint8_t)pwmFan2);
  prefs.putUShort("fan1Freq", (uint16_t)FAN1_PWM_FREQ);
  prefs.putUShort("fan2Freq", (uint16_t)FAN2_PWM_FREQ);

  prefs.putBool("fan1Auto", fan1Auto);
  prefs.putBool("fan2Auto", fan2Auto);
  prefs.putBool("rel1Auto", relay1Auto);
  prefs.putBool("rel2Auto", relay2Auto);

  prefs.putBytes("thermF1", &thermF1, sizeof(thermF1));
  prefs.putBytes("thermF2", &thermF2, sizeof(thermF2));
  prefs.putBytes("thermR1", &thermR1, sizeof(thermR1));
  prefs.putBytes("thermR2", &thermR2, sizeof(thermR2));

  prefs.putBytes("logicF1", &logicF1, sizeof(logicF1));
  prefs.putBytes("logicF2", &logicF2, sizeof(logicF2));
  prefs.putBytes("logicR1", &logicR1, sizeof(logicR1));
  prefs.putBytes("logicR2", &logicR2, sizeof(logicR2));

  prefs.putBytes("ftimerF1", &ftimerF1, sizeof(ftimerF1));
  prefs.putBytes("ftimerF2", &ftimerF2, sizeof(ftimerF2));
  prefs.putBytes("rtimerR1", &rtimerR1, sizeof(rtimerR1));
  prefs.putBytes("rtimerR2", &rtimerR2, sizeof(rtimerR2));

  prefs.putBytes("rHoldR1", &relayTimerR1, sizeof(relayTimerR1));
  prefs.putBytes("rHoldR2", &relayTimerR2, sizeof(relayTimerR2));

  prefs.end();
}

// Segéd: csak akkor tölti be a bájtokat, ha a mentett méret PONTOSAN
// egyezik a struktúra méretével (védelem firmware-frissítés utáni
// struktúra-változás esetére - ellenkező esetben az alapérték marad).
template <typename T>
void loadStructIfMatching(const char* key, T &target) {
  size_t len = prefs.getBytesLength(key);
  if (len == sizeof(T)) prefs.getBytes(key, &target, sizeof(T));
}

void loadAllSettings() {
  if (!prefs.begin(PREFS_NAMESPACE, true)) return; // read-only megnyitás

  currentMode = prefs.getUChar("mode", 0);
  timeFormat = prefs.getUChar("timeFmt", 0);
  dateFormat = prefs.getUChar("dateFmt", 0);
  wifiSsid = prefs.getString("wifiSsid", "");
  wifiPassword = prefs.getString("wifiPass", "");
  clockPos = prefs.getUChar("clockPos", 2);
  datePos = prefs.getUChar("datePos", 0);
  dayNamePos = prefs.getUChar("dayNmPos", 0);
  if (clockPos > 2) clockPos = 2;
  if (datePos > 2) datePos = 0;
  if (dayNamePos > 2) dayNamePos = 0;
  if (dateFormat > 4) dateFormat = 0;
  tempCompNTC1 = prefs.getFloat("compNTC1", 0.0);
  tempCompNTC2 = prefs.getFloat("compNTC2", 0.0);
  loadStructIfMatching("sensCfg1", sensCfg[0]);
  loadStructIfMatching("sensCfg2", sensCfg[1]);
  if (currentMode > 4) currentMode = 0; // védelem sérült/hibás mentett érték ellen
  displayBrightness = prefs.getUChar("brightness", 100);
  sleepTimeoutIdx = prefs.getUChar("sleepIdx", 8);
  if (sleepTimeoutIdx >= SLEEP_OPTIONS_COUNT) sleepTimeoutIdx = 8;

  hr = prefs.getUChar("hr", 12);
  min_t = prefs.getUChar("min", 0);
  sec = prefs.getUChar("sec", 0);
  day = prefs.getUChar("day", 1);
  month = prefs.getUChar("month", 1);
  yr = prefs.getUShort("yr", 2025);

  pwmR = prefs.getUChar("pwmR", 0);
  pwmG = prefs.getUChar("pwmG", 0);
  pwmB = prefs.getUChar("pwmB", 0);
  bool rgbOn = prefs.getBool("rgbOn", false);
  digitalWrite(RGB_ON, rgbOn ? HIGH : LOW);
  pwmFan = prefs.getUChar("pwmFan", 0);
  pwmFan2 = prefs.getUChar("pwmFan2", 0);
  FAN1_PWM_FREQ = prefs.getUShort("fan1Freq", 30);
  FAN2_PWM_FREQ = prefs.getUShort("fan2Freq", 30);

  fan1Auto = prefs.getBool("fan1Auto", false);
  fan2Auto = prefs.getBool("fan2Auto", false);
  relay1Auto = prefs.getBool("rel1Auto", false);
  relay2Auto = prefs.getBool("rel2Auto", false);

  loadStructIfMatching("thermF1", thermF1);
  loadStructIfMatching("thermF2", thermF2);
  loadStructIfMatching("thermR1", thermR1);
  loadStructIfMatching("thermR2", thermR2);

  loadStructIfMatching("logicF1", logicF1);
  loadStructIfMatching("logicF2", logicF2);
  loadStructIfMatching("logicR1", logicR1);
  loadStructIfMatching("logicR2", logicR2);

  loadStructIfMatching("ftimerF1", ftimerF1);
  loadStructIfMatching("ftimerF2", ftimerF2);
  loadStructIfMatching("rtimerR1", rtimerR1);
  loadStructIfMatching("rtimerR2", rtimerR2);

  loadStructIfMatching("rHoldR1", relayTimerR1);
  loadStructIfMatching("rHoldR2", relayTimerR2);

  prefs.end();
}

// Gyári visszaállítás: törli a flash-ben mentett MINDEN beállítást, majd
// újraindítja az eszközt - a globális változók ekkor a forráskódban
// megadott alapértékeikkel jönnek létre újra (üres, "gyári" állapot).
void factoryReset() {
  drawEditScreen("GYÁRI VISSZAÁLLÍTÁS", "Törlés...", "Újraindítás következik");
  ui_refresh_now(); // a delay()+restart miatt a loop() már nem rajzolna
  Preferences p;
  p.begin(PREFS_NAMESPACE, false);
  p.clear();
  p.end();
  delay(800);
  ESP.restart();
}
