# Architektura: upload firmware przez WiFi, logi zdalne, CLI dwukierunkowe

Ustalenia koncepcyjne z 2026-07-19. **Punkty 1–2 (OTA, log-socket) zaimplementowane
i zweryfikowane end-to-end na sprzęcie (2026-07-24)** — patrz sekcje "Status" niżej.
Punkt 3 (CLI dwukierunkowe) to wciąż tylko wzorzec koncepcyjny, nie dotyczy tego repo.
Krótki, przenośny wyciąg do skopiowania w kolejnych projektach (Dolewka, Dozownik):
`docs/OTA_WIFI_UPLOAD_PATTERN.md`.

Kontekst: jedna sztuka `thermo_control` (ESP32-S3), zamontowana w obudowie akwarium,
bez łatwego fizycznego dostępu do USB. Development odbywa się w tej samej sieci LAN
co laptop z VSCode/PlatformIO.

---

## 1. Upload firmware — ArduinoOTA (`espota`), nie "produkcyjne OTA"

Odrzucony wariant: pełny mechanizm OTA z GUI-upload, PIN-em, walidacją checksumów,
rollback dla nieznanych warunków u klienta — to architektura dla scenariusza
"wiele urządzeń u nietechnicznych klientów, brak fizycznego dostępu w razie awarii".
Nie dotyczy tego projektu (jedna sztuka, u siebie, w LAN).

Wybrany wariant: `ArduinoOTA` (biblioteka wbudowana w `framework-arduinoespressif32`,
zero nowych zależności) jako zamiennik kabla USB tylko na czas developmentu.

Zakres zmian:
- Drugi environment w `platformio.ini`, np. `[env:seeed_xiao_esp32s3_ota]`
  z `extends = env:seeed_xiao_esp32s3`, `upload_protocol = espota`,
  opcjonalnie `upload_port = <IP w LAN>` (bez tego — mDNS auto-discovery).
- `ArduinoOTA.begin()` w `main.cpp`, obsługa w `loop()`.
- Hasło przez `--auth=` — jedyne zabezpieczenie potrzebne przy LAN-only (brak
  tunelowania przez WireGuard/VPS, inny port niż `web_server.cpp` (80) — OTA
  nasłuchuje na 3232, ruch nie wychodzi poza LAN o ile router nie robi forwardingu).
- `ArduinoOTA.onStart()` jako hook do wywołania odpowiednika `setSystemState(false)`
  (wyłączenie grzałki/fana) przed zapisem do flash.

Koszt: **błędne założenie z 2026-07-19** — zapisano tu, że `huge_app.csv` "już dziś
dzieli flash na dwa sloty (app0/app1)". Nieprawda: `huge_app.csv` ma tylko jeden
slot aplikacji (`app0`, `ota_0`), bez `app1` — nie zweryfikowano wtedy treści
pliku CSV z frameworku. Realny koszt jednego slotu: OTA nadpisuje partycję,
z której firmware aktualnie działa (self-overwrite) — to i tak wspierany przez
`Update.h` schemat, ale na tym sprzęcie powodował twardy `abort()` w
`esp_flash_erase_region` przy pierwszym erase (patrz status niżej — naprawione
przejściem na `app3M_fat9M_16MB.csv`, prawdziwe dwa sloty, ten sam rozmiar
per-slot co poprzednio). Realny limit to rozmiar binarki (musi się zmieścić
w jednym slocie), nie RAM — sam kod `ArduinoOTA`/`Update.h` to kilkanaście KB,
nie dubluje zużycia RAM w runtime.

USB i OTA nie wykluczają się — oba environmenty mogą współistnieć w `platformio.ini`.

### Status: zaimplementowane (branch `feature/ota-logging-cli`)

- `platformio.ini`: `[env:seeed_xiao_esp32s3_ota]` z `extends = env:seeed_xiao_esp32s3`,
  `upload_protocol = espota`, bez `upload_port` (mDNS auto-discovery).
  Hasło OTA przez `${sysenv.OTA_PASSWORD}` (build_flags + upload_flags) —
  nic nie trafia do repo, ustawiane przed buildem: `export OTA_PASSWORD=...`.
- `main.cpp`: `ArduinoOTA.setPassword()` + `onStart` (wyłącza grzałkę/fan i
  `setSystemState(false)` przed zapisem flash) + `begin()` po `initWiFi()`,
  gated na `isWiFiConnected()`. `ArduinoOTA.handle()` w `loop()` co iterację
  (nie w 100ms ticku).
