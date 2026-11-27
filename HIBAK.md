# Hibajegyzet – áttekintés (firmware 5.0.0 + app)

Állapot: 2026-11-24. Súlyosság: **K** = kritikus/biztonsági, **M** = közepes, **A** = alacsony/kozmetikai.
A ✅ a már javított tételeket jelöli (a javítás helyben van, a fordítást lásd lent).

## Firmware

| # | Sev. | Hol | Hiba |
|---|---|---|---|
| F1 ✅ | K | `Sensors.h` `readNTCTemperature` | *(javítva: hibás szenzornál NaN, a relé KI-re áll, a kijelzőn „--”)* **Nincs szenzorhiba-felismerés.** Szakadt (ADC 4095 → nullával osztás, `VCC - voltage = 0`) vagy rövidzárlatos szenzornál a hőmérséklet −273 °C vagy ∞ lesz, és a relé-termosztát ezen dolgozik tovább. Fűtés módban (`t <= ton`) a relé **tartósan bekapcsolva marad**. Kell: érvényes ADC-tartomány (pl. 50–4045), hibánál NaN, és az automatika álljon biztonságos állapotba. |
| F2 ✅ | M | `Sensors.h` / `Config.h` | *(javítva: kalibrált `analogReadMilliVolts`, 16-os átlagolás, a tápfeszültség és a soros ellenállás beállítható)* **ADC-skála.** A feszültség `adc * VCC / 4095` képlettel számolódik `VCC = 5`-tel, de az ESP32 ADC-je 11 dB-nél kb. 3,1–3,3 V-nál telít. Ha a bemeneten nincs 5 V → 3,3 V osztó, a számított ellenállás és hőmérséklet hibás; ráadásul 5 V-os felhúzás az ESP32 bemenetét 3,3 V fölé viheti. Az ADC nemlineáris, és nincs átlagolás sem (1 db `analogRead`). → ezért kell a kalibrálható szenzor-beállítás. |
| F3 ✅ | M | `main.cpp` DHT11 ág | Sikertelen DHT11-olvasásnál a **régi (elavult) érték marad** korlátlan ideig, a termosztát tovább dolgozik rajta. Kell: időtúllépés, utána NaN/hiba. |
| F4 ✅ | K | `BLECommands.h` `SET_MODE` + `Storage.h` | *(javítva: 0–4 ellenőrzés beállításkor és betöltéskor)* | **A mód nincs ellenőrizve (0–4).** Egy hibás/rosszindulatú BLE-írás `currentMode = 99`-et állít, ezt NVS-be menti, és **minden induláskor** tömbön kívül indexel (`MODE_SHORT[...]`) → összeomlás, bootloop, csak gyári reset (vagy újraflashelés) segít. Kell: korlátozás 0–4-re mind beállításnál, mind betöltésnél. |
| F5 ✅ | K | BLE (teljes) | **A BLE nyitott, hitelesítés nélkül.** Bárki a közelben küldhet `FACTORY_RESET`-et, `SET_WIFI`-t, és **tetszőleges firmware-t tölthet fel** (`UPDATE:`). Tudatos döntés volt (a bonding GATT 133 hibát okozott), de biztonsági kockázat. Minimum: OTA csak a menüből engedélyezett „frissítési ablak”-ban, vagy aláírt/ellenőrzött image. |
| F6 | M | `WifiOTA.h` | `client.setInsecure()` – a HTTPS-letöltés **nem ellenőrzi a tanúsítványt**, így hálózati támadó (MITM) tetszőleges firmware-t adhat. Kell: GitHub gyökértanúsítvány beégetése, vagy a letöltött bin aláírás-/hash-ellenőrzése. |
| F16 | A | `Mqtt.h` | Az MQTT-kapcsolat TLS nélküli, a jelszó titkosítatlanul van az NVS-ben; a (nyitott) BLE-n bárki átállíthatja a bróker-beállítást (a jelszót visszaolvasni nem tudja). Kell: TLS (8883) + a BLE-hozzáférés védelme (lásd F5). |
| F7 ✅ | M | `BLECommands.h` | A **parancsfeldolgozás a BLE callback taskjában** fut, a fő loop pedig ugyanazokat a globálisakat (String-ek, struktúrák: `wifiSsid`, `thermF1`, `relayTimer*`) olvassa → versenyhelyzet, ritka összeomlás. Kell: parancsok sorba állítása, feldolgozás a loop()-ban. Ide tartozik: `factoryReset()` (delay + restart) és `Update.begin()` (flash törlés) a callbackből. |
| F8 ✅ | M | `OTA.h` / `BLECommands.h` | A BLE OTA sora (`std::queue`, csomagonként ~520 B) **nincs korlátozva** és nincs visszajelzés (ACK): ha az app gyorsabban küld, mint ahogy a flash-írás halad, elfogy a heap. A túl hosszú csomagot (`> MAX_OTA_CHUNK`) csendben **levágja**, ami sérült image-et ad. |
| F9 ✅ | M | `Globals.h` `WIFI_PW_MAX_LEN` | A jelszó legfeljebb **32 karakter**, a WPA2 viszont 63-at enged → hosszú jelszavú hálózatra a menüből nem lehet csatlakozni. |
| F10 ✅ | A | `WifiOTA.h` `handleSetWifi`, `WIFI_NET:` | A `:` az elválasztó, ezért **kettőspontot tartalmazó SSID/jelszó** hibásan darabolódik. |
| F11 ✅ | A | `Menu.h` WiFi lista | Legfeljebb 11 hálózat jelenik meg (a keresés 15-öt tárol, a lista 12 sorra méretezett). |
| F12 ✅ | A | `BLECommands.h` `TIME:` | Fix pozíciókból olvas, **tartomány-ellenőrzés nélkül** (pl. `TIME:99:99:99` → érvénytelen óra). |
| F13 ✅ | A | `Timers.h` | Az időzítők **napszűrése (`days`) tárolva van, de nincs használva**; az időzítő a relé lábat közvetlenül írja (`digitalWrite`), megkerülve a `setRelayAutomation()`-t (a Kapuzás/zárolás nem érvényesül). Nincs RTC: áramkimaradás után az óra az utolsó mentett értékről indul, amíg az app nem szinkronizál. |
| F14 | A | `Storage.h` | A mentett `mode` betöltéskor nincs ellenőrizve (lásd F4). |
| F15 ✅ | A | `lgfx_setup.h` (felhasználói módosítás) | `setRotation(3)` + `offset_rotation = 2` együtt működik, de tisztább lenne `setRotation(1)` + `offset_rotation = 0`. A 180°-os elforgatás után a két gomb **iránya** (Fel/Le) a képernyőhöz képest megfordulhat – érdemes `BTN_LEFT`/`BTN_RIGHT` szerepét ellenőrizni. |

