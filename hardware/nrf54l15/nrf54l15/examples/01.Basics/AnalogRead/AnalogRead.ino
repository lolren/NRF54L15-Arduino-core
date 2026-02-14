/*
 * Analog Read Example
 *
 * This example demonstrates reading analog inputs on the nRF54L15.
 *
 * The XIAO nRF54L15 has 8 analog inputs (A0-A7).
 * Connect a potentiometer or voltage source to A0.
 *
 * Licensed under the Apache License 2.0
 */

// Analog input pin
const int ANALOG_PIN = A0;

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        delay(10);
    }

    Serial.println("Analog Read Example");
    Serial.println("===================");
    Serial.println();

    // Set analog read resolution (optional, default is 10-bit / 0-1023)
    // Options: 8, 10, 12, or 14 bits
    analogReadResolution(10);

    Serial.print("Reading analog input on pin ");
    Serial.print(ANALOG_PIN);
    Serial.println("...");
    Serial.println();
}

void loop() {
    // Read the analog input
    int value = analogRead(ANALOG_PIN);

    // Calculate voltage (assuming 3.3V reference, scaled through internal divider)
    // Note: The actual voltage calculation depends on the SAADC configuration
    float voltage = (value * 3.3) / 1023.0;

    // Print the results
    Serial.print("Analog value: ");
    Serial.print(value);
    Serial.print("  Voltage: ");
    Serial.print(voltage, 3);
    Serial.println(" V");

    delay(100);
}
