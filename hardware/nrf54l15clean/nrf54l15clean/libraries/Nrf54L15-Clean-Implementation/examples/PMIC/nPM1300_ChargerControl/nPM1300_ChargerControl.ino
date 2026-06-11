/*
 * nPM1300 Charger Control
 *
 * Demonstrates how to configure the nPM1300 battery charger and
 * monitor its status.  The sketch cycles through three charge-current
 * profiles every time the button is pressed.
 *
 * Profile 1: 50 mA   — safe for small LiPo cells (< 100 mAh)
 * Profile 2: 200 mA  — standard for ~300 mAh cells
 * Profile 3: 500 mA  — fast charge for large cells
 *
 * LEDs:
 *   Green  = charger running, no error
 *   Red    = charger error (check Serial1)
 *   Blue   = button pressed (profile switch)
 *
 * Hardware:  XIAO nRF54LM20A
 * Output:    Serial1 D6 TX 115200
 */

#include <Arduino.h>
#include "npm1300.h"

static const uint16_t kProfiles[] = { 50, 200, 500 };
static const int kNumProfiles = sizeof(kProfiles) / sizeof(kProfiles[0]);
static int profile = 0;

void applyProfile(int idx) {
    uint16_t ma = kProfiles[idx];
    npm1300_charger_set_current(ma);
    npm1300_charger_set_term_voltage(4200);
    npm1300_charger_enable(true);

    Serial.print("\n=== Profile ");
    Serial.print(idx + 1); Serial.print("/");
    Serial.print(kNumProfiles);
    Serial.print(": "); Serial.print(ma);
    Serial.println(" mA, 4.20 V ===\n");
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("=== nPM1300 Charger Control ===");
    Serial.println("Press the USER button to cycle charge profiles.\n");

    pinMode(LED_RED,   OUTPUT); digitalWrite(LED_RED,   HIGH);
    pinMode(LED_GREEN, OUTPUT); digitalWrite(LED_GREEN, HIGH);
    pinMode(LED_BLUE,  OUTPUT); digitalWrite(LED_BLUE,  HIGH);
    pinMode(PIN_BUTTON, INPUT_PULLUP);

    applyProfile(profile);
}

void loop() {
    // Check for button press — cycle profile
    if (digitalRead(PIN_BUTTON) == LOW) {
        delay(50);                              // debounce
        if (digitalRead(PIN_BUTTON) == LOW) {   // confirmed
            digitalWrite(LED_BLUE, LOW);        // blue on

            profile = (profile + 1) % kNumProfiles;
            applyProfile(profile);

            while (digitalRead(PIN_BUTTON) == LOW);  // wait release
            digitalWrite(LED_BLUE, HIGH);             // blue off
            delay(200);
        }
    }

    // ── periodic status ──
    int32_t vbat  = npm1300_read_vbat_mv();
    int32_t ibat  = npm1300_read_ibat_ma();
    bool charging  = npm1300_charger_is_charging();

    uint8_t chg_st = 0, chg_err = 0;
    npm1300_charger_status(&chg_st);
    npm1300_charger_error(&chg_err);

    uint8_t vbus_st = 0;
    npm1300_vbus_status(&vbus_st);

    Serial.print("VBAT="); Serial.print(vbat); Serial.print("mV  ");
    Serial.print("IBAT="); Serial.print(ibat); Serial.print("mA  ");
    Serial.print("CHG_ST=0x"); Serial.print(chg_st, HEX);
    Serial.print(" CHG_ERR=0x"); Serial.print(chg_err, HEX);
    Serial.print(" VBUS=0x"); Serial.print(vbus_st, HEX);
    Serial.print(" CHG="); Serial.println(charging ? "ON" : "OFF");

    digitalWrite(LED_GREEN, charging          ? LOW : HIGH);
    digitalWrite(LED_RED,   (chg_err != 0)    ? LOW : HIGH);

    delay(2000);
}
