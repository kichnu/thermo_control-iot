# Thermo Control — Architektura (FINAL DRAFT)

> Status: gotowy do zatwierdzenia i startu implementacji.

---

## 1. Stack sprzętowy (potwierdzony)

| Element              | Wartość                                                         |
|----------------------|-----------------------------------------------------------------|
| MCU                  | Waveshare ESP32-S3-Pico (dual-core Xtensa LX7 240MHz, 512KB SRAM, octal PSRAM) |
| FRAM                 | MB85RS64V — **SPI, 8KB** (Adafruit SPI FRAM breakout)          |
| Temp. czujnik        | STS35 (Sensirion, Forbot) — I2C, ±0.1°C                        |
| Grzałka aktuator     | SSR (Fotek SSR-25DA lub odpowiednik), active LOW                |
| Wentylatory aktuator | Iduino ST1168 (MOSFET PWM, 5–24V DC)                           |
| Miernik energii      | wejście impulsów GPIO (open-collector, przez optocoupler PC817) |
| RTC                  | DS3231 — I2C                                                    |
| Buzzer               | pasywny, HIGH = ON                                              |
| Przycisk             | provisioning (dedykowany pin — bez dual-use)                    |

**Zmiana MCU: ESP32-C3 → ESP32-S3 → Waveshare ESP32-S3-Pico**
- Powód pierwszej zmiany (C3→S3): ESP32-C3 ma 1× I2C; STS35 na kablu ~1m wymaga osobnej magistrali
- ESP32-S3: 2× sprzętowe I2C (Wire + Wire1) — bez software bit-bang
- Źródło: ESP32-S3 Datasheet v1.6, sekcja Features: "2 × I2C"
- Wire (I2C0, GPIO5/6, 400kHz): DS3231 RTC — kable <5cm
- Wire1 (I2C1, GPIO7/8, 100kHz): STS35 — kabel ~1m, niższa prędkość = lepsza odporność

**Zmiana FRAM: I2C 32KB (MB85RC256V) → SPI 8KB (MB85RS64V)**
- Powód: przejście na event-driven logging temperatury (ring buffer loguje tylko
  zmiany strefy/heartbeat, nie periodic co 30 min — patrz sekcja 5-6) potrzebuje
  dużo mniej miejsca; mniejszy, tańszy chip wystarcza
- SPI wymaga 4 linii (SCK/MOSI/MISO/CS) obok już zajętych I2C0+I2C1+aktuatorów —
  XIAO ESP32-S3 (11 wyprowadzonych GPIO) nie ma na to miejsca
- Stąd druga zmiana płytki: Seeed XIAO ESP32-S3 → **Waveshare ESP32-S3-Pico**
  (dużo więcej wyprowadzonych GPIO, ten sam chip ESP32-S3)
- `platform`/`board` w PlatformIO zostaje bez zmian (`seeed_xiao_esp32s3`) —
  board id opisuje chip/flash/toolchain, nie fizyczny pinout płytki; pinout
  jest wyłącznie w `hardware_pins.h` (konwencja potwierdzona: Dozownik/ATO
  używa tego samego wzorca — `board` w `[env]` niezależny od faktycznej płytki)
- SPI FRAM na domyślnych pinach IO_MUX SPI2/FSPI ESP32-S3 (najszybsza ścieżka,
  bez GPIO matrix): CS=GPIO10, MOSI=GPIO11, SCK=GPIO12, MISO=GPIO13
- Biblioteka `Adafruit FRAM SPI` (zamiast `Adafruit FRAM I2C`)
- Pełny pinout płytki: `docs/ESP32-S3-Pico pinout.md`

> ⚠️ GPIO3, GPIO21 = wyprowadzone tylko jako test pointy (TP) przy USB-C, nie
> w goldpinie — fizycznie niepraktyczne do podpięcia, NIE używać.
> GPIO1,2,3,4: LOW glitch ~60µs przy power-on (ESP32-S3 DS Table 2-2). Akceptowalne.
> GPIO9: brak boot glitch → idealny dla ISR energy pulse.
> GPIO33-37: octal PSRAM (Pico) — nie używać. GPIO38: safety cutoff (opis płytki) — zarezerwowane.
> GPIO39-42: JTAG — unikać w produkcji.
> ⚠️ Iduino ST1168: zweryfikować napięcie bramki MOSFET (3.3V z S3 wystarczy dla logic-level).

