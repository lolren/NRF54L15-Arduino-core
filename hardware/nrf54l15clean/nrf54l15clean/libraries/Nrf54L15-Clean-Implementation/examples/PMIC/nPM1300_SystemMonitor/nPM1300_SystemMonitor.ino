/*
 * nPM1300 System Monitor
 *
 * Prints all available PMIC power-rail measurements once per second.
 * Useful for verifying the PMIC is alive and measuring correctly
 * during development or field diagnostics.
 *
 * Output format (CSV-like, one line per second):
 *   vbat_mv, temp_mC, ibat_ma, vsys_mv, vbus_mv, vbus_status, chg_status
 *
 * LEDs:
 *   Green  = PMIC alive (read succeeded)
 *   Red    = PMIC read failed
 *
 * Hardware:  XIAO nRF54LM20A
 * Output:    USB Serial Monitor at 115200 baud
 */

#include <Arduino.h>
#include "npm1300.h"

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("vbat_mv, temp_mC, ibat_ma, vsys_mv, vbus_mv, vbus_st, chg_st");

    pinMode(LED_RED,   OUTPUT); digitalWrite(LED_RED,   HIGH);
    pinMode(LED_GREEN, OUTPUT); digitalWrite(LED_GREEN, HIGH);
}

void loop() {
    int32_t vbat  = npm1300_read_vbat_mv();
    int32_t temp  = npm1300_read_temp_mc();
    int32_t ibat  = npm1300_read_ibat_ma();
    int32_t vsys  = npm1300_read_vsys_mv();
    int32_t vbus  = npm1300_read_vbus_mv();

    uint8_t vbus_st = 0;
    npm1300_vbus_status(&vbus_st);

    uint8_t chg_st = 0;
    npm1300_charger_status(&chg_st);

    // CSV line — easy to import into spreadsheets
    Serial.print(vbat);   Serial.print(", ");
    Serial.print(temp);   Serial.print(", ");
    Serial.print(ibat);   Serial.print(", ");
    Serial.print(vsys);   Serial.print(", ");
    Serial.print(vbus);   Serial.print(", ");
    Serial.print(vbus_st, HEX); Serial.print(", ");
    Serial.println(chg_st, HEX);

    // PMIC alive check: any read above -1 is valid
    bool alive = (vbat > -1);
    digitalWrite(LED_GREEN, alive ? LOW : HIGH);
    digitalWrite(LED_RED,   alive ? HIGH : LOW);

    delay(1000);
}
