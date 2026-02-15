/*
  Expansion-base buzzer sample.
*/

#ifndef BUZZER_PIN
#define BUZZER_PIN PIN_D0
#endif

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
}

void loop() {
  tone(BUZZER_PIN, 1000, 120);
  delay(220);
  tone(BUZZER_PIN, 1500, 120);
  delay(220);
  tone(BUZZER_PIN, 2000, 180);
  delay(500);
}