---

## 2. Pinout — Waveshare ESP32-S3-Pico

Pełny diagram fizyczny płytki: `docs/ESP32-S3-Pico pinout.md`. Poniżej tylko
piny faktycznie użyte (źródło: `src/hardware/hardware_pins.h`).

### Tabela przypisań

| GPIO    | Funkcja              | Kierunek | Uwagi                                        |
|---------|----------------------|----------|----------------------------------------------|
| GPIO1   | HEATER_SSR_PIN       | OUT      | Active LOW; LOW glitch 60µs przy boot (DS §2.2)|
| GPIO2   | FAN_PWM_PIN          | OUT      | LEDC PWM 25kHz; LOW glitch 60µs (fan OFF)    |
| GPIO4   | BUZZER_PIN           | OUT      | HIGH = ON; LOW glitch = cisza przy boot      |
| GPIO5   | I2C0 SDA (Wire)      | I/O      | DS3231 RTC (0x68), 400kHz                   |
| GPIO6   | I2C0 SCL (Wire)      | OUT      | I2C0 clock                                  |
| GPIO7   | STS35 SDA (Wire1)    | I/O      | I2C1, 100kHz, kabel ~1m                     |
| GPIO8   | STS35 SCL (Wire1)    | OUT      | I2C1 clock                                  |
| GPIO9   | ENERGY_PULSE_PIN     | IN       | INPUT_PULLUP, ISR FALLING; brak boot glitch  |
| GPIO10  | FRAM_SPI_CS_PIN      | OUT      | SPI2/FSPI, domyślny IO_MUX (bez GPIO matrix) |
| GPIO11  | FRAM_SPI_MOSI_PIN    | OUT      | SPI2/FSPI                                    |
| GPIO12  | FRAM_SPI_SCK_PIN     | OUT      | SPI2/FSPI                                    |
| GPIO13  | FRAM_SPI_MISO_PIN    | IN       | SPI2/FSPI                                    |
| GPIO14  | RESET_PIN            | IN       | INPUT_PULLUP; GPIO3 pominięty (tylko TP)     |

GPIO15-18 — wolne (zarezerwowane na przyszłość).
GPIO0, GPIO3, GPIO21, GPIO33-42 — nieużywane celowo (strapping/TP/PSRAM/safety cutoff/JTAG, patrz sekcja 1).

### Schemat I2C bus

```
I2C0 (Wire, 400kHz):
GPIO5 (SDA) ────── DS3231 0x68 (RTC) ── kable <5cm
GPIO6 (SCL) ──────

I2C1 (Wire1, 100kHz):
GPIO7 (SDA) ────── STS35 (0x4B) ── kabel ~1m ── akwarium
GPIO8 (SCL) ────── STS35
Pull-up: 4.7kΩ do 3V3 (przy module STS35)
STS35 ADDR pin → VDD = 0x4B; → GND = 0x4A (dla drugiego czujnika v2)
```

### Schemat SPI FRAM

```
MB85RS64V (8KB, SPI)          ESP32-S3-Pico
CS   ────────────────────────── GPIO10
MOSI ────────────────────────── GPIO11
SCK  ────────────────────────── GPIO12
MISO ────────────────────────── GPIO13
```

### Schemat Energy Pulse (izolacja galwaniczna)

```
Miernik energii         ESP32-S3-Pico
(5–24V open-coll.) ─── PC817 ──── GPIO9 (INPUT_PULLUP)
                    optocoupler
```

---

## 3. Algorytm sterowania

Szczegóły: `docs/ALGORITHM_DECISION.md`

### Grzałka: ON/OFF histereza via SSR

```
SSR ON  gdy: temp < T_set − heat_hyst
SSR OFF gdy: temp > T_set − heat_hyst + hysteresis_band
```

### Wentylatory: proporcjonalny PWM (krzywa liniowa)

```cpp
float fan_pct = clamp(
    (temp − cool_start) / (cool_full − cool_start) * 100.0f,
    0.0f, 100.0f
);
ledcWrite(FAN_PWM_CHANNEL, (uint32_t)(fan_pct * 2.55f));
```

### Alarm trendu (wczesne wykrycie awarii grzałki)

