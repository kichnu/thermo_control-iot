# Decyzja algorytmu sterowania

> Status: ZAMKNIĘTA. Decyzja: ON/OFF histereza via SSR.

---

## 1. Dlaczego ON/OFF wystarcza — first principles

**Obiekt regulacji:** 240L wody, masa cieplna ~240 kg × 4186 J/(kg·K) ≈ **1 MJ/°C**

Grzałka 300W potrzebuje ~56 minut żeby podnieść temp o 1°C (bez strat).
Stała czasowa układu: rzędu **godzin**.

Przy tak wolnym obiekcie cykl graniczny bang-bang ma amplitudę:

```
ΔT_cykl ≈ histereza + opóźnienie_transportowe(sonda↔grzałka)
```

W praktyce przy histereze 0.2°C i dobrej lokalizacji sondy (w przepływie, nie
w tubie grzałki) → wahania **grubo poniżej 0.5°C**.

Empiria (Caselarsen test): samo setpoint ON/OFF dawało ~0.3°F (0.17°C)
w oknie 15 minut. Apex vs Inkbird: różnica wynika z jakości sondy
i ciaśniejszej logiki, NIE z PID.

### Co realnie poprawia stabilność

1. Sonda w przepływie wody (nie we wnętrzu tuby grzałki)
2. Wąska skalibrowana histereza (~0.2°C)
3. Brak opóźnień odczytu (STS35 single-shot I2C ~15ms)

Wniosek: PID nad ON/OFF w 240L = kosmetyka. Nie warto.

---

## 2. Dlaczego jednak SSR zamiast relay mechanicznego

| Cecha              | Relay mech.         | SSR                       |
|--------------------|---------------------|---------------------------|
| Żywotność          | ~100k cykli         | praktycznie nieograniczona |
| Cichy              | ❌ klika            | ✅                        |
| PWM / time-prop.   | ❌                  | ✅ (przyszłościowo)       |
| Pomiar duty cycle  | ❌ bez sensu        | ✅ sygnał diagnostyczny   |
| Cena               | tańszy              | ~15-25 PLN                |

SSR jest właściwym wyborem **nie dla precyzji sterowania** (tu nie ma różnicy),
ale dla:
- braku zużycia mechaniki
- ciągłego sygnału `heater_on_ratio` jako zmiennej diagnostycznej
- opcji przejścia na time-proportioning PI w przyszłości (firmware only)

---

## 3. Pomiar mocy i zużycia energii

### Podejście A — obliczone (bez czujnika prądu)

User wpisuje moc znamionową grzałek w web UI → zapisana w `ThermoConfig`.

```
power_now_W = heater_on ? heater_watt : 0
energy_wh  += power_now_W * dt_s / 3600
```

Ring buffer przechowuje `heater_state` przy każdym rekordzie → można obliczyć
`on_ratio` za dowolny przedział czasu.

Sygnał diagnostyczny:
- `on_ratio` rośnie z dnia na dzień → temperatura pokoju spada (zima)
- `on_ratio` skacznie wzrasta → grzałka słabnie / awaria
- `on_ratio` = 0 przy chłodnym pokoju → czujnik rozłączony lub grzałka zgasła

### Podejście B — fizyczny czujnik prądu AC (opcjonalne rozszerzenie)

Możliwości:
- **SCT-013** (clamp-on transformer) — brak galwanicznego połączenia z siecią,
  najBEZPIECZNIEJSZE dla DIY, wyjście AC na ADC przez burden resistor
- **ACS712** (5A) — w linii AC, 185mV/A, ADC ESP32-C3, koszt ~5 PLN
- **PZEM-004T** — gotowy miernik V/A/W/kWh, UART, ale zajmuje port szeregowy

**Rekomendacja dla tego projektu:**
Zaczynamy od Podejścia A (zero dodatkowego hardware). Rezerwujemy pin ADC (D0/GPIO2)
pod czujnik prądu jako opcjonalne rozszerzenie.

---

## 4. ESP32 + PID — biblioteki Arduino

Pytanie: czy jest wsparcie?

