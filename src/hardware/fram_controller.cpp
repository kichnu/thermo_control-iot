#include "fram_controller.h"
#include "../core/logging.h"
#include "../hardware/hardware_pins.h"
#include <SPI.h>
#include <Adafruit_FRAM_SPI.h>
#include "../crypto/fram_encryption.h"
#include "../algorithm/algorithm_config.h"

Adafruit_FRAM_SPI fram(FRAM_SPI_CS_PIN, &SPI, 1000000);
bool framInitialized = false;

// MB85RS64V (SPI FRAM) requires WREN before every write — the write-enable
// latch auto-clears after each write cycle, so it must be re-asserted each time.
static bool framWrite(uint32_t addr, const void* data, size_t len) {
    fram.writeEnable(true);
    return fram.write(addr, (const uint8_t*)data, len);
}

bool isFramInitialized() {
    return framInitialized;
}

bool framReconnect() {
    SPI.begin(FRAM_SPI_SCK_PIN, FRAM_SPI_MISO_PIN, FRAM_SPI_MOSI_PIN, FRAM_SPI_CS_PIN);
    if (!fram.begin()) {
        framInitialized = false;
        LOG_ERROR("framReconnect: FRAM not found on SPI");
        return false;
    }
    framInitialized = true;
    LOG_INFO("framReconnect: FRAM re-initialized on SPI bus");
    return true;
}

bool initFRAM() {
    LOG_INFO("");
    LOG_INFO("Initializing FRAM (SPI, MB85RS64V)...");

    SPI.begin(FRAM_SPI_SCK_PIN, FRAM_SPI_MISO_PIN, FRAM_SPI_MOSI_PIN, FRAM_SPI_CS_PIN);

    if (!fram.begin()) {
        LOG_ERROR("");
        LOG_ERROR("FRAM not found on SPI!");
        framInitialized = false;
        return false;
    }

    framInitialized = true;
    LOG_INFO("");
    LOG_INFO("FRAM initialized successfully (MB85RS64V, 8KB)");

    if (!verifyFRAM()) {
        LOG_WARNING("");
        LOG_WARNING("FRAM magic/version mismatch — reinitializing header");

        uint32_t magic = FRAM_MAGIC_NUMBER;
        framWrite(FRAM_ADDR_MAGIC, &magic, 4);

        uint16_t version = FRAM_DATA_VERSION;
        framWrite(FRAM_ADDR_VERSION, &version, 2);

        LOG_INFO("FRAM header written");
    }

    return true;
}

bool verifyFRAM() {
    if (!framInitialized) return false;

    uint32_t magic = 0;
    fram.read(FRAM_ADDR_MAGIC, (uint8_t*)&magic, 4);

    if (magic != FRAM_MAGIC_NUMBER) {
        LOG_WARNING("");
        LOG_WARNING("FRAM magic number mismatch: 0x%08X", magic);
        return false;
    }

    uint16_t version = 0;
    fram.read(FRAM_ADDR_VERSION, (uint8_t*)&version, 2);

    if (version != FRAM_DATA_VERSION) {
        LOG_WARNING("");
        LOG_WARNING("FRAM version mismatch: %d (expected %d)", version, FRAM_DATA_VERSION);
        return false;
    }

    LOG_INFO("");
    LOG_INFO("FRAM verification successful (v%d)", FRAM_DATA_VERSION);
    return true;
}

// ===============================
// FRAM CREDENTIALS SECTION
// (Shared with FRAM Programmer)
// ===============================

bool readCredentialsFromFRAM(FRAMCredentials& creds) {
    if (!framInitialized) {
        LOG_ERROR("");
        LOG_ERROR("FRAM not initialized for credentials read");
        return false;
    }

    fram.read(FRAM_CREDENTIALS_ADDR, (uint8_t*)&creds, sizeof(FRAMCredentials));
    LOG_INFO("");
    LOG_INFO("Read credentials from FRAM at address 0x%04X", FRAM_CREDENTIALS_ADDR);
    return true;
}

bool writeCredentialsToFRAM(const FRAMCredentials& creds) {
    if (!framInitialized) {
        LOG_ERROR("");
        LOG_ERROR("FRAM not initialized for credentials write");
        return false;
    }

    for (int attempt = 0; attempt < 2; attempt++) {
        if (attempt > 0) {
            LOG_WARNING("FRAM credentials write: retrying after verification mismatch (attempt %d)", attempt + 1);
            if (!framReconnect()) {
                LOG_ERROR("FRAM re-init failed during credentials write recovery");
                return false;
            }
        }

        framWrite(FRAM_CREDENTIALS_ADDR, &creds, sizeof(FRAMCredentials));

        FRAMCredentials verify_creds;
        fram.read(FRAM_CREDENTIALS_ADDR, (uint8_t*)&verify_creds, sizeof(FRAMCredentials));

        if (memcmp(&creds, &verify_creds, sizeof(FRAMCredentials)) == 0) {
            LOG_INFO("");
            LOG_INFO("Credentials written to FRAM at address 0x%04X", FRAM_CREDENTIALS_ADDR);
            return true;
        }

        LOG_WARNING("FRAM write verification mismatch (attempt %d)", attempt + 1);
    }

    LOG_ERROR("");
    LOG_ERROR("FRAM credentials write failed after retry");
    return false;
}

