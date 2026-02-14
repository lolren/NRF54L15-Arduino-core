/*
 * Blink Example for XIAO nRF54L15
 *
 * Turns the built-in LED on for 500 ms, then off for 500 ms, repeatedly.
 *
 * This example code is in the public domain.
 */

// The built-in LED is connected to P2.00 (D16)
// It is active-low (on when the pin is LOW)

void setup() {
    // Initialize the LED pin as an output
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
    digitalWrite(LED_BUILTIN, HIGH);  // Turn LED off (active-low)
    delay(500);                      // Wait 500 ms
    digitalWrite(LED_BUILTIN, LOW);   // Turn LED on (active-low)
    delay(500);                      // Wait 500 ms
}