Tak: `br3ttb/Arduino-PID-Library` działa na ESP32 (też na C3).
Ale nie potrzebujemy jej dla tego projektu — ON/OFF histereza wystarczy.

Gdybyśmy kiedyś chcieli PI z time-proportioning:
```c
// Wystarczy ~20 linii kodu bez zewnętrznej biblioteki:
// error = setpoint - temp
// integral += error * dt
// integral = clamp(integral, -max_i, max_i)  // anti-windup
// output = Kp * error + Ki * integral         // bez Kd (szum sondy)
// duty_cycle = clamp(output, 0.0, 1.0)
// SSR ON przez duty_cycle * period_ms milisekund
```

D-term pominąć — w wolnym, zaszumionym pomiarze temperatury wody
amplifikuje szum bardziej niż wnosi korektę.

---

## 5. Finalna decyzja algorytmu

```
Grzałka:     ON/OFF histereza VIA SSR (nie relay mech.)
             histereza: ±0.2°C (regulowana w konfiguracji)
             SSR: Fotek SSR-25DA lub odpowiednik (~15-25 PLN)

Wentylator:  ON/OFF histereza VIA relay mech. (OK, wentylator nie cykli tak często)

Pomiar mocy: obliczany z heater_watt × on_ratio (bez czujnika)
             opcja: ADC pin rezerwowany pod SCT-013 / ACS712 w przyszłości

Upgrade path: ON/OFF → PI time-proportioning (firmware only, hardware SSR bez zmian)
```

---

## 6. Stany automatu (finalne)

```
STATE_IDLE          — temp w strefie komfortu [T_set-heat_hyst, T_set+cool_hyst]
STATE_HEATING       — SSR ON: temp < T_set - heat_hyst
STATE_COOLING       — fan ON: temp > T_set + cool_hyst
STATE_ALARM_LOW     — temp < T_set - alarm_low  → buzzer + SSR ON (100%)
STATE_ALARM_HIGH    — temp > T_set + alarm_high → buzzer + fan ON
STATE_SENSOR_FAULT  — 3× błąd CRC STS35 → SSR OFF + fan OFF + alarm (fail-safe)
```

Przejścia z histerezą (zapobiegają migotaniu):
```
IDLE → HEATING:  temp spadła poniżej (T_set - heat_hyst)
HEATING → IDLE:  temp wzrosła powyżej (T_set - heat_hyst + hysteresis_band)

IDLE → COOLING:  temp wzrosła powyżej (T_set + cool_hyst)
COOLING → IDLE:  temp spadła poniżej (T_set + cool_hyst - hysteresis_band)
```

---

## 7. ThermoConfig — finalne pola

```c
struct ThermoConfig {           // FRAM-persisted
    float   target_temp;        // temp zadana [°C],           default: 25.0
    float   heat_hyst;          // ON poniżej T - heat_hyst,   default: 0.3
    float   cool_hyst;          // ON powyżej T + cool_hyst,   default: 0.5
    float   hysteresis_band;    // OFF offset (zapobiega migot),default: 0.2
    float   alarm_delta_low;    // ALARM poniżej T - X,        default: 2.0
    float   alarm_delta_high;   // ALARM powyżej T + X,        default: 1.5
    float   temp_offset;        // kalibracja STS35 [°C],      default: 0.0
    float   heater_watt;        // moc znamionowa grzałek [W], default: 0.0
    uint8_t is_configured;      // 0xA9 = ważna konfiguracja
    uint8_t _pad[3];
};  // 32B + 4B pad = 36B → +2B checksum = 38B w FRAM
```

---

## 8. TempRecord — pola w ring buffer

```c
struct TempRecord {             // 12B — FRAM ring buffer
    uint32_t timestamp;         // Unix UTC
    int16_t  temp_x10;          // temp × 10 (np. 250 = 25.0°C)
    uint8_t  state;             // AlgorithmState (IDLE/HEATING/COOLING/ALARM/FAULT)
    uint8_t  alarm_flags;       // bitmaska alarmów
    uint16_t heater_on_s;       // czas SSR ON w tej minucie [s] (0–60)
    uint16_t _pad;
};  // 12B
```

`heater_on_s` per rekord → backend może liczyć kWh za dowolny okres.