### Az 5.0.0-s átállásnál bevezetett hibák (az én hibáim)
| # | Sev. | Hiba | Állapot |
|---|---|---|---|
| N1 | M | Az idle képernyőn **NaN hőmérséklet** (szakadt szenzor) soha nem frissült, a csempe üres maradt (`fabs(NaN-x) >= 0.1` mindig hamis). | ✅ javítva (`tempChanged()`) |
| N2 | A | A **gyári visszaállítás** képernyője nem rajzolódott ki (a `delay()+restart` előtt a loop nem jut `ui_tick()`-hez). | ✅ javítva (`ui_refresh_now()`) |
| N3 | A | Hosszú **WiFi-jelszónál a vége nem látszott** (a szöveg jobbról volt levágva). | ✅ javítva (a vég látszik) |
| N4 | M | NimBLE-nél a sok egymás utáni notify (pl. `GET_ALL`) elveszhet, ha a belső puffer betelik. | ✅ javítva (újrapróbálás, `Comm.h`) |
| N5 | A | A fok-jel (`°`) glifája a kijelző (Open Sans) betűiben **nincs ellenőrizve**; ha hiányzik, négyzet jelenik meg. Hardveren / szimulátorban megnézendő. | nyitott |
| N6 | A | A kijelző kinézete **nincs képként ellenőrizve** (a PC-s szimulátor fordítási hibán megállt) – átfedések lehetségesek. | nyitott |
| N7 ✅ | A | Ékezetes betűk: a saját betűtípusok (`firmware/src/fonts`, generálás: `tools/gen_fonts.sh`) beépítve, `-DHU_FONTS=1`. A `°`, `Ω`, `β` glifák megjelenése hardveren ellenőrizendő. | javítva |

