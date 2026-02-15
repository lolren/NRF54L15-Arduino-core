/*
  Expansion-base Grove relay sample.
*/

#ifndef RELAY_PIN
#define RELAY_PIN PIN_D0
#endif

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
}

void loop() {
  digitalWrite(RELAY_PIN, HIGH);
  delay(1000);
  digitalWrite(RELAY_PIN, LOW);
  delay(1000);
}
