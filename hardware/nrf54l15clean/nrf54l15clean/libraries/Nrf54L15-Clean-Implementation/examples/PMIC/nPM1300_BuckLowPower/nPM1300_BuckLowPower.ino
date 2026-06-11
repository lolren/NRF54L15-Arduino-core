/*
 * nPM1300 Buck Low-Power Mode
 *
 * Shows how to combine the nPM1300 buck hysteretic mode with nRF54L
 * SYSTEM OFF for the absolute lowest system power consumption.
 *
 * Strategy:
 *   1. Before entering SYSTEM OFF, force buck to HYSTERETIC mode.
 *      This drops the buck's quiescent current from ~30 µA (PWM)
 *      to ~1 µA (hysteretic).
 *   2. The PMIC register settings persist while the MCU is OFF.
 *   3. On wake, the MCU boots fresh. The buck mode stays in
 *      hysteretic unless you explicitly change it back.
 *
 * This sketch blinks the red LED every 2 seconds to confirm the
 * wake/sleep cycle is working. Between blinks it enters SYSTEM OFF.
 *
 * HARDWARE REQUIREMENT: XIAO nRF54LM20B with battery on VBAT.
 * The board MUST be battery-powered during SYSTEM OFF — USB VBUS
 * keeps the PMIC awake and defeats the measurement.
 *
 * To measure: insert a µCurrent meter in series with the battery.
 * Expect ~2-3 µA total system current in SYSTEM OFF with HYST mode.
 */

#include <Arduino.h>
#include <nrf54l15_hal.h>
#include "npm1300.h"

using namespace xiao_nrf54l15;

void setup() {
    // LED on P1.22 (red) — active low
    NRF_P1->DIRSET = (1UL << 22);
    NRF_P1->OUTSET = (1UL << 22);

    // Enable buck1 in forced hysteretic for lowest sleep current
    npm1300_buck1_enable(true);
    npm1300_buck1_set_mode(NPM1300_BUCK_MODE_FORCE_HYST);

    // Optional: print status on Serial (only works if USB is connected,
    // which defeats the low-power measurement — remove for production).
    Serial.begin(115200);
    delay(200);
    Serial.println("Buck low-power: FORCE_HYST mode active.");
    Serial.println("Entering SYSTEM OFF loop...");
}

void loop() {
    // Quick LED pulse to confirm we're alive
    NRF_P1->OUTCLR = (1UL << 22);  // LED ON
    delay(5);
    NRF_P1->OUTSET = (1UL << 22);  // LED OFF

    // Re-apply hysteretic mode (safe no-op if already set)
    npm1300_buck1_set_mode(NPM1300_BUCK_MODE_FORCE_HYST);

    // Enter SYSTEM OFF with 2-second wake timer.
    // Uses the PowerManager API — same as LowPowerZephyrParityBlink.
    PowerManager pm;
    pm.systemOffTimedWakeUsNoRetention(2000000UL);
}
