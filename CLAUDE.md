# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Temperature control system for a 240L aquarium heater/fan, on Waveshare ESP32-S3-Pico
(PlatformIO, Arduino framework). Symmetric ON/OFF hysteresis for a heater (via SSR) and
fan (via PWM), STS35 temperature sensor, DS3231 RTC, SPI FRAM for persistent config/
history/alarms, energy usage estimation, web dashboard, captive portal provisioning,
buzzer alarms. Development/upload tooling additionally supports OTA (WiFi) and a
remote log socket — see "Dev tooling" below.

## Build Commands

```bash
# USB — default environment (default_envs in platformio.ini)
pio run -t upload
pio device monitor              # 115200 baud

# OTA (WiFi) — only works once a firmware containing ArduinoOTA is already
# running on the device (first flash of that firmware must be via USB)
# OTA_PASSWORD_192_168_10_4 set persistently via ~/.secrets/iot.env, see README
pio run -e seeed_xiao_esp32s3_ota -t upload

# Remote logs (no USB) — only works once firmware with the log-socket is running
pio device monitor --port socket://<device-ip>:8880

# Clean build
pio run -t clean
```

## Architecture

### Entry Point and Operation Modes

`src/main.cpp` implements two runtime modes determined at boot:

1. **Provisioning Mode** — activated by holding `RESET_PIN` (GPIO37) for 5+ seconds
   during boot. Starts a WiFi AP (`ESP32-WATER-SETUP` / `setup12345`,
   `src/provisioning/prov_config.h`) with captive portal for initial credential setup.
   (Boot log at `main.cpp:55` prints a different string, "ESP32-THERMO-SETUP" — that's
   a stale log message, not the real SSID; known cosmetic bug, not yet fixed.)

2. **Production Mode** — normal operation: temp algorithm, web server, OTA, log-socket.

### Core Modules (src/)

```
algorithm/          Temperature control algorithm
  algorithm_config.h   ThermoConfig, TempAvgRecord, AlarmEvent, ALARM_FLAG_*, DEFAULT_*
  temp_algorithm.*     TempAlgorithm class — heater/fan hysteresis, alarms, ring-buffer logging

config/             Configuration and credential management
  config.*             Global settings, system enable/disable, TRUSTED_PROXY_IP
  credentials_manager.*  Dynamic credential loading from FRAM

core/               Logging
  logging.*             LOG_INFO/WARNING/ERROR macros; also hosts the log-socket
                         (WiFiServer :8880, dev tooling — dublicates Serial to one TCP client)

crypto/             AES-256 encryption for FRAM credentials
  aes.*, sha256.*, fram_encryption.*

hardware/           Hardware abstraction layer
  buzzer_controller.*  FreeRTOS task (50ms tick); modes OFF/PROVISIONING/WARNING/ALARM
  fram_constants.h     FRAM magic/version/address map (SPI, 8KB)
  fram_controller.*    FRAM memory management (SPI, MB85RS64V)
  hardware_pins.h      GPIO pin definitions — source of truth for the pinout
  heater_controller.*  SSR control (active LOW)
  fan_controller.*     PWM fan control (LEDC)
  temp_sensor.*        STS35 I2C1 read + validity/fault tracking
  rtc_controller.*     DS3231 RTC with NTP sync
  energy_meter.*       Pulse-counter energy metering

network/            Network connectivity
  wifi_manager.*       WiFi connection with dynamic credentials

provisioning/       Captive portal for initial setup
  ap_core.*            Access point management
  ap_server.*          Web server for provisioning
  ap_handlers.*        HTTP request handlers
  ap_html.*            Embedded HTML pages
  credentials_validator.*  Input validation for provisioning form
  prov_config.h        Provisioning constants (button pin, hold time, AP SSID/password)
  prov_detector.*      Boot button detection
  wifi_scanner.*       Network scanning

security/           Authentication and rate limiting
  auth_manager.*       Password verification, trusted-proxy bypass (VPS/nginx)
  session_manager.*    Session token management
  rate_limiter.*       Request throttling

web/                Production web interface
  web_server.*         ESPAsyncWebServer setup, route registration
  web_handlers.*       API endpoint handlers (registerThermoHandlers)
  html_pages.*         Dashboard HTML (embedded)
```

