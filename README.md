# Pascal-Controller

ESP32 (LilyGO TTGO T-Display) alapú vezérlő két NTC hőmérővel, két PWM ventilátorral,
két relével és RGB LED-szalaggal, valamint a hozzá tartozó .NET MAUI Android alkalmazás
(„Pascal-Controll”), ami Bluetooth Low Energy-n kommunikál vele, és OTA-n frissíti a firmware-t.

**Aktuális verzió:** firmware **v5.6.1** (PlatformIO, NimBLE, LovyanGFX + LVGL kijelző). Az `app/` még a v4.9.0-hoz tartozó változat.

## Mappák

```
firmware/                    PlatformIO projekt (platformio.ini, partitions.csv, include/lv_conf.h, src/)
app/                         .NET 10 MAUI alkalmazás (Controll.sln)
```

A lefordított `.bin` fájlok ennek a repónak a [Releases](https://github.com/ROOCK18/Pascal-Controller/releases)
oldalán vannak (release-enként egy `.bin` melléklet). A repónak publikusnak kell lennie, hogy az app és az
eszköz token nélkül le tudja tölteni őket.
A hardverfájlok (SolidWorks, STL, Proteus) Drive-on vannak.

## Firmware (v5.6.1)

- Fordítás: VS Code + PlatformIO, `pio run` a `firmware/` mappában. A platform a pioarduino
  `55.03.311` (Arduino core 3.3.11), fixált könyvtárverziókkal. Windowson PowerShellből futtasd, Git Bash alól az ESP-IDF telepítő nem megy.
- Partíciók (`partitions.csv`, 16 MB): két 6 MB-os OTA slot + ~3,9 MB SPIFFS.
- Főképernyő, jobb felső sarok: **WiFi** és **Bluetooth** jel mindig látszik. WiFi: zöld = csatlakozva, szürke = nincs. Bluetooth: kék = csatlakozva, szürke = hirdet (nincs kapcsolat), **piros = nem hirdet** (hiba).
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

Csatlakozva az "Átcsatlakozás" gomb bontja a kapcsolatot, és rögtön az eszközkeresőt mutatja, így másik eszközre lehet váltani.


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

## Közeledés érzékelés és értesítések (v5.2.0)

**Közeledés** - az app az UTOLJÁRA használt eszközt figyeli a háttérben (Android előtér-szolgáltatás; az Android kötelezően mutat hozzá
egy jelzést, ez minimális fontosságú, néma értesítés az értesítési panel alján - az app a közeledésről nem küld értesítést; az app
indításakor egyszer meg kell nyitni). A távolság a BLE jelerősségből (RSSI) **becsült**, falak/test/környezet miatt több méteres eltérés lehet.

- **Beállítások → Automatikus csatlakozás közeledéskor:** csak annyi, hogy az app az eszköz X méteres közelében magától csatlakozik. Vezérlés nincs itt.
- **Csatornánkénti közeledés** a vezérlés helyén: Vezérlés → Automatikák beállítása → a Ventilátor 1-2 és Relé 1-2 kártyák alján egy
  "Közeledés" legördülő (reléknél a Kapuzás alatt, ventilátornál a kártya legalján). Lenyitva azonnal látszik minden beállítás, legfelül a
  *bekapcsolva* kapcsolóval: *távolság (méter)*, *mit csináljon* (ventilátor: fordulatszám %; relé: BE vagy KI),
  *meddig* (mp), *újraélesedés* (teljes kapcsolatszakadás után / ha X méteren kívül vagyok / X mp múlva, ha X méteren kívül vagyok).
  Az RGB-nek külön oldala van: RGB oldal → "RGB automatikák" gomb (lásd lent).
- **Szabály:** az eszköz csak azt a ventilátort/relét kapcsolja, amelyik **Automata** állapotban van (mint a többi automatika); az RGB-nek nincs
  Automata/Kézi módja. Ventilátor: az időtartam végén visszaáll az előző értékre; relé: az időtartam alatt az automatika nem írja felül, utána átveszi;
  RGB: az időtartam után az ellenkezőjére áll (0 mp: marad). Reléknél a 0 mp a relé "Kapuzás" beállított ideje.
- A távolságot és az újraélesedést az app tárolja (az app méri), a "mit/meddig" és a bekapcsolás az eszközön is tárolódik (`SET_PROX`, `PROX_TRIGGER:<csatorna>`). Firmware 5.3.1 szükséges.

**RGB automatikák (v5.5.0):** az RGB oldalon (és az RGB automatikák oldal tetején) egy **Automata / Kézi** kapcsoló van; az automatikák
csak Automata állapotban működnek, Kézi állapotban egyik sem ír az RGB-re. RGB oldal → "RGB automatikák" gomb: négy legördülő
(mindegyik lenyitva azonnal mutat mindent, legfelül a *bekapcsolva* kapcsolóval):
- **Időzítő:** napok (H-V), max. 5 slot: *időpont, BE/KI, szín, fényerő %, átmeneti idő* (BE: felfutás, KI: lefutás). A legutóbbi elmúlt mai esemény érvényes a következőig.
- **Hőmérséklet-görbe:** forrás (1. vagy 2. hőmérő), max. 5 pont: *hőmérséklet, szín, fényerő %*; a pontok között folyamatos átmenet.
- **Napszak-görbe:** napok, max. 10 pont: *időpont, max. fényerő %* (szín nincs); a pontok között folyamatos átmenet, éjfélen át körbeér; a legördülőben grafikon mutatja a görbét. Ha be van kapcsolva és fut, **minden automatika (közeledés, időzítő, hőmérséklet-görbe) fényerejét ez adja** (a színt az automatika); a KI műveletek kikapcsolnak. Ha nincs más aktív automatika, a görbe maga világít a legutóbbi automatika-színnel (alapból fehér). 0 % a görbében = sötét szakasz, az automatika sem világít.
- **Közeledés:** X méteren belülre érve a beállított *színnel* és *fényerővel* elkezd világítani a *felfutási idővel*. *Világítási mód:* csak felkapcsol
  (utána a többi automatika dönt) / amíg ott vagyok világít (a távozást az Újraélesedés X métere adja; ha a kapcsolat 15 mp-ig megszakad, az eszköz távozottnak veszi) /
  X mp-ig világít; a kikapcsolás a *lefutási idővel* történik.
  A közeledés (és minden közeledés-legördülő) csak bekapcsolva mutatja a beállításait. A fényerőt a Napszak-görbe adja, ha fut (lásd fent).
  Időpontot az Időzítőben és a Napszak-görbében görgethető óra-választóval adsz meg (nem kell gépelni).

**Automata és Kézi kizárja egymást:** Kézi állapotban semmilyen automatika nem működik; Automata állapotban a kézi RGB-vezérlés (app, eszköz menü, MQTT) nem működik, az RGB oldalon a színkerék, a fényerő és a BE/KI szürke.
Elsőbbség: **közeledés → időzítő → hőmérséklet-görbe → napszak-görbe** (a fényerőt a napszak-görbe felülírja, ha fut). Az eszköz az átmeneteket lineárisan, az átmenet-motoron át adja ki.
BLE: `SET_AUTO:RGB:<0/1>`, `SET_RGBTIMER`, `SET_RGBTHERM`, `SET_RGBDAY` (`óó:pp:fényerő`; a régi, színes alak is elfogadott), `SET_RGBPROX`, `GET_RGBAUTO` (válasz: `LOAD_RGBAUTO/TIMER/THERM/DAY/PROX`), `PROX_LEAVE:RGB`.

**App (kezelőfelület):** a Beállítások oldalon csatlakozás után minden menüpont azonnal látszik, a lekérdezés csak a szövegeket tölti ki (nem ugrál a kép); a lekérdezések közti várakozás rövidebb, az oldalak (Vezérlés, RGB, Automatikák, RGB automatikák) a háttérben előre felépülnek. A BLE védelem PIN-je megmarad (SecureStorage + tartalék; átmeneti BLE-hiba nem törli, csak az eszköz kifejezett elutasítása). Egységes megjelenés: a vezérlők (mezők, gombok inaktív állapota, időválasztók) világosabb szürke háttérrel (`ControlGray`, #5A5A5E) jól elütnek a kártyáktól, a halvány szövegek (#AEAEB2) olvashatók, a kalibráló oldalak ugyanazt a kártya- és gombstílust használják, mint a többi.

**RGB színkalibrálás (v5.6.0 / v5.6.1):** az RGB oldalon a **Kalibrálás** gomb nyitja a kalibráló oldalt. Alapból **három csúszka** (piros, zöld, kék; számmezővel is) van: a LED-ek fehéren
világítanak a mentett kalibrációval, és a csúszkák a három csatorna fényerejét (%) állítják élőben; a **MENTÉS** az **eszközbe** tárolja (NVS), így másik telefonnal is érvényes. Mentés nélküli kilépéskor
a régi kalibráció marad. Ha nincs mentett kalibráció, mind a három 100 %. A **Kalibrálás kamerával** gomb automatikus mérést indít (CameraX, rögzített expozíció és napfény-fehéregyensúly): az app BLE-n végigfuttatja
a csatornákat, a kamera méri a fényt, és csatornánként egy **17 pontos táblázatot** számol (logikai szint 0, 16 ... 240, 255 -> PWM-kitöltés): a kimenő fény arányos lesz a szinttel (nincs korai nyitás / kimaradó szint),
és a csatornák maximuma kiegyenlített (a túl erős csatornát visszaveszi), így a `#FFFFFF` a kamera szerint semleges fehér. **Minden szín** (kézi, időzítő, görbék, közeledés, MQTT) a táblázaton át jelenik meg (`setPWM()`).
Van *Kalibráció használata* kapcsoló és *Törlés*. Kalibrálás / referencia-fehér közben az automatikák és a kézi RGB-vezérlés szünetel, az eszköz fejlécében "KAL" látszik; a mód parancs nélkül 2 perc után,
vagy a BLE kapcsolat megszakadásakor magától véget ér.
BLE: `CAL_BEGIN`, `CAL_RAW:r:g:b`, `CAL_END`, `REFWHITE:1/0`, `REFW:r:g:b`, `SET_RGBCAL:17:<R csv>:<G csv>:<B csv>`, `GET_RGBCAL` (válasz: `LOAD_RGBCAL:<használatban>:<van mentett>` + `LOAD_RGBCALTAB:<0..2>:<csv>`), `RGBCAL_ON:<0/1>`, `RGBCAL_RESET`.
Az eddigi RGB-közeledés (BE/KI az időtartamra) megszűnt: helyét a fenti Közeledés vette át, és csak Automata állapotban működik.

**Figyelmeztetések csatornánként (v5.4.0):** Vezérlés → Automatikák beállítása → a Ventilátor 1-2 és Relé 1-2 kártyák alján, a Közeledés alatt egy "Figyelmeztetés" legördülő (bekapcsolóval): *szenzorhiba*, *túl alacsony hőmérséklet* (alsó határ), *túl magas hőmérséklet* (felső határ). A csatorna szenzora a termosztátjáé (1. vagy 2. hőmérő). Az eszköz `CALERT:<csatorna>:<állapot>:<érték>` sort küld (`SET_CALERT` / `GET_CALERT`), az app ebből a csatorna nevével ír értesítést. Az RGB-nek nincs szenzora, ott nincs figyelmeztetés. A Beállítások → Értesítések panel (a két hőmérőre közös riasztás) változatlan.

**Értesítések / riasztások** (Beállítások → Értesítések, az eszközön tárolt határokkal): szenzorhiba (szakadt szál,
rövidzár, értelmezhetetlen érték), túl alacsony és túl magas hőmérséklet. Az eszköz `ALERT:<szenzor>:<állapot>:<érték>`
sort küld, az app ebből Android értesítést készít. A riasztás 5 mp-es késleltetéssel és 0,5 °C hiszterézissel működik, az
eszköz kijelzőjén piros felkiáltójel jelzi. Az értesítés akkor érkezik meg, ha a telefon kapcsolatban van az eszközzel (BLE
hatótáv, háttérfigyelés bekapcsolva). Távoli értesítéshez az MQTT / Home Assistant kapcsolat használható.

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
