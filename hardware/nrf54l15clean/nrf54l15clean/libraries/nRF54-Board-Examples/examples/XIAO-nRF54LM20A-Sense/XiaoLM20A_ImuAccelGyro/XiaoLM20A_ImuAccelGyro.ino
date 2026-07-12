#include <Arduino.h>
#include <Wire.h>
#include "npm1300.h"

#define IMU_ADDR      0x6A
#define IMU_CS_PIN    39  // P3.12

#define WHO_AM_I      0x0F
#define CTRL1_XL      0x10
#define CTRL2_G       0x11
#define OUTX_L_XL     0x28
#define OUTX_L_G      0x22
#define OUT_TEMP_L    0x20

void setup() {
    Serial.begin(115200);
    delay(250);
    Serial.println("IMU Test");

    // CS HIGH BEFORE power (IMU samples CS at power-up)
    pinMode(IMU_CS_PIN, OUTPUT);
    digitalWrite(IMU_CS_PIN, HIGH);
    delay(10);

    // Power IMU+MIC rail
    npm1300_begin();
    if (!npm1300_imu_mic_power_enable(true))
        Serial.println("ERROR: nPM1300 rail failed");
    delay(50);

    Wire1.begin();
    Wire1.setClock(400000);

    // Check WHO_AM_I
    Wire1.beginTransmission(IMU_ADDR);
    Wire1.write(WHO_AM_I);
    Wire1.endTransmission(false);
    Wire1.requestFrom(IMU_ADDR, 1);
    uint8_t whoami = Wire1.available() ? Wire1.read() : 0;
    Serial.print("WHO_AM_I=0x");
    Serial.println(whoami, HEX);

    if (whoami != 0x6A) { Serial.println("IMU not found!"); return; }

    // Configure: 416 Hz, +/-2g accel, +/-250dps gyro
    Wire1.beginTransmission(IMU_ADDR);
    Wire1.write(CTRL1_XL); Wire1.write(0x60);
    Wire1.endTransmission(true);
    Wire1.beginTransmission(IMU_ADDR);
    Wire1.write(CTRL2_G); Wire1.write(0x60);
    Wire1.endTransmission(true);

    Serial.println("ax,ay,az(g),gx,gy,gz(dps),temp(C)");
}

void loop() {
    int16_t ax, ay, az, gx, gy, gz, tmp;

    Wire1.beginTransmission(IMU_ADDR); Wire1.write(OUTX_L_XL);
    Wire1.endTransmission(false); Wire1.requestFrom(IMU_ADDR, 6);
    if (Wire1.available() >= 6) {
        ax = Wire1.read() | (Wire1.read() << 8);
        ay = Wire1.read() | (Wire1.read() << 8);
        az = Wire1.read() | (Wire1.read() << 8);
    }

    Wire1.beginTransmission(IMU_ADDR); Wire1.write(OUTX_L_G);
    Wire1.endTransmission(false); Wire1.requestFrom(IMU_ADDR, 6);
    if (Wire1.available() >= 6) {
        gx = Wire1.read() | (Wire1.read() << 8);
        gy = Wire1.read() | (Wire1.read() << 8);
        gz = Wire1.read() | (Wire1.read() << 8);
    }

    Wire1.beginTransmission(IMU_ADDR); Wire1.write(OUT_TEMP_L);
    Wire1.endTransmission(false); Wire1.requestFrom(IMU_ADDR, 2);
    if (Wire1.available() >= 2) tmp = Wire1.read() | (Wire1.read() << 8);

    Serial.print(ax * 0.061f); Serial.print(", ");
    Serial.print(ay * 0.061f); Serial.print(", ");
    Serial.print(az * 0.061f); Serial.print(", ");
    Serial.print(gx * 8.75f);  Serial.print(", ");
    Serial.print(gy * 8.75f);  Serial.print(", ");
    Serial.print(gz * 8.75f);  Serial.print(", ");
    Serial.println(25.0f + (tmp / 256.0f));
    delay(100);
}
