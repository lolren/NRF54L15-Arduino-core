/*
 * Serial Echo Example for XIAO nRF54L15
 *
 * Echoes characters received on Serial back to the sender.
 *
 * This example code is in the public domain.
 */

void setup() {
    // Initialize serial communication at 115200 baud
    Serial.begin(115200);

    // Wait for serial port to connect (useful for USB CDC)
    while (!Serial) {
        ; // wait for serial port to connect
    }

    Serial.println("XIAO nRF54L15 Serial Echo Example");
    Serial.println("Type something and press Enter...");
}

void loop() {
    // Check if data is available
    if (Serial.available() > 0) {
        // Read the incoming byte
        int incomingByte = Serial.read();

        // Echo it back
        Serial.write(incomingByte);
    }
}
