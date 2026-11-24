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
- Szenzorok: alapértelmezés **PT1000** (Callendar–Van Dusen), mellette NTC 10k és Egyedi (β-modell).
  Szenzoronként állítható: névleges ellenállás, alap hőfok (NTC), béta (NTC) és kompenzáció.
  A **felhúzó ellenállás (R10/R11 = 2,2 kΩ) és a 3,3 V a NYÁK-on rögzített**, ezért a firmware-ben fix (`SENSOR_PULLUP_OHM`, `SENSOR_VSUPPLY`).
  Az eszköz menüjében: *Beállítások → Szenzor beállítások*; az appban: *Hőmérő beállítások*; BLE: `SET_SENS` / `GET_SENS` / `GET_SENSRAW`.
  A mérés kalibrált mV-olvasás 16-os átlagolással; szakadt/rövidzárt szenzornál az érték „--”, a relé-termosztát KI-re áll.
  A 2,2 kΩ-os felhúzóval a PT1000 ~3,4 ADC-lépés/°C felbontást ad; 100 kΩ-os NTC-vel és PT100-zal **nem** mérhető (a jel telítésbe, ill. túl kicsire esik).
- Kinézet: az app színeivel és kártyás felépítésével megegyező (sötét kártyák, piros akcentus); az idle képernyőn V1/V2 ventilátor-kártya, amelynek 5 lapátos propellere forog, ha a sebesség > 0%.
- Ékezetes betűk: az app Open Sans Semibold betűjéből generált betűtípusok (`firmware/src/fonts`, újragenerálás: `bash tools/gen_fonts.sh`, a `-DHU_FONTS=1` build flaggel).
- Méret: firmware.bin ~1,76 MB, vagyis a régi 1,9 MB-os partícióba is belefér (OTA-val frissíthető 4.9.0-ról).
  Az új partíciós tábla csak USB-s teljes flash-eléssel (`firmware.factory.bin`) kerül fel.

**Még NINCS hardveren kipróbálva.** Ellenőrizendő: a kijelző elforgatása és eltolása (`lgfx_setup.h`, `setRotation(3)`, `offset_x/y`),
a BLE-kapcsolat az appal, a gombok és a WiFi OTA. A kijelző kinézetét PC-s szimulátorral még nem sikerült képként ellenőrizni.
A szövegek ékezet nélküliek (az LVGL beépített betűi csak ASCII-t tartalmaznak).

## Alkalmazás

## Csatlakozás (app)

- Az app megjegyzi a csatlakoztatott eszközt (BLE azonosító alapján), és szétkapcsolódás után, illetve indításkor magától újracsatlakozik, amint az eszköz hatótávban van.
- **Több mentett eszköznél nincs néma csatlakozás**: indításkor az app a hatótávban látható mentett eszközök közül megkérdezi, melyikre csatlakozzon.
- Az eszközök hely-nevet kapnak (pl. „Terrárium”): a listában a ✎ gombbal vagy hosszú nyomással (Android), csatlakozva pedig a panelre koppintva szerkeszthető; ugyanitt „Elfelejtés”.
- A kézi „Lecsatlakozás” kikapcsolja az automatikus csatlakozást, amíg újra kézzel nem csatlakozol.

- .NET 10 MAUI (Android 8.0 / API 26 vagy újabb; a projektben iOS, MacCatalyst és Windows cél is van), Plugin.BLE
- Funkciók: vezérlés, adatok, RGB, automatikák (termosztát, logika, kapuzás, időzítők),
  WiFi-beállítás, séma-mentés `.pcscheme` fájlba, firmware-frissítés GitHub release-ből vagy helyi `.bin`-ből

Megnyitás: `app/Controll.sln` Visual Studio 2022+ programban (.NET MAUI workload szükséges).

**Firmware-frissítés forrása:** `app/Controll/Services/GitHubUpdateService.cs`
(`GitHubOwner` / `GitHubRepo`). Az app tokent nem használ, ezért a megadott repónak
**publikusnak** kell lennie, és minden release-hez egy `.bin` asset kell. A tag legyen a
verziószám (pl. `4.9.0`).

## Okosotthon: MQTT + Home Assistant

1. Állítsd be az eszköz WiFi-jét (menü vagy app).
2. Az appban: *Beállítások → Okosotthon (MQTT / Home Assistant)*: bróker címe (pl. a Home Assistant gép IP-je, port 1883), felhasználó/jelszó, az eszköz neve (pl. „Terrárium”), bekapcsolás, mentés.
3. Home Assistantban az eszköz **magától megjelenik** (MQTT discovery; az MQTT integráció és egy bróker, pl. Mosquitto kell hozzá).