- **Gotcha**: dodanie drugiego environmentu bez sekcji `[platformio]`
  sprawia, że goły `pio run`/`pio run -t upload` (bez `-e`) operuje na
  WSZYSTKICH environmentach naraz — czyli próbowałby też espota-upload przy
  zwykłym uploadzie USB i wysypywał się (brak `OTA_PASSWORD`/urządzenia w
  mDNS). Naprawione przez `default_envs = seeed_xiao_esp32s3` w
  `[platformio]` — USB zostaje domyślny, OTA wymaga jawnego
  `-e seeed_xiao_esp32s3_ota`.
- Nieprzetestowane na fizycznym sprzęcie (brak urządzenia w sieci w trakcie
  implementacji) — do zweryfikowania przy najbliższej okazji.
- **Pierwszy upload musi pójść przez USB.** OTA działa tylko gdy na
  urządzeniu już działa firmware z aktywnym `ArduinoOTA.begin()` — dopóki na
  sterowniku leci firmware sprzed tej zmiany (bez OTA listenera na porcie
  3232), `espota`/mDNS discovery nic nie znajdzie. Firmware zawierający ten
  kod trzeba więc wgrać raz przez `-e seeed_xiao_esp32s3` (USB); dopiero
  kolejne uploady mogą iść przez `-e seeed_xiao_esp32s3_ota`.

Użycie: `export OTA_PASSWORD=haslo && pio run -e seeed_xiao_esp32s3_ota -t upload`.

### Gotcha: komp dev i ESP32 w różnych podsieciach (Mikrotik, izolacja IoT) — 2026-07-23

Pierwszy realny test na sprzęcie: `pio run -e seeed_xiao_esp32s3_ota -t upload
--upload-port 192.168.10.4` — auth przechodzi (`Authenticating...OK`), ale potem
`Waiting for device... No response from device` (timeout 10s).

Diagnoza (tcpdump na interfejsie kompa podczas próby): UDP invitation + auth
handshake (4 pakiety) przechodzi w obie strony bez problemu. Zwrotne połączenie
**TCP inicjowane przez ESP32** z powrotem do kompa (na losowy `host_port`) w ogóle
nie pojawia się w przechwyconym ruchu — ani jako SYN od urządzenia, ani odrzucony.

Przyczyna: komp (`192.168.2.10/24`) i ESP32 (`192.168.10.4/24`) są w różnych
podsieciach za routerem Mikrotik, z **celową** izolacją IoT VLAN. Ruch inicjowany
z LAN do IoT (GET-y dashboardu, UDP invitation) działa — to established/related
z punktu widzenia firewalla. Nowe połączenie inicjowane PRZEZ urządzenie IoT z
powrotem do LAN (dokładnie to, czego wymaga espota po autentykacji) jest
blokowane. Typowy wzorzec reguł "IoT może być odpytywane, nie może samo wchodzić
do LAN".

Fix (dwie części):

1. **W repo** — `platformio.ini`, `upload_flags` dla `seeed_xiao_esp32s3_ota`:
   dodany `--host_port=8266`, żeby wymusić stały port zwrotny (domyślnie
   `espota.py` losuje z zakresu 10000-60000 przy każdym uruchomieniu — bez tego
   nie da się zawęzić reguły firewalla do jednego portu).
2. **Poza repo, na routerze Mikrotik** — tymczasowa reguła firewalla,
   domyślnie wyłączona, włączana ręcznie tylko na czas uploadu:

   ```
   /ip firewall filter add chain=forward action=accept protocol=tcp \
       src-address=192.168.10.4 dst-address=192.168.2.10 dst-port=8266 \
       comment="OTA-thermo-temp" disabled=yes place-before=<numer reguly blokujacej>
   ```

   Workflow przy każdym OTA-uploadzie:
   ```
   # Mikrotik, przed uploadem
   /ip firewall filter enable [find comment="OTA-thermo-temp"]

   # komp
   export OTA_PASSWORD=... && pio run -e seeed_xiao_esp32s3_ota -t upload --upload-port 192.168.10.4

   # Mikrotik, po uploadzie
   /ip firewall filter disable [find comment="OTA-thermo-temp"]
   ```

**Status: ustalone, reguła na Mikrotiku jeszcze nie dodana/przetestowana end-to-end**
(wracamy do tego — dodanie reguły i pełny upload firmware to kolejny krok).

---

### Gotcha: `abort()` w `esp_flash_erase_region` przy pierwszym erase — 2026-07-23/24

