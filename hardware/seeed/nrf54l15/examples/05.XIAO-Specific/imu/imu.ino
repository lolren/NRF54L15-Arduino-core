#include <Arduino.h>
#include <Wire.h>

/*
  IMU probe sample: scans common IMU addresses and reads WHO_AM_I-like regs.
*/

static bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static int readReg(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return -1;
  }

  if (Wire.requestFrom(addr, static_cast<uint8_t>(1), true) != 1) {
    return -1;
  }

  return Wire.read();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin();
  Wire.setClock(400000U);
  Serial.println("XIAO nRF54L15 IMU probe sample");
}

void loop() {
  const uint8_t candidates[] = {0x68, 0x69, 0x6A, 0x6B, 0x1C, 0x1D, 0x1E, 0x19};
  bool found = false;

  for (size_t i = 0; i < sizeof(candidates); ++i) {
    const uint8_t addr = candidates[i];
    if (!i2cPresent(addr)) {
      continue;
    }

    found = true;
    const int who0f = readReg(addr, 0x0F);
    const int who75 = readReg(addr, 0x75);

    Serial.print("IMU candidate @ 0x");
    if (addr < 16) {
      Serial.print('0');
    }
    Serial.print(addr, HEX);
    Serial.print(" WHO_AM_I(0x0F)=0x");
    if (who0f >= 0) {
      Serial.print(who0f, HEX);
    } else {
      Serial.print("??");
    }
    Serial.print(" WHO_AM_I(0x75)=0x");
    if (who75 >= 0) {
      Serial.print(who75, HEX);
    } else {
      Serial.print("??");
    }
    Serial.println();
  }

  if (!found) {
    Serial.println("No common IMU addresses detected.");
  }

  delay(1500);
}
