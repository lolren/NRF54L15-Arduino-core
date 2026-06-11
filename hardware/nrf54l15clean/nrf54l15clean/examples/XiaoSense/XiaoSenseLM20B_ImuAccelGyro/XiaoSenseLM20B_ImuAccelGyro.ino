/*
 * XiaoSenseLM20B_ImuAccelGyro
 *
 * Reads accelerometer and gyroscope from the onboard LSM6DS3TR-C
 * on XIAO nRF54LM20B.
 *
 * Pins:
 *   SDA = P0.08, SCL = P0.07 (Wire1/TWIM30)
 *   CS  = P3.12 (HIGH = I2C mode)
 *   Power = nPM1300 LDO1
 *
 * Output (Serial 115200):
 *   ax,ay,az (mg), gx,gy,gz (mdps), temp (C)
 */

#include <Arduino.h>
#include <Wire.h>
#include "npm1300.h"

#define IMU_ADDR      0x6A
#define IMU_CS_PIN    (44)  // P3.12

// LSM6DS3TR-C registers
#define WHO_AM_I      0x0F
#define CTRL1_XL      0x10  // Accelerometer control
#define CTRL2_G       0x11  // Gyroscope control
#define OUTX_L_XL     0x28  // Accel X low byte
#define OUTX_L_G      0x22  // Gyro X low byte
#define OUT_TEMP_L    0x20  // Temperature low byte

static void writeReg(uint8_t reg, uint8_t val) {
    Wire1.beginTransmission(IMU_ADDR);
    Wire1.write(reg);
    Wire1.write(val);
    Wire1.endTransmission(true);
}

static int16_t read16(uint8_t reg) {
    Wire1.beginTransmission(IMU_ADDR);
    Wire1.write(reg);
    Wire1.endTransmission(false);
    Wire1.requestFrom(IMU_ADDR, 2);
    return (Wire1.read()) | (Wire1.read() << 8);
}

static float accel_mg(int16_t raw) {
    // ±2g = ±2000 mg, 16-bit → 0.061 mg/LSB
    return raw * 0.061f;
}

static float gyro_mdps(int16_t raw) {
    // ±250 dps = 250000 mdps, 16-bit → 8.75 mdps/LSB
    return raw * 8.75f;
}

static float temp_c(int16_t raw) {
    return 25.0f + (raw / 256.0f);
}

void setup() {
    Serial.begin(115200);
    delay(250);

    // Power + CS
    npm1300_ldo1_enable(true);
    delay(10);

    pinMode(IMU_CS_PIN, OUTPUT);
    digitalWrite(IMU_CS_PIN, HIGH);

    Wire1.begin();
    Wire1.setClock(400000);

    // Configure IMU
    writeReg(CTRL1_XL, 0x60);  // Accel: 416 Hz, ±2g
    writeReg(CTRL2_G,  0x60);  // Gyro:  416 Hz, ±250 dps
    delay(10);

    Serial.println("ax_mg,ay_mg,az_mg,gx_mdps,gy_mdps,gz_mdps,temp_c");
}

void loop() {
    int16_t ax = read16(OUTX_L_XL);
    int16_t ay = read16(OUTX_L_XL + 2);
    int16_t az = read16(OUTX_L_XL + 4);
    int16_t gx = read16(OUTX_L_G);
    int16_t gy = read16(OUTX_L_G + 2);
    int16_t gz = read16(OUTX_L_G + 4);
    int16_t tmp = read16(OUT_TEMP_L);

    Serial.print(accel_mg(ax)); Serial.print(", ");
    Serial.print(accel_mg(ay)); Serial.print(", ");
    Serial.print(accel_mg(az)); Serial.print(", ");
    Serial.print(gyro_mdps(gx)); Serial.print(", ");
    Serial.print(gyro_mdps(gy)); Serial.print(", ");
    Serial.print(gyro_mdps(gz)); Serial.print(", ");
    Serial.println(temp_c(tmp));

    delay(100);
}