Po ustaleniu reguły Mikrotika (wyżej): auth przechodzi, ale upload zawsze utykał
na 0% i po ~10s host zgłaszał `timed out`/`Connection reset by peer`. Serial
(USB, bo log-socket nie łapie crasha — pisze na UART z pominięciem
`logging.cpp`) pokazywał identyczny crash za każdym razem:

```
=== OTA START — wylaczam grzalke/fan przed zapisem flash ===
=== SYSTEM DISABLED — algorytm wstrzymany ===

abort() was called at PC 0x40375ca7 on core 1
Backtrace: ... 40375ca7 ... esp_flash_erase_region ... UpdateClass::_writeBuffer ...
```

Dekodowanie przez `xtensa-esp32s3-elf-addr2line`/`objdump` na `firmware.elf`
z tego samego builda: PC leży dokładnie w miejscu, gdzie `esp_flash_erase_region`
sprawdza wynik `chip_drv->erase_sector(...)` — jeśli wynik ≠ `ESP_OK` i
≠ `ESP_ERR_NOT_ALLOWED` (`0x10D`), IDF robi świadomy `abort()` (nie losowy crash
pamięci). Pierwsza hipoteza — szum EMI na GPIO9 (~700 fałszywych imp/s, patrz
pamięć projektu) przerywający polling flash w oknie, gdy IDF zawiesza drugi
rdzeń/cache — **obalona empirycznie**: dodanie `pauseEnergyPulseInterrupt()`/
`resumeEnergyPulseInterrupt()` (`energy_meter.h/.cpp`) wywoływanego w
`ArduinoOTA.onStart()`/`onError()` nie zmieniło niczego — identyczny PC.

Prawdziwa przyczyna: `board_build.partitions = huge_app.csv` miał (wbrew temu,
co napisano w §1 wyżej) tylko **jeden** slot aplikacji (`app0`) — OTA nadpisywało
partycję, z której firmware aktualnie działał (self-overwrite). Na tym
konkretnym sprzęcie (Waveshare ESP32-S3-Pico, realnie 16MB flash, uruchamiany
przez profil płytki `seeed_xiao_esp32s3` który zresztą też się myli co do
PSRAM — stąd `PSRAM ID read error` przy każdym boocie) ten schemat powodował
twardy błąd sterownika flash przy erase.

Fix: `board_build.partitions` zmienione na `app3M_fat9M_16MB.csv` (dopasowane
do realnych 16MB flash) — prawdziwe dwa sloty `app0`/`app1`, **każdy dokładnie
tej samej wielkości co poprzedni pojedynczy slot** (0x300000 = 3MB), więc zero
straty miejsca na firmware; partycja danych zmieniona ze SPIFFS na FAT
(nieużywana w projekcie — dane trzymane w zewnętrznym FRAM SPI). Wymagało
jednego reflashu USB (zmiana partycji nie przechodzi przez samo OTA). Po
zmianie: OTA upload przeszedł 0→100%, `Result: OK`, urządzenie odpowiada
normalnie po restarcie (`/api/health`).

**Status: naprawione, OTA upload end-to-end działa (2026-07-24).**
Pauza ISR licznika energii w `onStart()`/`onError()` została w kodzie mimo że
nie była przyczyną tego konkretnego crasha — nieszkodliwa i tak czy inaczej
rozsądna higiena wokół flash-erase.

---

## 2. Logi zdalne — zamiast USB serial monitor

Punkt zaczepienia: `src/core/logging.cpp:17-48` — `logInfo/logWarning/logError`
to jedyne miejsce w kodzie, gdzie trafia `Serial.printf(...)`. Cała reszta kodu
woła wyłącznie makra `LOG_INFO/LOG_WARNING/LOG_ERROR` (logging.h:14-16, sterowane
flagami `ENABLE_FULL_LOGGING`/`ENABLE_SERIAL_DEBUG`, oba `true` w config.h:7-8).
Rozszerzenie o kanał sieciowy to zmiana w jednym pliku, nie w dziesiątkach miejsc
wywołania.

Wybrany wariant: `WiFiServer` (np. port 8880) dodany w `logging.cpp`, dublujący
każdy `Serial.printf(...)` do podłączonych klientów TCP. Dzięki temu działa
natywnie:

```
pio device monitor --port socket://<ip-sterownika>:8880
```

PlatformIO/miniterm obsługuje `socket://` tak samo jak port szeregowy — ten sam
skrót w VSCode, tylko zamiast `/dev/ttyACM0` wskazuje się gniazdo sieciowe.

