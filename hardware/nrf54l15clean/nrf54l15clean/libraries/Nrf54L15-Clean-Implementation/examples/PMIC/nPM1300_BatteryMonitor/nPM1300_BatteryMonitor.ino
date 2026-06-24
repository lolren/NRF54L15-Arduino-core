/*
 * nPM1300 Battery Monitor
 * 
 * Continuously reads and displays the PMIC's battery, charger, and
 * power-rail measurements on the hardware UART (D6 TX, 115200 baud).
 *
 * LED signals:
 *   Green  = battery OK          (> 3.7 V)
 *   Red    = battery low         (< 3.6 V)
 *   Blue   = VBUS present / charging
 *
 * Hardware: XIAO nRF54LM20A
 * Output:   USB Serial Monitor at 115200 baud.
 */

#include <Arduino.h>
#include "npm1300.h"

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("=== nPM1300 Battery Monitor ===");

    // Configure charger: 200 mA, 4.2 V termination
    npm1300_charger_set_current(200);
    npm1300_charger_set_term_voltage(4200);
    npm1300_charger_enable(true);

    // LEDs
    pinMode(LED_RED,   OUTPUT); digitalWrite(LED_RED,   HIGH); // off
    pinMode(LED_GREEN, OUTPUT); digitalWrite(LED_GREEN, HIGH); // off
    pinMode(LED_BLUE,  OUTPUT); digitalWrite(LED_BLUE,  HIGH); // off
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

    bool charging = npm1300_charger_is_charging();

    // ── display ──
    Serial.print("VBAT="); Serial.print(vbat); Serial.print("mV  ");
    Serial.print("TEMP="); Serial.print(temp / 1000); Serial.print(".");
    Serial.print(abs(temp) % 1000 / 100); Serial.print("C  ");
    Serial.print("IBAT="); Serial.print(ibat); Serial.print("mA  ");
    Serial.print("VBUS_ILIM="); Serial.print(npm1300_vbus_get_input_current_limit_ma()); Serial.print("mA  ");
    Serial.print("VSYS="); Serial.print(vsys); Serial.print("mV  ");
    Serial.print("VBUS="); Serial.print(vbus); Serial.print("mV  ");
    Serial.print("VBUS_ST=0x"); Serial.print(vbus_st, HEX);
    Serial.print(" CHG_ST=0x"); Serial.print(chg_st, HEX);
    Serial.print(" CHG="); Serial.println(charging ? "ON" : "OFF");

    // ── LEDs ──
    digitalWrite(LED_GREEN, (vbat > 3700)      ? LOW : HIGH);
    digitalWrite(LED_RED,   (vbat > 0 && vbat < 3600) ? LOW : HIGH);
    digitalWrite(LED_BLUE,  charging           ? LOW : HIGH);

    delay(2000);
}
