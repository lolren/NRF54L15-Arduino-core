#include <Arduino.h>
#include <Watchdog.h>

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);

  if (Watchdog.begin(4000)) {
    Serial.println("Watchdog enabled (4s)");
  } else {
    Serial.println("Watchdog enable failed");
  }
}

void loop() {
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  Watchdog.feed();
  delay(500);
}
