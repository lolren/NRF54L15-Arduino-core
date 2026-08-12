/*
 * nPM1300 Buck FORCE_PWM Mode + Low-Power Blink
 *
 * The board's BUCK2 system rail forced to PWM mode. This minimizes output
 * ripple at the cost of higher quiescent current. Use when clean power matters
 * more than battery life, then restore AUTO mode when finished.
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
    npm1300_system_buck_set_mode(NPM1300_BUCK_MODE_FORCE_PWM);
}

void loop() {
    NRF_P1->OUTCLR = (1UL << 22);   // LED ON
    delay(5);
    NRF_P1->OUTSET = (1UL << 22);   // LED OFF

    npm1300_system_buck_set_mode(NPM1300_BUCK_MODE_FORCE_PWM);

    PowerManager pm;
    pm.systemOffTimedWakeUsNoRetention(2000000UL);
}
