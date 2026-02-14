/*
 * SerialBridge01
 *
 * Bridges USB/console Serial <-> Serial1 (D6 TX, D7 RX).
 *
 * Usage:
 * - Open Serial Monitor on USB Serial at 115200.
 * - Connect external UART device to D6/D7, or loopback D6->D7 for self-test.
 */

void setup()
{
  Serial.begin(115200);
  Serial1.begin(115200);

  delay(250);
  Serial.println("serial-bridge: Serial <-> Serial1 active @115200");
}

void loop()
{
  while (Serial.available() > 0) {
    Serial1.write(static_cast<uint8_t>(Serial.read()));
  }

  while (Serial1.available() > 0) {
    Serial.write(static_cast<uint8_t>(Serial1.read()));
  }
}
