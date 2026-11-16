#include "energy_meter.h"
#include "hardware_pins.h"
#include "hardware/fram_controller.h"
#include "hardware/rtc_controller.h"
#include "core/logging.h"

static volatile uint32_t s_pulse_count    = 0;
static uint32_t          s_day_start      = 0;
static uint16_t          s_pulses_per_kwh = 1000;
static uint32_t          s_last_pulse_ms  = 0;
static uint32_t          s_prev_pulse_ms  = 0;
static uint32_t          s_last_saved_count = 0;  // do progu zapisu co N impulsów
static uint32_t          s_reset_ts       = 0;    // Unix UTC ostatniego kasowania licznika total

static const uint32_t ENERGY_SAVE_EVERY_N_PULSES = 10;

// Energy of the last 24 full hours (see EnergyHourSlot): RAM copy of the FRAM slots, cached sum
static EnergyHourSlot s_slots[ENERGY_HOUR_SLOTS];
static uint32_t       s_hour_idx    = 0;   // current UTC hour; 0 = not started (no valid time yet)
static uint32_t       s_hour_start  = 0;   // pulse count at its start
static uint32_t       s_sum24_wh    = 0;   // last 24 full hours

// Min. odstęp między akceptowanymi impulsami — odrzuca zbocza fizycznie
// niemożliwe dla licznika S0 (drgania styków / szum EMI z sąsiadującego kabla
// 230V, patrz pamięć projektu). 250ms = maks. 4 imp/s = ok. 14,4 kW przy
// pulses_per_kwh=1000 — wciąż duży zapas ponad realną moc grzałki/pompy.
// Podniesione z 50ms 2026-07-25: podwójne zliczanie pojedynczego impulsu
// (wolne, zaszumione zbocze narastające po impulsie) dawało pary odległe
// o ok. 100ms, więc 50ms filtra tego nie łapało — patrz log debug w
// updateEnergyMeter().
static const uint32_t MIN_PULSE_INTERVAL_MS = 250;

void IRAM_ATTR onEnergyPulse() {
    uint32_t now = millis();
    if (s_last_pulse_ms != 0 && (now - s_last_pulse_ms) < MIN_PULSE_INTERVAL_MS) {
        return;  // zbyt szybko od poprzedniego — szum, nie realny impuls
    }
    s_prev_pulse_ms = s_last_pulse_ms;
    s_last_pulse_ms = now;
    s_pulse_count++;
}

void initEnergyMeter(uint16_t pulses_per_kwh) {
    s_pulses_per_kwh = pulses_per_kwh;
    pinMode(ENERGY_PULSE_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(ENERGY_PULSE_PIN), onEnergyPulse, FALLING);
    energyLoadFromFRAM();
    LOG_INFO("EnergyMeter: init, ppkwh=%d", s_pulses_per_kwh);
}

void pauseEnergyPulseInterrupt() {
    detachInterrupt(digitalPinToInterrupt(ENERGY_PULSE_PIN));
}

void resumeEnergyPulseInterrupt() {
    attachInterrupt(digitalPinToInterrupt(ENERGY_PULSE_PIN), onEnergyPulse, FALLING);
}

static uint16_t pulsesToWh(uint32_t pulses) {
    if (!s_pulses_per_kwh) return 0;
    uint32_t wh = (pulses * 1000UL + s_pulses_per_kwh / 2) / s_pulses_per_kwh;
    return wh > 0xFFFF ? 0xFFFF : (uint16_t)wh;
}

// Full hours cur-24 .. cur-1 (the current hour is not complete yet)
static void recomputeSum24(uint32_t cur) {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < ENERGY_HOUR_SLOTS; i++) {
        uint32_t h = s_slots[i].hour_idx;
        if (h && h < cur && cur - h <= ENERGY_HOUR_SLOTS) sum += s_slots[i].wh;
    }
    s_sum24_wh = sum;
}

static void closeHour(uint32_t hour, uint32_t pulses) {
    uint8_t slot = hour % ENERGY_HOUR_SLOTS;
    s_slots[slot] = EnergyHourSlot{hour, pulsesToWh(pulses), 0};
    saveEnergyHourSlotToFRAM(slot, s_slots[slot]);
}

static void startHour(uint32_t hour, uint32_t count) {
    s_hour_idx = hour;
    s_hour_start = count;
    saveEnergyHourStartToFRAM(EnergyHourStart{hour, count, 0, 0});
}

