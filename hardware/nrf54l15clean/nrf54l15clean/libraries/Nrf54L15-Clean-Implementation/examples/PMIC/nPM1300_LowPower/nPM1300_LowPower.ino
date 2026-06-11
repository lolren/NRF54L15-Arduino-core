/*
 * nPM1300 Ultra-Low Power Demo
 *
 * Demonstrates how to use the nPM1300 for battery-operated sensor nodes:
 *   - Power-gate the IMU and microphone via LDO1 between readings
 *   - Read battery voltage while sensors are off
 *   - Enter ship mode (sub-µA) on button long-press
 *
 * Ship mode disables PMIC outputs. Wake depends on the board PMIC wake
 * wiring; validate your battery setup before relying on ship mode.
 *
 * LEDs:
 *   Green  = IMU/MIC powered (LDO1 ON)
 *   Blue   = measuring battery
 *   Red    = low battery warning, or shipping in 3...2...1...
 *
 * Hardware:  XIAO nRF54LM20A
 */

#include <Arduino.h>
#include "npm1300.h"

void setup() {
    pinMode(LED_RED,   OUTPUT); digitalWrite(LED_RED,   HIGH);
    pinMode(LED_GREEN, OUTPUT); digitalWrite(LED_GREEN, HIGH);
    pinMode(LED_BLUE,  OUTPUT); digitalWrite(LED_BLUE,  HIGH);
    pinMode(PIN_BUTTON, INPUT_PULLUP);
}

void loop() {
    // Power-ON sequence. On XIAO nRF54LM20A, LDO1 feeds IMU&MIC_3V3.
    npm1300_imu_mic_power_enable(true);
    digitalWrite(LED_GREEN, LOW);  // green = IMU/MIC rail ON
    delay(50);                     // let LDO ramp

    // Read the IMU or microphone here.
    // e.g. Wire1.begin();  readWHOAMI();  Wire1.end();

    delay(100);

    // Battery measurement before shutting the sensor rail down.
    digitalWrite(LED_BLUE, LOW);   // blue = measuring
    int32_t vbat = npm1300_read_vbat_mv();
    digitalWrite(LED_BLUE, HIGH);

    // Low battery warning.
    if (vbat > 0 && vbat < 3600) {
        digitalWrite(LED_RED, LOW);   // red = low battery
    } else {
        digitalWrite(LED_RED, HIGH);
    }

    // Power-OFF: gate sensors to save current between measurements.
    npm1300_imu_mic_power_enable(false);
    digitalWrite(LED_GREEN, HIGH);

    // Check button for ship mode.
    if (digitalRead(PIN_BUTTON) == LOW) {
        unsigned long start = millis();
        while (digitalRead(PIN_BUTTON) == LOW) {
            if (millis() - start > 3000) {   // 3-second hold
                // Countdown on red LED
                for (int i = 0; i < 3; i++) {
                    digitalWrite(LED_RED, LOW);  delay(200);
                    digitalWrite(LED_RED, HIGH); delay(200);
                }
                npm1300_enter_ship_mode();
                // We never reach here — PMIC cuts all power
            }
        }
    }

    delay(5000);  // duty-cycle the sensor: 5 s off between readings
}
