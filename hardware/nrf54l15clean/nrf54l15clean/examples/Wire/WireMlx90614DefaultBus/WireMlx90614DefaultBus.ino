/*
 * WireMlx90614DefaultBus
 *
 * Demonstrates the default XIAO nRF54L15 header I2C route:
 *   Wire.begin() -> SDA=D4/P1.10, SCL=D5/P1.11
 *
 * MLX90614 defaults to SMBus address 0x5A and commonly works best at 100 kHz.
 */

#include <Wire.h>

static const uint8_t kMlx90614Address = 0x5A;
static const uint8_t kObjectTempReg = 0x07;

static bool readMlx90614Raw(uint16_t* raw) {
  Wire.beginTransmission(kMlx90614Address);
  Wire.write(kObjectTempReg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  const uint8_t received = Wire.requestFrom(kMlx90614Address, (uint8_t)3, (uint8_t)true);
  if (received < 2 || Wire.available() < 2) {
    return false;
  }

  const uint8_t lo = (uint8_t)Wire.read();
  const uint8_t hi = (uint8_t)Wire.read();
  if (Wire.available() > 0) {
    (void)Wire.read(); // PEC byte, not checked by this minimal probe.
  }
  *raw = ((uint16_t)hi << 8) | lo;
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin();
  Wire.setClock(100000);

  Serial.println("WireMlx90614DefaultBus start");
  Serial.println("Wire default pins: SDA=D4/P1.10, SCL=D5/P1.11");
}

void loop() {
  uint16_t raw = 0;
  if (readMlx90614Raw(&raw)) {
    const float kelvin = raw * 0.02f;
    const float celsius = kelvin - 273.15f;
    Serial.print("object_c=");
    Serial.println(celsius, 2);
  } else {
    Serial.println("MLX90614 not detected at 0x5A on default Wire bus");
  }
  delay(1000);
}
