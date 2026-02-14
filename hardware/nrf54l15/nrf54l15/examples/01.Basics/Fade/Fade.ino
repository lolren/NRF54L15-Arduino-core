/*
 * Fade Example (PWM)
 *
 * This example demonstrates PWM output using analogWrite().
 * It fades an LED connected to a PWM-capable pin.
 *
 * The XIAO nRF54L15 has PWM on pins D6, D7, D8, D9.
 *
 * Wiring:
 *   LED + resistor (220 ohm) -> D6
 *   LED cathode -> GND
 *
 * Licensed under the Apache License 2.0
 */

// PWM-capable pin
const int LED_PIN = 6;  // D6 is PWM capable

int brightness = 0;    // Current brightness
int fadeAmount = 5;    // How much to change brightness each step

void setup() {
    // No need to call pinMode for analogWrite
    // The PWM peripheral handles pin configuration

    Serial.begin(115200);
    Serial.println("PWM Fade Example");
    Serial.println("================");
}

void loop() {
    // Set the brightness
    analogWrite(LED_PIN, brightness);

    // Print the brightness to serial
    Serial.print("Brightness: ");
    Serial.println(brightness);

    // Change brightness for next time
    brightness = brightness + fadeAmount;

    // Reverse direction at the ends
    if (brightness <= 0 || brightness >= 255) {
        fadeAmount = -fadeAmount;
    }

    delay(30);
}
