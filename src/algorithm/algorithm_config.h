#ifndef ALGORITHM_CONFIG_H
#define ALGORITHM_CONFIG_H

#include <Arduino.h>

// ============================================================
// POMIAR TEMPERATURY
// ============================================================
#define TEMP_MEASURE_INTERVAL_S     15      // co ile sekund odczyt STS35
#define TREND_ALARM_SAMPLES         20      // próbek w RAM dla detekcji trendu

// ============================================================
// SENSOR FAULT
// ============================================================
#define SENSOR_FAULT_THRESHOLD      3       // kolejnych błędów CRC → STATE_SENSOR_FAULT

// ============================================================
// DOMYŚLNA KONFIGURACJA
// ============================================================
#define DEFAULT_TARGET_TEMP         25.0f
#define DEFAULT_HEAT_HYST           0.3f    // SSR ON poniżej T - X
#define DEFAULT_COOL_START          0.3f    // fan startuje powyżej T + X
#define DEFAULT_COOL_FULL           1.0f    // fan 100% powyżej T + X
#define DEFAULT_HYSTERESIS_BAND     0.2f    // OFF offset (anty-migotanie)
#define DEFAULT_ALARM_DELTA_LOW     2.0f    // ALARM poniżej T - X
#define DEFAULT_ALARM_DELTA_HIGH    1.5f    // ALARM powyżej T + X
#define DEFAULT_TREND_ALARM_DELTA   0.5f    // spadek [°C/5min] przy SSR ON → alarm
#define DEFAULT_TEMP_OFFSET         0.0f    // kalibracja czujnika
#define DEFAULT_HEATER_WATT         0.0f    // moc grzałek [W] — user wpisuje
#define DEFAULT_PULSES_PER_KWH      1000    // miernik energii

// ============================================================
// LOGOWANIE ZDARZENIOWE — progi LogZone (konfigurowalne z GUI jako różnice
// od target_temp) i stałe niekonfigurowalne (heartbeat, anti-flapping).
// Kontekst doboru (2026-07-08): typowy swing regulacji w normalnych
// warunkach ~0.4°C → NORMAL_DELTA=0.5°C daje 0.1°C marginesu, żeby granica
// logowania nie triggerowała się przy każdym cyklu SSR ON/OFF. Szacowany
// worst-case "jeszcze normalnego" odchylenia ~1°C → STEP_1=1.0°C.
// ============================================================
#define DEFAULT_LOG_ZONE_NORMAL_DELTA   0.5f   // połowa szerokości strefy prawidłowej wokół target_temp
#define DEFAULT_LOG_ZONE_LOW_STEP_1     1.0f   // próg stopnia 1 poniżej target_temp
#define DEFAULT_LOG_ZONE_LOW_STEP_2     2.0f   // próg stopnia 2 poniżej target_temp (stopień 3 = powyżej tego progu, nieograniczony)
#define DEFAULT_LOG_ZONE_HIGH_STEP_1    1.0f   // próg stopnia 1 powyżej target_temp
#define DEFAULT_LOG_ZONE_HIGH_STEP_2    2.0f   // próg stopnia 2 powyżej target_temp (stopień 3 = powyżej tego progu, nieograniczony)

#define LOG_HEARTBEAT_INTERVAL_S    86400UL    // 24h — wymuszony wpis mimo braku zmiany strefy (NIE z GUI)
#define LOG_MIN_EVENT_INTERVAL_S    1800UL     // 30 min — bezwładność 240L wody wyklucza szybszą realną zmianę
#define LOG_FLAPPING_COUNT          3          // ta sama granica przekroczona N× ...
#define LOG_FLAPPING_WINDOW_S       21600UL    // ...w oknie 6h → flaga UNSTABLE (sygnał diagnostyczny, nie tłumione)

// ============================================================
// STANY AUTOMATU (sterowanie aktuatorami — bez zmian)
// ============================================================
enum AlgorithmState : uint8_t {
    STATE_IDLE          = 0,
    STATE_HEATING       = 1,
    STATE_COOLING       = 2,
    STATE_ALARM_LOW     = 3,
    STATE_ALARM_HIGH    = 4,
    STATE_SENSOR_FAULT  = 5,
    STATE_TREND_ALARM   = 6,
};

// ============================================================
// BITMASKA _alarm_flags (warstwa sterowania — TempAlgorithm::getAlarmFlags(),
// niezależna od kodowania ring buffera poniżej)
// ============================================================
#define ALARM_FLAG_TEMP_LOW     0x01  // bit 0
#define ALARM_FLAG_TEMP_HIGH    0x02  // bit 1
#define ALARM_FLAG_TREND        0x04  // bit 2
#define ALARM_FLAG_SENSOR       0x08  // bit 3

// ============================================================
// STREFY LOGOWANIA (LogZone) — TYLKO do ring buffera, nie sterują aktuatorami.
// ALARM_LOW/HIGH rozbite na 3 stopnie względem DEFAULT_LOG_ZONE_*_STEP_*,
// bo przekroczenie o 1°C i o 3.5°C to dla użytkownika zupełnie różna sytuacja.
// ============================================================
enum LogZone : uint8_t {
    ZONE_IDLE          = 0,
    ZONE_HEATING       = 1,
    ZONE_COOLING       = 2,
    ZONE_ALARM_LOW_1   = 3,   // deviation w (normal_delta, low_step_1]
    ZONE_ALARM_LOW_2   = 4,   // deviation w (low_step_1, low_step_2]
    ZONE_ALARM_LOW_3   = 5,   // deviation > low_step_2
    ZONE_ALARM_HIGH_1  = 6,
    ZONE_ALARM_HIGH_2  = 7,
    ZONE_ALARM_HIGH_3  = 8,
    ZONE_TREND_ALARM   = 9,
    ZONE_SENSOR_FAULT  = 10,
};

