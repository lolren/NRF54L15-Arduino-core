/*
 * nPM1300 Buck FORCE_HYST Mode + Low-Power Blink
 *
 * The board's BUCK2 system rail forced to HYSTERETIC mode. This minimizes
 * converter quiescent current but has higher ripple and reduced load capacity.
 * Use only for controlled low-load SYSTEM OFF measurements; use AUTO for
 * normal applications.
 *
 * Blink:  5 ms LED on every 2 seconds via SYSTEM OFF wake.
 * LED:    P1.22 (red) active-low on XIAO nRF54LM20A.
 *
 * HARDWARE: XIAO nRF54LM20A on battery power.
 *           Remove USB before measuring current.
 */

#include <Arduino.h>
#include <nrf54l15_hal.h>
#include "npm1300.h"

using namespace xiao_nrf54l15;

void setup() {
    NRF_P1->DIRSET = (1UL << 22);
    NRF_P1->OUTSET = (1UL << 22);

    // Mode-only control cannot disable or change the system-rail voltage.
    npm1300_system_buck_set_mode(NPM1300_BUCK_MODE_FORCE_HYST);
}

void loop() {
    NRF_P1->OUTCLR = (1UL << 22);   // LED ON
    delay(5);
    NRF_P1->OUTSET = (1UL << 22);   // LED OFF

    // Re-apply before each SYSTEM OFF (safe no-op if already set)
    npm1300_system_buck_set_mode(NPM1300_BUCK_MODE_FORCE_HYST);

    PowerManager pm;
    pm.systemOffTimedWakeUsNoRetention(2000000UL);
}
