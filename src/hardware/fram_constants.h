#ifndef FRAM_CONSTANTS_H
#define FRAM_CONSTANTS_H

// ============================================================
// SHARED FRAM CONSTANTS — Thermo Control
// MB85RC256V, I2C 0x50, 32KB (0x0000–0x7FFF)
// ============================================================

#define FRAM_MAGIC_NUMBER       0x54484D4F  // "THMO" in hex
#define FRAM_DATA_VERSION       0x0001

// ============================================================
// MEMORY LAYOUT
// ============================================================

// 0x0000–0x0017: Header (24B)
#define FRAM_ADDR_MAGIC         0x0000      // 4B
#define FRAM_ADDR_VERSION       0x0004      // 2B
// 0x0006–0x0017: reserved

// 0x0018–0x0417: Encrypted credentials (1024B) — bez zmian ze struktury ATO
#define FRAM_CREDENTIALS_ADDR   0x0018
#define FRAM_CREDENTIALS_SIZE   1024

// 0x0500–0x054B: System data
#define FRAM_ADDR_THERMO_CONFIG     0x0500  // ThermoConfig (40B)
#define FRAM_ADDR_THERMO_CFG_CHKSUM 0x0528  // 2B checksum
#define FRAM_ADDR_ENERGY_STORE      0x0530  // EnergyStore (14B)
#define FRAM_ADDR_ENERGY_CHKSUM     0x053E  // 2B checksum
#define FRAM_ADDR_BUF_COUNT         0x0540  // 2B — liczba zapisanych rekordów
#define FRAM_ADDR_BUF_WPTR          0x0542  // 2B — write pointer (0–2399)
#define FRAM_ADDR_BUF_META_CHKSUM   0x0544  // 2B checksum metadanych

// 0x0600–0x767F: Ring buffer (2400 × 12B = 28800B)
#define FRAM_ADDR_RING_BUFFER       0x0600
#define RING_BUFFER_CAPACITY        2400    // rekordów
#define RING_RECORD_SIZE            12      // bajtów (sizeof(TempRecord))
// koniec bufora: 0x0600 + 28800 = 0x767F

// 0x7680–0x7FFF: wolne (2432B)

// ============================================================
// ENCRYPTION (credentials — bez zmian z ATO)
// ============================================================
#define ENCRYPTION_SALT     "ESP32_FRAM_SALT_2024"
#define ENCRYPTION_SEED     "THERMO_SYSTEM_SEED_V1"

// ============================================================
// FIELD SIZES (credentials — bez zmian z ATO)
// ============================================================
#define MAX_DEVICE_NAME_LEN     31
#define MAX_WIFI_SSID_LEN       63
#define MAX_WIFI_PASSWORD_LEN   127
#define MAX_VPS_TOKEN_LEN       255
#define MAX_VPS_URL_LEN         100

// ============================================================
// THERMO CONFIG MAGIC
// ============================================================
#define THERMO_CONFIG_MAGIC     0xA9    // is_configured field value

#endif // FRAM_CONSTANTS_H