### Temp Algorithm State Machine

Located in `algorithm/temp_algorithm.cpp`, states in `algorithm_config.h`:

```
STATE_IDLE ⇄ STATE_HEATING   (SSR ON below target - heat_hyst, OFF at target_temp)
STATE_IDLE ⇄ STATE_COOLING   (fan ON above fan_on_point = target + thermal_buffer,
                               OFF at fan_on_point - cool_start — see below)
STATE_ALARM_LOW / STATE_ALARM_HIGH  — temp outside [target - alarm_delta_low, fan_on_point + alarm_delta_high], self-heals
STATE_TREND_ALARM            — heater ON continuously ≥ trend_window_min without enough rise,
                                self-heals (fixed 2026-07-24 — used to stick in GUI, see code comment)
STATE_SENSOR_FAULT           — STS35 invalid past threshold, LATCHED — heater/fan
                                emergency-off, needs manual reset (resetSensorFaultLatch())
```

Fan uses a day/night thermal buffer (2026-07-24, replaced the old symmetric
"ON at target+cool_start, OFF at target_temp" scheme and the separate C-09
deviation feature, see `docs/FEATURES_REGISTRY.md` C-09/C-10): `fan_on_point =
target_temp + thermal_buffer` — lets daytime heat surplus accumulate instead of
immediately cooling it away, so the heater has less work at night. `cool_start`
is now a hysteresis width *below* `fan_on_point` (OFF at `fan_on_point -
cool_start`), `cool_full` a width *above* it (100% at `fan_on_point +
cool_full`), and `alarm_delta_high` is now measured from `fan_on_point` too
(not `target_temp`) — otherwise ALARM_HIGH could fire mid-ramp before the fan
even reaches 100%. Validated server-side (`web_handlers.cpp`):
`thermal_buffer >= cool_start` (else the OFF point would dip below
`target_temp` and fight the heater) and `cool_full < alarm_delta_high`.
Heater side (`heat_hyst`) unchanged. A planned follow-up (not yet
implemented): color the `#temp` GUI element as a quiet warning while inside
the buffer zone.

Key defaults in `algorithm_config.h`:
- `DEFAULT_TARGET_TEMP` 25.0°C, `DEFAULT_HEAT_HYST` 0.3°C, `DEFAULT_COOL_START` 0.3°C,
  `DEFAULT_COOL_FULL` 1.0°C, `DEFAULT_THERMAL_BUFFER` 1.0°C, `DEFAULT_FAN_MIN_PCT` 30%
- `DEFAULT_ALARM_DELTA_LOW` 2.0°C, `DEFAULT_ALARM_DELTA_HIGH` 1.5°C
- `DEFAULT_TREND_ALARM_DELTA` 0.2°C over `DEFAULT_TREND_WINDOW_MIN` 60 min

Design rationale (ON/OFF+SSR over PID, SSR over mechanical relay) in
`docs/ALGORITHM_DECISION.md` — still valid conceptually, but its code-snippet sections
(struct definitions) are stale; `algorithm_config.h` is the source of truth for those.

### FRAM Memory Layout

SPI (MB85RS64V, 8KB, 0x0000–0x1FFF). Defined in `hardware/fram_constants.h`:
- `0x0000–0x0017`: magic ("THMO") + version
- `0x0018–0x0417`: AES-256 encrypted credentials (WiFi, admin password hash, device ID)
- `0x0500–0x053F`: `ThermoConfig` (48B, grew from 44B 2026-07-24 — added `thermal_buffer`) +
  checksum, in a fixed 64B slot (~28% headroom reserved for future fields without
  reshuffling everything downstream again — see `fram_constants.h` comment)
- `0x0540–0x054B`: `EnergyStore`
- `0x054C–0x055D`: ring-buffer metadata (hourly/daily/alarm — count/wptr/checksum)
- `0x055E–0x05FF`: free (~63% of the config block, before the ring buffers below)
- `0x0600–0x06BF`: hourly ring (24 × 8B)
- `0x06C0–0x122F`: daily ring (366 × 8B)
- `0x1230–...`: alarm-event ring
- `0x1B90–0x1B9B`: `LockPin`
- `0x1BA0–0x1CA1`: free (was C-09 `TempDeviationState`/`DeviationReasonList`, removed 2026-07-24)
- `0x1CA2–0x1CA9`: `EnergyResetInfo`

