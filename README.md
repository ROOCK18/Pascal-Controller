# Pascal-Controller

ESP32 (LilyGO TTGO T-Display) alapú vezérlő két NTC hőmérővel, két PWM ventilátorral,
két relével és RGB LED-szalaggal, valamint a hozzá tartozó .NET MAUI Android alkalmazás
(„Pascal-Controll”), ami Bluetooth Low Energy-n kommunikál vele, és OTA-n frissíti a firmware-t.

**Aktuális verzió:** firmware **v5.0.0** (PlatformIO, NimBLE, LovyanGFX + LVGL kijelző). Az `app/` még a v4.9.0-hoz tartozó változat.

## Mappák

```
firmware/                    PlatformIO projekt (platformio.ini, partitions.csv, include/lv_conf.h, src/)
app/                         .NET 10 MAUI alkalmazás (Controll.sln)
```

A lefordított `.bin` fájlok nem ebben a repóban vannak, hanem a külön publikus
[ROOCK18/Pascal-Controller-firmware](https://github.com/ROOCK18/Pascal-Controller-firmware)
repó Releases-ében, hogy az app és az eszköz token nélkül le tudja tölteni őket.
A hardverfájlok (SolidWorks, STL, Proteus) Drive-on vannak.

## Firmware (v5.0.0)

- Fordítás: VS Code + PlatformIO, `pio run` a `firmware/` mappában. A platform a pioarduino
  `55.03.311` (Arduino core 3.3.11), fixált könyvtárverziókkal. Windowson PowerShellből futtasd, Git Bash alól az ESP-IDF telepítő nem megy.
- Partíciók (`partitions.csv`, 16 MB): két 6 MB-os OTA slot + ~3,9 MB SPIFFS.
- BLE: NimBLE (a Bluedroid helyett, ~490 KB flash megtakarítás), UUID-k és protokoll változatlan.
- Kijelző: LovyanGFX (ST7789 meghajtó) + LVGL 9.2, a menü logikája (`Menu.h`) változatlan, a megjelenítés az `ui.h`-ban és a `Display.h`-ban van.
- Szenzorok: alapértelmezés **PT1000** (Callendar–Van Dusen), mellette NTC 10k, NTC 100k és Egyedi (β-modell).
  Szenzoronként állítható: névleges ellenállás, alap hőfok (NTC), béta (NTC), soros (felhúzó) ellenállás, tápfeszültség és kompenzáció.
  Az eszköz menüjében: *Beállítások → Szenzor beállítások*; az appban: *Hőmérő beállítások*; BLE: `SET_SENS` / `GET_SENS` / `GET_SENSRAW`.
  A mérés kalibrált mV-olvasás 16-os átlagolással; szakadt/rövidzárt szenzornál az érték „--”, a relé-termosztát KI-re áll.
  **PT1000-hez felhúzónak 1,0–1,2 kΩ ajánlott** (3,3 V-ra kötve): ~3,9 ADC-lépés/°C, a 10 kΩ-mal csak ~1,4.
- Ékezetes betűk: saját Montserrat-alapú betűtípusok (`firmware/src/fonts`, újragenerálás: `bash tools/gen_fonts.sh`, a `-DHU_FONTS=1` build flaggel).
- Méret: firmware.bin ~1,74 MB, vagyis a régi 1,9 MB-os partícióba is belefér (OTA-val frissíthető 4.9.0-ról).
  Az új partíciós tábla csak USB-s teljes flash-eléssel (`firmware.factory.bin`) kerül fel.

**Még NINCS hardveren kipróbálva.** Ellenőrizendő: a kijelző elforgatása és eltolása (`lgfx_setup.h`, `setRotation(3)`, `offset_x/y`),
a BLE-kapcsolat az appal, a gombok és a WiFi OTA. A kijelző kinézetét PC-s szimulátorral még nem sikerült képként ellenőrizni.
A szövegek ékezet nélküliek (az LVGL beépített betűi csak ASCII-t tartalmaznak).

## Alkalmazás

- .NET 10 MAUI (Android, iOS, MacCatalyst, Windows célplatformok), Plugin.BLE
- Funkciók: vezérlés, adatok, RGB, automatikák (termosztát, logika, kapuzás, időzítők),
  WiFi-beállítás, séma-mentés `.pcscheme` fájlba, firmware-frissítés GitHub release-ből vagy helyi `.bin`-ből

Megnyitás: `app/Controll.sln` Visual Studio 2022+ programban (.NET MAUI workload szükséges).

**Firmware-frissítés forrása:** `app/Controll/Services/GitHubUpdateService.cs`
(`GitHubOwner` / `GitHubRepo`). Az app tokent nem használ, ezért a megadott repónak
**publikusnak** kell lennie, és minden release-hez egy `.bin` asset kell. A tag legyen a
verziószám (pl. `4.9.0`).
