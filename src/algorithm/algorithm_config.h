#ifndef ALGORITHM_CONFIG_H
#define ALGORITHM_CONFIG_H

#include <Arduino.h>

// ============================================================
// POMIAR TEMPERATURY
// ============================================================
#define TEMP_MEASURE_INTERVAL_S     15      // co ile sekund odczyt STS35
#define TEMP_SAMPLES_PER_WINDOW     120     // próbek na okno 30 min (1800s / 15s)
#define TREND_ALARM_SAMPLES         20      // próbek w RAM dla detekcji trendu

// ============================================================
// ZAPIS DO FRAM
// ============================================================
#define WINDOW_DURATION_S           1800    // 30 minut — jeden rekord FRAM
#define WINDOW_DURATION_MS          (WINDOW_DURATION_S * 1000UL)

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
// STANY AUTOMATU
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
// BITMASKA ALARMÓW (alarm_flags w TempRecord)
// ============================================================
#define ALARM_FLAG_TEMP_LOW     0x01
#define ALARM_FLAG_TEMP_HIGH    0x02
#define ALARM_FLAG_TREND        0x04
#define ALARM_FLAG_SENSOR       0x08

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
static_assert(sizeof(ThermoConfig) == 40, "ThermoConfig must be 40 bytes");

// ============================================================
// REKORD RING BUFFER (persystowany w FRAM co 30 min)
// ============================================================
struct TempRecord {
    uint32_t timestamp;         // Unix UTC — koniec okna 30-minutowego
    int16_t  temp_x10;          // średnia temp × 10 (250 = 25.0°C)
    uint8_t  state;             // AlgorithmState dominujący w oknie
    uint8_t  alarm_flags;       // OR flag alarmów z całego okna
    uint8_t  heater_pct;        // % próbek ze SSR ON (0–100)
    uint8_t  fan_pct;           // średnie wypełnienie PWM (0–100)
    uint16_t _pad;
};
static_assert(sizeof(TempRecord) == 12, "TempRecord must be 12 bytes");

// ============================================================
// ENERGIA (persystowana osobno w FRAM)
// ============================================================
struct EnergyStore {
    uint32_t total_pulses;      // narastający licznik impulsów
    uint32_t day_start_pulses;  // wartość na początku bieżącej doby
    uint16_t pulses_per_kwh;    // kopia z ThermoConfig
    uint16_t checksum;
};
static_assert(sizeof(EnergyStore) == 16, "EnergyStore must be 16 bytes");

// ============================================================
// AKUMULATOR OKNA (w RAM, nie persystowany)
// ============================================================
struct WindowAccumulator {
    int32_t  temp_sum_x10;      // suma temp × 10 z próbek
    uint16_t heater_on_count;   // liczba próbek ze SSR ON
    uint16_t fan_pct_sum;       // suma wypełnienia PWM
    uint8_t  alarm_flags_or;    // OR wszystkich flag alarmów
    uint8_t  sample_count;      // liczba zebranych próbek (max 120)
    uint8_t  dominant_state;    // stan z najdłuższym czasem
    uint8_t  _pad;
    uint32_t window_start_ts;   // timestamp początku okna
};

#endif // ALGORITHM_CONFIG_H
