#include "energy_meter.h"
#include "hardware_pins.h"
#include "hardware/fram_controller.h"
#include "core/logging.h"

// TODO: Etap 2c — implementacja

static volatile uint32_t s_pulse_count    = 0;
static uint32_t          s_day_start      = 0;
static uint16_t          s_pulses_per_kwh = 1000;
static uint32_t          s_last_pulse_ms  = 0;
static uint32_t          s_prev_pulse_ms  = 0;

void IRAM_ATTR onEnergyPulse() {
    s_prev_pulse_ms = s_last_pulse_ms;
    s_last_pulse_ms = millis();
    s_pulse_count++;
}

void initEnergyMeter(uint16_t pulses_per_kwh) {
    s_pulses_per_kwh = pulses_per_kwh;
    pinMode(ENERGY_PULSE_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(ENERGY_PULSE_PIN), onEnergyPulse, FALLING);
    energyLoadFromFRAM();
    LOG_INFO("EnergyMeter: init, ppkwh=%d", s_pulses_per_kwh);
}

void updateEnergyMeter() {
    // TODO: zapis do FRAM co 10 impulsów
}

void  setEnergyPulsesPerKwh(uint16_t ppk) { s_pulses_per_kwh = ppk; }

float getEnergyKwhTotal() {
    uint32_t count;
    noInterrupts(); count = s_pulse_count; interrupts();
    return (float)count / s_pulses_per_kwh;
}

float getEnergyKwhToday() {
    uint32_t count;
    noInterrupts(); count = s_pulse_count; interrupts();
    return (float)(count - s_day_start) / s_pulses_per_kwh;
}

uint32_t getEnergyPulseCount() {
    uint32_t count;
    noInterrupts(); count = s_pulse_count; interrupts();
    return count;
}

float getPowerWatt() {
    // moc z czasu między ostatnimi dwoma impulsami
    if (s_prev_pulse_ms == 0 || s_last_pulse_ms == s_prev_pulse_ms) return 0.0f;
    uint32_t dt_ms = s_last_pulse_ms - s_prev_pulse_ms;
    if (dt_ms == 0) return 0.0f;
    // 1 imp = 1 Wh (przy 1000 imp/kWh) → moc = 3600000 / (dt_ms * ppkwh / 1000)
    return 3600000.0f / ((float)dt_ms * s_pulses_per_kwh / 1000.0f);
}

void energyResetDayCounter() {
    noInterrupts(); s_day_start = s_pulse_count; interrupts();
    energySaveToFRAM();
}

void energyLoadFromFRAM() { /* TODO: Etap 4e */ }
void energySaveToFRAM()   { /* TODO: Etap 4e */ }
