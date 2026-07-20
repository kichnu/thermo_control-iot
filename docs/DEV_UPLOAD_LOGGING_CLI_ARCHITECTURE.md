# Architektura: upload firmware przez WiFi, logi zdalne, CLI dwukierunkowe

Ustalenia koncepcyjne z 2026-07-19. **Nic z poniższego nie jest jeszcze zaimplementowane**
— to zapis decyzji architektonicznych do wdrożenia później.

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

Koszt: partycje OTA (app0/app1) w `board_build.partitions = huge_app.csv`
(platformio.ini:9) **już dziś** dzielą flash na dwa sloty — to nie jest nowy koszt
wynikający z dodania kodu OTA, tylko istniejący układ partycji. Realny limit to
rozmiar binarki (musi się zmieścić w jednym slocie), nie RAM — sam kod
`ArduinoOTA`/`Update.h` to kilkanaście KB, nie dubluje zużycia RAM w runtime.

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

- (1) OTA — zaimplementowane 2026-07-20, patrz sekcja 1 wyżej. Niepotwierdzone
  na sprzęcie.
- (2) log-socket — zaimplementowane 2026-07-20, patrz sekcja 2 wyżej.
  Niepotwierdzone na sprzęcie.
- (3) CLI — nie zaimplementowane, nie dotyczy tego repo.