bool verifyCredentialsInFRAM() {
    if (!framInitialized) {
        LOG_ERROR("");
        LOG_ERROR("FRAM not initialized for credentials verify");
        return false;
    }

    FRAMCredentials creds;
    if (!readCredentialsFromFRAM(creds)) {
        return false;
    }

    if (creds.magic != FRAM_MAGIC_NUMBER) {
        LOG_WARNING("");
        LOG_WARNING("Invalid credentials magic number: 0x%08X", creds.magic);
        return false;
    }

    // Accept v1, v2, v3 — historical credential struct versions
    if (creds.version != 0x0001 && creds.version != 0x0002 && creds.version != 0x0003) {
        LOG_WARNING("");
        LOG_WARNING("Invalid credentials version: %d", creds.version);
        return false;
    }

    size_t checksum_offset = offsetof(FRAMCredentials, checksum);
    uint16_t calculated_checksum = calculateChecksum((uint8_t*)&creds, checksum_offset);

    if (creds.checksum != calculated_checksum) {
        LOG_WARNING("");
        LOG_WARNING("Credentials checksum mismatch: stored=%d, calculated=%d",
                    creds.checksum, calculated_checksum);
        return false;
    }
    LOG_INFO("");
    LOG_INFO("Credentials verification successful (version %d)", creds.version);
    return true;
}

// ===============================
// THERMO CONFIG SECTION
// ===============================

bool saveThermoConfigToFRAM(const ThermoConfig& cfg) {
    if (!framInitialized) return false;

    framWrite(FRAM_ADDR_THERMO_CONFIG, &cfg, sizeof(ThermoConfig));

    uint16_t chksum = calculateChecksum((const uint8_t*)&cfg, sizeof(ThermoConfig));
    framWrite(FRAM_ADDR_THERMO_CFG_CHKSUM, &chksum, 2);

    LOG_INFO("ThermoConfig saved: target=%.1f heat_hyst=%.1f cool_start=%.1f cool_full=%.1f",
             cfg.target_temp, cfg.heat_hyst, cfg.cool_start, cfg.cool_full);
    return true;
}

bool loadThermoConfigFromFRAM(ThermoConfig& cfg) {
    if (!framInitialized) return false;

    ThermoConfig tmp;
    fram.read(FRAM_ADDR_THERMO_CONFIG, (uint8_t*)&tmp, sizeof(ThermoConfig));

    uint16_t stored = 0;
    fram.read(FRAM_ADDR_THERMO_CFG_CHKSUM, (uint8_t*)&stored, 2);

    uint16_t calc = calculateChecksum((const uint8_t*)&tmp, sizeof(ThermoConfig));
    if (calc != stored) {
        LOG_WARNING("ThermoConfig checksum mismatch — using defaults");
        return false;
    }

    cfg = tmp;
    return true;
}

// ===============================
// LOG ZONE CONFIG SECTION
// ===============================

bool saveLogZoneConfigToFRAM(const LogZoneConfig& cfg) {
    if (!framInitialized) return false;

    framWrite(FRAM_ADDR_LOG_ZONE_CONFIG, &cfg, sizeof(LogZoneConfig));

    uint16_t chksum = calculateChecksum((const uint8_t*)&cfg, sizeof(LogZoneConfig));
    framWrite(FRAM_ADDR_LOG_ZONE_CHKSUM, &chksum, 2);

    LOG_INFO("LogZoneConfig saved: normal=%.1f low=%.1f/%.1f high=%.1f/%.1f",
             cfg.normal_delta, cfg.low_step_1, cfg.low_step_2,
             cfg.high_step_1, cfg.high_step_2);
    return true;
}

bool loadLogZoneConfigFromFRAM(LogZoneConfig& cfg) {
    if (!framInitialized) return false;

    LogZoneConfig tmp;
    fram.read(FRAM_ADDR_LOG_ZONE_CONFIG, (uint8_t*)&tmp, sizeof(LogZoneConfig));

    uint16_t stored = 0;
    fram.read(FRAM_ADDR_LOG_ZONE_CHKSUM, (uint8_t*)&stored, 2);

    if (calculateChecksum((const uint8_t*)&tmp, sizeof(LogZoneConfig)) != stored) {
        LOG_WARNING("LogZoneConfig checksum mismatch — using defaults");
        return false;
    }

    cfg = tmp;
    return true;
}

// ===============================
// ENERGY STORE SECTION (checksum wbudowany w struct, jak FRAMCredentials)
// ===============================