RAM: cykliczny bufor 20 próbek × 15s = 5 min. Wyzwolenie gdy:
- SSR jest ON
- temp spadła o > `trend_alarm_delta` w ciągu 5 min

### Diagram stref temperatur

```
                   23.0    24.7   25.0   25.3  26.0  26.5 [°C]
                    │       │      │      │     │     │
ALARM_LOW          ◄┤       │      │      │     │     │
SSR ON (HEAT)       ├──────►│      │      │     │     │
IDLE                         ├─────►      │     │     │
FAN rośnie (0→100%)                 ├────────────►    │
ALARM_HIGH                                            ├►
                         T−hyst  T_set  T+c_start T+c_full T+alarm_high
```

### Stany automatu

```
STATE_IDLE          — komfort, SSR OFF, fan 0%
STATE_HEATING       — SSR ON, fan 0%
STATE_COOLING       — SSR OFF, fan PWM proporcjonalny
STATE_ALARM_LOW     — temp < T−alarm_low  → buzzer ALARM + SSR ON
STATE_ALARM_HIGH    — temp > T+alarm_high → buzzer ALARM + fan 100%
STATE_SENSOR_FAULT  — 3× błąd CRC STS35  → SSR OFF + fan 0% + buzzer (fail-safe)
STATE_TREND_ALARM   — temp spada szybko przy SSR ON → buzzer WARNING
```

> Uwaga: powyższe stany sterują aktuatorami i zostają bez zmian. Do logowania
> (ring buffer FRAM) ALARM_LOW/ALARM_HIGH są dodatkowo rozbite na 3 stopnie
> nasilenia (bo 1°C i 3°C przekroczenia to dla użytkownika zupełnie różna
> sytuacja) — patrz `LogZone` w sekcji 5, osobny enum tylko do logowania.

---

## 4. Miernik energii — wejście impulsów

```cpp
volatile uint32_t g_pulse_count = 0;

void IRAM_ATTR onEnergyPulse() {  // ISR — musi być w RAM
    g_pulse_count++;
}

// setup():
attachInterrupt(digitalPinToInterrupt(ENERGY_PULSE_PIN),
                onEnergyPulse, FALLING);

// Przeliczenie:
float energy_kwh = (float)g_pulse_count / config.pulses_per_kwh;
```

Zapis do FRAM: co 10 impulsów (≈ 10 Wh przy 1000 imp/kWh).

---

## 5. Struktury danych C

