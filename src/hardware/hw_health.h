#ifndef HW_HEALTH_H
#define HW_HEALTH_H

#include <Arduino.h>

// Critical blocks: FRAM (SPI), DS3231 RTC and the Wi-Fi credentials stored in FRAM.
// A failure at boot or during operation raises a latched critical alarm (buzzer,
// GUI, /api/status, /api/health) that stays until the block works again and the
// user acknowledges it in the GUI. The latch survives software restarts
// (midnight, OTA) and is cleared at power-on.
// Temperature control keeps running meanwhile: it needs only the STS35 sensor,
// the configuration stays in RAM (defaults when FRAM was missing at boot).
// Pattern: ~/dev-knowledge/esp32/critical-hardware-alarm.md (top_off_water_new-iot).

enum HwFaultBit : uint8_t {
    HW_FAULT_FRAM = 0x01,
    HW_FAULT_RTC  = 0x02,
    HW_FAULT_CRED = 0x04,   // no credentials in FRAM — Wi-Fi from the build-time fallback
};

void initHwHealth();      // end of setup(), after the FRAM, RTC and credentials init
void updateHwHealth();    // every loop() — periodic check, buzzer
void noteFramIoError();   // failed FRAM transfer — check soon
void noteRtcIoError();    // failed DS3231 read — check soon

bool     isHwFaultAlarm();       // latched — buzzer, GUI
uint8_t  getHwFaultActive();     // HwFaultBit mask, failing now
uint8_t  getHwFaultLatched();    // HwFaultBit mask, latched
uint32_t getHwFaultSinceTs();    // unix time of the first fault, 0 = unknown
String   getHwFaultText();
bool     ackHwFaults();          // GUI acknowledge: clears what works again; true if anything was latched

// FRAM missing at boot answers again — restart to re-init it (limited count)
bool isHwRecoveryRestartWanted();
void noteHwRecoveryRestart();

#endif // HW_HEALTH_H