Odrzucona alternatywa (nie wykluczona na przyszłość): log-viewer w istniejącym
dashboardzie przez `AsyncEventSource` (SSE, już w zależnościach ESPAsyncWebServer)
— wygodne w przeglądarce, ale inny nawyk pracy niż `pio device monitor`.

### Status: zaimplementowane (branch `feature/ota-logging-cli`)

- `logging.h`/`logging.cpp`: `WiFiServer` na porcie 8880 (`LOG_SERVER_PORT`).
  `logInfo/logWarning/logError` scalone do wspólnego `_logLine()` (były już
  identyczne poza nazwą) — dubluje `Serial.printf` do `g_logClient`, jeśli
  podłączony i `connected()`.
- Tylko jeden klient naraz — kolejne próby połączenia są odrzucane
  (`newClient.stop()`), dopóki poprzedni się nie rozłączy. Wystarczające dla
  dev-tool LAN-only, bez kolejki/broadcastu do wielu klientów.
- `startLogServer()` wołane raz w `main.cpp` po `initWiFi()`, gated na
  `isWiFiConnected()` — razem z inicjalizacją OTA. `updateLogServer()`
  (przyjmuje nowe połączenia) w 100ms ticku w `loop()`, obok `updateWiFi()`.
- Nieprzetestowane na fizycznym sprzęcie — do zweryfikowania razem z OTA
  przy najbliższej okazji.
- Tak jak OTA: działa dopiero od firmware zawierającego ten kod — pierwszy
  upload musi pójść przez USB (patrz sekcja 1).

Użycie: `pio device monitor --port socket://<ip-sterownika>:8880`.

---

## 3. CLI dwukierunkowe — wzorzec ogólny (nie dotyczy obecnego kodu thermo_control)

**Uwaga:** ten punkt powstał w kontekście innego urządzenia (dozownik/ATO), którego
kodu nie ma w tym repozytorium (`thermo_control-iot` nie zawiera modułu pompy/
dozownika — sprawdzone, zero plików `*pump*`/`*dos*` w `src/`). Wzorzec architektury
jest jednak ogólny i przenosi się na dowolne urządzenie z aktuatorem wymagającym
komend podczas testów.

Zakres zmian (szacunkowo 1 nowy plik + integracja w `loop()`):

1. **Transport — jedno gniazdo TCP, nie dwa kanały.** TCP jest z natury
   dwukierunkowe — ten sam socket co logi (pkt 2) obsługuje też wejście
   (`client.available()` / `client.readStringUntil('\n')`), bez osobnego portu.

2. **Nowy parser komend** — jeden mały moduł (np. `core/cli.cpp`), protokół
   liniowy tekstowy (nie JSON — ma być wpisywalny ręcznie w terminalu podczas
   testów). Dispatcher np. `status`, `dose <ml>`, `pump-on`, `pump-off`, `stop`,
   `reset-stats`.

3. **Bez duplikacji logiki** — CLI woła te same funkcje co istniejące handlery
   HTTP (np. odpowiedniki `handlePumpDirectOn`/`Off` w `web_handlers.cpp`).
   CLI to druga "wejściówka" do tej samej logiki, nie równoległa ścieżka sterowania.

4. **Integracja z `loop()` — nieblokująco.** Odczyt komend musi być non-blocking
   (obok `ArduinoOTA.handle()`) — algorytm sterujący jest czasowo wrażliwy i nie
   może czekać na wpisanie komendy.

Dodatkowo, bo CLI steruje aktuatorem (nie tylko podgląd):
- Minimalny token/hasło przy połączeniu — LAN-only wystarcza, ale komenda typu
  `dose 200` ma fizyczny efekt.
- Komenda `stop`/e-stop jako priorytet — istotne przy testach na żywym sprzęcie.

---

## Status

Ustalenia koncepcyjne z 2026-07-19. Wdrożenie w kolejności: (1) OTA + drugi
environment, (2) log-socket w `logging.cpp`, (3) CLI — dopiero gdy dotyczy
konkretnego urządzenia z aktuatorem.

- (1) OTA — zaimplementowane 2026-07-20. Pierwszy test na sprzęcie 2026-07-23:
  auth/handshake potwierdzony, pełny upload zablokowany przez izolację IoT VLAN
  na Mikrotiku (komp i ESP32 w różnych podsieciach) — workaround ustalony,
  patrz "Gotcha" w sekcji 1, jeszcze nie przetestowany end-to-end.
- (2) log-socket — zaimplementowane 2026-07-20, patrz sekcja 2 wyżej.
  Niepotwierdzone na sprzęcie.
- (3) CLI — nie zaimplementowane, nie dotyczy tego repo.
