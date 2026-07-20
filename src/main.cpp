#include <Arduino.h>
#include <Wire.h>
#include "core/logging.h"
#include "config/config.h"
#include "config/credentials_manager.h"
#include "hardware/rtc_controller.h"
#include "hardware/fram_controller.h"
#include "hardware/hardware_pins.h"
#include "hardware/temp_sensor.h"
#include "hardware/heater_controller.h"
#include "hardware/fan_controller.h"
#include "hardware/energy_meter.h"
#include "hardware/buzzer_controller.h"
#include "network/wifi_manager.h"
#include "security/auth_manager.h"
#include "security/session_manager.h"
#include "security/rate_limiter.h"
#include "web/web_server.h"
#include "algorithm/temp_algorithm.h"
#include "provisioning/prov_detector.h"
#include "provisioning/ap_core.h"
#include "provisioning/ap_server.h"

static uint32_t g_restartAtTs = 0;
static unsigned long g_lastTempUpdate = 0;
static const unsigned long TEMP_MEASURE_MS = (unsigned long)TEMP_MEASURE_INTERVAL_S * 1000UL;

void setup() {
    delay(1500);
    initLogging();
    initBuzzer();
    buzzerPowerOn();

    LOG_INFO("====================================");
    LOG_INFO("=== Thermo Control — Akwarium 240L ===");
    LOG_INFO("ESP32-S3-Pico, DS3231+STS35 I2C, FRAM SPI");
    LOG_INFO("Flash: %d  Heap: %d", ESP.getFlashChipSize(), ESP.getFreeHeap());
    LOG_INFO("Checking provisioning button...");

    if (checkProvisioningButton()) {
        LOG_INFO("=== PROVISIONING MODE ===");

        if (!initFRAM()) {  // FRAM na SPI — credentials
            LOG_INFO("WARNING: FRAM init failed — credentials may not save");
            buzzerCritical();
            delay(2000);
        }

        if (!startAccessPoint() || !startDNSServer()) {
            LOG_INFO("FATAL: AP start failed — halting");
            while (1) delay(1000);
        }

        LOG_INFO("SSID: ESP32-THERMO-SETUP  pass: setup12345");
        LOG_INFO("URL: http://%s", getAPIPAddress().toString().c_str());

        if (!startWebServer()) {
            while (1) delay(1000);
        }

        buzzerOK();
        runProvisioningLoop();  // nie wraca
    }

    // === PRODUCTION MODE ===
    LOG_INFO("=== Production Mode — Thermo Control ===");

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);    // I2C0: DS3231
    Wire.setClock(400000);
    Wire1.begin(STS35_SDA_PIN, STS35_SCL_PIN); // I2C1: STS35 na długim kablu
    Wire1.setClock(100000);
    delay(50);

    initializeRTC();
    initSystemFRAM();  // FRAM na SPI — init + header check

    bool credentials_loaded = initCredentialsManager();
    LOG_INFO("Device ID: %s", credentials_loaded ? getDeviceID() : "FALLBACK");

    pinMode(RESERVE_PIN, INPUT_PULLUP);  // pin rezerwowy, sąsiaduje z HEATER_SSR_PIN — patrz hardware_pins.h

    initTempSensor();
    initHeaterController();
    initFanController();
    initEnergyMeter(DEFAULT_PULSES_PER_KWH);
    thermoAlgorithm.begin();

    initWiFi();
    initAuthManager();
    initSessionManager();
    initRateLimiter();
    initWebServer();

    // Zaplanuj restart na następną lokalną północ
    uint32_t nowTs = (uint32_t)getUnixTimestamp();
    struct tm t;
    time_t ts = (time_t)nowTs;
    localtime_r(&ts, &t);
    if (isRTCWorking() && t.tm_year > 120) {
        uint32_t secOfDay = (uint32_t)(t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec);
        g_restartAtTs = nowTs - secOfDay + 86400UL;
        LOG_INFO("Daily restart at ts=%lu (za %lus)", g_restartAtTs, g_restartAtTs - nowTs);
    }

    LOG_INFO("RTC: %s  |  %s", isRTCWorking() ? "OK" : "FAIL", getCurrentTimestamp().c_str());
    LOG_INFO("Thermo state: %s", thermoAlgorithm.getStateString());
    if (isWiFiConnected()) LOG_INFO("Dashboard: http://%s", getLocalIP().toString().c_str());

    bool bootOk = isFramInitialized() && credentials_loaded && isRTCWorking();
    bootOk ? buzzerOK() : buzzerCritical();

    LOG_INFO("=== Init complete ===");
}

void loop() {
    unsigned long now = millis();

    // Codzienny restart o północy (minimalny uptime: 30 min)
    if (g_restartAtTs > 0 && now > 1800000UL) {
        if ((uint32_t)getUnixTimestamp() >= g_restartAtTs) {
            LOG_INFO("=== DAILY RESTART ===");
            heaterEmergencyOff();
            energyResetDayCounter();  // nowa doba — zeruje "dziś" i zapisuje stan do FRAM
            delay(1000);
            ESP.restart();
        }
    }

    // Pomiar temperatury co TEMP_MEASURE_INTERVAL_S
    if (now - g_lastTempUpdate >= TEMP_MEASURE_MS) {
        g_lastTempUpdate = now;
        updateTempSensor();
        thermoAlgorithm.update();  // steruje aktuatorami + loguje zdarzenia do FRAM ring buffera

        checkSystemAutoEnable();
    }

    // 100ms tick — sieć, bezpieczeństwo i granica godziny/doby dla logowania.
    // checkLogRollover() musi być poza 15s cyklu update() — restart o lokalnej
    // północy (patrz wyżej) jest sprawdzany co pętlę i mógłby wygrać wyścig
    // z detekcją granicy doby, gdyby ta była gated na TEMP_MEASURE_MS.
    static unsigned long lastTick = 0;
    if (now - lastTick >= 100) {
        lastTick = now;
        updateSessionManager();
        updateRateLimiter();
        updateWiFi();
        updateEnergyMeter();
        updateRTCSync();
        thermoAlgorithm.checkLogRollover();
    }

    delay(10);
}
