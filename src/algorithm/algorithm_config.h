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
#define DEFAULT_HEAT_HYST           0.3f    // SSR ON poniżej T - X, OFF przy T (patrz MIN_HYSTERESIS_OFFSET)
#define DEFAULT_COOL_START          0.3f    // fan ON powyżej T + X, OFF przy T
#define DEFAULT_COOL_FULL           1.0f    // fan 100% powyżej T + X
#define DEFAULT_FAN_MIN_PCT         30      // floor rampy fana (poniżej fan się nie rusza / jest nieefektywny)
#define DEFAULT_ALARM_DELTA_LOW     2.0f    // ALARM poniżej T - X
#define DEFAULT_ALARM_DELTA_HIGH    1.5f    // ALARM powyżej T + X
#define DEFAULT_TREND_ALARM_DELTA   0.5f    // spadek [°C/5min] przy SSR ON → alarm
#define DEFAULT_TEMP_OFFSET         0.0f    // kalibracja czujnika
#define DEFAULT_HEATER_WATT         0.0f    // moc grzałek [W] — user wpisuje
#define DEFAULT_PULSES_PER_KWH      1000    // miernik energii

// Minimalna dopuszczalna wartość heat_hyst/cool_start — poniżej tego próg ON/OFF
// praktycznie pokrywa się z szumem czujnika STS35 (±0.1°C) i grozi chatteringiem.
#define MIN_HYSTERESIS_OFFSET       0.1f

// ============================================================
// LOGOWANIE — trzy proste bufory FRAM: godzinowy (rolling 24h, średnia
// liczona w RAM), dobowy (~rok, średnia z 24 rekordów godzinowych) i
// zdarzeń alarmowych (start/end epizodu). Zastąpiło to wcześniejszy
// event-driven LogZone/LogZoneConfig (2026-07-08 → 2026-07-14, patrz
// pamięć projektu) — decyzja: prostota i czytelny wykres długookresowy
// ważniejsze niż granulacja stopni ALARM_LOW/HIGH 1/2/3 i flapping.
// ============================================================
#define HOURLY_BUFFER_CAPACITY   24    // rolling 24h — nadpisywany co godzinę
#define DAILY_BUFFER_CAPACITY    366   // ~rok (+dzień przestępny) średnich dobowych
#define ALARM_BUFFER_CAPACITY    300   // epizody alarmowe (start/end), patrz AlarmEvent

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
// KONFIGURACJA ALGORYTMU (persystowana w FRAM)
// ============================================================
struct ThermoConfig {
    float    target_temp;       // temperatura zadana [°C]
    float    heat_hyst;         // SSR ON gdy temp < target - heat_hyst; OFF gdy temp >= target_temp
    float    cool_start;        // fan ON gdy temp > target + cool_start; OFF gdy temp <= target_temp
    float    cool_full;         // fan 100% gdy temp > target + cool_full
    float    alarm_delta_low;   // ALARM gdy temp < target - X
    float    alarm_delta_high;  // ALARM gdy temp > target + X
    float    trend_alarm_delta; // ALARM gdy spadek > X °C / 5 min
    float    temp_offset;       // kalibracja czujnika STS35 [°C]
    float    heater_watt;       // moc znamionowa grzałek [W]
    uint16_t pulses_per_kwh;    // konfiguracja miernika energii
    uint8_t  is_configured;     // THERMO_CONFIG_MAGIC = ważna konfiguracja
    uint8_t  fan_min_pct;       // floor rampy fana [%] (0-100), patrz DEFAULT_FAN_MIN_PCT
};
static_assert(sizeof(ThermoConfig) == 40, "ThermoConfig must be 40 bytes"); // 9 float + u16 + 2×u8 = 40B

// ============================================================
// REKORD ŚREDNIEJ TEMPERATURY — używany przez bufor godzinowy (24 rekordy,
// rolling) i dobowy (~rok). Ta sama struktura, różne pojemności/adresy FRAM.
// 4B+2B logicznie, ale kompilator dopełnia do 8B (wyrównanie uint32_t) —
// przyjęte wprost zamiast wymuszać __attribute__((packed)) na Xtensa.
// ============================================================
struct TempAvgRecord {
    uint32_t timestamp;   // 4B — Unix UTC początku okresu (godziny / doby lokalnej)
    int16_t  temp_x10;    // 2B — średnia arytmetyczna, zaokrąglona do 0.1°C
};
static_assert(sizeof(TempAvgRecord) == 8, "TempAvgRecord must be 8 bytes (padded)");

// ============================================================
// ZDARZENIA ALARMOWE — start/end epizodu, żeby dało się je "wyłowić"
// z długookresowego wykresu temp (który sam w sobie nie pokazuje alarmów,
// bo to tylko uśrednione wartości). Nie steruje aktuatorami — to robi
// _alarm_flags/_updateAlarms(), niezależnie od tego logu.
// ============================================================
enum AlarmEventType : uint8_t {
    ALARM_EVT_LOW_START    = 0,
    ALARM_EVT_LOW_END      = 1,
    ALARM_EVT_HIGH_START   = 2,
    ALARM_EVT_HIGH_END     = 3,
    ALARM_EVT_TREND_START  = 4,
    ALARM_EVT_TREND_END    = 5,
    ALARM_EVT_SENSOR_START = 6,
    ALARM_EVT_SENSOR_END   = 7,
};

struct AlarmEvent {
    uint32_t timestamp;   // 4B — Unix UTC zdarzenia
    int16_t  temp_x10;    // 2B — temp w chwili zdarzenia (ostatnia znana przy SENSOR_*)
    uint8_t  type;        // 1B — AlarmEventType
    uint8_t  _pad;        // 1B
};
static_assert(sizeof(AlarmEvent) == 8, "AlarmEvent must be 8 bytes");

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