## App

| # | Sev. | Hol | Hiba |
|---|---|---|---|
| A1 ✅ | M | `GitHubUpdateService.ParseVersion` | A verziót a **tag nevéből** számolja; a korábbi tagek (`PaC_FW_4.2.1`, `Pac_FW`) hibásan értelmeződnek (`PaC_FW_4` → 0), így **rossz sorrend / hamis „van újabb verzió”**. A release-ek tagje legyen tiszta `x.y.z` (vagy `v x.y.z`). |
| A2 ✅ | A | `GetAllReleasesAsync` | Csak az első oldalt (30 release) kéri le (nincs lapozás), és a hitelesítés nélküli GitHub API korlátja 60 kérés/óra/IP – közös (mobilháló) IP-nél elfogyhat. |
| A3 | A | Releases | A kiadás tagje és a firmware `FW_VERSION`-ja nincs összevetve letöltés után (nem derül ki, ha rossz bint töltöttek fel a taghez). |
| A4 ✅ | A | `Settings.xaml.cs` | Az app a BLE OTA-nál 128 B-os csomagot küld, a firmware pedig ennél hosszabbat csendben levág (lásd F8) – a két oldal szerződése csak megjegyzésben létezik. |

## Javítva az utolsó körben
- **F3**: a DHT11 15 mp érvényes mérés nélkül NaN-t ad (nincs elavult érték).
- **F7**: a BLE parancsok a callbackben csak sorba kerülnek (`bleCmdPush`), a feldolgozás a fő szálon fut (`processBleCommands()` a `loop()` elején) – nincs versenyhelyzet, a `factoryReset()`/`Update.begin()` sem fut a BLE taskban.
- **F8**: a túl nagy OTA-csomag hibával zár (nem vágja le), a sor legfeljebb 400 csomag; túlterhelésnél az OTA hibával leáll.
- **F9**: a WiFi-jelszó legfeljebb 63 karakter. **F10**: `SET_WIFI` escape-elés (`\:`, `\`), régi formátummal kompatibilis; az app escape-el. **F11**: a WiFi lista 15 hálózatig mutat (a kijelölés és a lista eltért). **F12**: `TIME:` tartomány-ellenőrzés.
- **A1**: a verziót a tag *bármely* előtagjával/utótagjával értelmezi (`PaC_FW_4.2.1`, `v5.0.0`). **A2**: 100 release lekérése. **A4**: a firmware-app csomagméret-szerződés hibát ad csendes csonkolás helyett.
- **Relé-kattanás induláskor**: a relé/ventilátor/RGB lábak a `setup()` legelején kapnak KI szintet (az LVGL indulása előtt). Hardver oldalon továbbra is kell egy ~10 kΩ lehúzó ellenállás a relé-meghajtók bemenetére (GPIO32/33), mert a reset–firmware-indulás közti rövid időben a láb lebeg; ezt szoftver nem tudja kiküszöbölni.

## Nyitott (szándékosan nem javítva)
- **F16** (MQTT TLS) és **F6** (tanúsítvány-ellenőrzés): a javításuk protokoll-/infrastruktúra-változás, hardveres próba nélkül az OTA-t vagy a csatlakozást törhetné el.
- **F13** (időzítők napszűrése): megvalósítva (7 bites maszk, 0. bit = hétfő); az időzítő a relé lábat továbbra is közvetlenül írja.
- **F15** (kijelző forgatása): lezárva, egyetlen orientáció van (a felhasználó beállította).
- **N5/N6** (jelek, kinézet): csak hardveren ellenőrizhető.
- **Hardver**: a DH_ON a GPIO36-on van, ami csak bemenet – át kell kötni GPIO21/22-re (lásd a beszélgetést).


## Utolsó átnézés (firmware + app)
- **App – versenyhelyzet:** a keresés szálán futott a duplikáció-ellenőrzés, miközben a UI szál módosította a listát (`Collection was modified` kivétel esélye). Az ellenőrzés és a módosítás most a UI szálon fut.
- **App – párhuzamos csatlakozás:** kézi csatlakozáskor a háttér-újracsatlakozás ugyanahhoz az eszközhöz is próbálkozhatott. Kézi csatlakozás előtt most leáll (`StopAutoReconnect()`), sikertelen kézi próbálkozás után újraindul.
- **Firmware – MQTT:** a task egy pillanatkép-másolatot használ a beállításokból (a BLE szál módosíthatja azokat), az állapot-közzététel kódja áttekinthetőbb lett (a fordító félrevezető behúzást jelzett).
- **Firmware – apróságok:** szűkítő konverzió a menü sor-inicializálójában, `printf` formátum (`%lu`), felesleges ismételt `pinMode` hívások a `setup()`-ban.
- A fordító `-Wall -Wextra` mellett a saját forrásokban további gyanús helyet nem jelzett.


## Fordítási figyelmeztetések (0 / 0 követelmény)
- **App:** 145 figyelmeztetésből 0 maradt. Az elavult MAUI-hívások korszerűsítve (`DisplayAlertAsync`, `DisplayActionSheetAsync`, ablak-alapú oldal-elérés `Windows[0].Page`, `CreateWindow`, `IPlatformApplication`), a platformfüggő hívások verzió-ellenőrzéssel (`IsAndroidVersionAtLeast(29)`, Android min. 26), a kötéseknél `x:DataType` (Release-ben az XC0022). A 75 nullázhatósági jelzés **mind kijavítva**, a `<Nullable>enable</Nullable>` bekapcsolva maradt (nullázható típusok, null-ellenőrzések, `UiHelper`/`ThemeColor` segédek, az adapter egyszeri inicializálása).
- **Firmware:** `-Wall -Wextra -Werror` mellett tiszta fordítás, a harmadik fél könyvtárak sem adnak figyelmeztetést.
- A build ezt érvényesíti is (`-Werror`, `TreatWarningsAsErrors`).

## Fordítás / környezet
- A PlatformIO a `~/.platformio/penv`-et megosztja a VS Code bővítménnyel; 2026-11-24 09:05-kor a bővítmény újraépítette, és a parancssoros (`python -m platformio`) fordítás emiatt hibával áll le (`No pyvenv.cfg file`). A fenti javítások **fordítása emiatt nem lett újra ellenőrizve**; a VS Code-ban (`Build`) ellenőrizhető.

## Firmware-ellenőrzés (utolsó kör)
- Fordítás `-Wall -Wextra -Werror`: 0 figyelmeztetés, 0 hiba; `pio check`: nincs high/medium találat (a `WifiOTA.h` `httpCode` jelzése téves, a változó használatban van).
- Számítási logika Python-tükörrel ellenőrizve: PT1000 (Callendar–Van Dusen, -50…250 °C, 2,2 kΩ felhúzó) hiba < 1e-9 °C, NTC β-modell, időzítő-napmaszk (4000 nap, 0 eltérés).
- Megjegyzés: szenzorhibánál (NaN) a termosztátrelé KI-re áll; a ventilátorgörbe ilyenkor a görbe utolsó (legnagyobb) pontját használja – szándékosan biztonságos oldal.
- Hardveren nem próbáltam (BLE+PIN, OTA, MQTT, DHT11, PT1000, új kijelző-kinézet, propeller-sebesség).
- **N7** (súlyos, javítva): a Beállítások menü 9 elemű, de a `drawMenuList()` csak 8 soros tömböt használt, ezért a lista alsó elemeinél (BLE védelem, Factory reset, Vissza) tömbön kívüli olvasás → az eszköz újraindult. Most 12 soros a tömb, és a szám le van korlátozva.

## 5.2.0 – új funkciók: ismert korlátok
- A közeledés-távolság a BLE jelerősségből becsült, nem pontos mérés (falak, test, telefon: több méter eltérés).
- A háttérfigyelés Android előtér-szolgáltatás: az appot telefon-újraindítás után egyszer meg kell nyitni.
- A riasztás-értesítés csak kapcsolódott (hatótávon belüli) telefonra érkezik; távoli értesítéshez MQTT/Home Assistant kell (a riasztás MQTT-s bejelentése még nincs megvalósítva).
- Hardveren / telefonon nem próbáltam: közeledés-csatlakozás, relé-impulzus, ALERT értesítés, előtér-szolgáltatás.

## 5.4.0 – csatornánkénti figyelmeztetések: ismert korlátok
- A csatorna-figyelmeztetés ugyanúgy csak kapcsolódott (hatótávon belüli) telefonra érkezik, mint az 5.2.0 riasztás; MQTT-s bejelentése nincs.
- A csatorna szenzora a termosztátjáé; páratartalom-forrásnál az 1. hőmérő hőmérséklete számít. Az RGB-nek nincs figyelmeztetése (nincs szenzora).
- A Közeledés a háttér-kapcsolat jelzését az Android nem engedi teljesen elrejteni: minimális fontosságú, néma értesítés marad az értesítési panel alján.
- Hardveren / telefonon nem próbáltam: CALERT értesítés, közeledés legördülő elrendezés.

## 5.5.0 – RGB automatikák: ismert korlátok
- Az RGB kézi színállítása (kör, csúszka, ON/OFF) Automata állapotban is megy, de az automatika a következő változásnál (időzítő-esemény, görbe-érték, közeledés) felülírja; tartós kézi vezérléshez Kézi állapot kell.
- Az időzítő a legutóbbi elmúlt mai eseményt tartja érvényben; éjfél után, az első esemény előtt a görbék (ha be vannak kapcsolva) vagy az utolsó állapot marad.
- A napszak-görbe az eszköz órájára épül: ha az óra nincs szinkronban (TIME), a görbe is eltér.
- Az RGB-közeledés "amíg ott vagyok" módja a telefon becsült távolságára épül (RSSI); a távozást a telefon jelzi, kapcsolatvesztésnél az eszköz 15 mp után magától.
- Hardveren / telefonon nem próbáltam: átmenet-motor, időzítő, görbék, közeledés-felfutás, Automata/Kézi.

## 5.5.2 / 5.5.3 – napszak-görbe, Automata/Kézi, állapotjelek: ismert korlátok
- A napszak-görbe csak fényerőt ad; egyedül a legutóbbi automatika színével világít (alapból fehér). Ez a "legutóbbi szín" újraindításkor elvész (fehérre áll vissza).
- Automata alatt a kézi RGB-parancsok (app, eszköz menü, MQTT) eldobódnak; Kézi alatt az automatikák nem futnak.
- A fejléc BT ikonja piros, ha az eszköz nem hirdet (kapcsolat nélkül); csatlakozva kék, hirdetve szürke. WiFi: zöld/szürke.
- Hardveren / telefonon nem próbáltam: napszak-grafikon, görgethető időválasztó, fejléc-ikonok.

## 5.6.0 – RGB színkalibrálás: ismert korlátok
- A "fehér" a telefon kamerája szerinti semleges fehér (napfény-fehéregyensúllyal); a kamera színérzékenysége nem azonos a szemével, ezért jó közelítés, nem műszeres mérés. Sötét helyiség és nyugodt kéz kell.
- A kalibráció a túl erős csatornákat visszaveszi (a gyengébbet nem tudja erősíteni), ezért a teljes fényerő kicsit csökken; 8 bites PWM mellett a visszavett csatornán kevesebb finom lépés marad.
- A korán nyitó csatornánál (pl. kék) a legalsó fényerő-szinteket a 8 bites PWM felbontása korlátozza: a logikai 1..30 körüli szintek ott 0 vagy 1-2 kitöltésre képződnek le, finomabb lépéshez nagyobb PWM-felbontás kellene (későbbi fejlesztés).
- Színes falról visszaverődő fénynél (a kamera nem közvetlenül a LED-et látja) a fal színe torzít: fehér felületre vagy közvetlenül a szalagra irányítsd.
- Kalibrálás közben az automatikák és a kézi RGB-vezérlés szünetel; a mód 2 perc parancs nélkül, vagy a BLE kapcsolat megszakadásakor magától véget ér.
- Az Androidos CameraX-hívások (AE-zár, napfény-fehéregyensúly) eszközönként eltérően támogatottak; ha egy telefon nem veszi figyelembe, a mérés pontatlanabb lehet.
- Hardveren / telefonon nem próbáltam: a teljes kalibrálási menet (kamera, mérés, számítás, mentés), a "Kalibráció használata" kapcsoló, a törlés.
