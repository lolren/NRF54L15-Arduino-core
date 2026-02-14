#include <Arduino.h>

void setup() {
  pinMode(PIN_D6, OUTPUT);
  analogWriteResolution(8);
}

void loop() {
  for (int v = 0; v <= 255; ++v) {
    analogWrite(PIN_D6, v);
    delay(5);
  }

  for (int v = 255; v >= 0; --v) {
    analogWrite(PIN_D6, v);
    delay(5);
  }
}
