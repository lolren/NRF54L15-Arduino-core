#include <Arduino.h>

/*
  Low-power style sample: keep CPU mostly idle with long sleep intervals.
  For deeper reductions, combine with lower radio activity and external hardware setup.
*/

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  delay(1000);
  Serial.println("XIAO nRF54L15 lowpower sample");
}

void loop() {
  Serial.print("awake, uptime(ms)=");
  Serial.println(millis());

  digitalWrite(LED_BUILTIN, LOW);
  delay(40);
  digitalWrite(LED_BUILTIN, HIGH);

  delay(5000);
}
