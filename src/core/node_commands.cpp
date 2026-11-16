#include "node_commands.h"

#include "../algorithm/temp_algorithm.h"
#include "../config/config.h"
#include "../core/logging.h"
#include "../hardware/buzzer_controller.h"
#include "../hardware/fan_controller.h"
#include "../hardware/heater_controller.h"
#include "../hardware/hw_health.h"
#include "../hardware/temp_sensor.h"

using namespace thermo;

AckResult cmdSetMode(bool autoMode, bool* changed) {
    bool isAuto = !isSystemDisabled();
    *changed = autoMode != isAuto;
    if (!*changed) return ACK_OK;
    setSystemState(autoMode);
    // Service Mode: the algorithm stops driving the buzzer (TempAlgorithm::update()), so it is
    // silenced here. The alarm flags stay visible. Latched alarms (hardware, sensor fault) need
    // intervention — Service Mode does not silence them.
    if (!autoMode && !isHwFaultAlarm() && !thermoAlgorithm.isSensorFaultLatched()) setBuzzerMode(BUZZER_OFF);
    return ACK_OK;
}

AckResult cmdAlarmMute() {
    thermoAlgorithm.muteActiveAlarms();
    return ACK_OK;
}

AckResult cmdSensorReset() {
    thermoAlgorithm.resetSensorFaultLatch();
    return ACK_OK;
}

AckResult cmdHeater(bool on) {
    if (!isSystemDisabled()) return ACK_NOT_SERVICE;
    if (on) {
        // No heating without a temperature reading — nothing would stop it
        if (thermoAlgorithm.isSensorFaultLatched() || !isTempValid()) {
            LOG_WARNING("Manual heater ON refused — sensor fault");
            return ACK_SENSOR_FAULT;
        }
        if (getTemperature() > thermoAlgorithm.getAlarmHighThreshold()) {
            LOG_WARNING("Manual heater ON refused — %.2f C above the TEMP_HIGH threshold %.2f C", getTemperature(),
                        thermoAlgorithm.getAlarmHighThreshold());
            return ACK_TEMP_HIGH;
        }
        setHeaterOn();
    } else {
        setHeaterOff();
    }
    LOG_INFO("Manual heater %s", on ? "ON" : "OFF");
    return ACK_OK;
}

AckResult cmdFan(int pct) {
    if (pct < 0 || pct > 100) return ACK_OUT_OF_RANGE;
    if (!isSystemDisabled()) return ACK_NOT_SERVICE;
    setFanSpeed((uint8_t)pct);
    LOG_INFO("Manual fan %d%%", pct);
    return ACK_OK;
}

const char* ackResultString(uint8_t result) {
    switch (result) {
        case ACK_OK:           return "OK";
        case ACK_NOT_SERVICE:  return "Service Mode only";
        case ACK_SENSOR_FAULT: return "Sensor fault - heater locked";
        case ACK_TEMP_HIGH:    return "Temperature above the alarm threshold - heater locked";
        case ACK_STALE_NONCE:  return "Stale nonce";
        case ACK_OUT_OF_RANGE: return "Out of range";
        case ACK_BAD_CMD:      return "Bad command";
        default:               return "?";
    }
}
