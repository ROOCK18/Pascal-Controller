# Pascal-Controller

ESP32 (LilyGO TTGO T-Display) alapú vezérlő két NTC hőmérővel, két PWM ventilátorral,
két relével és RGB LED-szalaggal, valamint a hozzá tartozó .NET MAUI Android alkalmazás
(„Pascal-Controll”), ami Bluetooth Low Energy-n kommunikál vele, és OTA-n frissíti a firmware-t.

**Aktuális verzió:** firmware **v4.9.0** + a hozzá tartozó app.

## Mappák

```
firmware/PascalController/   firmware forrás (Arduino, modulokra bontva)
app/                         .NET 10 MAUI alkalmazás (Controll.sln)
```

A lefordított `.bin` fájlok nem ebben a repóban vannak, hanem egy külön publikus
release-repóban, hogy az app és az eszköz token nélkül le tudja tölteni őket.
A hardverfájlok (SolidWorks, STL, Proteus) Drive-on vannak.

## Firmware

- Kártya: ESP32 (TTGO T-Display, ST7789 kijelző), 16 MB flash
- Arduino beállítások: `esp32:esp32:esp32`, Flash Mode **DIO**, partíciós séma `min_spiffs`
  (OTA-hoz két app partíció kell)
- Könyvtárak: Adafruit GFX, Adafruit ST7789; BLE, WiFi és HTTPClient az ESP32 core része
- BLE név: `Pascal-Controller`, Nordic UART UUID-k (`6E400001-…`)
- Funkciók: 5 szenzor-üzemmód, ventilátor-görbe, relé-termosztát, kapcsoló-logika, kapuzás,
  időzítők, NVS tárolás (`Storage.h`), kijelzős menü WiFi-beállítással,
  BLE OTA és WiFi OTA GitHub Releases-ből (`WifiOTA.h`, `OTA_URL:<ver>:<url>`)

Megnyitás: Arduino IDE-ben a `firmware/PascalController/PascalController.ino`. A mappa összes
`.h` fájlja a sketch része. Az első feltöltés USB-n megy, utána OTA-n is frissíthető
(Arduino IDE: *Sketch → Export Compiled Binary*, a sima `.ino.bin` kell, nem a merged).

## Alkalmazás

- .NET 10 MAUI (Android, iOS, MacCatalyst, Windows célplatformok), Plugin.BLE
- Funkciók: vezérlés, adatok, RGB, automatikák (termosztát, logika, kapuzás, időzítők),
  WiFi-beállítás, séma-mentés `.pcscheme` fájlba, firmware-frissítés GitHub release-ből vagy helyi `.bin`-ből

Megnyitás: `app/Controll.sln` Visual Studio 2022+ programban (.NET MAUI workload szükséges).

**Firmware-frissítés forrása:** `app/Controll/Services/GitHubUpdateService.cs`
(`GitHubOwner` / `GitHubRepo`). Az app tokent nem használ, ezért a megadott repónak
**publikusnak** kell lennie, és minden release-hez egy `.bin` asset kell. A tag legyen a
verziószám (pl. `4.9.0`).
