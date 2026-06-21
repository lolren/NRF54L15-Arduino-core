#include "npm1300.h"

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("nPM1300 Discharge Current Test");
    
    npm1300_charger_enable(true);
    delay(100);
    
    // Set discharge current limit to 500 mA
    if (npm1300_charger_set_discharge_current_ma(500))
        Serial.println("Discharge limit: 500 mA");
    else
        Serial.println("Failed to set discharge limit");
    
    Serial.println("Vbat(mV)\tVsys(mV)\tVbus(mV)\tIbat(mA)\tCharging");
}

void loop() {
    int32_t vbat = npm1300_read_vbat_mv();
    int32_t vsys = npm1300_read_vsys_mv();
    int32_t vbus = npm1300_read_vbus_mv();
    int32_t ibat = npm1300_read_ibat_ma();
    bool charging = npm1300_charger_is_charging();
    
    Serial.print(vbat); Serial.print("\t");
    Serial.print(vsys); Serial.print("\t");
    Serial.print(vbus); Serial.print("\t");
    Serial.print(ibat); Serial.print("\t");
    Serial.println(charging ? "YES" : "NO");
    delay(2000);
}
