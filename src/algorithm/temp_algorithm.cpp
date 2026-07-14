#include "temp_algorithm.h"
#include "hardware/temp_sensor.h"
#include "hardware/heater_controller.h"
#include "hardware/fan_controller.h"
#include "hardware/fram_controller.h"
#include "hardware/rtc_controller.h"
#include "hardware/fram_constants.h"
#include "hardware/buzzer_controller.h"
#include "core/logging.h"
#include "config/config.h"

TempAlgorithm thermoAlgorithm;

void TempAlgorithm::begin() {
    loadConfig();        // stosuje defaults wewnętrznie, jeśli FRAM pusty/niepoprawny
    LOG_INFO("TempAlgorithm: init, target=%.1f heat_hyst=%.1f",
             _config.target_temp, _config.heat_hyst);
}

void TempAlgorithm::update() {
    if (isSystemDisabled()) {
        return;  // system wstrzymany — grzałka/fan pod ręcznym sterowaniem (test GUI)
    }

    if (!isTempValid()) {
        bool sensorFault = getSensorFaultCount() >= SENSOR_FAULT_THRESHOLD;
        _setAlarmFlag(ALARM_FLAG_SENSOR, sensorFault, getTemperature());
        if (sensorFault) {
            _state = STATE_SENSOR_FAULT;
            heaterEmergencyOff();
            fanEmergencyOff();
            if (!(_muted_flags & ALARM_FLAG_SENSOR)) setBuzzerMode(BUZZER_ALARM);
        }
        return;  // brak ważnego odczytu — nic do zaakumulowania w buforze godzinowym
    }
    _setAlarmFlag(ALARM_FLAG_SENSOR, false, getTemperature());  // czujnik znów sprawny — czyści flagę i mute

    float temp = getTemperature();
    _accumulateHourlySample(temp);
    _updateAlarms(temp);
    if (_state == STATE_ALARM_LOW || _state == STATE_ALARM_HIGH) {
        return;  // _updateAlarms() już ustawił grzałkę/fan (alarm ma priorytet nad normalną regulacją)
    }
    _updateHeater(temp);
    _updateFan(temp);
}

void TempAlgorithm::_applyDefaults() {
    _config.target_temp      = DEFAULT_TARGET_TEMP;
    _config.heat_hyst        = DEFAULT_HEAT_HYST;
    _config.cool_start       = DEFAULT_COOL_START;
    _config.cool_full        = DEFAULT_COOL_FULL;
    _config.alarm_delta_low  = DEFAULT_ALARM_DELTA_LOW;
    _config.alarm_delta_high = DEFAULT_ALARM_DELTA_HIGH;
    _config.trend_alarm_delta= DEFAULT_TREND_ALARM_DELTA;
    _config.temp_offset      = DEFAULT_TEMP_OFFSET;
    _config.heater_watt      = DEFAULT_HEATER_WATT;
    _config.pulses_per_kwh   = DEFAULT_PULSES_PER_KWH;
    _config.is_configured    = 0;
    _config.fan_min_pct      = DEFAULT_FAN_MIN_PCT;
}

// Histereza symetryczna wokół target_temp, jeden parametr na kanał (patrz pamięć
// projektu): ON przy target - heat_hyst, OFF dokładnie przy target_temp (>=).
void TempAlgorithm::_updateHeater(float temp) {
    float on_thresh = _config.target_temp - _config.heat_hyst;
    if (temp < on_thresh) {
        setHeaterOn();
        _state = STATE_HEATING;
    } else if (temp >= _config.target_temp && _state == STATE_HEATING) {
        setHeaterOff();
        _state = STATE_IDLE;
    }
}

// Analogicznie do grzałki, zwierciadlane: ON przy target + cool_start, OFF przy
// target_temp (<=). W przeciwieństwie do grzałki fan nie jest on/off — potrzebuje
// własnego latcha (_fanActive), bo prędkość między progami zależy od tego, czy fan
// już pracuje (inaczej calcFanSpeed() nie wiedziałby, czy poniżej cool_start_abs
// ma zwrócić 0, czy trzymać floor fan_min_pct).
void TempAlgorithm::_updateFan(float temp) {
    float cool_start_abs = _config.target_temp + _config.cool_start;
    float cool_full_abs  = _config.target_temp + _config.cool_full;

    if (!_fanActive && temp > cool_start_abs) {
        _fanActive = true;
    } else if (_fanActive && temp <= _config.target_temp) {
        _fanActive = false;
    }

    uint8_t spd = _fanActive ? calcFanSpeed(temp, cool_start_abs, cool_full_abs, _config.fan_min_pct) : 0;
    setFanSpeed(spd);

    if (_fanActive && _state != STATE_HEATING) _state = STATE_COOLING;
    else if (!_fanActive && _state == STATE_COOLING) _state = STATE_IDLE;
}

