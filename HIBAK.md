# Hibajegyzet – áttekintés (firmware 5.0.0 + app)

Állapot: 2026-11-24. Súlyosság: **K** = kritikus/biztonsági, **M** = közepes, **A** = alacsony/kozmetikai.
A ✅ a már javított tételeket jelöli (a javítás helyben van, a fordítást lásd lent).

## Firmware

| # | Sev. | Hol | Hiba |
|---|---|---|---|
| F1 ✅ | K | `Sensors.h` `readNTCTemperature` | *(javítva: hibás szenzornál NaN, a relé KI-re áll, a kijelzőn „--”)* **Nincs szenzorhiba-felismerés.** Szakadt (ADC 4095 → nullával osztás, `VCC - voltage = 0`) vagy rövidzárlatos szenzornál a hőmérséklet −273 °C vagy ∞ lesz, és a relé-termosztát ezen dolgozik tovább. Fűtés módban (`t <= ton`) a relé **tartósan bekapcsolva marad**. Kell: érvényes ADC-tartomány (pl. 50–4045), hibánál NaN, és az automatika álljon biztonságos állapotba. |
| F2 ✅ | M | `Sensors.h` / `Config.h` | *(javítva: kalibrált `analogReadMilliVolts`, 16-os átlagolás, a tápfeszültség és a soros ellenállás beállítható)* **ADC-skála.** A feszültség `adc * VCC / 4095` képlettel számolódik `VCC = 5`-tel, de az ESP32 ADC-je 11 dB-nél kb. 3,1–3,3 V-nál telít. Ha a bemeneten nincs 5 V → 3,3 V osztó, a számított ellenállás és hőmérséklet hibás; ráadásul 5 V-os felhúzás az ESP32 bemenetét 3,3 V fölé viheti. Az ADC nemlineáris, és nincs átlagolás sem (1 db `analogRead`). → ezért kell a kalibrálható szenzor-beállítás. |
| F3 | M | `main.cpp` DHT11 ág | Sikertelen DHT11-olvasásnál a **régi (elavult) érték marad** korlátlan ideig, a termosztát tovább dolgozik rajta. Kell: időtúllépés, utána NaN/hiba. |
| F4 ✅ | K | `BLECommands.h` `SET_MODE` + `Storage.h` | *(javítva: 0–4 ellenőrzés beállításkor és betöltéskor)* | **A mód nincs ellenőrizve (0–4).** Egy hibás/rosszindulatú BLE-írás `currentMode = 99`-et állít, ezt NVS-be menti, és **minden induláskor** tömbön kívül indexel (`MODE_SHORT[...]`) → összeomlás, bootloop, csak gyári reset (vagy újraflashelés) segít. Kell: korlátozás 0–4-re mind beállításnál, mind betöltésnél. |
| F5 | K | BLE (teljes) | **A BLE nyitott, hitelesítés nélkül.** Bárki a közelben küldhet `FACTORY_RESET`-et, `SET_WIFI`-t, és **tetszőleges firmware-t tölthet fel** (`UPDATE:`). Tudatos döntés volt (a bonding GATT 133 hibát okozott), de biztonsági kockázat. Minimum: OTA csak a menüből engedélyezett „frissítési ablak”-ban, vagy aláírt/ellenőrzött image. |
| F6 | M | `WifiOTA.h` | `client.setInsecure()` – a HTTPS-letöltés **nem ellenőrzi a tanúsítványt**, így hálózati támadó (MITM) tetszőleges firmware-t adhat. Kell: GitHub gyökértanúsítvány beégetése, vagy a letöltött bin aláírás-/hash-ellenőrzése. |
| F16 | A | `Mqtt.h` | Az MQTT-kapcsolat TLS nélküli, a jelszó titkosítatlanul van az NVS-ben; a (nyitott) BLE-n bárki átállíthatja a bróker-beállítást (a jelszót visszaolvasni nem tudja). Kell: TLS (8883) + a BLE-hozzáférés védelme (lásd F5). |
| F7 | M | `BLECommands.h` | A **parancsfeldolgozás a BLE callback taskjában** fut, a fő loop pedig ugyanazokat a globálisakat (String-ek, struktúrák: `wifiSsid`, `thermF1`, `relayTimer*`) olvassa → versenyhelyzet, ritka összeomlás. Kell: parancsok sorba állítása, feldolgozás a loop()-ban. Ide tartozik: `factoryReset()` (delay + restart) és `Update.begin()` (flash törlés) a callbackből. |
| F8 | M | `OTA.h` / `BLECommands.h` | A BLE OTA sora (`std::queue`, csomagonként ~520 B) **nincs korlátozva** és nincs visszajelzés (ACK): ha az app gyorsabban küld, mint ahogy a flash-írás halad, elfogy a heap. A túl hosszú csomagot (`> MAX_OTA_CHUNK`) csendben **levágja**, ami sérült image-et ad. |
| F9 | M | `Globals.h` `WIFI_PW_MAX_LEN` | A jelszó legfeljebb **32 karakter**, a WPA2 viszont 63-at enged → hosszú jelszavú hálózatra a menüből nem lehet csatlakozni. |
| F10 | A | `WifiOTA.h` `handleSetWifi`, `WIFI_NET:` | A `:` az elválasztó, ezért **kettőspontot tartalmazó SSID/jelszó** hibásan darabolódik. |
| F11 | A | `Menu.h` WiFi lista | Legfeljebb 11 hálózat jelenik meg (a keresés 15-öt tárol, a lista 12 sorra méretezett). |
| F12 | A | `BLECommands.h` `TIME:` | Fix pozíciókból olvas, **tartomány-ellenőrzés nélkül** (pl. `TIME:99:99:99` → érvénytelen óra). |
| F13 | A | `Timers.h` | Az időzítők **napszűrése (`days`) tárolva van, de nincs használva**; az időzítő a relé lábat közvetlenül írja (`digitalWrite`), megkerülve a `setRelayAutomation()`-t (a Kapuzás/zárolás nem érvényesül). Nincs RTC: áramkimaradás után az óra az utolsó mentett értékről indul, amíg az app nem szinkronizál. |
| F14 | A | `Storage.h` | A mentett `mode` betöltéskor nincs ellenőrizve (lásd F4). |
| F15 | A | `lgfx_setup.h` (felhasználói módosítás) | `setRotation(3)` + `offset_rotation = 2` együtt működik, de tisztább lenne `setRotation(1)` + `offset_rotation = 0`. A 180°-os elforgatás után a két gomb **iránya** (Fel/Le) a képernyőhöz képest megfordulhat – érdemes `BTN_LEFT`/`BTN_RIGHT` szerepét ellenőrizni. |

