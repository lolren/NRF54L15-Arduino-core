/*
 * SPI Digital Potentiometer Example
 *
 * This example demonstrates SPI communication using the nRF54L15.
 * It controls a MCP4101/4111 digital potentiometer (or compatible).
 *
 * Wiring for MCP4101:
 *   CS   -> D2 (or use manual chip select)
 *   SCK  -> D8
 *   MOSI -> D10
 *   MISO -> D9
 *   VCC  -> 3.3V
 *   GND  -> GND
 *
 * Licensed under the Apache License 2.0
 */

#include <SPI.h>

// Chip select pin (can be any digital pin)
const int CS_PIN = PIN_SPI_SS;

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        delay(10);
    }

    Serial.println("SPI Digital Potentiometer Example");
    Serial.println("==================================");

    // Initialize SPI
    // Default: 4MHz, MSB first, SPI Mode 0
    SPI.begin();

    // Configure chip select
    pinMode(CS_PIN, OUTPUT);
    digitalWrite(CS_PIN, HIGH);  // Deselect
}

void loop() {
    // Sweep the potentiometer wiper from 0 to 255
    Serial.println("Sweeping up...");
    for (int i = 0; i <= 255; i++) {
        digitalPotWrite(i);
        delay(10);
    }

    delay(500);

    // Sweep the potentiometer wiper from 255 to 0
    Serial.println("Sweeping down...");
    for (int i = 255; i >= 0; i--) {
        digitalPotWrite(i);
        delay(10);
    }

    delay(500);
}

void digitalPotWrite(int value) {
    // Ensure value is in valid range
    if (value < 0) value = 0;
    if (value > 255) value = 255;

    // Select the chip
    digitalWrite(CS_PIN, LOW);

    // Send command and value
    // MCP4101 command byte: 0x11 = write to potentiometer 0
    SPI.transfer(0x11);
    SPI.transfer(value);

    // Deselect the chip
    digitalWrite(CS_PIN, HIGH);
}
