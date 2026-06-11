/*
 * XIAO nRF54LM20A variant initialization.
 *
 * Sets up:
 * - RGB LED as output (all off initially)
 * - Button as input with pull-up
 * - System clocks already configured by SystemInit()
 */

#include "variant.h"
#include "Arduino.h"

void initVariant(void)
{
    // Initialize RGB LED pins as outputs (off = HIGH for active-low)
    pinMode(PIN_LED_RED, OUTPUT);
    pinMode(PIN_LED_BLUE, OUTPUT);
    pinMode(PIN_LED_GREEN, OUTPUT);
    
    digitalWrite(PIN_LED_RED, HIGH);
    digitalWrite(PIN_LED_BLUE, HIGH);
    digitalWrite(PIN_LED_GREEN, HIGH);
    
    // Initialize button as input with pull-up
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    
    // Quick LED flash to confirm boot
    digitalWrite(PIN_LED_GREEN, LOW);
    delay(50);
    digitalWrite(PIN_LED_GREEN, HIGH);
    
    // PMIC-controlled rails are intentionally opt-in. Sketches that need the
    // IMU/MIC rail should include npm1300.h and enable the required LDO.
}