// Hour boundary on the UTC clock — only with a valid DS3231 time (no compile-time fallback hours).
// Runs in the 100 ms tick before the midnight restart check, so the last hour is closed first.
static void updateEnergyHours(uint32_t count) {
    uint32_t ts = (uint32_t)getUnixTimestamp();
    if (!isUnixTimeValid()) return;
    uint32_t h = ts / 3600UL;
    if (h == s_hour_idx) return;

    if (s_hour_idx == 0) {
        // After boot: same hour → keep its start; an hour that ended during the restart / power-off →
        // close it with the pulses counted until the restart
        EnergyHourStart st;
        bool have = loadEnergyHourStartFromFRAM(st) && st.hour_idx;
        if (have && st.hour_idx == h) {
            s_hour_idx = h;
            s_hour_start = st.start_pulses <= count ? st.start_pulses : count;
            recomputeSum24(h);
            return;
        }
        if (have && st.hour_idx < h && h - st.hour_idx <= ENERGY_HOUR_SLOTS && count >= st.start_pulses)
            closeHour(st.hour_idx, count - st.start_pulses);
    } else if (h > s_hour_idx) {
        closeHour(s_hour_idx, count >= s_hour_start ? count - s_hour_start : count);
    }   // clock went back (resync): the partial hour is dropped
    startHour(h, count);
    recomputeSum24(h);
    LOG_INFO("EnergyMeter: last 24 h %u Wh", s_sum24_wh);
}

void updateEnergyMeter() {
    uint32_t count;
    noInterrupts(); count = s_pulse_count; interrupts();

    if (count - s_last_saved_count >= ENERGY_SAVE_EVERY_N_PULSES) {
        energySaveToFRAM();
    }
    updateEnergyHours(count);
}

float    getEnergyKwh24h() { return s_sum24_wh / 1000.0f; }
uint32_t getEnergyWh24h()  { return s_sum24_wh; }

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

uint32_t getEnergyResetTimestamp() { return s_reset_ts; }

void energyResetTotalCounter() {
    noInterrupts(); s_pulse_count = 0; interrupts();
    s_day_start = 0;
    if (s_hour_idx) startHour(s_hour_idx, 0);   // the current hour restarts from the zeroed counter
    s_reset_ts  = (uint32_t)getUnixTimestamp();
    energySaveToFRAM();

    EnergyResetInfo rinfo;
    rinfo.reset_ts   = s_reset_ts;
    rinfo._reserved  = 0;
    rinfo.checksum   = 0;  // przeliczane wewnątrz saveEnergyResetInfoToFRAM()
    saveEnergyResetInfoToFRAM(rinfo);

    LOG_INFO("EnergyMeter: total counter reset, reset_ts=%u", s_reset_ts);
}

void energyLoadFromFRAM() {
    EnergyStore store;
    if (!loadEnergyStoreFromFRAM(store)) {
        LOG_WARNING("EnergyStore not found/invalid in FRAM — starting from 0");
        s_last_saved_count = 0;
    } else {
        noInterrupts();
        s_pulse_count = store.total_pulses;
        interrupts();
        s_day_start = store.day_start_pulses;
        if (store.pulses_per_kwh > 0) s_pulses_per_kwh = store.pulses_per_kwh;
        s_last_saved_count = store.total_pulses;

        LOG_INFO("EnergyStore loaded from FRAM: total_pulses=%u day_start=%u ppkwh=%u",
                 store.total_pulses, store.day_start_pulses, s_pulses_per_kwh);
    }

    EnergyResetInfo rinfo;
    if (loadEnergyResetInfoFromFRAM(rinfo)) {
        s_reset_ts = rinfo.reset_ts;
    }

    if (!loadEnergyHourSlotsFromFRAM(s_slots)) memset(s_slots, 0, sizeof(s_slots));
}

void energySaveToFRAM() {
    uint32_t count;
    noInterrupts(); count = s_pulse_count; interrupts();

    EnergyStore store;
    store.total_pulses     = count;
    store.day_start_pulses = s_day_start;
    store.pulses_per_kwh   = s_pulses_per_kwh;
    store.checksum         = 0;  // przeliczane wewnątrz saveEnergyStoreToFRAM()

    if (saveEnergyStoreToFRAM(store)) {
        s_last_saved_count = count;
    }
}