### Az 5.0.0-s átállásnál bevezetett hibák (az én hibáim)
| # | Sev. | Hiba | Állapot |
|---|---|---|---|
| N1 | M | Az idle képernyőn **NaN hőmérséklet** (szakadt szenzor) soha nem frissült, a csempe üres maradt (`fabs(NaN-x) >= 0.1` mindig hamis). | ✅ javítva (`tempChanged()`) |
| N2 | A | A **gyári visszaállítás** képernyője nem rajzolódott ki (a `delay()+restart` előtt a loop nem jut `ui_tick()`-hez). | ✅ javítva (`ui_refresh_now()`) |
| N3 | A | Hosszú **WiFi-jelszónál a vége nem látszott** (a szöveg jobbról volt levágva). | ✅ javítva (a vég látszik) |
| N4 | M | NimBLE-nél a sok egymás utáni notify (pl. `GET_ALL`) elveszhet, ha a belső puffer betelik. | ✅ javítva (újrapróbálás, `Comm.h`) |
| N5 | A | A fok-jel (`°`) glifája az LVGL beépített Montserrat betűiben **nincs ellenőrizve**; ha hiányzik, négyzet jelenik meg. Hardveren / szimulátorban megnézendő. | nyitott |
| N6 | A | A kijelző kinézete **nincs képként ellenőrizve** (a PC-s szimulátor fordítási hibán megállt) – átfedések lehetségesek. | nyitott |
| N7 ✅ | A | Ékezetes betűk: a saját betűtípusok (`firmware/src/fonts`, generálás: `tools/gen_fonts.sh`) beépítve, `-DHU_FONTS=1`. A `°`, `Ω`, `β` glifák megjelenése hardveren ellenőrizendő. | javítva |

## App

| # | Sev. | Hol | Hiba |
|---|---|---|---|
| A1 | M | `GitHubUpdateService.ParseVersion` | A verziót a **tag nevéből** számolja; a korábbi tagek (`PaC_FW_4.2.1`, `Pac_FW`) hibásan értelmeződnek (`PaC_FW_4` → 0), így **rossz sorrend / hamis „van újabb verzió”**. A release-ek tagje legyen tiszta `x.y.z` (vagy `v x.y.z`). |
| A2 | A | `GetAllReleasesAsync` | Csak az első oldalt (30 release) kéri le (nincs lapozás), és a hitelesítés nélküli GitHub API korlátja 60 kérés/óra/IP – közös (mobilháló) IP-nél elfogyhat. |
| A3 | A | Releases | A kiadás tagje és a firmware `FW_VERSION`-ja nincs összevetve letöltés után (nem derül ki, ha rossz bint töltöttek fel a taghez). |
| A4 | A | `Settings.xaml.cs` | Az app a BLE OTA-nál 128 B-os csomagot küld, a firmware pedig ennél hosszabbat csendben levág (lásd F8) – a két oldal szerződése csak megjegyzésben létezik. |

## Fordítás / környezet
- A PlatformIO a `~/.platformio/penv`-et megosztja a VS Code bővítménnyel; 2026-11-24 09:05-kor a bővítmény újraépítette, és a parancssoros (`python -m platformio`) fordítás emiatt hibával áll le (`No pyvenv.cfg file`). A fenti javítások **fordítása emiatt nem lett újra ellenőrizve**; a VS Code-ban (`Build`) ellenőrizhető.
