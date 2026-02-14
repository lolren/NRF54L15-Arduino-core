#include <Arduino.h>
#include <Wire.h>
#include <math.h>

/*
  Expansion-base style attitude monitor.
  This sketch reads MPU6050-compatible accel data over I2C (0x68) and prints pitch/roll.
*/

#ifndef IMU_ADDR
#define IMU_ADDR 0x68
#endif

static bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool readRegs(uint8_t startReg, uint8_t *dst, uint8_t len) {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(startReg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  if (Wire.requestFrom(IMU_ADDR, len, true) != len) {
    return false;
  }

  for (uint8_t i = 0; i < len; ++i) {
    dst[i] = static_cast<uint8_t>(Wire.read());
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin();
  Wire.setClock(400000U);

  Serial.println("XIAO Expanded attitude_monitor-ncs sample");

  // Wake MPU6050 if present.
  if (!writeReg(0x6B, 0x00)) {
    Serial.println("IMU not detected at 0x68");
  }
}

void loop() {
  uint8_t raw[6] = {0};
  if (!readRegs(0x3B, raw, sizeof(raw))) {
    Serial.println("IMU read failed");
    delay(500);
    return;
  }

  const int16_t ax = static_cast<int16_t>((raw[0] << 8) | raw[1]);
  const int16_t ay = static_cast<int16_t>((raw[2] << 8) | raw[3]);
  const int16_t az = static_cast<int16_t>((raw[4] << 8) | raw[5]);

  const float fax = static_cast<float>(ax);
  const float fay = static_cast<float>(ay);
  const float faz = static_cast<float>(az);

  const float pitch = atan2f(-fax, sqrtf(fay * fay + faz * faz)) * 57.2958f;
  const float roll = atan2f(fay, faz) * 57.2958f;

  Serial.print("pitch=");
  Serial.print(pitch, 2);
  Serial.print(" roll=");
  Serial.println(roll, 2);
  delay(100);
}
