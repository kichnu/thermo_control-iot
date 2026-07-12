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

// LogZone jest TYLKO do logowania (nie steruje aktuatorami — to robi AlgorithmState,
// patrz algorithm_config.h). W paśmie normal_delta rozróżnia IDLE/HEATING/COOLING wg
// aktualnego AlgorithmState; poza pasmem klasyfikuje czysto wg odchylenia od target_temp,
// niezależnie od AlgorithmState — to daje wcześniejszą granulację niż "twardy" ALARM_LOW/HIGH
// (który uruchamia się dopiero przy alarm_delta_low/high).
static LogZone computeLogZone(AlgorithmState state, float temp, float target, const LogZoneConfig& lzc) {
    if (state == STATE_SENSOR_FAULT) return ZONE_SENSOR_FAULT;
    if (state == STATE_TREND_ALARM)  return ZONE_TREND_ALARM;

    float dev = temp - target;  // dodatnie = powyżej target
    if (dev < 0) {
        float d = -dev;
        if (d <= lzc.normal_delta) return (state == STATE_HEATING) ? ZONE_HEATING : ZONE_IDLE;
        if (d <= lzc.low_step_1)   return ZONE_ALARM_LOW_1;
        if (d <= lzc.low_step_2)   return ZONE_ALARM_LOW_2;
        return ZONE_ALARM_LOW_3;
    } else {
        if (dev <= lzc.normal_delta) return (state == STATE_COOLING) ? ZONE_COOLING : ZONE_IDLE;
        if (dev <= lzc.high_step_1)  return ZONE_ALARM_HIGH_1;
        if (dev <= lzc.high_step_2)  return ZONE_ALARM_HIGH_2;
        return ZONE_ALARM_HIGH_3;
    }
}

// Sygnał diagnostyczny "UNSTABLE": ta sama strefa wchodzona ≥LOG_FLAPPING_COUNT razy
// w oknie LOG_FLAPPING_WINDOW_S — patrz pamięć projektu fram_event_logging pkt 4.
static bool checkFlapping(LogZone newZone, uint32_t now) {
    TempRecord recent[LOG_FLAPPING_COUNT + 8];
    uint16_t n = loadTempHistory(recent, LOG_FLAPPING_COUNT + 8);

    uint8_t matches = 1;  // ten nowy zapis liczy się jako pierwsze wystąpienie
    for (uint16_t i = 0; i < n; i++) {
        if (now - recent[i].timestamp > LOG_FLAPPING_WINDOW_S) break;  // newest-first — dalej tylko starsze
        if (TREC_ZONE_GET(recent[i].zone_reason) == newZone &&
            TREC_REASON_GET(recent[i].zone_reason) == REASON_ZONE_CHANGE) {
            matches++;
        }
    }
    return matches >= LOG_FLAPPING_COUNT;
}

void TempAlgorithm::begin() {
    loadConfig();        // stosuje defaults wewnętrznie, jeśli FRAM pusty/niepoprawny
    loadLogZoneConfig();
    LOG_INFO("TempAlgorithm: init, target=%.1f heat_hyst=%.1f",
             _config.target_temp, _config.heat_hyst);
}

void TempAlgorithm::update() {
    if (isSystemDisabled()) {
        return;  // system wstrzymany — grzałka/fan pod ręcznym sterowaniem (test GUI)
    }

    if (!isTempValid()) {
        if (getSensorFaultCount() >= SENSOR_FAULT_THRESHOLD) {
            _state = STATE_SENSOR_FAULT;
            _alarm_flags |= ALARM_FLAG_SENSOR;
            heaterEmergencyOff();
            fanEmergencyOff();
            setBuzzerMode(BUZZER_ALARM);
            _updateLogging(getTemperature());  // loguje z ostatnią znaną temperaturą
        }
        return;
    }

    float temp = getTemperature();
    _updateAlarms(temp);
    if (_state == STATE_ALARM_LOW || _state == STATE_ALARM_HIGH) {
        // _updateAlarms() już ustawił grzałkę/fan (alarm ma priorytet nad normalną regulacją)
        _updateLogging(temp);
        return;
    }
    _updateHeater(temp);
    _updateFan(temp);
    _updateLogging(temp);
}

