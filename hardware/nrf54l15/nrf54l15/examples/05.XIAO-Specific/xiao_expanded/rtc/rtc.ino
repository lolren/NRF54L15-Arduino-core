#include <Arduino.h>
#include <Wire.h>

/*
  Expansion-base RTC sample for DS3231/DS1307-like I2C RTC (default 0x68).
*/

#ifndef RTC_ADDR
#define RTC_ADDR 0x68
#endif

static uint8_t bcdToDec(uint8_t v) {
  return static_cast<uint8_t>((v >> 4) * 10 + (v & 0x0F));
}

static bool readRtcRegs(uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x00);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  if (Wire.requestFrom(RTC_ADDR, len, true) != len) {
    return false;
  }

  for (uint8_t i = 0; i < len; ++i) {
    buf[i] = static_cast<uint8_t>(Wire.read());
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin();
  Wire.setClock(100000U);

  Serial.println("XIAO Expanded RTC sample");
}

void loop() {
  uint8_t r[7];
  if (!readRtcRegs(r, sizeof(r))) {
    Serial.println("RTC read failed");
    delay(1000);
    return;
  }

  const uint8_t sec = bcdToDec(r[0] & 0x7F);
  const uint8_t min = bcdToDec(r[1] & 0x7F);
  const uint8_t hour = bcdToDec(r[2] & 0x3F);
  const uint8_t day = bcdToDec(r[4] & 0x3F);
  const uint8_t month = bcdToDec(r[5] & 0x1F);
  const uint8_t year = bcdToDec(r[6]);

  Serial.print("20");
  if (year < 10) {
    Serial.print('0');
  }
  Serial.print(year);
  Serial.print('-');
  if (month < 10) {
    Serial.print('0');
  }
  Serial.print(month);
  Serial.print('-');
  if (day < 10) {
    Serial.print('0');
  }
  Serial.print(day);
  Serial.print(' ');
  if (hour < 10) {
    Serial.print('0');
  }
  Serial.print(hour);
  Serial.print(':');
  if (min < 10) {
    Serial.print('0');
  }
  Serial.print(min);
  Serial.print(':');
  if (sec < 10) {
    Serial.print('0');
  }
  Serial.println(sec);

  delay(1000);
}
