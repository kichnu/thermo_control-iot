#ifndef TEMP_ALGORITHM_H
#define TEMP_ALGORITHM_H

#include <Arduino.h>
#include "algorithm_config.h"

// Temperature control state machine:
//  - symmetric hysteresis (one parameter per channel, OFF at target_temp) for the heater SSR and the fan PWM
//  - trend warning (no buzzer): heater ON for >= trend_window_min minutes without enough temperature rise
//  - SENSOR_FAULT is a CRITICAL fault — latched, heater/fan frozen until a manual reset
//    (resetSensorFaultLatch()), even if the sensor recovers by itself
//  - logging: hourly buffer (RAM accumulator → FRAM every hour), daily buffer (average of 24 hourly),
//    log of alarm occurrences (no start/end) — see TempAvgRecord/AlarmEvent in algorithm_config.h

class TempAlgorithm {
public:
    void begin();
    void update();                          // call every TEMP_MEASURE_INTERVAL_S (sensor + control + accumulation)
    void checkLogRollover();                // call every loop / ~100 ms — hour / day boundary detection (see .cpp)

    AlgorithmState getState() const         { return _state; }
    const char*    getStateString() const;
    uint8_t        getAlarmFlags() const    { return _alarm_flags; }
    uint8_t        getMutedFlags() const    { return _muted_flags & _alarm_flags; }  // muted and still active
    bool           isAlarmActive() const    { return _alarm_flags != 0; }
    // Silences TEMP_LOW / TEMP_HIGH active right now (web API). _alarm_flags stay
    // (visible while the condition lasts); the mute covers only the current episode:
    // when the condition clears and comes back, it sounds again (see _setAlarmFlag()).
    // Not for SENSOR_FAULT / hardware alarm (intervention: reset / acknowledge);
    // TREND is a warning without a buzzer.
    void           muteActiveAlarms();

    // SENSOR_FAULT as a critical fault (2026-07-14, user): once latched, the heater (and the fan in
    // Auto) is forced OFF in EVERY update() until someone resets it manually — the sensor recovering
    // by itself is NOT enough (deliberately unlike ALARM_LOW/HIGH/TREND, which clear by themselves).
    // If the fault still lasts, the next update() latches it again at once.
    bool           isSensorFaultLatched() const { return _sensorFaultLatched; }
    void           resetSensorFaultLatch();

    // TEMP_HIGH threshold: fan_on_point (target + thermal_buffer) + alarm_delta_high
    float          getAlarmHighThreshold() const;

    // Configuration (loaded from FRAM or defaults)
    bool           loadConfig();
    bool           saveConfig();
    const ThermoConfig& getConfig() const  { return _config; }
    bool           setConfig(const ThermoConfig& cfg);

private:
    AlgorithmState   _state       = STATE_IDLE;
    uint8_t          _alarm_flags = 0;   // active NOW (live, not latched)
    uint8_t          _muted_flags = 0;   // silenced by muteActiveAlarms(), per episode
    ThermoConfig       _config    = {};
    bool             _fanActive   = false;  // latch: ON at temp > fan_on_point, OFF at temp <= fan_off_point (see _updateFan)
    bool             _sensorFaultLatched = false;
    // Set in update() on every cycle while the system is paused (Service Mode) — on the next resume
    // the heater / fan must be reset to a known state (see update() in .cpp for why).
    bool             _resumeResetPending = false;

    // Hourly accumulator — RAM ONLY, flushed to FRAM when the hour changes.
    float            _hourAccumSum   = 0.0f;
    uint16_t         _hourAccumCount = 0;
    // (year*366 + day_of_year)*24 + hour of the last seen local hour.
    // -1 = not initialised yet (first tick after boot, flush nothing).
    int32_t          _lastHourKey    = -1;

    // Trend reference point: start of the CURRENT continuous heating (reset on every OFF→ON edge
    // in _updateHeater). 0 = no reference (the heater is not heating at the moment).
    uint32_t         _heatStartTs   = 0;
    float             _heatStartTemp = 0.0f;

    void _applyDefaults();
    void _updateHeater(float temp);
    void _updateFan(float temp);
    void _updateAlarms(float temp);
    bool _checkSensor();         // false: no valid reading or fault latched (heater forced OFF)
    void _guardServiceMode();    // Service Mode: sensor fault and TEMP_HIGH protection of the manual heater
    bool _checkTrendAlarm(float temp) const;
    void _accumulateHourlySample(float temp);
    void _flushHourlyAccumulator(uint32_t hourStartTs);
    void _rollupDailyAverage();
    void _logAlarmEvent(uint8_t flag, float temp);
    // Sets / clears a bit in _alarm_flags; on a 0→1 edge logs an AlarmEvent (the occurrence, no
    // end/duration), on 1→0 clears the matching bit in _muted_flags, so the next occurrence of the
    // same alarm sounds again (the mute is per episode, not permanent per type).
    void _setAlarmFlag(uint8_t flag, bool active, float temp);
};

extern TempAlgorithm thermoAlgorithm;

#endif // TEMP_ALGORITHM_H
