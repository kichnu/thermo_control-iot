#ifndef FRAM_CONTROLLER_H
#define FRAM_CONTROLLER_H

#include <Arduino.h>
#include "fram_constants.h"

// Basic FRAM functions
bool initFRAM();
bool isFramInitialized();
bool framReconnect();
bool verifyFRAM();
bool framProbe();   // chip answers on SPI (device ID) — does not mark FRAM as initialized

// Raw access for the health check (hw_health); false = FRAM not initialized
bool framReadRaw(uint32_t addr, uint8_t* data, size_t len);
bool framWriteRaw(uint32_t addr, const uint8_t* data, size_t len);

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
// ENERGY STORE SECTION (licznik impulsów miernika energii, checksum wbudowany w struct)
// ===============================

struct EnergyStore;

bool saveEnergyStoreToFRAM(const EnergyStore& store);
bool loadEnergyStoreFromFRAM(EnergyStore& store);

struct EnergyResetInfo;

bool saveEnergyResetInfoToFRAM(const EnergyResetInfo& info);
bool loadEnergyResetInfoFromFRAM(EnergyResetInfo& info);

// Energy of the last 24 h (EnergyHourSlot × ENERGY_HOUR_SLOTS, EnergyHourStart); checksum set inside.
// load*: a slot / start with a bad checksum comes back with hour_idx = 0 (= not valid).
struct EnergyHourSlot;
struct EnergyHourStart;

bool saveEnergyHourSlotToFRAM(uint8_t slot, const EnergyHourSlot& s);
bool loadEnergyHourSlotsFromFRAM(EnergyHourSlot* slots);   // ENERGY_HOUR_SLOTS entries
bool saveEnergyHourStartToFRAM(const EnergyHourStart& s);
bool loadEnergyHourStartFromFRAM(EnergyHourStart& s);

// ===============================
// RING BUFFERS — godzinowy/dobowy (TempAvgRecord) i zdarzeń alarmowych
// (AlarmEvent), patrz fram_constants.h i algorithm_config.h
// ===============================

struct TempAvgRecord;
struct AlarmEvent;

bool     saveHourlyRecord(const TempAvgRecord& rec);
uint16_t loadHourlyHistory(TempAvgRecord* buf, uint16_t maxCount);  // newest-first
uint16_t getHourlyRecordCount();

bool     saveDailyRecord(const TempAvgRecord& rec);
uint16_t loadDailyHistory(TempAvgRecord* buf, uint16_t maxCount);   // newest-first
uint16_t getDailyRecordCount();

bool     saveAlarmEvent(const AlarmEvent& ev);
uint16_t loadAlarmEvents(AlarmEvent* buf, uint16_t maxCount);       // newest-first
uint16_t getAlarmEventCount();

// Czyści bufor (count=0, wptr=0) — dane pod spodem zostają w FRAM, ale są
// logicznie niewidoczne. Używane przez /api/test/clear-history.
void clearHourlyHistory();
void clearDailyHistory();
void clearAlarmEvents();

// ===============================
// Lock-PIN edycji GUI (2026-07-15) — checksum wbudowany w struct, jak EnergyStore.
// ===============================

struct LockPin;

bool saveLockPinToFRAM(const LockPin& pin);
bool loadLockPinFromFRAM(LockPin& pin);                       // złą checksumę traktuje jako "brak PIN-u" — lazy-init do "1234"

#endif
