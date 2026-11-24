// ============================================================
// Sensors.h - NTC hőmérséklet, kapcsoló-állapot (analóg) és DHT11 kiolvasás
// ============================================================
#pragma once

// sensor: 1=NTC1, 2=NTC2, 3=Páratartalom (%)
float getSensorTemp(uint8_t sensor) {
  if (sensor == 3) return humidity;
  return (sensor == 2) ? ntc2Temperature : ntc1Temperature;
}

// ============================================================
// HŐMÉRŐ (PT1000 / NTC) - paraméterezhető, lásd SensorCfg
// ============================================================
// PT1000/PT100: Callendar-Van Dusen R(T) = R0 (1 + A T + B T^2 [+ C (T-100) T^3, T<0])
static float ptTempFromR(float r, float r0) {
  const float A = 3.9083e-3f, B = -5.775e-7f, C = -4.183e-12f;
  float ratio = r / r0;
  float disc = A * A - 4.0f * B * (1.0f - ratio);
  if (disc < 0) return NAN;
  float t = (-A + sqrtf(disc)) / (2.0f * B); // T >= 0 zárt alak
  if (t < 0) {                               // T < 0: Newton-iteráció a C taggal
    for (int i = 0; i < 6; i++) {
      float f = 1.0f + A * t + B * t * t + C * (t - 100.0f) * t * t * t - ratio;
      float df = A + 2.0f * B * t + C * (4.0f * t * t * t - 300.0f * t * t);
      if (df == 0) break;
      t -= f / df;
    }
  }
  return t;
}

float sensorTempFromR(const SensorCfg &c, float r) {
  if (!(r > 0) || isnan(r) || isinf(r)) return NAN;
  float t;
  if (c.type == 0) {
    t = ptTempFromR(r, c.r0);
  } else {
    // NTC beta-modell: 1/T = ln(R/R0)/beta + 1/T0
    float s = logf(r / c.r0) / c.beta + 1.0f / (c.t0 + 273.15f);
    t = (s == 0) ? NAN : (1.0f / s - 273.15f);
  }
  if (isnan(t) || t < -100.0f || t > 400.0f) return NAN; // kívül esik a hihető tartományon
  return t;
}

// A kiolvasott feszültség (mV): a kalibrált analogReadMilliVolts (eFuse Vref)
// 16 mérés átlaga - ez zajtalanabb és pontosabb, mint a nyers ADC érték.
static float sensReadMilliVolts(int pin) {
  uint32_t acc = 0;
  const int N = 16;
  for (int i = 0; i < N; i++) acc += analogReadMilliVolts(pin);
  return acc / (float)N;
}

// Visszaadja a hőmérsékletet (C), vagy NAN-t, ha a szenzor hibás (szakadt/rövidzárt/
// kívül esik a tartományon). A kompenzációt (offset) a hívó adja hozzá.
// A függvény neve a régi NTC-s kódból maradt (a hívók változatlanok).
float readNTCTemperature(int ntcPin) {
  int idx = (ntcPin == NTC2_PIN) ? 1 : 0;
  const SensorCfg &c = sensCfg[idx];
  float mv = sensReadMilliVolts(ntcPin);
  sensLastMv[idx] = mv;
  sensLastR[idx] = NAN;

  const float adcTopMv = 3100.0f;                      // az ESP32 ADC (11 dB) felső széle
  if (mv < 15.0f) return NAN;                          // rövidzár
  if (mv > adcTopMv || mv >= c.vSupply * 1000.0f - 15.0f) return NAN; // szakadás / telítés
  float v = mv / 1000.0f;
  float r = c.rSeries * v / (c.vSupply - v);
  sensLastR[idx] = r;
  return sensorTempFromR(c, r);
}

// ============================================================
// KAPCSOLÓ (NTC1/NTC2 duál funkció "1 kapcsoló+1 hőmérő" / "2 kapcsoló"
// módban): a láb 10k-val 5V-ra fel van húzva -> nyitva magas ADC érték,
// rövidre zárva (zárt kapcsoló) ~0 ADC érték. Analóg olvasással (nem
// digitalRead-del) dolgozunk, mert ez biztonságosabb/megbízhatóbb ezeken
// az ADC-only lábakon, függetlenül a pontos feszültségosztótól.
// ============================================================
bool isSwitchClosed(int pin) {
  return analogRead(pin) < 2000; // félskála körüli küszöb (12 bit ADC, 0-4095)
}

// ============================================================
// DHT11 (páratartalom+hőmérséklet modul az NTC2_PIN lábon, "páratartalom"
// módban). Saját, könyvtár-független bit-bangelt kiolvasás.
// ============================================================
bool readDHT11(int pin, float &outTemp, float &outHumidity) {
  uint8_t data[5] = {0, 0, 0, 0, 0};

  // Start jel: húzzuk le a vonalat legalább 18ms-ra, majd engedjük fel
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  delay(18);
  digitalWrite(pin, HIGH);
  delayMicroseconds(30);
  pinMode(pin, INPUT_PULLUP);

  unsigned long t0 = micros();
  while (digitalRead(pin) == HIGH) { if (micros() - t0 > 100) { pinMode(pin, INPUT); return false; } }
  t0 = micros();
  while (digitalRead(pin) == LOW)  { if (micros() - t0 > 100) { pinMode(pin, INPUT); return false; } }
  t0 = micros();
  while (digitalRead(pin) == HIGH) { if (micros() - t0 > 100) { pinMode(pin, INPUT); return false; } }

  // 40 bit adat beolvasása (mindegyik bit: ~50us LOW, majd rövid(0)/hosszú(1) HIGH impulzus)
  for (int i = 0; i < 40; i++) {
    t0 = micros();
    while (digitalRead(pin) == LOW) { if (micros() - t0 > 100) { pinMode(pin, INPUT); return false; } }
    unsigned long pulseStart = micros();
    t0 = micros();
    while (digitalRead(pin) == HIGH) { if (micros() - t0 > 100) { pinMode(pin, INPUT); return false; } }
    unsigned long pulseLen = micros() - pulseStart;
    data[i / 8] <<= 1;
    if (pulseLen > 40) data[i / 8] |= 1; // ~26-28us=0 bit, ~70us=1 bit
  }
  pinMode(pin, INPUT); // vissza NTC/analóg üzemmódba a következő normál módváltásig

  uint8_t checksum = data[0] + data[1] + data[2] + data[3];
  if (checksum != data[4]) return false;

  outHumidity = data[0]; // DHT11: egész % (data[1] tört rész, jellemzően 0)
  outTemp = data[2];     // DHT11: egész °C (data[3] tört rész, jellemzően 0)
  return true;
}
