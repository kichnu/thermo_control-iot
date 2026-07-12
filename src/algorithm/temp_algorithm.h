#ifndef TEMP_ALGORITHM_H
#define TEMP_ALGORITHM_H

#include <Arduino.h>
#include "algorithm_config.h"

// Automat sterowania temperaturą:
//  - histereza ON/OFF dla SSR grzałki
//  - proporcjonalny PWM dla wentylatora (krzywa liniowa)
//  - detekcja alarmu trendu (RAM bufor)
//  - logowanie zdarzeniowe do ring buffera FRAM (zmiana LogZone, heartbeat 24h,
//    boot marker, flapping) — patrz LogZoneConfig/LogZone/TempRecord w
//    algorithm_config.h

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

    // Progi logowania zdarzeniowego (ring buffer FRAM) — osobna konfiguracja od ThermoConfig
    bool           loadLogZoneConfig();
    bool           saveLogZoneConfig();
    const LogZoneConfig& getLogZoneConfig() const { return _logZoneConfig; }
    bool           setLogZoneConfig(const LogZoneConfig& cfg);

private:
    AlgorithmState   _state       = STATE_IDLE;
    uint8_t          _alarm_flags = 0;
    ThermoConfig     _config      = {};

    LogZoneConfig    _logZoneConfig  = {};
    LogZone          _lastLoggedZone = ZONE_IDLE;
    uint32_t         _lastLogTs      = 0;
    bool             _bootLogged     = false;

    void _applyDefaults();
    void _applyLogZoneDefaults();
    void _updateHeater(float temp);
    void _updateFan(float temp);
    void _updateAlarms(float temp);
    void _updateLogging(float temp);
};

extern TempAlgorithm thermoAlgorithm;

#endif // TEMP_ALGORITHM_H
