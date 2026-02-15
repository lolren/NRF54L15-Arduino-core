#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
}

void loop() {
  int raw = analogRead(PIN_A0);
  Serial.print("A0=");
  Serial.println(raw);
  delay(250);
}