```c
// algorithm_config.h

struct ThermoConfig {               // 40B + 2B checksum
    float    target_temp;           // temp zadana [°C]              default 25.0
    float    heat_hyst;             // SSR ON poniżej T−X            default 0.3
    float    cool_start;            // fan startuje powyżej T+X      default 0.3
    float    cool_full;             // fan 100% powyżej T+X          default 1.0
    float    hysteresis_band;       // OFF offset (anty-migot)       default 0.2
    float    alarm_delta_low;       // ALARM poniżej T−X             default 2.0
    float    alarm_delta_high;      // ALARM powyżej T+X             default 1.5
    float    trend_alarm_delta;     // spadek temp [°C/5min] → alarm  default 0.5
    float    temp_offset;           // kalibracja STS35 [°C]         default 0.0
    float    heater_watt;           // moc grzałek [W], 10–1000      default 0.0
    uint16_t pulses_per_kwh;        // miernik energii [imp/kWh]     default 1000
    uint8_t  is_configured;         // 0xA9 = ważna konfiguracja
    uint8_t  _pad;
};
static_assert(sizeof(ThermoConfig) == 40);

struct TempRecord {                 // 8B — ring buffer entry, EVENT-DRIVEN
                                     // (zmiana strefy / heartbeat / boot — NIE periodic co 30 min)
    uint32_t timestamp;             // Unix UTC zdarzenia
    int16_t  temp_x10;              // temp × 10 w chwili zdarzenia (250 = 25.0°C)
    uint8_t  zone_reason;           // [3:0] LogZone, [5:4] LogReason, [6] UNSTABLE, [7] reserved
    uint8_t  actuator_snapshot;     // [0] heater on/off, [7:1] fan_pct (0–100) w chwili zdarzenia
};
static_assert(sizeof(TempRecord) == 8);

enum LogZone : uint8_t {            // TYLKO do logowania — nie steruje aktuatorami (to robi AlgorithmState)
    ZONE_IDLE          = 0,
    ZONE_HEATING       = 1,
    ZONE_COOLING       = 2,
    ZONE_ALARM_LOW_1   = 3,         // najlżejsze przekroczenie w dół
    ZONE_ALARM_LOW_2   = 4,
    ZONE_ALARM_LOW_3   = 5,         // najcięższe
    ZONE_ALARM_HIGH_1  = 6,
    ZONE_ALARM_HIGH_2  = 7,
    ZONE_ALARM_HIGH_3  = 8,
    ZONE_TREND_ALARM   = 9,
    ZONE_SENSOR_FAULT  = 10,
};
// Progi ustalone (2026-07-08), konfigurowalne z GUI jako różnice od target_temp
// (struct LogZoneConfig, 28B, FRAM 0x052E). Domyślne wartości — patrz
// DEFAULT_LOG_ZONE_* w algorithm_config.h:
//   normal_delta = 0.5°C (typowy swing regulacji ~0.4°C w normalnych warunkach
//                  → 0.1°C marginesu, żeby granica nie triggerowała się na
//                  każdym cyklu SSR ON/OFF — margines świadomie ciasny, do
//                  monitorowania empirycznie po uruchomieniu)
//   low_step_1/2/3  = 1.0 / 2.0 / 3.5°C poniżej target_temp
//   high_step_1/2/3 = 1.0 / 2.0 / 3.5°C powyżej target_temp (osobna konfiguracja)
// Walidacja przy zapisie z GUI: step_1 < step_2, step_1 > normal_delta.
// (2026-07-13: step_3 usunięty — stopień 3 to zawsze "powyżej step_2",
// nieograniczony od góry; step_3 nigdy nie był sprawdzany jako granica.)

enum LogReason : uint8_t {
    REASON_ZONE_CHANGE = 0,         // przejście między strefami (z histerezą)
    REASON_HEARTBEAT   = 1,         // wymuszony wpis mimo braku zmiany strefy
    REASON_BOOT        = 2,         // znacznik restartu urządzenia
};

struct LogZoneConfig {              // 20B + 2B checksum, FRAM 0x052E — progi z GUI
    float normal_delta;   // domyślnie 0.5°C
    float low_step_1;     // domyślnie 1.0°C
    float low_step_2;     // domyślnie 2.0°C — stopień 3 = powyżej tego, nieograniczony
    float high_step_1;    // domyślnie 1.0°C
    float high_step_2;    // domyślnie 2.0°C — stopień 3 = powyżej tego, nieograniczony
};
static_assert(sizeof(LogZoneConfig) == 20);

// Zasady zapisu (ustalone, implementacja w Etapie 4):
//   - odstęp minimalny między zdarzeniami: 30 min = LOG_MIN_EVENT_INTERVAL_S
//     (bezwładność cieplna 240L wody fizycznie wyklucza szybszą realną zmianę)
//   - heartbeat: 24h = LOG_HEARTBEAT_INTERVAL_S — TYLKO #define, nie z GUI
//   - flaga UNSTABLE: ta sama granica przekroczona ≥3× (LOG_FLAPPING_COUNT)
//     w oknie 6h (LOG_FLAPPING_WINDOW_S) → nie tłumić, tylko oznaczyć jako
//     sygnał diagnostyczny (kandydat też na buzzer WARNING)
//   - rekonstrukcja przy odczycie/eksporcie: fill-forward (zero-order hold) —
//     liczone przy odczycie, NIE zapisywane jako syntetyczne rekordy w FRAM

struct EnergyStore {                // 16B (osobny adres, częste zapisy)
    uint32_t total_pulses;          // narastający licznik
    uint32_t day_start_pulses;      // wartość na początku bieżącej doby
    uint16_t pulses_per_kwh;        // kopia z ThermoConfig
    uint16_t checksum;
};
static_assert(sizeof(EnergyStore) == 16);

enum AlgorithmState : uint8_t {
    STATE_IDLE         = 0,
    STATE_HEATING      = 1,
    STATE_COOLING      = 2,
    STATE_ALARM_LOW    = 3,
    STATE_ALARM_HIGH   = 4,
    STATE_SENSOR_FAULT = 5,
    STATE_TREND_ALARM  = 6,
};

#define ALARM_FLAG_LOW    0x01
#define ALARM_FLAG_HIGH   0x02
#define ALARM_FLAG_TREND  0x04
#define ALARM_FLAG_SENSOR 0x08
```