### Hardware Pin Configuration (Waveshare ESP32-S3-Pico)

Defined in `hardware/hardware_pins.h` (source of truth — `docs/ARCHITECTURE_SKELETON.md`
§2 has an older, now-incorrect pin table, kept only as historical draft):

- GPIO 1: `BUZZER_PIN` — HIGH = ON
- GPIO 2: `FAN_PWM_PIN` — LEDC PWM
- GPIO 5/6: `I2C_SDA_PIN`/`I2C_SCL_PIN` — I2C0 (Wire), DS3231 RTC, 400kHz
- GPIO 7/8: `STS35_SDA_PIN`/`STS35_SCL_PIN` — I2C1 (Wire1), STS35, 100kHz, ~1m cable
- GPIO 9: `ENERGY_PULSE_PIN` — INPUT_PULLUP, ISR FALLING, no boot glitch
- GPIO 10-13: `FRAM_SPI_CS/MOSI/SCK/MISO_PIN` — default IO_MUX SPI2/FSPI
- GPIO 17: `HEATER_SSR_PIN` — **active LOW**
- GPIO 18: `RESERVE_PIN` — INPUT_PULLUP, physically adjacent to GPIO17 on the module
  footprint; explicitly initialized to avoid floating-pin crosstalk with the SSR pin
- GPIO 37: `RESET_PIN` — INPUT_PULLUP, active LOW, hold 5s → provisioning

### Key API Endpoints (Production Mode)

Registered in `web/web_server.cpp` + `web/web_handlers.cpp` (`registerThermoHandlers`).
All require session auth except `/api/health`.

```
POST /api/login | /api/logout
GET  /api/health
GET  /api/status                          full system status JSON
GET  /api/history-hourly | /api/history-daily
GET  /api/alarm-events
GET|POST /api/thermo-config               algorithm thresholds
POST /api/mute-alarm                      silence active buzzer, flags stay visible
POST /api/reset-sensor-fault              clears STATE_SENSOR_FAULT latch
POST /api/system-toggle                   Auto Mode / Service Mode (15-min auto re-enable)
GET|POST /api/energy | /api/energy-reset-day | /api/energy-reset-total
POST /api/test/fan | /api/test/heater     manual override (test)
POST /api/verify-pin                      lock-PIN gate for settings edits
```

### Dev tooling (OTA + remote logs)

Concept + implementation notes: `docs/DEV_UPLOAD_LOGGING_CLI_ARCHITECTURE.md`.

- `platformio.ini`: `[env:seeed_xiao_esp32s3_ota]` (`extends` the main env,
  `upload_protocol = espota`). `default_envs = seeed_xiao_esp32s3` keeps bare
  `pio run -t upload` on USB — without it, PlatformIO would try to build/upload
  *every* environment, including a doomed OTA attempt.
- `OTA_PASSWORD` (the firmware-side `#define`) is injected via
  `${sysenv.OTA_PASSWORD_192_168_10_4}` into both `build_flags` (firmware) and
  `upload_flags` (espota) — never hardcode it in `platformio.ini`. The shell
  variable is named after this device's local IP, not the project — see
  README for why (name collisions across the other IoT projects sharing
  `~/.secrets/iot.env`).
- `main.cpp`: `ArduinoOTA` set up after `initWiFi()`, gated on `isWiFiConnected()`.
  `onStart` calls `heaterEmergencyOff()`/`fanEmergencyOff()`/`setSystemState(false)`
  before the flash write. `ArduinoOTA.handle()` runs every `loop()` iteration
  (not the 100ms tick) — must stay responsive.
- Log-socket: `WiFiServer` on port 8880 in `logging.cpp`, one client at a time,
  duplicates every `Serial.printf` from `logInfo/logWarning/logError`. Started via
  `startLogServer()` alongside OTA; accepts new connections via `updateLogServer()`
  in the 100ms tick.
- CLI (bidirectional command line over the same socket) is a documented pattern for
  actuator-bearing devices but does **not** apply to this repo (no pump/doser here) —
  not implemented, see doc §3.

