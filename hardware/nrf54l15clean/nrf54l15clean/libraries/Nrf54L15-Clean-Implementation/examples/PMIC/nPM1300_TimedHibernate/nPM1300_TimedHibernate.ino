/*
 * nPM1300 Timed Hibernate
 *
 * XIAO nRF54LM20A timed cold-boot example.
 * - On battery/VBAT, the helper programs the nPM1300 wake timer and enters
 *   true PMIC Hibernate.
 * - On USB/VBUS, where the PMIC cannot enter Hibernate, the helper uses the
 *   nRF54 GRTC System OFF wake-reset path.
 *
 * Current measurement notes:
 * - To measure the lowest-current PMIC path, supply the VBAT/battery pads and
 *   disconnect USB/VBUS plus debug wiring.
 * - The USB fallback keeps the board's USB power path active and is not the
 *   sub-uA PMIC Hibernate measurement case.
 * - Both successful paths wake by cold boot and restart setup().
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
    // Invalid delay, PMIC status read, or timer setup failed. Blink fast so
    // the failure is visible without requiring Serial while measuring current.
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