---

## 6. Layout FRAM 8KB (MB85RS64V, SPI, 0x0000–0x1FFF)

```
Adres       Rozmiar    Zawartość
────────────────────────────────────────────────────────────────
0x0000       4B        Magic "THMO" (0x544D4F48)
0x0004       2B        Data version (0x0001)
0x0006      18B        Reserved
0x0018    1024B        Encrypted credentials   [koniec: 0x0417]
0x0418     232B        Gap (reserved)          [koniec: 0x04FF]
0x0500      44B        ThermoConfig (realnie 44B — 10 float+u16+2×u8, nie 40B)
                                               [koniec: 0x052B]
0x052C       2B        ThermoConfig checksum   [koniec: 0x052D]
0x052E      28B        LogZoneConfig (progi LogZone z GUI) [koniec: 0x0549]
0x054A       2B        LogZoneConfig checksum  [koniec: 0x054B]
0x054C      12B        EnergyStore (checksum wbudowany w struct, realnie 12B)
                                               [koniec: 0x0557]
0x0558       2B        Ring buffer count
0x055A       2B        Ring buffer wptr
0x055C       2B        Ring buffer checksum    [koniec: 0x055D]
0x055E     162B        Gap (reserved)          [koniec: 0x05FF]
0x0600    6400B        Ring buffer — 800 × 8B, EVENT-DRIVEN
                        (zmiana strefy/heartbeat/boot, NIE periodic co 30 min)
                                               [koniec: 0x1EFF]
0x1F00     256B        WOLNE (rezerwa)         [koniec: 0x1FFF]
────────────────────────────────────────────────────────────────
Użyte: 7 936B / 8 192B  (96,9%)
Rezerwa: 256B

Math: 800 rek. × 8B = 6 400B  ✓

Pokrycie czasowe ZMIENNE (zależne od częstości zdarzeń — to jest właśnie cel
event-driven logging, nie stały interwał jak przy periodic):
  - stabilny system (sam heartbeat 1×/dobę):                    ~800 dni
  - typowy (heartbeat + kilka zmian strefy/dobę):                ~150–300 dni
  - najgorszy przypadek (ciągłe flapowanie co 30 min = minimalny
    odstęp między zdarzeniami):                          800/48 ≈ 16,6 doby

Trade-off świadomie zaakceptowany: poprzednia wersja (32KB, periodic, 3600×8B)
dawała deterministyczne 75 dni zawsze. Ta wersja: mniejszy/tańszy chip (8KB),
dużo dłuższe realne pokrycie w typowym przypadku, gorszy worst-case niż
poprzednio. Szczegóły decyzji: pamięć projektu `fram_event_logging`.
```

**Jeden bufor, jeden interwał:**
- Pomiar STS35: co **15s** (`TEMP_MEASURE_INTERVAL_S`)
- Akumulacja w RAM przez **30 minut** (`WINDOW_DURATION_S = 1800`): avg(temp), sum(heater_on), OR(alarm_flags), dominant state
- Zapis do FRAM: **1 rekord co 30 minut** (brak downsamplingu, brak drugiego bufora)

**Zawartość rekordu 8B:**
- `timestamp` (4B): Unix UTC koniec okna
- `temp_x10` (2B): średnia z ~120 próbek — rozdzielczość 0.1°C, zakres ±3276.7°C
- `heater_pct` (1B): (próbki SSR ON / 120) × 100 — przybliżony duty cycle grzałki
- `flags` (1B): bity [3:0] = alarmy aktywne w oknie; bity [6:4] = dominujący stan automatu

---

## 7. API endpoints

> Uwaga (2026-07-14): zaktualizowane po redesignie logowania — `/api/history`
> i `/api/log-zone-config` (opisane niżej w tym dokumencie jako "Nowe") już
> NIE ISTNIEJĄ, zastąpione trzema prostymi buforami. Patrz pamięć projektu
> `hourly_daily_alarm_ring_redesign_2026-07-14`.

Bez zmian (z ATO): `POST /api/login`, `POST /api/logout`, `GET /api/health`