void TempAlgorithm::_applyDefaults() {
    _config.target_temp      = DEFAULT_TARGET_TEMP;
    _config.heat_hyst        = DEFAULT_HEAT_HYST;
    _config.cool_start       = DEFAULT_COOL_START;
    _config.cool_full        = DEFAULT_COOL_FULL;
    _config.hysteresis_band  = DEFAULT_HYSTERESIS_BAND;
    _config.alarm_delta_low  = DEFAULT_ALARM_DELTA_LOW;
    _config.alarm_delta_high = DEFAULT_ALARM_DELTA_HIGH;
    _config.trend_alarm_delta= DEFAULT_TREND_ALARM_DELTA;
    _config.temp_offset      = DEFAULT_TEMP_OFFSET;
    _config.heater_watt      = DEFAULT_HEATER_WATT;
    _config.pulses_per_kwh   = DEFAULT_PULSES_PER_KWH;
    _config.is_configured    = 0;
}

void TempAlgorithm::_applyLogZoneDefaults() {
    _logZoneConfig.normal_delta = DEFAULT_LOG_ZONE_NORMAL_DELTA;
    _logZoneConfig.low_step_1   = DEFAULT_LOG_ZONE_LOW_STEP_1;
    _logZoneConfig.low_step_2   = DEFAULT_LOG_ZONE_LOW_STEP_2;
    _logZoneConfig.low_step_3   = DEFAULT_LOG_ZONE_LOW_STEP_3;
    _logZoneConfig.high_step_1  = DEFAULT_LOG_ZONE_HIGH_STEP_1;
    _logZoneConfig.high_step_2  = DEFAULT_LOG_ZONE_HIGH_STEP_2;
    _logZoneConfig.high_step_3  = DEFAULT_LOG_ZONE_HIGH_STEP_3;
}

void TempAlgorithm::_updateHeater(float temp) {
    float on_thresh  = _config.target_temp - _config.heat_hyst;
    float off_thresh = on_thresh + _config.hysteresis_band;
    if (temp < on_thresh) {
        setHeaterOn();
        _state = STATE_HEATING;
    } else if (temp > off_thresh && _state == STATE_HEATING) {
        setHeaterOff();
        _state = STATE_IDLE;
    }
}

void TempAlgorithm::_updateFan(float temp) {
    float cool_start_abs = _config.target_temp + _config.cool_start;
    float cool_full_abs  = _config.target_temp + _config.cool_full;
    uint8_t spd = calcFanSpeed(temp, cool_start_abs, cool_full_abs);
    setFanSpeed(spd);
    if (spd > 0 && _state != STATE_HEATING) _state = STATE_COOLING;
    else if (spd == 0 && _state == STATE_COOLING) _state = STATE_IDLE;
}

