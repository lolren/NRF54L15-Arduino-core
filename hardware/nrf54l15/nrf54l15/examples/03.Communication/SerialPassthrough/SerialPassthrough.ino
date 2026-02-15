#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);
}

void loop() {
  if (Serial.available()) {
    Serial1.write((uint8_t)Serial.read());
  }

  if (Serial1.available()) {
    Serial.write((uint8_t)Serial1.read());
  }
}
