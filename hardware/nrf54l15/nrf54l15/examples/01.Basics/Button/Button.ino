#include <Arduino.h>

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
}

void loop() {
  int pressed = digitalRead(PIN_BUTTON) == LOW;
  digitalWrite(LED_BUILTIN, pressed ? HIGH : LOW);
  delay(10);
}
