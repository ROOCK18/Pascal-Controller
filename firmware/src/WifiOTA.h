// ============================================================
// WifiOTA.h - WiFi-alapú firmware frissítés
//
// A BLE-s csomagolt OTA (OTA.h) VÁLTOZATLANUL megmarad és megy tovább,
// ahogy eddig. Ez itt egy MÁSODIK, alternatív út: a telefon a BLE-n
// keresztül csak egy letöltési URL-t küld el (SET_WIFI + OTA_URL), az ESP32
// pedig a SAJÁT WiFi rádióján tölti le és flash-eli a firmware-t - a telefon
// közben végig BLE-n marad kapcsolódva, nem kell neki a WiFi hálózaton lennie.
// ============================================================
#pragma once

// Megpróbál csatlakozni a mentett WiFi hálózathoz, timeout-tal.
bool connectWifi(unsigned long timeoutMs) {
  if (wifiSsid.length() == 0) return false;
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(200);
  }
  return WiFi.status() == WL_CONNECTED;
}

// WiFi esemény-figyelő: valós idejű visszajelzés a csatlakozási kísérletről.
// A "rossz jelszó" esetet a disconnect ok-kódjából ismerjük fel (auth hiba).
void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (!wifiConnectPending) return;

  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    wifiConnectPending = false;
    notifyLine("WIFI_CONNECTED:" + WiFi.localIP().toString());
  }
  else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    uint8_t reason = info.wifi_sta_disconnected.reason;
    bool authFail = (reason == WIFI_REASON_AUTH_FAIL ||
                      reason == WIFI_REASON_AUTH_EXPIRE ||
                      reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                      reason == WIFI_REASON_HANDSHAKE_TIMEOUT);
    if (authFail) {
      wifiConnectPending = false;
      // FONTOS: enélkül az ESP32 a háttérben automatikusan tovább
      // próbálkozna újracsatlakozni a rossz jelszóval - ez blokkolja/
      // tönkreteszi a rákövetkező WiFi.scanNetworks() hívást (a rádió
      // egyszerre csak egy dolgot tud csinálni), és úgy tűnik, mintha
      // "nincs elérhető hálózat" lenne, pedig csak a rádió foglalt.
      WiFi.disconnect(true, false);
      notifyLine("WIFI_WRONG_PASSWORD");
    }
    // egyéb okoknál (pl. gyenge jel, router épp nem elérhető) hagyjuk, hogy
    // az ESP32 automatikusan újrapróbálja - a timeout majd elkapja, ha
    // tartósan nem sikerül (lásd loop()).
  }
}

// Ténylegesen elindítja a csatlakozási kísérletet (a loop()-ból hívva, hogy
// ne a BLE callback taskban fusson).
void startWifiConnect() {
  wifiConnectPending = true;
  wifiConnectStartTime = millis();
  notifyLine("WIFI_CONNECTING");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true); // korábbi próbálkozás/kapcsolat törlése
  delay(50);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
}

// Parancs: SET_WIFI:<ssid>:<jelszo> - elmenti ÉS azonnal csatlakozási
// kísérletet is indít (a tényleges WiFi.begin() a loop()-ban indul,
// lásd wifiConnectRequested a PascalController.ino loop()-jában).
void handleSetWifi(const String &body) {
  // Formátum: <ssid>:<jelszó>. Az SSID/jelszó ':' és '\' karaktere "\:" ill. "\\" alakban jön
  // (escape-elve), így ezek is megadhatók; escape nélküli (régi) formátum változatlanul működik.
  String parts[2];
  int idx = 0;
  String cur;
  for (size_t i = 0; i < body.length(); i++) {
    char ch = body[i];
    if (ch == '\\' && i + 1 < body.length() && (body[i + 1] == ':' || body[i + 1] == '\\')) {
      cur += body[++i];
    } else if (ch == ':' && idx == 0) {
      parts[0] = cur; cur = ""; idx = 1;
    } else {
      cur += ch;
    }
  }
  if (idx == 0) return;
  parts[1] = cur;
  wifiSsid = parts[0];
  wifiPassword = parts[1];
  markDirty();
  notifyLine("WIFI_OK");
  wifiConnectRequested = true;
}

