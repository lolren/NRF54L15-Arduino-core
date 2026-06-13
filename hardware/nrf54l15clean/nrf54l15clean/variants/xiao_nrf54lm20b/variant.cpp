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

extern "C" void initVariant(void)
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

    // The onboard PY25Q64 flash is not used for code execution. Keep it in
    // deep power-down by default so simple low-power sketches are not charged
    // for an awake external flash. XiaoQspiFlash.begin() wakes it again.
    (void)xiaoNrf54lm20QspiFlashPrepareForSleep();
    
    // PMIC-controlled rails are intentionally opt-in. Sketches that need the
    // IMU/MIC rail should include npm1300.h and enable the required LDO.
}
