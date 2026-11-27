// ============================================================
// RgbCal.h - RGB színkalibráció (a telefon kamerás kalibrálása állítja be, az eszköz tárolja)
//
// A LED-csatornák nem egyformán viselkednek (korai nyitás, eltérő maximális fényerő), ezért a logikai szint
// (0..255, amit az UI, az MQTT és az automatikák használnak) és a tényleges PWM-kitöltés között csatornánként
// egy 17 pontos táblázat van: a logikai szint 0, 16, 32 ... 240, 255 pontjaihoz tartozó kitöltés,
// köztük lineáris interpolációval. A teljes fény (255) kitöltése a csatorna fehéregyensúly-visszavételét is hordozza.
// Az alkalmazás egy helyen történik: setPWM() (PWMControl.h) - így minden forrás (kézi, időzítő, görbék, közeledés, MQTT) kalibrált.
// A beállításokat a Storage.h menti/tölti (NVS: "rgbcal", "rgbcalOn", "rgbcalHas").
// ============================================================
#pragma once

#define RGBCAL_N 17

bool rgbCalOn = true;     // a kalibráció használatban van (a telefon appja kapcsolja, összehasonlításhoz)
bool rgbCalHas = false;   // van mentett kalibráció
uint8_t rgbCalLut[3][RGBCAL_N];

// Kalibrációs mód: az app közvetlen (nyers) kitöltést ír (CAL_RAW), az automatikák szünetelnek, a táblázat nem érvényes.
// Nem perzisztens; magától véget ér (időtúllépés / BLE-szétkapcsolódás), hogy a szalag ne ragadjon nyers állapotban.
bool calMode = false;
bool calPrevOn = false;
unsigned long calLastCmdMs = 0;
#define CAL_TIMEOUT_MS 120000UL

// Referencia-fehér mód (a Kalibrálás oldal csúszkái): az RGB kimenet logikai fehér a megadott csatorna-kitöltésekkel (refDuty),
// élőben állítható; nem perzisztens, a mentés külön (SET_RGBCAL). Az app 30 mp-enként megújítja (REFWHITE:1), különben
// REF_TIMEOUT_MS után, vagy a BLE-kapcsolat megszakadásakor magától kilép és visszaáll az előző RGB-állapot.
bool refMode = false;
uint8_t refDuty[3] = { 255, 255, 255 };
#define REF_TIMEOUT_MS 120000UL

// Logikai szint (0..255) -> tényleges kitöltés a csatorna táblázatán át
inline int rgbCalMap(int ch, int v) {
  if (v <= 0) return 0;
  if (calMode || !rgbCalOn || !rgbCalHas) return v > 255 ? 255 : v;
  if (v >= 255) return rgbCalLut[ch][RGBCAL_N - 1];
  int k = v >> 4;                 // 0..15
  int rem = v & 15;               // 0..15
  int span = (k == RGBCAL_N - 2) ? 15 : 16;   // az utolsó szakasz 240..255
  int a = rgbCalLut[ch][k], b = rgbCalLut[ch][k + 1];
  return a + ((b - a) * rem + span / 2) / span;
}
