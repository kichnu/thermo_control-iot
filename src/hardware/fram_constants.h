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

// 0x0500–0x054B: System config + metadane 3 ringów
// Uwaga: ThermoConfig realnie 44B (9 float+2×u16+2×u8, padded), EnergyStore
// realnie 12B (checksum wbudowany w struct) — static_assert w algorithm_config.h.
// (2026-07-14: ThermoConfig zyskało trend_window_min, 40B→44B — adresy poniżej
// przesunięte o +4B, stary zapis odrzuci się przez checksum mismatch.)
#define FRAM_ADDR_THERMO_CONFIG     0x0500  // ThermoConfig (44B)
#define FRAM_ADDR_THERMO_CFG_CHKSUM 0x052C  // 2B checksum
#define FRAM_ADDR_ENERGY_STORE      0x052E  // EnergyStore (12B, checksum wbudowany)

// Metadane ring bufferów (count/wptr/checksum, 6B każdy — wzorzec jak w
// pierwotnym ring bufferze event-driven, patrz calculateChecksum(wptr+count))
#define FRAM_ADDR_HOURLY_COUNT      0x053A  // 2B
#define FRAM_ADDR_HOURLY_WPTR       0x053C  // 2B
#define FRAM_ADDR_HOURLY_META_CHKSUM 0x053E // 2B
#define FRAM_ADDR_DAILY_COUNT       0x0540  // 2B
#define FRAM_ADDR_DAILY_WPTR        0x0542  // 2B
#define FRAM_ADDR_DAILY_META_CHKSUM 0x0544  // 2B
#define FRAM_ADDR_ALARM_COUNT       0x0546  // 2B
#define FRAM_ADDR_ALARM_WPTR        0x0548  // 2B
#define FRAM_ADDR_ALARM_META_CHKSUM 0x054A  // 2B
// 0x054C–0x05FF: gap (180B rezerwa)

// 0x0600–0x06BF: Ring godzinowy — HOURLY_BUFFER_CAPACITY(24) × 8B = 192B
// rolling 24h, nadpisywany co godzinę (nawet niepełną — patrz pamięć projektu,
// restart o północy gwarantuje ≥1 niepełną godzinę dziennie). sizeof(TempAvgRecord)
// to logicznie 6B (4B+2B), ale kompilator dopełnia do 8B — przyjęte wprost.
#define FRAM_ADDR_HOURLY_BUFFER     0x0600
#define HOURLY_RECORD_SIZE          8       // sizeof(TempAvgRecord), padded

// 0x06C0–0x122F: Ring dobowy — DAILY_BUFFER_CAPACITY(366) × 8B = 2928B
// średnia z 24 rekordów godzinowych, zapisywana przy zmianie doby lokalnej
#define FRAM_ADDR_DAILY_BUFFER      0x06C0
#define DAILY_RECORD_SIZE           8       // sizeof(TempAvgRecord), padded

// 0x1230–0x1B8F: Ring zdarzeń alarmowych — ALARM_BUFFER_CAPACITY(300) × 8B = 2400B,
// jeden wpis na fakt wystąpienia alarmu (bez end/duration, patrz AlarmEvent)
#define FRAM_ADDR_ALARM_BUFFER      0x1230
#define ALARM_RECORD_SIZE           8       // sizeof(AlarmEvent)

// 0x1B90–0x1CA1: C-09 (odstępstwo od temp. zadanej) + lock-PIN edycji GUI
// (2026-07-15) — patrz LockPin/TempDeviationState/DeviationReasonList w
// algorithm_config.h. Reszta wolnego bloku (~862B) zostaje rezerwą.
#define FRAM_ADDR_LOCK_PIN           0x1B90  // LockPin (12B)
#define FRAM_ADDR_DEVIATION_STATE    0x1BA0  // TempDeviationState (12B)
#define FRAM_ADDR_DEVIATION_REASONS  0x1BB0  // DeviationReasonList (242B)

// 0x1CA2–0x1CA9: EnergyResetInfo (8B) — timestamp kasowania licznika total
// (2026-07-18), osobno od EnergyStore (0x052E) żeby nie przesuwać layoutu
#define FRAM_ADDR_ENERGY_RESET        0x1CA2  // EnergyResetInfo (8B)

// 0x1CAA–0x1FFF: wolne (~854B rezerwa)

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