## Important Patterns

### Credential Loading

Credentials loaded at boot from AES-256 encrypted FRAM. Fallback hardcoded values exist
if FRAM empty/corrupted. Access via macros in `config.h`:
- `WIFI_SSID_DYNAMIC`, `WIFI_PASSWORD_DYNAMIC`
- `ADMIN_PASSWORD_HASH_DYNAMIC`, `DEVICE_ID_DYNAMIC`

(No `VPS_AUTH_TOKEN_DYNAMIC` in this repo — auth for the VPS reverse-proxy path uses
`TRUSTED_PROXY_IP` instead, see Security below.)

### System Disable/Enable

Global system control with **15-minute** auto-re-enable timeout
(`SYSTEM_AUTO_ENABLE_MS`, `config.h`). Functions:
- `setSystemState(bool enabled)`
- `isSystemDisabled()`
- `checkSystemAutoEnable()`

When disabled, `TempAlgorithm::update()` returns immediately — heater/fan stay under
manual/test control.

### POST Body Parameters

ESPAsyncWebServer `request->hasParam("name", true)` only parses
`application/x-www-form-urlencoded`. All POST handlers use form-encoded body — never
JSON for POST requests.

### Daily Restart (not a 24h-uptime timer)

`main.cpp` computes `g_restartAtTs` at boot = next **local midnight** (via RTC +
`localtime_r`), not "24 hours after boot". `loop()` restarts once
`getUnixTimestamp() >= g_restartAtTs` (guarded by a 30-minute minimum uptime).
Heater is forced off (`heaterEmergencyOff()`) and the daily energy counter reset
before `ESP.restart()`.

### Timezone Handling

Configured for Poland (CET/CEST) with automatic DST (`rtc_controller.cpp`,
`POLAND_TZ = "CET-1CEST,M3.5.0,M10.5.0/3"`). RTC stores UTC; display/schedule times
converted via `localtime_r()`. NTP servers are IP-literals (Google/Cloudflare) for
DNS independence. Periodic resync every `NTP_SYNC_INTERVAL` (1h) via `updateRTCSync()`.

### Init Order in `main.cpp`

`Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN)` (I2C0/DS3231) and `Wire1.begin(STS35_SDA_PIN,
STS35_SCL_PIN)` (I2C1/STS35) run before `initializeRTC()`/`initSystemFRAM()`. FRAM is
on SPI (independent bus, default IO_MUX pins) — unlike sibling ESP32 projects that use
I2C FRAM, there's no hard FRAM-after-Wire.begin() dependency here; the ordering in
`main.cpp` is just "sensors/RTC bus up before anything reads them".

## Security

Session cookie auth (`security/auth_manager.cpp`, `session_manager.cpp`), rate
limiting (`rate_limiter.cpp`) — constants in `config.h` (`SESSION_TIMEOUT_MS`,
`MAX_REQUESTS_PER_SECOND`, `MAX_FAILED_ATTEMPTS`, `BLOCK_DURATION_MS`).
`checkAuthentication()` bypasses the session-cookie check for requests whose
`remoteIP()` matches `TRUSTED_PROXY_IP` — this is the VPS/nginx reverse-proxy path
(`10.99.0.1`); see `docs/NETWORK_TOPOLOGY_ESP32.md` for the full topology and the
firmware-side gotchas found while integrating it (relative paths required under
`/device/thermo/`, trusted-proxy auth bypass implications).

## Dependencies (platformio.ini)

- ESPAsyncWebServer (mathieucarbou) ^3.3.23
- AsyncTCP (mathieucarbou) ^3.2.14
- ArduinoJson ^7.4.2
- RTClib ^2.1.1 (Adafruit)
- Adafruit FRAM **SPI** (not I2C — see FRAM Memory Layout above)

## Docs (`docs/`)

`FEATURES_REGISTRY.md` is the most actively maintained scope/roadmap reference.
`ARCHITECTURE_SKELETON.md` and `ALGORITHM_DECISION.md` are pre-implementation design
docs — read them for rationale, but treat any code snippet/pin table inside as a
historical snapshot, not current truth; the actual header files always win.