void TempAlgorithm::_updateAlarms(float temp) {
    float alarm_low_thresh  = _config.target_temp - _config.alarm_delta_low;
    float alarm_high_thresh = _config.target_temp + _config.alarm_delta_high;

    if (temp < alarm_low_thresh) {
        _state = STATE_ALARM_LOW;
        _alarm_flags |= ALARM_FLAG_TEMP_LOW;
        setHeaterOn();
        setFanSpeed(0);
        setBuzzerMode(BUZZER_ALARM);
        return;
    }

    if (temp > alarm_high_thresh) {
        _state = STATE_ALARM_HIGH;
        _alarm_flags |= ALARM_FLAG_TEMP_HIGH;
        setHeaterOff();
        setFanSpeed(100);
        setBuzzerMode(BUZZER_ALARM);
        return;
    }

    // Temp wróciła do zakresu — wyjdź ze stanu alarmowego (flaga zostaje aż do
    // ręcznego acknowledgeAlarm(), to celowe: latching alarm, nie auto-clear)
    if (_state == STATE_ALARM_LOW || _state == STATE_ALARM_HIGH) {
        _state = STATE_IDLE;
    }

    // Trend: SSR ON i szybki spadek temp w oknie 5 min (patrz ARCHITECTURE_SKELETON.md)
    if (isHeaterOn() && isTrendAlarm(_config.trend_alarm_delta)) {
        _state = STATE_TREND_ALARM;
        _alarm_flags |= ALARM_FLAG_TREND;
        setBuzzerMode(BUZZER_WARNING);
        return;
    }

    // Brak żadnej latched flagi — cisza. Jeśli coś jest wciąż latched (np. ALARM_LOW
    // sprzed chwili), buzzer świadomie zostaje na ostatnim wzorcu do acknowledgeAlarm().
    if (_alarm_flags == 0) {
        setBuzzerMode(BUZZER_OFF);
    }
}

void TempAlgorithm::_updateLogging(float temp) {
    uint32_t now = (uint32_t)getUnixTimestamp();
    LogZone zone = computeLogZone(_state, temp, _config.target_temp, _logZoneConfig);

    auto writeRecord = [&](LogReason reason) {
        TempRecord rec;
        rec.timestamp = now;
        rec.temp_x10  = (int16_t)(temp * 10.0f);

        uint8_t zr = 0;
        zr = TREC_ZONE_SET(zr, zone);
        zr = TREC_REASON_SET(zr, reason);
        if (reason == REASON_ZONE_CHANGE && checkFlapping(zone, now)) {
            zr = TREC_UNSTABLE_SET(zr);
        }
        rec.zone_reason = zr;

        uint8_t as = 0;
        as = TREC_HEATER_ON_SET(as, isHeaterOn());
        as = TREC_FAN_PCT_SET(as, getFanSpeedPct());
        rec.actuator_snapshot = as;

        saveTempRecord(rec);
        _lastLoggedZone = zone;
        _lastLogTs = now;
    };

    if (!_bootLogged) {
        writeRecord(REASON_BOOT);
        _bootLogged = true;
        return;
    }

    if (zone != _lastLoggedZone) {
        if (now - _lastLogTs >= LOG_MIN_EVENT_INTERVAL_S) {
            writeRecord(REASON_ZONE_CHANGE);
        }
        return;  // za wcześnie od ostatniego zapisu — kolejna próba w następnym ticku
    }

    if (now - _lastLogTs >= LOG_HEARTBEAT_INTERVAL_S) {
        writeRecord(REASON_HEARTBEAT);
    }
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

bool TempAlgorithm::loadLogZoneConfig() {
    if (loadLogZoneConfigFromFRAM(_logZoneConfig)) {
        LOG_INFO("LogZoneConfig loaded from FRAM: normal=%.1f low1=%.1f high1=%.1f",
                 _logZoneConfig.normal_delta, _logZoneConfig.low_step_1, _logZoneConfig.high_step_1);
        return true;
    }
    _applyLogZoneDefaults();
    LOG_WARNING("LogZoneConfig not found/invalid in FRAM — using defaults");
    return false;
}

bool TempAlgorithm::saveLogZoneConfig() {
    return saveLogZoneConfigToFRAM(_logZoneConfig);
}

bool TempAlgorithm::setLogZoneConfig(const LogZoneConfig& cfg) {
    _logZoneConfig = cfg;
    return saveLogZoneConfig();
}

void TempAlgorithm::acknowledgeAlarm()     { _alarm_flags = 0; _state = STATE_IDLE; setBuzzerMode(BUZZER_OFF); }
void TempAlgorithm::resetStateMachine()    { _state = STATE_IDLE; _alarm_flags = 0; setBuzzerMode(BUZZER_OFF); }

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
