#pragma once
// Node commands — one gate to the state for the web handlers and the panel (ESP-NOW TH_CMD).
// Every rule is checked here, in the controller, not in a GUI: manual heater / fan only in Service
// Mode, heater ON refused with a sensor fault (latched or no valid reading) or above the TEMP_HIGH
// threshold. Results use the protocol codes (thermo::AckResult); the web handlers map them to HTTP.

#include <stdint.h>

#include "../network/thermo_proto.h"

// Auto (true) / Service Mode (false), idempotent: the same mode again keeps the running Service timer.
// Entering Service Mode silences the buzzer unless a latched alarm needs intervention. *changed: switched.
thermo::AckResult cmdSetMode(bool autoMode, bool* changed);

// Mute TEMP_LOW / TEMP_HIGH active now (current episode); nothing to mute is OK too.
thermo::AckResult cmdAlarmMute();

// Clear the sensor fault latch; it latches again on the next cycle if the sensor still fails.
thermo::AckResult cmdSensorReset();

// Manual heater (Service Mode only). OFF is always allowed in Service Mode.
thermo::AckResult cmdHeater(bool on);

// Manual fan 0–100 % (Service Mode only).
thermo::AckResult cmdFan(int pct);

const char* ackResultString(uint8_t result);
