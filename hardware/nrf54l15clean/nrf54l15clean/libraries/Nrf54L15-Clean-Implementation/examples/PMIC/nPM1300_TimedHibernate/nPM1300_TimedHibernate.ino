/*
 * nPM1300 Timed Hibernate
 *
 * XIAO nRF54LM20A PMIC hibernate example. This programs the nPM1300 wake
 * timer, then asks the PMIC to enter hibernate mode. On a battery/PPK2 supply
 * this is the sub-uA path reported by msfujino's test sketch.
 *
 * Current measurement notes:
 * - Measure from the VBAT/battery pads, not through USB.
 * - USB/SAMD/debug wiring can dominate the current reading.
 * - Wake is a cold boot after the PMIC timer expires.
 */

#include <Arduino.h>
#include "npm1300.h"

static constexpr uint32_t kHibernateMs = 2000UL;

static void blinkBootMarker() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  for (int i = 0; i < 5; ++i) {
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
  }
}

void setup() {
  blinkBootMarker();

  // Let a PPK2 trace show the active region before hibernate.
  delay(2000);

  if (!npm1300_enter_timed_hibernate_ms(kHibernateMs)) {
    // PMIC not present or timer setup failed. Blink fast so the failure is
    // visible without requiring Serial while measuring current.
    while (true) {
      digitalWrite(LED_BUILTIN, LOW);
      delay(50);
      digitalWrite(LED_BUILTIN, HIGH);
      delay(450);
    }
  }
}

void loop() {
}
