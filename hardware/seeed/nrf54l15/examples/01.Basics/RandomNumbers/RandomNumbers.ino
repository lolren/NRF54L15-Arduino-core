#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(200);
  randomSeed(micros());
}

void loop() {
  long r = random(0, 1000);
  Serial.println(r);
  delay(250);
}