Entitások: hőmérő(k) / páratartalom / kapcsoló-állapot (a szenzor-módtól függően), Relé 1–2 (kapcsoló), Ventilátor 1–2 (fan, százalékkal), RGB (fény), és csatornánként egy „automata” kapcsoló.
A Google Home, Alexa és Apple Home a Home Assistanton keresztül éri el ezeket (Home Assistant Cloud vagy Matter hídként).

- Egy csatornára érkező kézi parancs (Home Assistant / Google Home) a csatornát **kézi módba** kapcsolja, különben a termosztát azonnal visszaírná. Az „automata” kapcsolóval állítható vissza.
- Témák: `pascal/<id>/…` (az `id` az eszköz MAC-címének utolsó 3 bájtjából jön, így több eszköz nem ütközik).
- A jelszó titkosítatlanul tárolódik az eszköz flash-ében, és a kapcsolat sima MQTT (TLS nélkül) – csak megbízható helyi hálózaton használd. A jelszó BLE-n sosem megy vissza az appnak.

## Hardver-megjegyzések (T-Display, lábkiosztás)

| Funkció | GPIO | Csatlakozó láb |
|---|---|---|
| NTC1 / NTC2 (analóg) | 37 / 38 (csak bemenet) | 15 / 16 |
| Relé 1 / Relé 2 | 33 / 32 | 19 / 18 |
| Ventilátor 1/2 engedélyező | 27 / 26 | 22 / 21 |
| RGB engedélyező | 25 | 20 |
| DHT11 tápja (47 Ω-on át az NTC1 csatlakozó 1-es lábára) | 21 | 3 |
| DHT11 adatvonala (47 Ω-on át az NTC2 csatlakozó 1-es lábára) | 22 | 4 |

- **DHT11** (páratartalom mód): a tápot a GPIO21 adja, az adatvonal a GPIO22 (kimenetként is tud működni, így a DHT11 indítása is megy). A DHT11 az NTC1 (táp) és NTC2 (adat) csatlakozó 1-es lábára kerül, GND a 2-es lábra. Más módban mindkét láb nagyimpedanciás bemenet, hogy az NTC mérését ne zavarja. Bekapcsolás után ~1,5 mp felfutási idő van.
- **Felhúzó ellenállás (R10/R11)**: 2,2 kΩ, fix. A DHT11 adatvonalának is jó felhúzója (kb. 1,5 mA).
- Relé-kattanás ellen: ~10 kΩ lehúzó ellenállás a relé-meghajtók bemenetére (GPIO32/33), lásd HIBAK.md.

## BLE védelem (PIN)

- Az eszköz első induláskor (és gyári reset után) egy véletlen **6 jegyű PIN-t** generál. Megtekinthető a kijelzőn: *Beállítások → BLE védelem* (ott új PIN is generálható, és a védelem ki-be kapcsolható).
- Az app az első csatlakozáskor kéri a PIN-t, eszközönként elmenti (SecureStorage), és utána magától igazol.
- A PIN **sosem megy át BLE-n**: az eszköz véletlen nonce-t küld, az app `HMAC-SHA256(PIN, nonce)`-szal válaszol (`AUTH_BEGIN` → `AUTH_NONCE:` → `AUTH:` → `AUTH_OK`). Hitelesítés nélkül csak a `GET_VER` megy; minden más (OTA, gyári reset, WiFi stb.) `ERROR:AUTH_REQUIRED`-et kap. 5 hibás próba után 1 percre zár.
- Ha elfelejtetted a PIN-t, az eszköz kijelzőjén megnézheted. Régi firmware-rel (ami nem ismeri az AUTH-ot) az app változatlanul működik.

## Build és minőségi követelmény

**Követelmény: 0 hiba és 0 figyelmeztetés** mind a firmware-ben, mind az appban. Ezt a build maga érvényesíti:
- **Firmware** (`firmware/platformio.ini`): a saját forrásokra `-Wall -Wextra -Werror`, vagyis egyetlen figyelmeztetés is megállítja a buildet.
- **App** (`app/Controll/Controll.csproj`): `TreatWarningsAsErrors`.

Fordítás (PowerShellből, Git Bash alól a PlatformIO ESP-IDF telepítője nem fut):
```powershell
# firmware
cd firmware; python -m platformio run
# app (Android)
cd app\Controll; dotnet build Controll.csproj -f net10.0-android -c Release
```
Az iOS és MacCatalyst célt Windowson nem lehet fordítani (Mac kell hozzá), azt itt nem ellenőriztem.

Megjegyzések:
- Az app **bekapcsolt nullázhatósági elemzéssel** (`<Nullable>enable</Nullable>`) fordul, minden jelzés javítva: ahol a null szándékos (pl. nincs kapcsolat), a típus nullázható, egyébként null-ellenőrzés vagy kivétel van.
- Az Android minimum 8.0 (API 26), mert a MediaElement ezt kéri.
- A firmware-ben a harmadik fél könyvtárak verziója rögzített (`platformio.ini`), a platform a pioarduino `55.03.311`.
