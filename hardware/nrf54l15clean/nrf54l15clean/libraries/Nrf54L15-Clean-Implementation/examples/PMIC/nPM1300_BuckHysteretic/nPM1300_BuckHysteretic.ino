/*
 * nPM1300 Buck Hysteretic Mode Demo
 *
 * Demonstrates the three buck converter operating modes and their trade-offs:
 *
 *   AUTO (default)  — Hysteretic at light load, auto-switches to PWM at heavy load.
 *                      Best balance of efficiency and ripple for most use cases.
 *
 *   FORCE_HYST      — Forced Hysteretic mode. Lowest quiescent current (~1 µA).
 *                      Higher output ripple, but ideal for SYSTEM OFF / sleep.
 *                      Use when the MCU is in deep sleep and load is < 1 mA.
 *
 *   FORCE_PWM       — Forced PWM mode. Lowest output ripple, best for RF/analog.
 *                      Higher quiescent current. Use when ADC, radio, or audio
 *                      are active and clean power is critical.
 *
 * The sketch cycles through all three modes every 5 seconds and prints
 * the current mode + battery voltage to Serial (115200 baud).
 *
 * Hardware:  XIAO nRF54LM20B (requires nPM1300 PMIC)
 */

#include <Arduino.h>
#include "npm1300.h"

static const char* modeName(uint8_t mode) {
    switch (mode) {
        case NPM1300_BUCK_MODE_AUTO:       return "AUTO";
        case NPM1300_BUCK_MODE_FORCE_HYST: return "FORCE_HYST";
        case NPM1300_BUCK_MODE_FORCE_PWM:  return "FORCE_PWM";
        default:                           return "?";
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("=== nPM1300 Buck Hysteretic Mode Demo ===");
    Serial.println("Cycling AUTO → FORCE_HYST → FORCE_PWM every 5s");
    Serial.println();
    Serial.println("Mode           | VBAT    | Description");
    Serial.println("---------------|---------|---------------------------");
}

void loop() {
    static uint8_t mode = NPM1300_BUCK_MODE_AUTO;
    static unsigned long lastSwitch = 0;

    if (millis() - lastSwitch > 5000) {
        lastSwitch = millis();

        // Cycle mode: AUTO → HYST → PWM → AUTO ...
        mode = (mode + 1) % 3;
        if (mode == 0) mode = NPM1300_BUCK_MODE_AUTO;  // wrap

        npm1300_buck1_set_mode(mode);

        int32_t vbat = npm1300_read_vbat_mv();

        Serial.print(modeName(mode));
        Serial.print("           | ");  // pad
        if (vbat >= 0) {
            Serial.print(vbat);
            Serial.print(" mV  | ");
        } else {
            Serial.print("---     | ");
        }

        switch (mode) {
            case NPM1300_BUCK_MODE_AUTO:
                Serial.println("auto-switch: hysteretic @ light load, PWM @ heavy");
                break;
            case NPM1300_BUCK_MODE_FORCE_HYST:
                Serial.println("lowest IQ (~1 uA), higher ripple — best for sleep");
                break;
            case NPM1300_BUCK_MODE_FORCE_PWM:
                Serial.println("lowest ripple, higher IQ — best for RF/analog/audio");
                break;
        }
    }
}
