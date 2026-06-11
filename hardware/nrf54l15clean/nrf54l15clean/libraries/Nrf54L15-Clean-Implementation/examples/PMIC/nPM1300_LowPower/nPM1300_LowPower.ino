/*
 * nPM1300 Ultra-Low Power Demo
 * Demonstrates PMIC features for battery-powered sensors:
 * - LDO control for IMU/MIC power gating
 * - Battery voltage monitoring
 * - Ship mode entry on button long-press
 *
 * LED: Green = IMU on, Blue = measuring, Red = low battery
 */
#include <Arduino.h>
#include "npm1300.h"

void setup() {
    pinMode(LED_RED, OUTPUT); digitalWrite(LED_RED, HIGH);
    pinMode(LED_GREEN, OUTPUT); digitalWrite(LED_GREEN, HIGH);
    pinMode(LED_BLUE, OUTPUT); digitalWrite(LED_BLUE, HIGH);
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    
    npm1300_begin();
    
    // Quick boot blink
    digitalWrite(LED_GREEN, LOW); delay(100); digitalWrite(LED_GREEN, HIGH);
}

void loop() {
    // Power up IMU (LDO1)
    npm1300_ldo1_enable(true);
    digitalWrite(LED_GREEN, LOW);  // Green = IMU powered
    delay(10);
    
    // Measure battery
    int32_t vbat = npm1300_read_vbat_mv();
    digitalWrite(LED_BLUE, LOW);  // Blue = measuring
    
    // Low battery warning
    if (vbat > 0 && vbat < 3600) {
        digitalWrite(LED_RED, LOW);
    }
    
    delay(100);
    digitalWrite(LED_BLUE, HIGH);
    
    // Power down IMU between readings (save power)
    npm1300_ldo1_enable(false);
    digitalWrite(LED_GREEN, HIGH);
    
    // Check for button long-press → ship mode
    if (digitalRead(PIN_BUTTON) == LOW) {
        delay(3000);  // 3 second hold
        if (digitalRead(PIN_BUTTON) == LOW) {
            // Enter ship mode — lowest power
            digitalWrite(LED_RED, LOW); delay(500);
            npm1300_enter_ship_mode();
        }
    }
    
    delay(5000);  // Measure every 5 seconds
}
