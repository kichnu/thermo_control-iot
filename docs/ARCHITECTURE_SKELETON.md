# Thermo Control — Architektura (FINAL DRAFT)

> Status: gotowy do zatwierdzenia i startu implementacji.

---

## 1. Stack sprzętowy (potwierdzony)

| Element              | Wartość                                                         |
|----------------------|-----------------------------------------------------------------|
| MCU                  | Seeed XIAO ESP32-C3                                             |
| FRAM                 | MB85RC256V — **I2C, 32KB** (Adafruit #1895 lub bare chip)      |
| Temp. czujnik        | STS35 (Sensirion, Forbot) — I2C, ±0.1°C                        |
| Grzałka aktuator     | SSR (Fotek SSR-25DA lub odpowiednik), active LOW                |
| Wentylatory aktuator | Iduino ST1168 (MOSFET PWM, 5–24V DC)                           |
| Miernik energii      | wejście impulsów GPIO (open-collector, przez optocoupler PC817) |
| RTC                  | DS3231 — I2C                                                    |
| Buzzer               | pasywny, HIGH = ON                                              |
| Przycisk             | provisioning (dedykowany pin — bez dual-use)                    |

**Zmiana decyzji FRAM: SPI 8KB → I2C 32KB**
- FRAM I2C współdzieli magistralę z RTC (SDA/SCL) — odzyskujemy 4 GPIO (SCK, MISO, MOSI, CS)
- 32KB = wystarczy na 30 dni historii w rozdzielczości 1h + 24h w 1-min
- Biblioteka `Adafruit FRAM I2C` — ta sama co w projekcie ATO, driver częściowo reużywalny
- Adres I2C: MB85RC256V = 0x50, DS3231 = 0x68 → brak konfliktu

> ⚠️ Iduino ST1168: zweryfikować napięcie sterowania bramki MOSFET.
> Logic-level (IRLZ44N): 3.3V z ESP32-C3 wystarczy.
> Nielogic-level (IRF520): może wymagać level-shiftera 3.3V→5V na sygnale PWM.

---

## 2. Pinout — XIAO ESP32-C3

### Diagram funkcjonalny

```
                        ┌─────────────────────────┐
                        │      [USB-C]             │
                        │                          │
                GND ────┤ GND              GND ────┤
                 5V ────┤ 5V              3V3 ─────┤──► 3.3V out
                3V3 ────┤ 3V3 (in)        RST ─────┤
          NC (wolny) ───┤ D0 / GPIO 2  GPIO10 / D10├──── NC (wolny)
  SSR grzałka  ◄────────┤ D1 / GPIO 3   GPIO9 / D9 ├──── NC (wolny)
  Fan PWM      ◄────────┤ D2 / GPIO 4   GPIO8 / D8 ├──── NC (wolny)
  Energy pulse ────────►┤ D3 / GPIO 5  GPIO20 / D7 ├◄─── Provisioning btn
  I2C SDA      ◄───────►┤ D4 / GPIO 6  GPIO21 / D6 ├───► Buzzer
  I2C SCL      ◄───────►┤ D5 / GPIO 7              │
                        └─────────────────────────┘
```

### Tabela przypisań

| Pin fizyczny | GPIO  | Funkcja              | Kierunek | Uwagi                                 |
|--------------|-------|----------------------|----------|---------------------------------------|
| D0           | GPIO2 | NC (wolny)           | —        | Zarezerwowany na przyszłość           |
| D1           | GPIO3 | HEATER_SSR_PIN       | OUT      | Active LOW → SSR control input        |
| D2           | GPIO4 | FAN_PWM_PIN          | OUT      | LEDC PWM 25kHz → Iduino ST1168        |
| D3           | GPIO5 | ENERGY_PULSE_PIN     | IN       | INPUT_PULLUP, ISR FALLING, optocoupler|
| D4           | GPIO6 | I2C SDA              | I/O      | DS3231 (0x68) + MB85RC256V (0x50) + STS35 (0x4B)|
| D5           | GPIO7 | I2C SCL              | OUT      | I2C clock                             |
| D6           | GPIO21| BUZZER_PIN           | OUT      | HIGH = ON                             |
| D7           | GPIO20| RESET_PIN            | IN       | INPUT_PULLUP, hold 5s → provisioning  |
| D8           | GPIO8 | NC                   | —        | Wolny, zarezerwowany na przyszłość    |
| D9           | GPIO9 | NC                   | —        | Wolny, zarezerwowany na przyszłość    |
| D10          | GPIO10| NC                   | —        | Wolny, zarezerwowany na przyszłość    |

### Schemat I2C bus

```
GPIO6 (SDA) ─────┬──────────────────┬──────────────┐
                 │                  │              │
              DS3231             MB85RC256V     STS35
              0x68 (RTC)         0x50 (FRAM)   0x4B (temp)
                 │                  │              │
GPIO7 (SCL) ─────┴──────────────────┴──────────────┘

Pull-up: 4.7kΩ do 3V3 (jeden zestaw na magistrali — można z RTC breakout)
STS35 ADDR pin → VDD = adres 0x4B; → GND = adres 0x4A (drugi czujnik w v2)
```

### Schemat Energy Pulse (izolacja galwaniczna)

```
Miernik energii         ESP32-C3
(5–24V open-coll.) ─── PC817 ──── GPIO5 (INPUT_PULLUP)
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

struct TempRecord {                 // 12B — ring buffer entry
    uint32_t timestamp;             // Unix UTC
    int16_t  temp_x10;             // temp × 10  (250 = 25.0°C)
    uint8_t  state;                 // AlgorithmState
    uint8_t  alarm_flags;           // bitmaska aktywnych alarmów
    uint8_t  heater_pct;            // % czasu SSR ON w tym interwale (0–100)
    uint8_t  fan_pct;               // średnie wypełnienie PWM (0–100)
    uint16_t _pad;
};
static_assert(sizeof(TempRecord) == 12);

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

## 6. Layout FRAM 32KB (MB85RC256V, 0x0000–0x7FFF)

```
Adres       Rozmiar    Zawartość
────────────────────────────────────────────────────────────────
0x0000       4B        Magic "THMO"
0x0004       2B        Data version (0x0001)
0x0006      18B        Reserved
0x0018    1024B        Encrypted credentials (bez zmian z ATO)
                       [koniec: 0x0417]
0x0500      42B        ThermoConfig + 2B checksum
0x0530      16B        EnergyStore (z własnym checksum)
0x0540      12B        Ring buffer metadata (count + wptr + checksum)
0x0600   28800B        Ring buffer — 2400 × 12B (30-min records, 50 dni)
                       [koniec: 0x767F]
0x7680    2432B        WOLNE (rezerwa ~7.4%)
────────────────────────────────────────────────────────────────
Użyte: 30336 / 32768 B (92.6%)

Math: 50 dni × 24h × 2 rec/h × 12B = 28 800B ✅
```

**Jeden bufor, jeden interwał:**
- Pomiar STS35: co **15s** (single-shot I2C, konfigurowalny stałą `TEMP_MEASURE_INTERVAL_S`)
- Akumulacja w RAM przez 30 minut: avg(temp), sum(heater_on_samples), avg(fan_pct), OR(alarm_flags)
- Zapis do FRAM: **co 30 minut** (1 rekord per okno)
- Brak downsampli, brak drugiego bufora — prosta, przewidywalna logika

**Zawartość rekordu per okno 30 min:**
- `temp_x10`: średnia temperatura z ~120 próbek (30 min / 15s)
- `heater_pct`: (liczba próbek SSR ON / 120) × 100
- `fan_pct`: średnie wypełnienie PWM
- `alarm_flags`: OR wszystkich flag z okna (1 → alarm był aktywny)
- `state`: dominujący stan (najdłużej aktywny w oknie)

---

## 7. API endpoints

Bez zmian (z ATO): `POST /api/login`, `POST /api/logout`, `GET /api/health`

Nowe:
```
GET  /api/status             — temp_c, state, fan_pct, heater_on, power_w,
                               alarm_flags, energy_kwh_today, energy_kwh_total
GET  /api/history            — ring buffer 2400 rek. (newest-first, 50 dni)
GET  /api/thermo-config
POST /api/thermo-config      — wszystkie pola ThermoConfig (form-encoded)
POST /api/reset-alarm        — manual ack
POST /api/system-toggle      — enable/disable
POST /api/system-reset       — reset automatu stanów
GET  /api/energy             — total_kwh, today_kwh, pulse_count, power_w_now
POST /api/energy-reset-day   — reset licznika dobowego
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
src/hardware/fram_controller.*  I2C wraca — przepisać layout, zachować wzorzec kodu
src/hardware/fram_constants.h   nowe stałe adresów, magic "THMO"
```

### Przepisać od zera
```
src/hardware/hardware_pins.h
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
[env:seeed_xiao_esp32c3]
platform  = espressif32
board     = seeed_xiao_esp32c3
framework = arduino

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
    adafruit/Adafruit FRAM I2C       ; powrót do I2C — ta sama lib co ATO
    ; STS35: raw Wire (Sensirion I2C protocol) — bez dodatkowej biblioteki
```

---

## 10. Zamknięte decyzje

| Temat                     | Decyzja                                                    |
|---------------------------|------------------------------------------------------------|
| FRAM                      | MB85RC256V, I2C, 32KB (Adafruit #1895)                    |
| I2C bus sharing           | RTC (0x68) + FRAM (0x50) na tej samej magistrali          |
| GPIO dual-use             | USUNIĘTY — dedykowany pin dla każdej funkcji               |
| Grzałka aktuator          | SSR active LOW                                             |
| Wentylator aktuator       | Iduino ST1168, LEDC PWM 25kHz                              |
| Algorytm grzania          | ON/OFF histereza via SSR                                   |
| Algorytm chłodzenia       | proporcjonalny PWM, liniowa krzywa                         |
| Alarm trendu              | RAM bufor 20×15s, brak zapisu do FRAM                      |
| Czujnik temp              | 1× STS35 (Forbot/Sensirion), I2C 0x4B, co 15s, ±0.1°C   |
| Termostat grzałki         | przestawić na 28–30°C                                      |
| Pomiar mocy grzałki       | obliczany: heater_watt × heater_pct                        |
| Miernik energii           | pulse input GPIO5, ISR FALLING, optocoupler PC817          |
| Ring buffer               | 2400×12B, interwał 30 min, 50 dni — jeden bufor, brak downsampli |
| Historia                  | 50 dni w rozdzielczości 30-minutowej                       |
| PID                       | odrzucony                                                  |
| SPI (FRAM)                | odrzucony — zbyt wiele GPIO                                |

---

## 11. Plan implementacji

```
Etap 1 — Infrastruktura hardware
  1a. platformio.ini (board, lib deps)
  1b. hardware_pins.h (ESP32-C3, I2C FRAM)
  1c. fram_controller (I2C, layout 32KB, Buffer A/B, EnergyStore)
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