// AlarmEventType odpowiadający fladze i kierunkowi zbocza — patrz algorithm_config.h.
static AlarmEventType alarmEventTypeFor(uint8_t flag, bool starting) {
    switch (flag) {
        case ALARM_FLAG_TEMP_LOW:  return starting ? ALARM_EVT_LOW_START    : ALARM_EVT_LOW_END;
        case ALARM_FLAG_TEMP_HIGH: return starting ? ALARM_EVT_HIGH_START   : ALARM_EVT_HIGH_END;
        case ALARM_FLAG_TREND:     return starting ? ALARM_EVT_TREND_START  : ALARM_EVT_TREND_END;
        default:                   return starting ? ALARM_EVT_SENSOR_START : ALARM_EVT_SENSOR_END;
    }
}

void TempAlgorithm::_logAlarmEvent(uint8_t flag, bool starting, float temp) {
    AlarmEvent ev;
    ev.timestamp = (uint32_t)getUnixTimestamp();
    ev.temp_x10  = (int16_t)roundf(temp * 10.0f);
    ev.type      = alarmEventTypeFor(flag, starting);
    ev._pad      = 0;
    saveAlarmEvent(ev);
}

void TempAlgorithm::_setAlarmFlag(uint8_t flag, bool active, float temp) {
    bool wasActive = (_alarm_flags & flag) != 0;
    if (active && !wasActive) {
        _alarm_flags |= flag;
        _logAlarmEvent(flag, true, temp);
    } else if (!active && wasActive) {
        _alarm_flags &= ~flag;
        _muted_flags &= ~flag;  // warunek ustąpił — kolejne wystąpienie znów zadźwięczy
        _logAlarmEvent(flag, false, temp);
    }
}

void TempAlgorithm::_updateAlarms(float temp) {
    float alarm_low_thresh  = _config.target_temp - _config.alarm_delta_low;
    float alarm_high_thresh = _config.target_temp + _config.alarm_delta_high;

    bool low_now  = (temp < alarm_low_thresh);
    bool high_now = (!low_now && temp > alarm_high_thresh);

    _setAlarmFlag(ALARM_FLAG_TEMP_LOW,  low_now, temp);
    _setAlarmFlag(ALARM_FLAG_TEMP_HIGH, high_now, temp);

    if (low_now) {
        _state = STATE_ALARM_LOW;
        setHeaterOn();
        setFanSpeed(0);
        if (!(_muted_flags & ALARM_FLAG_TEMP_LOW)) setBuzzerMode(BUZZER_ALARM);
        return;
    }

    if (high_now) {
        _state = STATE_ALARM_HIGH;
        setHeaterOff();
        setFanSpeed(100);
        if (!(_muted_flags & ALARM_FLAG_TEMP_HIGH)) setBuzzerMode(BUZZER_ALARM);
        return;
    }

    // Temp wróciła do zakresu — wyjdź ze stanu alarmowego
    // TODO (2026-07, do sprawdzenia — NIE naprawiać bez osobnej decyzji): STATE_TREND_ALARM
    // nie ma analogicznego auto-wyjścia niżej — gdy trend_now przejdzie true→false, _state
    // zostaje STATE_TREND_ALARM na stałe (w przeciwieństwie do ALARM_LOW/HIGH, które co tick
    // same wracają do IDLE). _alarm_flags/buzzer już się poprawnie czyszczą przez
    // _setAlarmFlag(), ale _state/getStateString() w GUI zostaje błędny. Po usunięciu
    // przycisku "Reset automatu stanów" nie ma już żadnego sposobu, by to ręcznie odblokować —
    // trend powinien działać tak samo jak pozostałe alarmy (self-heal do IDLE).
    if (_state == STATE_ALARM_LOW || _state == STATE_ALARM_HIGH) {
        _state = STATE_IDLE;
    }

    // Trend: SSR ON i szybki spadek temp w oknie 5 min (patrz ARCHITECTURE_SKELETON.md)
    bool trend_now = isHeaterOn() && isTrendAlarm(_config.trend_alarm_delta);
    _setAlarmFlag(ALARM_FLAG_TREND, trend_now, temp);
    if (trend_now) {
        _state = STATE_TREND_ALARM;
        if (!(_muted_flags & ALARM_FLAG_TREND)) setBuzzerMode(BUZZER_WARNING);
        return;
    }

    // Nic aktywnego — cisza (SENSOR obsługiwany osobno w update()).
    if (_alarm_flags == 0) {
        setBuzzerMode(BUZZER_OFF);
    }
}

void TempAlgorithm::_accumulateHourlySample(float temp) {
    _hourAccumSum   += temp;
    _hourAccumCount += 1;
}

