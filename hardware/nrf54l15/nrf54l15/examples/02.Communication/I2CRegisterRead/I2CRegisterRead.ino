/*
 * I2C Register Read Example
 *
 * Demonstrates repeated-start register reads:
 * beginTransmission(addr) + write(reg) + endTransmission(false) + requestFrom(addr, n)
 *
 * Wiring:
 *   SDA -> D4
 *   SCL -> D5
 */

#include <Wire.h>

// Example device address/register (MPU-6050 WHO_AM_I register).
static const uint8_t kDeviceAddr = 0x68;
static const uint8_t kWhoAmIReg = 0x75;

void setup()
{
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Wire.begin();
  Wire.setClock(400000);

  Serial.println("I2C register-read test using repeated start");
}

void loop()
{
  Wire.beginTransmission(kDeviceAddr);
  Wire.write(kWhoAmIReg);
  uint8_t writeErr = Wire.endTransmission(false);
  if (writeErr != 0) {
    Serial.print("Write phase failed, err=");
    Serial.println(writeErr);
    delay(1000);
    return;
  }

  uint8_t n = Wire.requestFrom(kDeviceAddr, static_cast<uint8_t>(1));
  if (n == 1) {
    int value = Wire.read();
    Serial.print("WHO_AM_I = 0x");
    Serial.println(value, HEX);
  } else {
    Serial.print("Read phase failed/short read, ret=");
    Serial.println(n);
  }

  delay(1000);
}
