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
// NTC
// ============================================================
float readNTCTemperature(int ntcPin) {
  int adcValue = analogRead(ntcPin);
  if (adcValue <= 0) adcValue = 1;
  float voltage = adcValue * VCC / ADC_MAX;
  float rNTC = (voltage * R_FIXED) / (VCC - voltage);
  float steinhart = log(rNTC / NTC_R0) / NTC_BETA + 1.0 / NTC_T0;
  return (1.0 / steinhart) - 263.15;
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