enum LogReason : uint8_t {
    REASON_ZONE_CHANGE = 0,   // przejście między strefami
    REASON_HEARTBEAT   = 1,   // wymuszony wpis co LOG_HEARTBEAT_INTERVAL_S mimo braku zmiany
    REASON_BOOT        = 2,   // znacznik restartu urządzenia
};

// ============================================================
// KODOWANIE PÓL W TempRecord
// zone_reason:       bity [3:0] LogZone, [5:4] LogReason, [6] UNSTABLE/flapping, [7] reserved
// actuator_snapshot: bit  [0]   heater on/off w chwili zdarzenia
//                    bity [7:1] fan_pct (0–100) w chwili zdarzenia
// ============================================================
#define TREC_ZONE_GET(zr)          (LogZone)((zr) & 0x0Fu)
#define TREC_ZONE_SET(zr, z)       (((zr) & ~0x0Fu) | ((uint8_t)(z) & 0x0Fu))
#define TREC_REASON_GET(zr)        (LogReason)(((zr) >> 4) & 0x03u)
#define TREC_REASON_SET(zr, r)     (((zr) & ~0x30u) | (((uint8_t)(r) & 0x03u) << 4))
#define TREC_UNSTABLE_GET(zr)      (((zr) >> 6) & 0x01u)
#define TREC_UNSTABLE_SET(zr)      ((zr) | 0x40u)

#define TREC_HEATER_ON_GET(as)     ((as) & 0x01u)
#define TREC_HEATER_ON_SET(as, on) (((as) & ~0x01u) | ((on) ? 0x01u : 0x00u))
#define TREC_FAN_PCT_GET(as)       (((as) >> 1) & 0x7Fu)
#define TREC_FAN_PCT_SET(as, pct)  (((as) & 0x01u) | (((uint8_t)(pct) & 0x7Fu) << 1))

// ============================================================
// KONFIGURACJA ALGORYTMU (persystowana w FRAM)
// ============================================================
struct ThermoConfig {
    float    target_temp;       // temperatura zadana [°C]
    float    heat_hyst;         // SSR ON gdy temp < target - heat_hyst
    float    cool_start;        // fan startuje gdy temp > target + cool_start
    float    cool_full;         // fan 100% gdy temp > target + cool_full
    float    hysteresis_band;   // offset wyłączenia (zapobiega migotaniu)
    float    alarm_delta_low;   // ALARM gdy temp < target - X
    float    alarm_delta_high;  // ALARM gdy temp > target + X
    float    trend_alarm_delta; // ALARM gdy spadek > X °C / 5 min
    float    temp_offset;       // kalibracja czujnika STS35 [°C]
    float    heater_watt;       // moc znamionowa grzałek [W]
    uint16_t pulses_per_kwh;    // konfiguracja miernika energii
    uint8_t  is_configured;     // THERMO_CONFIG_MAGIC = ważna konfiguracja
    uint8_t  _pad;
};
static_assert(sizeof(ThermoConfig) == 44, "ThermoConfig must be 44 bytes"); // 10 float + u16 + 2×u8 = 44B (pre-existing assert błędnie mówił 40)

// ============================================================
// PROGI LOGOWANIA ZDARZENIOWEGO (persystowane w FRAM, konfigurowalne z GUI
// jako różnice od target_temp — patrz DEFAULT_LOG_ZONE_* powyżej)
// ============================================================
struct LogZoneConfig {
    float normal_delta;   // połowa szerokości strefy prawidłowej wokół target_temp
    float low_step_1;     // próg stopnia 1 poniżej target_temp
    float low_step_2;     // próg stopnia 2 poniżej target_temp — stopień 3 = powyżej tego, nieograniczony
    float high_step_1;    // próg stopnia 1 powyżej target_temp
    float high_step_2;    // próg stopnia 2 powyżej target_temp — stopień 3 = powyżej tego, nieograniczony
};
static_assert(sizeof(LogZoneConfig) == 20, "LogZoneConfig must be 20 bytes");
// Walidacja przy zapisie z GUI: low_step_1 < low_step_2 (analogicznie high_*),
// i low_step_1/high_step_1 > normal_delta — inaczej strefy się nakładają/odwracają.

// ============================================================
// REKORD RING BUFFER — event-driven (zmiana LogZone / heartbeat / boot,
// NIE periodic co 30 min)
// ============================================================
struct TempRecord {
    uint32_t timestamp;           // 4B — Unix UTC zdarzenia
    int16_t  temp_x10;            // 2B — temp × 10 w chwili zdarzenia (250 = 25.0°C)
    uint8_t  zone_reason;         // 1B — patrz TREC_ZONE_*/TREC_REASON_*/TREC_UNSTABLE_* powyżej
    uint8_t  actuator_snapshot;   // 1B — patrz TREC_HEATER_ON_*/TREC_FAN_PCT_* powyżej
};
static_assert(sizeof(TempRecord) == 8, "TempRecord must be 8 bytes");

// ============================================================
// ENERGIA (persystowana osobno w FRAM)
// ============================================================
struct EnergyStore {
    uint32_t total_pulses;      // narastający licznik impulsów
    uint32_t day_start_pulses;  // wartość na początku bieżącej doby
    uint16_t pulses_per_kwh;    // kopia z ThermoConfig
    uint16_t checksum;
};
static_assert(sizeof(EnergyStore) == 12, "EnergyStore must be 12 bytes"); // 2×u32 + 2×u16 = 12B, checksum wbudowany (pre-existing assert błędnie mówił 16)

#endif // ALGORITHM_CONFIG_H
