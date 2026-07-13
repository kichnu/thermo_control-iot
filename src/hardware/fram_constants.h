#ifndef FRAM_CONSTANTS_H
#define FRAM_CONSTANTS_H

// ============================================================
// SHARED FRAM CONSTANTS — Thermo Control
// MB85RS64V, SPI (CS = FRAM_SPI_CS_PIN), 8KB (0x0000–0x1FFF)
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

// 0x0500–0x055D: System data
// Uwaga: ThermoConfig realnie 44B (10 float+u16+2×u8), EnergyStore realnie 12B
// (checksum wbudowany w struct) — poprawione static_assert w algorithm_config.h,
// dawne komentarze "40B"/"16B" były błędne od commitu Etap 0.
#define FRAM_ADDR_THERMO_CONFIG     0x0500  // ThermoConfig (44B)
#define FRAM_ADDR_THERMO_CFG_CHKSUM 0x052C  // 2B checksum
#define FRAM_ADDR_LOG_ZONE_CONFIG   0x052E  // LogZoneConfig (20B) — progi LogZone z GUI
                                             // (2026-07-13: usunięte martwe low_step_3/
                                             // high_step_3, było 28B — adresy poniżej
                                             // przesunięte, stary zapis odrzuci się przez
                                             // checksum mismatch i wróci do defaultów)
#define FRAM_ADDR_LOG_ZONE_CHKSUM   0x0542  // 2B checksum
// 0x0544–0x054B: gap (8B rezerwa, po zmniejszeniu LogZoneConfig)
#define FRAM_ADDR_ENERGY_STORE      0x054C  // EnergyStore (12B, checksum wbudowany)
#define FRAM_ADDR_BUF_COUNT         0x0558  // 2B — liczba zapisanych rekordów
#define FRAM_ADDR_BUF_WPTR          0x055A  // 2B — write pointer (0–799)
#define FRAM_ADDR_BUF_META_CHKSUM   0x055C  // 2B checksum metadanych
// 0x055E–0x05FF: gap (162B rezerwa)

// 0x0600–0x1EFF: Ring buffer, event-driven (zmiana strefy/heartbeat/boot,
// nie periodic co 30 min — patrz pamięć projektu fram_event_logging)
// 800 × 8B = 6400B. Pokrycie zmienne (zależne od częstości zdarzeń):
// ~800 dni gdy stabilnie (sam heartbeat), ~16,6 doby w najgorszym razie
// (ciągłe flapowanie co 30 min = min. odstęp między zdarzeniami).
#define FRAM_ADDR_RING_BUFFER       0x0600
#define RING_BUFFER_CAPACITY        800     // rekordów
#define RING_RECORD_SIZE            8       // bajtów (sizeof(TempRecord))
// koniec bufora: 0x0600 + 6400 = 0x1EFF

// 0x1F00–0x1FFF: wolne (256B rezerwa)

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