// Zapisuje uśrednioną próbkę bieżącej godziny do FRAM — wołane przy wykryciu
// przejścia granicy godziny, NIEZALEŻNIE od tego czy godzina była pełna
// (restart o lokalnej północy — patrz main.cpp — gwarantuje ≥1 niepełną
// godzinę dziennie; pomijanie takich godzin oznaczałoby, że bufor dobowy
// nigdy nie osiągnąłby kompletu 24 wpisów). Jedyny guard: zero próbek
// (np. czujnik w fault przez całą godzinę) — wtedy nic nie zapisujemy.
void TempAlgorithm::_flushHourlyAccumulator(uint32_t hourStartTs) {
    if (_hourAccumCount > 0) {
        TempAvgRecord rec;
        rec.timestamp = hourStartTs;
        rec.temp_x10  = (int16_t)roundf((_hourAccumSum / _hourAccumCount) * 10.0f);
        saveHourlyRecord(rec);
    }
    _hourAccumSum   = 0.0f;
    _hourAccumCount = 0;
}

// Średnia z 24 rekordów godzinowych = doba, która właśnie się zakończyła
// (bufor godzinowy to zawsze "ostatnie 24h" — patrz HOURLY_BUFFER_CAPACITY=24).
// Jeśli ring jeszcze nie ma 24 wpisów (pierwsza doba po uruchomieniu), pomijamy
// zapis zamiast liczyć średnią z niepełnego zestawu.
void TempAlgorithm::_rollupDailyAverage() {
    TempAvgRecord hourly[HOURLY_BUFFER_CAPACITY];
    uint16_t n = loadHourlyHistory(hourly, HOURLY_BUFFER_CAPACITY);  // newest-first
    if (n < HOURLY_BUFFER_CAPACITY) return;

    int32_t sum10 = 0;
    for (uint16_t i = 0; i < n; i++) sum10 += hourly[i].temp_x10;

    TempAvgRecord day;
    day.timestamp = hourly[n - 1].timestamp;  // najstarszy z 24 = początek zakończonej doby
    day.temp_x10  = (int16_t)lroundf((float)sum10 / n);
    saveDailyRecord(day);
}

// Wołane co pętlę (~100ms z main.cpp), NIEZALEŻNIE od 15s cyklu update()/sensora:
// granica lokalnej północy pokrywa się z restartem urządzenia (main.cpp planuje
// go dokładnie na najbliższą lokalną północ), który jest sprawdzany co pętlę —
// gdyby detekcja granicy godziny była związana z 15s cyklem update(), restart
// mógłby wygrać wyścig i zabić proces przed flushem/rollupem ostatniej godziny doby.
void TempAlgorithm::checkLogRollover() {
    time_t ts = (time_t)getUnixTimestamp();
    struct tm t;
    localtime_r(&ts, &t);
    if (t.tm_year <= 120) return;  // RTC/NTP jeszcze nie zsynchronizowany (rok < 2020)

    int32_t hourKey = ((int32_t)t.tm_year * 366 + t.tm_yday) * 24 + t.tm_hour;
    if (_lastHourKey < 0) {
        _lastHourKey = hourKey;  // pierwszy tick po boot — nie flushuj nieistniejącego okresu
        return;
    }
    if (hourKey == _lastHourKey) return;

    bool dayChanged = (hourKey / 24) != (_lastHourKey / 24);
    uint32_t hourStartTs = (uint32_t)(ts - (ts % 3600) - 3600);
    _flushHourlyAccumulator(hourStartTs);
    if (dayChanged) _rollupDailyAverage();

    _lastHourKey = hourKey;
}

bool TempAlgorithm::loadConfig() {
    if (loadThermoConfigFromFRAM(_config) && _config.is_configured == THERMO_CONFIG_MAGIC) {
        LOG_INFO("ThermoConfig loaded from FRAM: target=%.1f", _config.target_temp);
        return true;
    }
    _applyDefaults();
    LOG_WARNING("ThermoConfig not found/invalid in FRAM — using defaults");
    return false;
}

bool TempAlgorithm::saveConfig() {
    _config.is_configured = THERMO_CONFIG_MAGIC;
    return saveThermoConfigToFRAM(_config);
}

bool TempAlgorithm::setConfig(const ThermoConfig& cfg) {
    _config = cfg;
    return saveConfig();
}

void TempAlgorithm::muteActiveAlarms() {
    _muted_flags |= _alarm_flags;  // wycisza tylko to, co jest aktywne w tej chwili
    setBuzzerMode(BUZZER_OFF);
}

const char* TempAlgorithm::getStateString() const {
    switch (_state) {
        case STATE_IDLE:         return "IDLE";
        case STATE_HEATING:      return "HEATING";
        case STATE_COOLING:      return "COOLING";
        case STATE_ALARM_LOW:    return "ALARM_LOW";
        case STATE_ALARM_HIGH:   return "ALARM_HIGH";
        case STATE_SENSOR_FAULT: return "SENSOR_FAULT";
        case STATE_TREND_ALARM:  return "TREND_ALARM";
        default:                 return "UNKNOWN";
    }
}
