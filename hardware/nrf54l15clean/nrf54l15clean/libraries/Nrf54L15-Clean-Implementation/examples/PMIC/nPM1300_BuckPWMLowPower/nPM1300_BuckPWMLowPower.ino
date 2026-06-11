/*
 * nPM1300 Buck FORCE_PWM Mode + Low-Power Blink
 *
 * BUCK1 forced to PWM mode.  Lowest output ripple — best for RF, ADC, audio.
 * Higher quiescent current (~30 µA buck IQ).  Use when clean power matters
 * more than battery life.
 *
 * Blink:  5 ms LED on every 2 seconds via SYSTEM OFF wake.
 * LED:    P1.22 (red) active-low on XIAO nRF54LM20B.
 *
 * HARDWARE: XIAO nRF54LM20B on battery power.
 *           Remove USB before measuring current.
 *           Expect ~30-40 µA total system current in SYSTEM OFF.
 */

#include <Arduino.h>
#include <nrf54l15_hal.h>
#include "npm1300.h"

using namespace xiao_nrf54l15;

void setup() {
    NRF_P1->DIRSET = (1UL << 22);
    NRF_P1->OUTSET = (1UL << 22);

    // Enable BUCK1 forced PWM — lowest ripple
    npm1300_buck1_enable(true);
    npm1300_buck1_set_mode(NPM1300_BUCK_MODE_FORCE_PWM);
}

void loop() {
    NRF_P1->OUTCLR = (1UL << 22);   // LED ON
    delay(5);
    NRF_P1->OUTSET = (1UL << 22);   // LED OFF

    npm1300_buck1_set_mode(NPM1300_BUCK_MODE_FORCE_PWM);

    PowerManager pm;
    pm.systemOffTimedWakeUsNoRetention(2000000UL);
}
