#ifndef RTC_CONTROLLER_H
#define RTC_CONTROLLER_H

#include <Arduino.h>

void initializeRTC();
String getCurrentTimestamp();
bool isRTCWorking();
unsigned long getUnixTimestamp();
bool isUnixTimeValid();   // the last getUnixTimestamp() came from the DS3231 with a valid year
String getRTCInfo();

String getTimeSourceInfo();
bool isRTCHardware();

bool rtcNeedsSynchronization();
void initInternalTimeFromCompileTime();
bool setRTCFromNTP();
void updateRTCSync();
bool retryRTC();       // runtime retry of the DS3231 (back at once, or set from NTP) — hw_health  // wywołuj co loop() — okresowy resync z NTP (co NTP_SYNC_INTERVAL)

bool isBatteryIssueDetected();

#endif