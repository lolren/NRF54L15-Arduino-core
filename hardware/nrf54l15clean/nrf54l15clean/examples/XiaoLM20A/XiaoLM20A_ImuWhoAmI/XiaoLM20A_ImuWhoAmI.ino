/*
 * XiaoLM20A_ImuWhoAmI
 *
 * Verifies the onboard LSM6DS3TR-C on XIAO nRF54LM20A.
 *
 * LM20A I2C pins:
 *   SDA = P0.08 (Wire1 pin 36)
 *   SCL = P0.07 (Wire1 pin 37)
 *   CS  = P3.12, HIGH for I2C mode
 *
 * Power: nPM1300 LDO1 feeds IMU&MIC_3V3.
 *
 * Expected: addr=0x6A, WHO_AM_I=0x6A
 */

#include <Arduino.h>
#include <Wire.h>
#include "npm1300.h"

#if !(defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B))
#error "XiaoLM20A_ImuWhoAmI requires the XIAO nRF54LM20A board."
#endif

#define IMU_BUS       Wire1
#define IMU_ADDR      0x6A
#define IMU_CS_PIN    PIN_IMU_CS

static bool readWhoAmI(uint8_t* whoAmI) {
    IMU_BUS.beginTransmission(IMU_ADDR);
    IMU_BUS.write(0x0F);
    if (IMU_BUS.endTransmission(false) != 0) {
        return false;
    }

    if (IMU_BUS.requestFrom(IMU_ADDR, 1) != 1) {
        return false;
    }
    if (!IMU_BUS.available()) {
        return false;
    }

    *whoAmI = IMU_BUS.read();
    return true;
}

void setup() {
    Serial.begin(115200);
    delay(250);

    if (!npm1300_imu_mic_power_enable(true)) {
        Serial.println("ERROR: nPM1300 sensor rail enable failed");
    }
    delay(25);

    pinMode(IMU_CS_PIN, OUTPUT);
    digitalWrite(IMU_CS_PIN, HIGH);

    IMU_BUS.begin();
    IMU_BUS.setClock(400000);

    Serial.println("XiaoLM20A_ImuWhoAmI");
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
        Serial.println("IMU not found; check PMIC LDO1 and CS pin");
    }

    delay(1000);
}
