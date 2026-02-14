#include <Arduino.h>
#include <Watchdog.h>

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);

  if (Watchdog.begin(4000)) {
    Serial.println("Watchdog enabled (4s)");
  } else {
    Serial.print("Watchdog enable failed err=");
    Serial.println(Watchdog.lastError());
  }
}

void loop() {
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  if (!Watchdog.feed()) {
    Serial.print("Watchdog feed failed err=");
    Serial.println(Watchdog.lastError());
  }
  delay(500);
}
