/*
  XIAO nRF54L15 board test: analog read + serial monitor output.

  Wiring (potentiometer):
  - Middle pin -> A0
  - Outer pin  -> 3V3
  - Outer pin  -> GND

  Serial Monitor: 9600 baud
*/

void setup() {
  Serial.begin(9600);
  delay(1000);
  Serial.println("XIAO nRF54L15 AnalogReadSerial test");
}

void loop() {
  const int sensorValue = analogRead(A0);
  Serial.println(sensorValue);
  delay(10);
}