// Parancs: GET_WIFI_STATUS - visszaküldi az aktuális állapotot: 0=nincs
// beállítva/nincs kapcsolat, 1=csatlakozva, 2=csatlakozás folyamatban.
// (ez alapján dönti el az app, hogy a WiFi- vagy a BLE-OTA-t ajánlja fel,
// és hogy "csatlakozás folyamatban" üzenetet mutasson-e).
void handleGetWifiStatus() {
  bool connected = (WiFi.status() == WL_CONNECTED);
  int state = connected ? 1 : (wifiConnectPending ? 2 : 0);
  String ip = connected ? WiFi.localIP().toString() : "";
  notifyLine("WIFI_STATUS:" + String(state) + ":" + ip + ":" + wifiSsid);
}

// Parancs: WIFI_DISCONNECT - bontja a WiFi kapcsolatot (a mentett SSID/
// jelszó megmarad, csak a jelenlegi kapcsolat szűnik meg).
void handleWifiDisconnect() {
  WiFi.disconnect(true, false);
  wifiConnectPending = false;
  handleGetWifiStatus();
}

// Parancs: WIFI_FORGET - leállítja a folyamatban lévő (vagy aktív)
// csatlakozási kísérletet, ÉS törli a mentett SSID/jelszót is, hogy
// legközelebbi induláskor se próbálkozzon automatikusan.
void handleWifiForget() {
  WiFi.disconnect(true, true);
  wifiConnectPending = false;
  wifiConnectRequested = false;
  wifiSsid = "";
  wifiPassword = "";
  markDirty();
  handleGetWifiStatus();
}

// Ténylegesen elvégzi a hálózat-keresést (blokkoló - a loop()-ból hívva).
void doWifiScan() {
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  notifyLine("WIFI_SCAN_START:" + String(n < 0 ? 0 : n));
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue; // rejtett SSID-k kihagyva
    notifyLine("WIFI_NET:" + ssid + ":" + String(WiFi.RSSI(i)));
    delay(5); // ne torlodjon a BLE notify queue
  }
  notifyLine("WIFI_SCAN_DONE");
  WiFi.scanDelete();
}

// Kijelző-menüs WiFi keresés: a BLE-s doWifiScan() párja, de a talált
// hálózatokat a menü saját tömbjébe (wifiMenuScanSsids) tölti, jelerősség
// szerint csökkenő sorrendben, és a keresés végén a lista-képernyőre vált.
void doWifiScanForMenu() {
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  wifiMenuScanCount = 0;

  // egyszerű, helyben rendezés jelerősség szerint (csökkenő) - kevés elem,
  // beszúrásos rendezés bőven elég gyors
  String tmpSsid[WIFI_MENU_MAX_NETWORKS];
  int tmpRssi[WIFI_MENU_MAX_NETWORKS];
  int cnt = 0;
  for (int i = 0; i < n && cnt < WIFI_MENU_MAX_NETWORKS; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue; // rejtett SSID-k kihagyva
    bool dup = false;
    for (int j = 0; j < cnt; j++) if (tmpSsid[j] == ssid) { dup = true; break; }
    if (dup) continue; // ugyanaz a hálózat több csatornán is látszódhat
    tmpSsid[cnt] = ssid;
    tmpRssi[cnt] = WiFi.RSSI(i);
    cnt++;
  }
  // beszúrásos rendezés jelerősség szerint csökkenő sorrendbe
  for (int i = 1; i < cnt; i++) {
    String s = tmpSsid[i]; int r = tmpRssi[i];
    int j = i - 1;
    while (j >= 0 && tmpRssi[j] < r) { tmpSsid[j + 1] = tmpSsid[j]; tmpRssi[j + 1] = tmpRssi[j]; j--; }
    tmpSsid[j + 1] = s; tmpRssi[j + 1] = r;
  }
  for (int i = 0; i < cnt; i++) wifiMenuScanSsids[i] = tmpSsid[i];
  wifiMenuScanCount = cnt;

  WiFi.scanDelete();
  menuScreen = MENU_WIFI_SCAN_LIST;
  menuCursor = 0;
  idleNeedsFullClear = true;
  lastDrawnMenuScreen = MENU_NONE; // kényszerített teljes törlés+újrarajzolás
  drawMenu();
}

