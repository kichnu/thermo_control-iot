#ifndef TEMP_ALGORITHM_H
#define TEMP_ALGORITHM_H

#include <Arduino.h>
#include "algorithm_config.h"

// Automat sterowania temperaturą:
//  - histereza ON/OFF dla SSR grzałki
//  - proporcjonalny PWM dla wentylatora (krzywa liniowa)
//  - detekcja alarmu trendu (RAM bufor)
//  - akumulator okna 30-minutowego → zapis do FRAM

class TempAlgorithm {
public:
    void begin();
    void update();                          // wywołuj co loop()

    AlgorithmState getState() const         { return _state; }
    const char*    getStateString() const;
    uint8_t        getAlarmFlags() const    { return _alarm_flags; }
    bool           isAlarmActive() const    { return _alarm_flags != 0; }
    void           acknowledgeAlarm();      // manual reset alarmu (web API)
    void           resetStateMachine();     // reset do IDLE

    // Konfiguracja (ładowana z FRAM lub defaults)
    bool           loadConfig();
    bool           saveConfig();
    const ThermoConfig& getConfig() const  { return _config; }
    bool           setConfig(const ThermoConfig& cfg);

    // Akumulator okna (do zapisu FRAM co 30 min)
    bool           isWindowReady() const;   // true = minęło 30 min, rekord gotowy
    TempRecord     consumeWindowRecord();   // pobierz i wyczyść akumulator

private:
    AlgorithmState   _state       = STATE_IDLE;
    uint8_t          _alarm_flags = 0;
    ThermoConfig     _config      = {};
    WindowAccumulator _window     = {};

    void _applyDefaults();
    void _updateHeater(float temp);
    void _updateFan(float temp);
    void _updateAlarms(float temp);
    void _accumulateSample(float temp, bool heater_on, uint8_t fan_pct);
};

extern TempAlgorithm thermoAlgorithm;

#endif // TEMP_ALGORITHM_H
