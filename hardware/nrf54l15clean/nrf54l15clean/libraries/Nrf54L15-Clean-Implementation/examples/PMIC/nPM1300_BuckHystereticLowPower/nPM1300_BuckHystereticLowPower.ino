/*
 * nPM1300 Buck FORCE_HYST Mode + Low-Power Blink
 *
 * BUCK1 forced to HYSTERETIC mode.  Lowest quiescent current (~1 µA buck IQ).
 * Higher output ripple, but ideal for SYSTEM OFF / deep sleep with < 1 mA load.
 *
 * Blink:  5 ms LED on every 2 seconds via SYSTEM OFF wake.
 * LED:    P1.22 (red) active-low on XIAO nRF54LM20B.
 *
 * HARDWARE: XIAO nRF54LM20B on battery power.
 *           Remove USB before measuring current.
 *           Expect ~2-3 µA total system current in SYSTEM OFF.
 */

#include <Arduino.h>
#include <nrf54l15_hal.h>
#include "npm1300.h"

using namespace xiao_nrf54l15;

void setup() {
    NRF_P1->DIRSET = (1UL << 22);
    NRF_P1->OUTSET = (1UL << 22);

    // Enable BUCK1 forced hysteretic — lowest IQ
    npm1300_buck1_enable(true);
    npm1300_buck1_set_mode(NPM1300_BUCK_MODE_FORCE_HYST);
}

void loop() {
    NRF_P1->OUTCLR = (1UL << 22);   // LED ON
    delay(5);
    NRF_P1->OUTSET = (1UL << 22);   // LED OFF

    // Re-apply before each SYSTEM OFF (safe no-op if already set)
    npm1300_buck1_set_mode(NPM1300_BUCK_MODE_FORCE_HYST);

    PowerManager pm;
    pm.systemOffTimedWakeUsNoRetention(2000000UL);
}
