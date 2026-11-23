# Pascal-Controller

ESP32 (LilyGO TTGO T-Display) alapú vezérlő két NTC hőmérővel, két PWM ventilátorral,
két relével és RGB LED-szalaggal, valamint a hozzá tartozó .NET MAUI Android alkalmazás
(„Pascal-Controll”), ami Bluetooth Low Energy-n kommunikál vele és OTA-n frissíti a firmware-t.

## Mappaszerkezet

```
app/                      .NET MAUI alkalmazás (Controll.sln)
firmware/
  PascalController/       aktuális firmware – v4.9.0, modulokra bontva
  legacy/                 korábbi firmware-verziók és segédprogramok (referenciának)
  releases/               lefordított .bin fájlok OTA frissítéshez
docs/screenshots/         Google Play képernyőképek
```

A hardverfájlok (SolidWorks, STL, STEP, Proteus) nem ebben a repóban vannak, hanem Drive-on.

## Firmware

**Aktuális verzió:** `firmware/PascalController` – v4.9.0

- Kártya: ESP32 (TTGO T-Display, ST7789 kijelző)
- Arduino build beállítások: `esp32:esp32:esp32`, Flash Mode **DIO**,
  partíciós séma `min_spiffs` (OTA-hoz két app partíció kell)
- Könyvtárak: Adafruit GFX, Adafruit ST7789, ESP32 BLE + WiFi + HTTPClient (core része)
- BLE név: `Pascal-Controller`, Nordic UART UUID-k (`6E400001-…`)
- Funkciók: 5 szenzor-üzemmód, ventilátor-görbe, relé-termosztát, kapcsoló-logika, kapuzás,
  időzítők, NVS tárolás (`Storage.h`), kijelzős menü WiFi-beállítással,
  BLE OTA és WiFi OTA GitHub Releases-ből (`WifiOTA.h`, `OTA_URL:<ver>:<url>`)

Megnyitás: Arduino IDE-ben a `firmware/PascalController/PascalController.ino`.
A mappában lévő összes `.h` fájl a sketch része (lásd a `.ino` fejlécét).

### Frissítés

- **Első telepítés / USB:** Arduino IDE-ből feltöltés USB-n.
- **OTA:** az alkalmazás Beállítások oldalán „FIRMWARE FRISSÍTÉS (.bin)” → `firmware/releases/*.bin`.

### Verziótörténet (`firmware/legacy`)

| Mappa | Verzió | Megjegyzés |
|---|---|---|
| `terrarium_prototype` | – | Első prototípus („TTGO-Terrarium”), más lábkiosztás |
| `firmware_1_0_0` | 1.0.0 | Első Pascal-Controller firmware |
| `bt_updater` | – | Önálló BLE OTA frissítő logóval |
| `base_ota_firmware` | 1.0.1-base | Minimál alap firmware, csak BLE OTA fogadására |
| `firmware_1_2_0` | 1.2.0 | OTA javítás (flash írás queue-ból a loop()-ban) |
| `firmware_1_4_0` | 1.4.0 | |
| `firmware_1_7_0` | 1.7.0 | |
| `firmware_3_0_0` | 3.0.0 | |
| `firmware_3_1_0` | 3.1.0 | |
| `firmware_3_2_0` | 3.2.0 | |
| `firmware_3_3_0` | 3.3.0 | |
| `firmware_3_4_0_monolith` | 3.4.0 | Utolsó egyfájlos változat |
| `PascalController_3_4_0` | 3.4.0 | Első modulokra bontott változat |
| `../PascalController` | 4.9.0 | Aktuális: WiFi menü, WiFi OTA, NVS tárolás |

A 3.5.0–4.8.x közti verziók forrása nincs meg; a 4.2.1–4.9.0 `.bin` fájlok a GitHub Releases-ben vannak.

### Release binárisok (`firmware/releases`)

| Fájl | Megjegyzés |
|---|---|
| `pascal-controller-1.0.0.bin` | 1.0.0 |
| `pascal-controller-1.2.0.bin` | 1.2.0 |
| `pascal-controller-3.1.0.bin` | Eredetileg `firmware_1_3_0.ino.bin` néven készült, de a binárisban 3.1.0 a verzió |

A 4.x binárisok a GitHub Releases-ben vannak (tagek: `4.5.0`–`4.9.0`). Az app az OTA-hoz a
Releases API-t használja token nélkül, ezért a release-eknek **publikus** repóban kell lenniük.

## Alkalmazás – figyelem

Az `app/` a v4.9.0 firmware-hez tartozó .NET 10-es változat (WiFi panel, GitHub Releases
alapú frissítés, séma-mentés `.pcscheme` fájlba, Kapuzás, kijelző- és dátumbeállítások).

**Fontos:** a `app/Controll/Services/GitHubUpdateService.cs` a `ROOCK18/Pascal-Controller`
repó Releases-ét kérdezi le. Mivel az app tokent nem használ, a repónak **publikusnak** kell lennie,
különben a verzióválasztó (és az eszköz WiFi OTA-ja) nem működik. Ha a repó privát marad,
a `.bin` release-eket egy külön publikus repóba kell tenni, és a két konstanst átírni.

## Alkalmazás (`app/`)

- .NET 10 MAUI (Android, iOS, MacCatalyst, Windows célplatformok)
- Csomagok: Plugin.BLE, CommunityToolkit.Maui.MediaElement
- Oldalak: vezérlés, adatok, RGB, időzítés/termosztát, jegyzetek, beállítások (firmware-frissítés)

Megnyitás: `app/Controll.sln` Visual Studio 2022-ben (.NET MAUI workload szükséges).
