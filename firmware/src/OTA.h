// ============================================================
// OTA.h - BLE OTA firmware-frissítés feldolgozása
// ============================================================
#pragma once

void drawOtaStatus(const char* line1, const String &line2) {
  ui_ota_status(line1, line2);
}

// ============================================================
// OTA feldolgozás - a queue-t a loop() üríti, NEM a BLE callback!
// ============================================================
void otaFail(const String &reason) {
  Serial.printf("OTA hiba: %s\n", reason.c_str());
  Update.abort();
  otaInProgress = false;
  clearOtaQueue();
  notifyLine("ERROR:" + reason);
  drawOtaStatus("OTA HIBA!", reason);
  // Bármilyen OTA-hiba után az eszköz újraindul - enélkül a menü/kijelző
  // egy részleges, összezavarodott állapotban ragadhat (pl. csak egy
  // frissült érték látszik, a többi eltűnik), amíg valaki kézzel nem
  // indítja újra. Tiszta, kiszámítható állapotból induljon mindig.
  delay(2500);
  ESP.restart();
}

void processOtaQueue() {
  if (otaProblem) {
    uint8_t pr = otaProblem; otaProblem = 0;
    otaFail(pr == 1 ? "Tul nagy csomag" : "Tulterheles (sor betelt)");
    return;
  }
  int processed = 0;
  while (processed < 8) {
    OtaPacket packet;
    bool has = false;
    portENTER_CRITICAL(&otaMux);
    if (!otaQueue.empty()) { packet = otaQueue.front(); otaQueue.pop(); has = true; }
    portEXIT_CRITICAL(&otaMux);
    if (!has) break;

    if (Update.write(packet.data, packet.len) != packet.len) {
      otaFail(String(Update.errorString()));
      return;
    }
    currentSize += packet.len;
    processed++;

    int pct = fullSize > 0 ? (int)((currentSize * 100) / fullSize) : 0;
    if (pct != lastOtaPct) {
      lastOtaPct = pct;
      ui_ota_progress(pct);
    }

    if (currentSize >= fullSize) {
      if (Update.end(true)) {
        notifyLine("OK_RESTART");
        drawOtaStatus("OTA KÉSZ!", "Újraindítás...");
        delay(1500);
        ESP.restart();
      } else {
        otaFail(String(Update.errorString()));
      }
      return;
    }
  }
}
