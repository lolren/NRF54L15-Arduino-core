#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  Serial.println("Serial echo ready");
}

void loop() {
  if (Serial.available()) {
    int c = Serial.read();
    Serial.write((uint8_t)c);
  }
}
