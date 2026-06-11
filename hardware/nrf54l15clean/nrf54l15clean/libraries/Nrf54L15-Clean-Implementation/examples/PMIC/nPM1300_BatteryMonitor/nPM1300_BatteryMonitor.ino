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
 * Output:   Serial1 on D6 (TX) at 115200 baud — connect a USB-UART bridge.
 */

#include <Arduino.h>
#include "npm1300.h"

void setup() {
    Serial1.begin(115200);
    delay(500);

    Serial1.println();
    Serial1.println("=== nPM1300 Battery Monitor ===");

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
    Serial1.print("VBAT="); Serial1.print(vbat); Serial1.print("mV  ");
    Serial1.print("TEMP="); Serial1.print(temp / 1000); Serial1.print(".");
    Serial1.print(abs(temp) % 1000 / 100); Serial1.print("C  ");
    Serial1.print("IBAT="); Serial1.print(ibat); Serial1.print("mA  ");
    Serial1.print("VSYS="); Serial1.print(vsys); Serial1.print("mV  ");
    Serial1.print("VBUS="); Serial1.print(vbus); Serial1.print("mV  ");
    Serial1.print("VBUS_ST=0x"); Serial1.print(vbus_st, HEX);
    Serial1.print(" CHG_ST=0x"); Serial1.print(chg_st, HEX);
    Serial1.print(" CHG="); Serial1.println(charging ? "ON" : "OFF");

    // ── LEDs ──
    digitalWrite(LED_GREEN, (vbat > 3700)      ? LOW : HIGH);
    digitalWrite(LED_RED,   (vbat > 0 && vbat < 3600) ? LOW : HIGH);
    digitalWrite(LED_BLUE,  charging           ? LOW : HIGH);

    delay(2000);
}
