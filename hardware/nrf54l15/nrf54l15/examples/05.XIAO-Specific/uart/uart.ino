/*
  UART sample: Serial <-> Serial1 bridge.
  Serial1 pins on XIAO nRF54L15: TX=D6, RX=D7
*/

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);
  delay(1000);

  Serial.println("XIAO nRF54L15 UART bridge sample");
  Serial.println("Type in Serial Monitor to forward to Serial1.");
}

void loop() {
  while (Serial.available() > 0) {
    Serial1.write(static_cast<uint8_t>(Serial.read()));
  }

  while (Serial1.available() > 0) {
    Serial.write(static_cast<uint8_t>(Serial1.read()));
  }
}
