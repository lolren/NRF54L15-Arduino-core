/*
 * nPM1300 Battery Monitor
 * Reads VBAT, temperature, charge current, and system/USB voltage
 * every 2 seconds. Ideal for ultra-low power battery-operated devices.
 *
 * Output via Serial (USB-UART on D6/D7 @ 115200).
 * LED indicators:
 *   Green = charging
 *   Red   = low battery (< 3.6V)
 */
#include <Arduino.h>
#include "npm1300.h"

void setup() {
    Serial1.begin(115200);  // Hardware UART on D6/D7
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_RED, OUTPUT);
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_RED, HIGH);
    
    npm1300_begin();
    
    Serial1.println("\n--- nPM1300 Battery Monitor ---");
}

void loop() {
    int32_t vbat = npm1300_read_vbat_mv();
    int32_t temp = npm1300_read_temp_mc();
    int32_t ibat = npm1300_read_ibat_ma();
    int32_t vsys = npm1300_read_vsys_mv();
    int32_t vbus = npm1300_read_vbus_mv();
    bool charging = npm1300_charger_is_charging();
    
    Serial1.print("VBAT="); Serial1.print(vbat); Serial1.print("mV ");
    Serial1.print("TEMP="); Serial1.print(temp / 1000); Serial1.print("C ");
    Serial1.print("IBAT="); Serial1.print(ibat); Serial1.print("mA ");
    Serial1.print("VSYS="); Serial1.print(vsys); Serial1.print("mV ");
    Serial1.print("VBUS="); Serial1.print(vbus); Serial1.print("mV ");
    Serial1.print("CHG="); Serial1.println(charging ? "YES" : "NO");
    
    digitalWrite(LED_GREEN, charging ? LOW : HIGH);
    digitalWrite(LED_RED, (vbat > 0 && vbat < 3600) ? LOW : HIGH);
    
    delay(2000);
}
