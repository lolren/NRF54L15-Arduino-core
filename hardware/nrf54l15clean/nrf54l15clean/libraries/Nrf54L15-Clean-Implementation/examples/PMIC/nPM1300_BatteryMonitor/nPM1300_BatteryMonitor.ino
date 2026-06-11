/*
 * nPM1300 Battery Monitor
 * Reads VBAT, temperature, charge current, and system/USB voltage
 * every 2 seconds. Ideal for ultra-low power battery-operated devices.
 *
 * Output via Serial Monitor.
 * LED indicators:
 *   Green = charging
 *   Red   = low battery (< 3.6V)
 */
#include <Arduino.h>
#include "npm1300.h"

void setup() {
    Serial.begin(115200);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_RED, OUTPUT);
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_RED, HIGH);
    
    npm1300_begin();
    
    Serial.println("\n--- nPM1300 Battery Monitor ---");
    if (!npm1300_is_present()) {
        Serial.println("nPM1300 not detected. This example is for XIAO nRF54LM20A.");
    }
}

void loop() {
    int32_t vbat = npm1300_read_vbat_mv();
    int32_t temp = npm1300_read_temp_mc();
    int32_t ibat = npm1300_read_ibat_ma();
    int32_t vsys = npm1300_read_vsys_mv();
    int32_t vbus = npm1300_read_vbus_mv();
    uint8_t vbusStatus = 0;
    bool haveVbusStatus = npm1300_vbus_status(&vbusStatus);
    bool charging = npm1300_charger_is_charging();
    
    Serial.print("VBAT="); Serial.print(vbat); Serial.print("mV ");
    Serial.print("TEMP="); Serial.print(temp / 1000); Serial.print("C ");
    Serial.print("IBAT="); Serial.print(ibat); Serial.print("mA ");
    Serial.print("VSYS="); Serial.print(vsys); Serial.print("mV ");
    Serial.print("VBUS="); Serial.print(vbus); Serial.print("mV ");
    Serial.print("VBUS_STATUS=");
    if (haveVbusStatus) {
        Serial.print("0x");
        if (vbusStatus < 16) {
            Serial.print('0');
        }
        Serial.print(vbusStatus, HEX);
        Serial.print(' ');
    } else {
        Serial.print("NA ");
    }
    Serial.print("CHG="); Serial.println(charging ? "YES" : "NO");
    
    digitalWrite(LED_GREEN, charging ? LOW : HIGH);
    digitalWrite(LED_RED, (vbat > 0 && vbat < 3600) ? LOW : HIGH);
    
    delay(2000);
}
