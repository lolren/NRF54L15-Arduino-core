/*
 * SerialConfig
 *
 * Demonstrates Arduino-style serial format selection:
 *   Serial.begin(baud, SERIAL_8N1 / SERIAL_7E1 / SERIAL_8O1 ...)
 */

void setup() {
  Serial.begin(115200, SERIAL_8N1);
  delay(200);

  Serial.println("serial-config: ready");
  Serial.println("using Serial.begin(115200, SERIAL_8N1)");
  Serial.println("change the second argument to test other formats");
}

void loop() {
  Serial.print("uptime-ms=");
  Serial.println(millis());
  delay(1000);
}
