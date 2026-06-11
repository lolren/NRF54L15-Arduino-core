/*
 * XiaoSenseLM20B_ImuWhoAmI
 *
 * Verifies the onboard LSM6DS3TR-C on XIAO nRF54LM20B.
 *
 * LM20B I2C pins (different from L15 Sense):
 *   SDA = P0.08 (Wire1 pin 36)
 *   SCL = P0.07 (Wire1 pin 37)
 *   CS  = P3.12 — MUST be HIGH for I2C mode
 *
 * Power: nPM1300 LDO1 on P1.12 (controlled via npm1300 driver)
 *
 * Expected: addr=0x6A, WHO_AM_I=0x6A
 */

#include <Arduino.h>
#include <Wire.h>
#include "npm1300.h"

// LM20B IMU I2C bus (different from L15 Sense D12/D11)
#define IMU_BUS       Wire1
#define IMU_ADDR      0x6A
#define IMU_CS_PIN    (44)  // P3.12 — HIGH = I2C mode

static bool readWhoAmI(uint8_t* whoAmI) {
    IMU_BUS.beginTransmission(IMU_ADDR);
    IMU_BUS.write(0x0F);        // WHO_AM_I register
    if (IMU_BUS.endTransmission(false) != 0) return false;

    if (IMU_BUS.requestFrom(IMU_ADDR, 1) != 1) return false;
    if (!IMU_BUS.available()) return false;

    *whoAmI = IMU_BUS.read();
    return true;
}

void setup() {
    Serial.begin(115200);
    delay(250);

    // 1. Power IMU via PMIC LDO1
    npm1300_ldo1_enable(true);
    delay(10);

    // 2. Set CS HIGH — I2C mode (critical!)
    pinMode(IMU_CS_PIN, OUTPUT);
    digitalWrite(IMU_CS_PIN, HIGH);

    // 3. Init I2C bus
    IMU_BUS.begin();
    IMU_BUS.setClock(400000);

    Serial.println("XiaoSenseLM20B_ImuWhoAmI");
    Serial.println("bus=P0.08(SDA)/P0.07(SCL) twim=TWIM30 rail=nPM1300_LDO1");
}

void loop() {
    uint8_t whoAmI = 0;

    if (readWhoAmI(&whoAmI)) {
        Serial.print("IMU addr=0x6A WHO_AM_I=0x");
        Serial.print(whoAmI, HEX);
        Serial.print(" match=");
        Serial.println(whoAmI == 0x6A ? "YES" : "NO");
    } else {
        Serial.println("IMU not found — check PMIC LDO1 and CS pin");
    }

    delay(1000);
}