Aktualne:
```
GET  /api/status             — temp_c, state, fan_pct, heater_on, power_w,
                               alarm_flags, energy_kwh_today, energy_kwh_total
GET  /api/history-hourly     — bufor godzinowy, rolling 24h (newest-first)
GET  /api/history-daily      — bufor dobowy, ~rok (newest-first)
GET  /api/alarm-events       — epizody alarmowe start/end, max 100 zwracanych
                               (newest-first), bufor mieści 300
GET  /api/thermo-config
POST /api/thermo-config      — wszystkie pola ThermoConfig (form-encoded);
                               heat_hyst/cool_start >= 0.1°C (walidacja serwera)
POST /api/mute-alarm         — wycisza buzzer dla alarmów aktywnych w tej chwili
                               (per-epizod — kolejne wystąpienie znów zadźwięczy)
POST /api/system-toggle      — Auto Mode / Service Mode (enable/disable)
GET  /api/energy             — total_kwh, today_kwh, pulse_count, power_w_now
POST /api/energy-reset-day   — reset licznika dobowego
POST /api/test/fan           — pct=0-100, ręczne sterowanie (bypass algorytmu)
POST /api/test/heater        — on=0|1, ręczne sterowanie (bypass algorytmu)
POST /api/test/seed-history  — wypełnia hourly/daily/alarm losowymi danymi demo
POST /api/test/clear-history — czyści hourly/daily/alarm (count/wptr=0)
```

Wszystkie powyższe (poza `/api/health`) wymagają sesji — najpierw zaloguj się
i zapisz cookie, potem wywołuj z tym cookie:
```bash
curl -c cookies.txt -d "password=TWOJE_HASLO" http://<ip>/api/login

# do pracy nad GUI, zanim urządzenie zbierze prawdziwą historię:
curl -b cookies.txt -X POST http://<ip>/api/test/seed-history
curl -b cookies.txt -X POST http://<ip>/api/test/clear-history
```

---

## 8. Mapa modułów

### Przenoszone bez zmian
```
src/provisioning/              zmienić SSID: "ESP32-THERMO-SETUP"
src/security/                  auth, session, rate_limiter
src/network/wifi_manager.*
src/crypto/                    AES-256
src/core/logging.*
src/hardware/buzzer_controller.*
src/hardware/rtc_controller.*
```

### Częściowo reużywalne z ATO (adapter + nowy layout)
```
src/hardware/fram_controller.*  SPI (nie I2C) — przepisać driver + layout 8KB, zachować wzorzec kodu
src/hardware/fram_constants.h   ✓ zaktualizowane — 8KB SPI, ring buffer 800×8B
```

### Przepisać od zera
```
src/hardware/hardware_pins.h    ✓ zaktualizowane — Waveshare Pico + SPI FRAM
src/hardware/temp_sensor.*      STS35 I2C single-shot, CRC8, trend bufor RAM
src/hardware/heater_controller.* SSR ON/OFF
src/hardware/fan_controller.*   LEDC PWM
src/hardware/energy_meter.*     pulse ISR + EnergyStore
src/algorithm/temp_algorithm.*  histereza + fan curve + trend alarm
src/algorithm/algorithm_config.h
src/config/config.*
src/web/web_handlers.*
src/web/html_pages.*            Chart.js: temp + heater_pct + fan_pct + energia
src/main.cpp
```

### Usunąć
```
src/hardware/water_sensors.*
src/hardware/pump_controller.*
src/hardware/reserve_controller.*
src/algorithm/water_algorithm.*
```

---

## 9. Zależności PlatformIO

```ini
[env:seeed_xiao_esp32s3]
platform  = espressif32
board     = seeed_xiao_esp32s3
framework = arduino
; UWAGA: fizyczna płytka to Waveshare ESP32-S3-Pico, nie XIAO — board id w
; PlatformIO opisuje chip/flash/toolchain (ten sam ESP32-S3), nie pinout.
; Ten sam wzorzec (board != faktyczna płytka) stosowany w Dozowniku/ATO.
; Pinout wyłącznie w src/hardware/hardware_pins.h.

board_build.partitions = huge_app.csv
board_build.flash_mode = dio
board_build.f_flash    = 80000000L
monitor_speed          = 115200
upload_speed           = 460800

build_flags =
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DCORE_DEBUG_LEVEL=3
    -Isrc/config -Isrc/hardware -Isrc/algorithm
    -Isrc/web -Isrc/core -Isrc/network
    -Isrc/security -Isrc/crypto -Isrc/provisioning

lib_deps =
    Wire
    mathieucarbou/AsyncTCP@^3.2.14
    mathieucarbou/ESPAsyncWebServer@^3.3.23
    bblanchon/ArduinoJson@^7.4.2
    adafruit/RTClib@^2.1.1
    adafruit/Adafruit FRAM SPI       ; SPI, nie I2C — MB85RS64V 8KB
    ; SPI: wbudowana biblioteka Arduino core (SPI.h), bez wpisu w lib_deps
    ; STS35: raw Wire (Sensirion I2C protocol) — bez dodatkowej biblioteki
```

