/*
 * nPM1300 Buck AUTO Mode + Low-Power Blink
 *
 * The board's BUCK2 system rail in AUTO mode (default): hysteretic at light
 * load, PWM at heavy load. Best balance for most use cases.
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
    // LED on P1.22 — active low
    NRF_P1->DIRSET = (1UL << 22);
    NRF_P1->OUTSET = (1UL << 22);

    // Keep the fixed 3.3 V system buck in its default AUTO mode.
    npm1300_system_buck_set_mode(NPM1300_BUCK_MODE_AUTO);
}

void loop() {
    NRF_P1->OUTCLR = (1UL << 22);   // LED ON
    delay(5);
    NRF_P1->OUTSET = (1UL << 22);   // LED OFF

    PowerManager pm;
    pm.systemOffTimedWakeUsNoRetention(2000000UL);
}