// Parancs: WIFI_SCAN - csak jelzőt állít be, a tényleges (blokkoló) keresést
// a loop() indítja (ne a BLE callback taskban fusson).
void handleWifiScan() {
  wifiScanRequested = true;
}

void wifiOtaFail(const String &reason) {
  Serial.printf("WiFi OTA hiba: %s\n", reason.c_str());
  Update.abort();
  wifiOtaInProgress = false;
  notifyLine("ERROR:" + reason);
  drawOtaStatus("WIFI OTA HIBA!", reason);
  // Ugyanaz az elv, mint a BLE OTA-nál: bármilyen hiba után tiszta
  // újraindulás, ne ragadjon be részleges állapotban az eszköz.
  delay(2500);
  ESP.restart();
}

// Parancs: OTA_URL:<verzio>:<https url> - letölti és telepíti a firmware-t
// az ESP32 SAJÁT WiFi kapcsolatán keresztül. Szándékosan BLOKKOLÓ (mint a
// BLE OTA is) - amíg fut, a kijelző/gombok nem reagálnak, de ez elvárt egy
// firmware-frissítés alatt, és nem érinti a BLE stack-et (nem küld/fogad
// rajta adatot, sőt a letöltés idejére teljesen le is áll - lásd lejjebb).
void handleOtaUrl(const String &body) {
  if (wifiOtaInProgress || otaInProgress) {
    notifyLine("ERROR:Mar folyamatban egy frissites");
    return;
  }

  int c = body.indexOf(':');
  String version = (c >= 0) ? body.substring(0, c) : "";
  String url = (c >= 0) ? body.substring(c + 1) : body;
  String downloadLabel = version.length() > 0 ? ("Firmware " + version + " letoltese...") : "Firmware letoltese...";

  wifiOtaInProgress = true;

  drawOtaStatus("WIFI CSATLAKOZÁS...", wifiSsid);
  notifyLine("WIFI_CONNECTING");

  if (!connectWifi()) {
    wifiOtaFail("WiFi csatlakozas sikertelen (SSID/jelszo hibas, vagy nincs jelerosseg)");
    return;
  }

  notifyLine("WIFI_CONNECTED:" + WiFi.localIP().toString());
  drawOtaStatus("LETÖLTÉS...", downloadLabel);

  // ── BLE STACK LEÁLLÍTÁSA a letöltés idejére ──
  // A BLE stack (kapcsolat + advertising + belső bufferek) jelentős RAM-ot
  // foglal - ez pont az a memória, ami hiányzik a TLS (HTTPS) kézfogáshoz,
  // és "HTTP hiba: -1" kapcsolódási hibát okoz. Mivel ez a folyamat MINDIG
  // újraindítással zárul (siker ÉS hiba esetén is - lásd wifiOtaFail()),
  // nyugodtan teljesen leállíthatjuk a BLE-t: nincs szükség rá, hogy utána
  // visszaálljon, az ESP.restart() úgyis mindent tiszta állapotból indít.
  // A telefon a BLE-kapcsolat megszakadását fogja látni, de a folyamat
  // állapota a kijelzőn (progress bar) továbbra is valós időben követhető.
  drawOtaStatus("MEMÓRIA FELSZAB.", "BLE leállítása...");
  deviceConnected = false; // notifyLine() mostantól biztonságosan no-op lesz
  if (pServer != nullptr) {
    for (uint16_t h : pServer->getPeerDevices()) pServer->disconnect(h);
  }
  delay(200);
  NimBLEDevice::deinit(true);
  delay(200);
  Serial.printf("BLE leallitva letoltes elott. Szabad memoria: %lu byte\n", (unsigned long)ESP.getFreeHeap());
  drawOtaStatus("LETÖLTÉS...", downloadLabel);

  // FONTOS: a GitHub release-letöltési URL-ek szinte mindig átirányítanak
  // (302) egy MÁSIK domain-re (objects.githubusercontent.com), ahol a
  // tényleges fájl van. Az ESP32 HTTPClient beépített automatikus
  // átirányítás-követése (HTTPC_STRICT_FOLLOW_REDIRECTS) WiFiClientSecure-vel
  // gyakran "HTTP hiba: -1" kapcsolódási hibával elbukik, mert nem mindig
  // nyit friss TLS kapcsolatot az ÚJ hoszthoz. Ezért itt KÉZZEL követjük az
  // átirányítást: minden ugrásnál teljesen új WiFiClientSecure-t nyitunk.
  String currentUrl = url;
  int httpCode = 0;
  const int MAX_REDIRECTS = 5;

  for (int hop = 0; hop <= MAX_REDIRECTS; hop++) {
    WiFiClientSecure client;
    client.setInsecure(); // egyszerűsítés: nem ellenőrizzük a tanúsítvány-láncot

    HTTPClient https;
    https.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS); // KÉZZEL kezeljük
    https.setTimeout(15000);

    if (!https.begin(client, currentUrl)) {
      wifiOtaFail("HTTP kapcsolat nem nyithato meg (szabad memoria: " + String(ESP.getFreeHeap()) + " byte): " + currentUrl.substring(0, 40));
      return;
    }

    httpCode = https.GET();

    if (httpCode == HTTP_CODE_FOUND || httpCode == HTTP_CODE_MOVED_PERMANENTLY ||
        httpCode == 303 || httpCode == 307 || httpCode == 308) {
      String location = https.getLocation();
      https.end();
      if (location.length() == 0) {
        wifiOtaFail("Atiranyitas hiba: nincs Location fejlec");
        return;
      }
      currentUrl = location;
      continue; // következő hop: kövesd az új URL-t
    }

    if (httpCode != HTTP_CODE_OK) {
      wifiOtaFail("HTTP hiba: " + String(httpCode) + " (szabad memoria: " + String(ESP.getFreeHeap()) + " byte)");
      https.end();
      return;
    }

    // httpCode == 200 - itt már a tényleges fájlnál vagyunk, folytatjuk lejjebb
    int total = https.getSize();
    if (total <= 0) {
      wifiOtaFail("Ismeretlen vagy nulla fajlmeret");
      https.end();
      return;
    }

    if (!Update.begin(total)) {
      wifiOtaFail(String(Update.errorString()));
      https.end();
      return;
    }

    WiFiClient *stream = https.getStreamPtr();
  uint8_t buf[1024];
  int written = 0;
  int lastPct = -1;
  unsigned long lastDataTime = millis();

  while (https.connected() && written < total) {
    size_t avail = stream->available();
    if (avail > 0) {
      size_t toRead = (avail < sizeof(buf)) ? avail : sizeof(buf);
      int r = stream->readBytes(buf, toRead);
      if (r > 0) {
        if (Update.write(buf, r) != (size_t)r) {
          wifiOtaFail(String(Update.errorString()));
          https.end();
          return;
        }
        written += r;
        lastDataTime = millis();

        int pct = (int)(((long)written * 100L) / total);
        if (pct != lastPct) {
          lastPct = pct;
          notifyLine("OTA_PROGRESS:" + String(pct));
          ui_ota_progress(pct);
        }
      }
    } else {
      if (millis() - lastDataTime > 15000) {
        wifiOtaFail("Idotullepes letoltes kozben");
        https.end();
        return;
      }
      delay(5);
    }
  }
  https.end();

  if (written != total) {
    wifiOtaFail("Hianyos letoltes (" + String(written) + "/" + String(total) + " byte)");
    return;
  }

  if (Update.end(true)) {
    notifyLine("OK_RESTART");
    drawOtaStatus("OTA KÉSZ!", "Újraindítás...");
    delay(1500);
    ESP.restart();
  } else {
    wifiOtaFail(String(Update.errorString()));
  }
  } // for (hop)

  // Ha idáig eljutottunk, elfogytak az átirányítás-követési próbálkozások
  wifiOtaFail("Tul sok atiranyitas (" + String(MAX_REDIRECTS) + ")");
}