---

## 10. Zamknięte decyzje

| Temat                     | Decyzja                                                    |
|---------------------------|------------------------------------------------------------|
| MCU                       | Waveshare ESP32-S3-Pico (dual-core 240MHz, więcej GPIO niż XIAO — potrzebne pod SPI) |
| FRAM                      | MB85RS64V, **SPI**, 8KB — event-driven logging potrzebuje mało miejsca |
| SPI (FSPI/SPI2)           | FRAM: CS=GPIO10, MOSI=GPIO11, SCK=GPIO12, MISO=GPIO13 (domyślny IO_MUX) |
| I2C0 (Wire, 400kHz)       | DS3231 (0x68), GPIO5/6, kable <5cm — FRAM już nie na I2C  |
| I2C1 (Wire1, 100kHz)      | STS35 (0x4B), GPIO7/8, kabel ~1m                         |
| GPIO dual-use             | USUNIĘTY — dedykowany pin dla każdej funkcji               |
| Grzałka aktuator          | SSR active LOW                                             |
| Wentylator aktuator       | Iduino ST1168, LEDC PWM 25kHz                              |
| Algorytm grzania          | ON/OFF histereza via SSR                                   |
| Algorytm chłodzenia       | proporcjonalny PWM, liniowa krzywa                         |
| Alarm trendu              | RAM bufor 20×15s, brak zapisu do FRAM                      |
| Czujnik temp              | 1× STS35, Wire1 (I2C1) 0x4B, co 15s, ±0.1°C, kabel ~1m |
| Termostat grzałki         | przestawić na 28–30°C                                      |
| Pomiar mocy grzałki       | obliczany: heater_watt × heater_pct                        |
| Miernik energii           | pulse input GPIO9, ISR FALLING, optocoupler PC817          |
| Ring buffer               | 800×8B, EVENT-DRIVEN (zmiana LogZone/heartbeat/boot), nie periodic |
| Historia                  | zmienna: ~800 dni stabilnie / ~16,6 doby w najgorszym razie (flapowanie) |
| PID                       | odrzucony                                                  |
| SPI (FRAM)                | przyjęty — 8KB event-driven logging, wymaga płytki z większą liczbą GPIO |

---

## 11. Plan implementacji

```
Etap 1 — Infrastruktura hardware
  1a. platformio.ini (board, lib deps)
  1b. hardware_pins.h (Waveshare ESP32-S3-Pico, SPI FRAM) — ✓ zrobione
  1c. fram_controller (SPI, layout 8KB, ring buffer event-driven, EnergyStore)
  1d. temp_sensor (STS35 I2C single-shot, 15s, CRC8, trend bufor RAM 20 próbek)

Etap 2 — Aktuatory
  2a. heater_controller (SSR ON/OFF)
  2b. fan_controller (LEDC PWM, kanał 0)
  2c. energy_meter (ISR, EnergyStore FRAM, zapis co 10 impulsów)

Etap 3 — Algorytm
  3a. algorithm_config.h (struktury, stałe, enum AlgorithmState)
  3b. temp_algorithm (histereza + fan curve + trend alarm + sensor fault)

Etap 4 — Persistence
  4a. ThermoConfig FRAM R/W (checksum, defaults przy pierwszym uruchomieniu)
  4b. Buffer A: zapis (deadband) + ring pointer
  4c. Ring buffer: zapis co 30 min, akumulacja w RAM
  4e. EnergyStore FRAM R/W

Etap 5 — Web
  5a. web_handlers (wszystkie endpointy)
  5b. html_pages (dashboard + Chart.js multi-series)

Etap 6 — Integracja
  6a. main.cpp (setup/loop, daily restart o północy)
  6b. Build + flash + testy serial
  6c. Testy web end-to-end
```
