/*
 * I2C Scanner Example
 *
 * This example scans the I2C bus for connected devices.
 * It reports the 7-bit address of each device found.
 *
 * Wiring:
 *   SDA -> D4
 *   SCL -> D5
 *
 * Licensed under the Apache License 2.0
 */

#include <Wire.h>

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        delay(10);
    }

    Serial.println("I2C Scanner");
    Serial.println("============");
    Serial.println();

    // Initialize I2C
    Wire.begin();

    Serial.println("Scanning...");
}

void loop() {
    int nDevices = 0;

    Serial.println("Scanning I2C bus...");

    for (byte address = 1; address < 127; address++) {
        // Try to communicate with the device
        Wire.beginTransmission(address);
        byte error = Wire.endTransmission();

        if (error == 0) {
            // Device found
            Serial.print("  I2C device found at address 0x");
            if (address < 16) {
                Serial.print("0");
            }
            Serial.print(address, HEX);
            Serial.println("  !");

            nDevices++;
        } else if (error == 4) {
            // Error
            Serial.print("  Unknown error at address 0x");
            if (address < 16) {
                Serial.print("0");
            }
            Serial.println(address, HEX);
        }
    }

    if (nDevices == 0) {
        Serial.println("No I2C devices found");
    } else {
        Serial.println();
        Serial.print("Found ");
        Serial.print(nDevices);
        Serial.println(" device(s)");
    }

    Serial.println();
    Serial.println("Done scanning. Waiting 5 seconds...");
    Serial.println();

    delay(5000);
}