bool saveEnergyStoreToFRAM(const EnergyStore& store) {
    if (!framInitialized) return false;

    EnergyStore tmp = store;
    size_t checksum_offset = offsetof(EnergyStore, checksum);
    tmp.checksum = calculateChecksum((const uint8_t*)&tmp, checksum_offset);

    framWrite(FRAM_ADDR_ENERGY_STORE, &tmp, sizeof(EnergyStore));

    LOG_INFO("EnergyStore saved: total_pulses=%u day_start=%u ppkwh=%u",
             tmp.total_pulses, tmp.day_start_pulses, tmp.pulses_per_kwh);
    return true;
}

bool loadEnergyStoreFromFRAM(EnergyStore& store) {
    if (!framInitialized) return false;

    EnergyStore tmp;
    fram.read(FRAM_ADDR_ENERGY_STORE, (uint8_t*)&tmp, sizeof(EnergyStore));

    size_t checksum_offset = offsetof(EnergyStore, checksum);
    uint16_t calc = calculateChecksum((const uint8_t*)&tmp, checksum_offset);
    if (calc != tmp.checksum) {
        LOG_WARNING("EnergyStore checksum mismatch — using defaults (0)");
        return false;
    }

    store = tmp;
    return true;
}

// ================================================================
// RING BUFFER — TempRecord, event-driven (800 × 8B)
// ================================================================

static_assert(sizeof(TempRecord) == RING_RECORD_SIZE,
    "TempRecord size mismatch! Update RING_RECORD_SIZE in fram_constants.h");

// Metadane (count/wptr) chronione osobną checksumą — świeża/pusta FRAM ma
// losowe bajty, bez tego pierwszy odczyt potrafiłby zwrócić śmieciowy count.
static bool readRingMeta(uint16_t& count, uint16_t& wptr) {
    fram.read(FRAM_ADDR_BUF_COUNT, (uint8_t*)&count, 2);
    fram.read(FRAM_ADDR_BUF_WPTR,  (uint8_t*)&wptr,  2);

    uint8_t metabuf[4];
    memcpy(metabuf,     &wptr,  2);
    memcpy(metabuf + 2, &count, 2);
    uint16_t stored = 0;
    fram.read(FRAM_ADDR_BUF_META_CHKSUM, (uint8_t*)&stored, 2);

    if (calculateChecksum(metabuf, 4) != stored) {
        count = 0;
        wptr  = 0;
        return false;
    }
    return true;
}

static void writeRingMeta(uint16_t count, uint16_t wptr) {
    framWrite(FRAM_ADDR_BUF_WPTR,  &wptr,  2);
    framWrite(FRAM_ADDR_BUF_COUNT, &count, 2);

    uint8_t metabuf[4];
    memcpy(metabuf,     &wptr,  2);
    memcpy(metabuf + 2, &count, 2);
    uint16_t chksum = calculateChecksum(metabuf, 4);
    framWrite(FRAM_ADDR_BUF_META_CHKSUM, &chksum, 2);
}

bool saveTempRecord(const TempRecord& rec) {
    if (!framInitialized) return false;

    uint16_t count = 0, wptr = 0;
    readRingMeta(count, wptr);  // checksum invalid → count/wptr=0, traktujemy jako pusty bufor
    if (wptr >= RING_BUFFER_CAPACITY) wptr = 0;

    uint16_t addr = FRAM_ADDR_RING_BUFFER + wptr * RING_RECORD_SIZE;
    framWrite(addr, &rec, RING_RECORD_SIZE);

    wptr = (wptr + 1) % RING_BUFFER_CAPACITY;
    if (count < RING_BUFFER_CAPACITY) count++;

    writeRingMeta(count, wptr);
    return true;
}

uint16_t loadTempHistory(TempRecord* buf, uint16_t maxCount) {
    if (!framInitialized || !buf) return 0;

    uint16_t count = 0, wptr = 0;
    readRingMeta(count, wptr);
    if (count > RING_BUFFER_CAPACITY) count = RING_BUFFER_CAPACITY;
    if (count > maxCount) count = maxCount;
    if (count == 0) return 0;

    // Newest-first: najnowszy rekord jest w slocie tuż przed wptr.
    for (uint16_t i = 0; i < count; i++) {
        uint16_t slot = (wptr + RING_BUFFER_CAPACITY - 1 - i) % RING_BUFFER_CAPACITY;
        uint16_t addr = FRAM_ADDR_RING_BUFFER + slot * RING_RECORD_SIZE;
        fram.read(addr, (uint8_t*)&buf[i], RING_RECORD_SIZE);
    }
    return count;
}

uint16_t getTempRecordCount() {
    if (!framInitialized) return 0;
    uint16_t count = 0, wptr = 0;
    readRingMeta(count, wptr);
    return (count > RING_BUFFER_CAPACITY) ? RING_BUFFER_CAPACITY : count;
}
