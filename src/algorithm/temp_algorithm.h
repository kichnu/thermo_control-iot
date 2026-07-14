#ifndef TEMP_ALGORITHM_H
#define TEMP_ALGORITHM_H

#include <Arduino.h>
#include "algorithm_config.h"

// Automat sterowania temperaturą:
//  - histereza symetryczna (jeden parametr/kanał, OFF przy target_temp) dla SSR grzałki i PWM wentylatora
//  - detekcja alarmu trendu (RAM bufor)
//  - logowanie: bufor godzinowy (RAM-owy accumulator → FRAM co godzinę),
//    bufor dobowy (średnia z 24 godzinowych), log zdarzeń alarmowych
//    (start/end epizodu) — patrz TempAvgRecord/AlarmEvent w algorithm_config.h

class TempAlgorithm {
public:
    void begin();
    void update();                          // wywołuj co TEMP_MEASURE_INTERVAL_S (sensor+sterowanie+akumulacja)
    void checkLogRollover();                // wywołuj co pętlę/~100ms — detekcja granicy godziny/doby (patrz .cpp)

    AlgorithmState getState() const         { return _state; }
    const char*    getStateString() const;
    uint8_t        getAlarmFlags() const    { return _alarm_flags; }
    bool           isAlarmActive() const    { return _alarm_flags != 0; }
    // Wycisza buzzer dla alarmów aktywnych W TEJ CHWILI (web API). _alarm_flags
    // zostaje (widoczne w GUI dopóki warunek trwa) — wyciszenie dotyczy tylko
    // dźwięku i tylko bieżącego epizodu: gdy dany warunek ustąpi i pojawi się
    // ponownie, znów zadźwięczy (patrz _setAlarmFlag()).
    void           muteActiveAlarms();

    // Konfiguracja (ładowana z FRAM lub defaults)
    bool           loadConfig();
    bool           saveConfig();
    const ThermoConfig& getConfig() const  { return _config; }
    bool           setConfig(const ThermoConfig& cfg);

private:
    AlgorithmState   _state       = STATE_IDLE;
    uint8_t          _alarm_flags = 0;   // aktywne TERAZ (live, nie latchowane)
    uint8_t          _muted_flags = 0;   // wyciszone przez muteActiveAlarms(), per-epizod
    ThermoConfig     _config      = {};
    bool             _fanActive   = false;  // latch: ON przy temp>cool_start_abs, OFF przy temp<=target_temp

    // Akumulator godzinowy — TYLKO RAM, flushowany do FRAM przy zmianie godziny.
    float            _hourAccumSum   = 0.0f;
    uint16_t         _hourAccumCount = 0;
    // (rok*366 + dzień_roku)*24 + godzina, dla ostatnio widzianej godziny lokalnej.
    // -1 = jeszcze nie zainicjalizowany (pierwszy tick po boot, nic nie flushuj).
    int32_t          _lastHourKey    = -1;

    void _applyDefaults();
    void _updateHeater(float temp);
    void _updateFan(float temp);
    void _updateAlarms(float temp);
    void _accumulateHourlySample(float temp);
    void _flushHourlyAccumulator(uint32_t hourStartTs);
    void _rollupDailyAverage();
    void _logAlarmEvent(uint8_t flag, bool starting, float temp);
    // Ustawia/czyści bit w _alarm_flags; przy zboczu 0→1/1→0 loguje AlarmEvent,
    // a przy 1→0 kasuje też odpowiadający bit w _muted_flags, żeby kolejne
    // wystąpienie tego samego typu alarmu znów zadźwięczyło (mute nie jest
    // permanentny per-typ, tylko per-epizod).
    void _setAlarmFlag(uint8_t flag, bool active, float temp);
};

extern TempAlgorithm thermoAlgorithm;

#endif // TEMP_ALGORITHM_H
