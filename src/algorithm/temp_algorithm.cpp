#include "temp_algorithm.h"
#include "hardware/temp_sensor.h"
#include "hardware/heater_controller.h"
#include "hardware/fan_controller.h"
#include "hardware/fram_controller.h"
#include "hardware/rtc_controller.h"
#include "hardware/fram_constants.h"
#include "core/logging.h"

// TODO: Etap 3b — pełna implementacja automatu

TempAlgorithm thermoAlgorithm;

void TempAlgorithm::begin() {
    _applyDefaults();
    loadConfig();
    _window = {};
    _window.window_start_ts = (uint32_t)getUnixTimestamp();
    LOG_INFO("TempAlgorithm: init, target=%.1f heat_hyst=%.1f",
             _config.target_temp, _config.heat_hyst);
}

void TempAlgorithm::update() {
    if (!isTempValid()) {
        if (getSensorFaultCount() >= SENSOR_FAULT_THRESHOLD) {
            _state = STATE_SENSOR_FAULT;
            _alarm_flags |= ALARM_FLAG_SENSOR;
            heaterEmergencyOff();
            fanEmergencyOff();
        }
        return;
    }

    float temp = getTemperature();
    _updateAlarms(temp);
    if (_state == STATE_ALARM_LOW || _state == STATE_ALARM_HIGH) {
        // W trybie alarmu: grzałka ON (alarm low) lub fan 100% (alarm high)
        // TODO: pełna obsługa
        return;
    }
    _updateHeater(temp);
    _updateFan(temp);
    _accumulateSample(temp, isHeaterOn(), getFanSpeedPct());
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
    // TODO: pełna obsługa alarmów low/high/trend
}

void TempAlgorithm::_accumulateSample(float temp, bool heater_on, uint8_t fan_pct) {
    _window.temp_sum_x10  += (int32_t)(temp * 10.0f);
    if (heater_on) _window.heater_on_count++;
    _window.fan_pct_sum   += fan_pct;
    _window.alarm_flags_or |= _alarm_flags;
    _window.sample_count++;
}

bool TempAlgorithm::isWindowReady() const {
    if (_window.sample_count == 0) return false;
    uint32_t now = (uint32_t)getUnixTimestamp();
    return (now - _window.window_start_ts) >= WINDOW_DURATION_S;
}

TempRecord TempAlgorithm::consumeWindowRecord() {
    TempRecord rec = {};
    rec.timestamp  = (uint32_t)getUnixTimestamp();
    uint8_t n = _window.sample_count > 0 ? _window.sample_count : 1;
    rec.temp_x10    = (int16_t)(_window.temp_sum_x10 / n);
    rec.heater_pct  = (uint8_t)(_window.heater_on_count * 100 / n);
    rec.fan_pct     = (uint8_t)(_window.fan_pct_sum / n);
    rec.alarm_flags = _window.alarm_flags_or;
    rec.state       = (uint8_t)_state;
    _window = {};
    _window.window_start_ts = rec.timestamp;
    return rec;
}

bool TempAlgorithm::loadConfig()           { return false; /* TODO: Etap 4a */ }
bool TempAlgorithm::saveConfig()           { return false; /* TODO: Etap 4a */ }
bool TempAlgorithm::setConfig(const ThermoConfig& cfg) {
    _config = cfg;
    return saveConfig();
}
void TempAlgorithm::acknowledgeAlarm()     { _alarm_flags = 0; _state = STATE_IDLE; }
void TempAlgorithm::resetStateMachine()    { _state = STATE_IDLE; _alarm_flags = 0; }

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
