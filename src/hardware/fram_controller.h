#ifndef FRAM_CONTROLLER_H
#define FRAM_CONTROLLER_H

#include <Arduino.h>
#include "fram_constants.h"

// Basic FRAM functions
bool initFRAM();
bool isFramInitialized();
bool framReconnect();
bool verifyFRAM();

// ===============================
// FRAM CREDENTIALS SECTION
// (Used by programming mode)
// ===============================

// Forward declaration for credentials structure (defined in crypto/fram_encryption.h)
struct FRAMCredentials;

bool readCredentialsFromFRAM(FRAMCredentials& creds);
bool writeCredentialsToFRAM(const FRAMCredentials& creds);
bool verifyCredentialsInFRAM();

// ===============================
// THERMO CONFIG SECTION
// ===============================

// Forward declaration (defined in algorithm/algorithm_config.h)
struct ThermoConfig;

bool saveThermoConfigToFRAM(const ThermoConfig& cfg);
bool loadThermoConfigFromFRAM(ThermoConfig& cfg);

// ===============================
// LOG ZONE CONFIG SECTION (progi logowania zdarzeniowego, konfigurowalne z GUI)
// ===============================

struct LogZoneConfig;

bool saveLogZoneConfigToFRAM(const LogZoneConfig& cfg);
bool loadLogZoneConfigFromFRAM(LogZoneConfig& cfg);

// ===============================
// RING BUFFER SECTION (TempRecord, event-driven — patrz fram_constants.h)
// ===============================

struct TempRecord;

bool     saveTempRecord(const TempRecord& rec);
uint16_t loadTempHistory(TempRecord* buf, uint16_t maxCount);  // newest-first
uint16_t getTempRecordCount();

#endif
