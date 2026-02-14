#include <Arduino.h>

void setup() {
  pinMode(PIN_D7, OUTPUT);
}

void loop() {
  tone(PIN_D7, 523, 200);
  delay(250);
  tone(PIN_D7, 659, 200);
  delay(250);
  tone(PIN_D7, 784, 200);
  delay(500);
}
