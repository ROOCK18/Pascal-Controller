// ============================================================
// Comm.h - BLE üzenetküldés (notifyLine) és az OTA queue ürítése
// ============================================================
#pragma once

void clearOtaQueue() {
  portENTER_CRITICAL(&otaMux);
  while (!otaQueue.empty()) otaQueue.pop();
  portEXIT_CRITICAL(&otaMux);
}

void notifyLine(const String &s) {
  if (!deviceConnected) return;
  String withNl = s;
  if (!withNl.endsWith("\n")) withNl += "\n";
  if (pTxCharacteristic->getSubscribedCount() == 0) return; // az app még nem iratkozott fel
  // NimBLE: ha sok sor megy egymás után (pl. GET_ALL), a belső puffer rövid
  // időre betelhet, és a notify() false-t ad - ilyenkor röviden újrapróbáljuk,
  // különben az app elveszítené a LOAD_* sorokat.
  for (int i = 0; i < 15; i++) {
    if (pTxCharacteristic->notify((const uint8_t*)withNl.c_str(), withNl.length())) break;
    delay(3);
  }
}
