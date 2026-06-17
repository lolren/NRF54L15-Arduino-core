/*
 * nPM1300 Battery Current Monitor
 *
 * Measures battery charge and discharge current using the nPM1300 PMIC.
 *
 * How it works:
 *   The nPM1300 has an ADC for battery current (IBAT).
 *   It measures current during both charging and discharging.
 *   The direction is sensed automatically from VBUS presence and charger status.
 *
 *   - No battery connected → IBAT = 0 mA
 *   - Charging (USB + battery) → IBAT = charge current (mA)
 *   - Discharging (battery only) → IBAT = discharge current (mA)
 *
 * Hardware:
 *   XIAO nRF54LM20B with nPM1300 PMIC (onboard)
 *   Optional: LiPo battery on JST connector
 *
 * Output (Serial 115200):
 *   CSV: IBAT_mA, VBAT_mV, VSYS_mV, VBUS_mV, Direction
 *
 * Wiring:
 *   No external wiring needed — nPM1300 is onboard.
 *   USB-C for serial output + charging.
 *   JST connector for LiPo battery.
 */

#include <Arduino.h>
#include "npm1300.h"

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println(F("nPM1300 Battery Current Monitor"));
    Serial.println(F("==============================="));
    Serial.println(F("IBAT > 0   = charging or discharging"));
    Serial.println(F("IBAT = 0   = no battery or fully charged"));
    Serial.println(F("VBAT ~10mV = no battery connected"));
    Serial.println(F("VBAT ~3.7V = LiPo battery present"));
    Serial.println(F(""));
    Serial.println(F("Direction: CHARGE / DISCHARGE / IDLE / NO_BAT"));
    Serial.println(F(""));
    Serial.println(F("IBAT_mA,VBAT_mV,VSYS_mV,VBUS_mV,Direction"));

    npm1300_begin();
    npm1300_charger_enable(true);
    npm1300_charger_set_current(500);
}

void loop() {
    int32_t ibat = npm1300_read_ibat_ma();
    int32_t vbat = npm1300_read_vbat_mv();
    int32_t vsys = npm1300_read_vsys_mv();
    int32_t vbus = npm1300_read_vbus_mv();

    const char* dir = "NO_BAT";
    if (vbat > 1000) {
        dir = (vbus > 1000) ? "CHARGE" : "DISCHARGE";
    } else if (vbus > 1000) {
        dir = "IDLE";
    }

    Serial.print(ibat); Serial.print(F(","));
    Serial.print(vbat); Serial.print(F(","));
    Serial.print(vsys); Serial.print(F(","));
    Serial.print(vbus); Serial.print(F(","));
    Serial.println(dir);

    delay(2000);
}
