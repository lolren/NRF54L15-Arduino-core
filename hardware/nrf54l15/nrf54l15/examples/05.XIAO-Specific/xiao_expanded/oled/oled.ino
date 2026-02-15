#include <Arduino.h>
#include <Wire.h>

/*
  Expansion-base OLED sample for SSD1306-compatible I2C displays.
*/

#ifndef OLED_ADDR
#define OLED_ADDR 0x3C
#endif

static void oledCmd(uint8_t cmd) {
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x00);
  Wire.write(cmd);
  Wire.endTransmission();
}

static void oledDataChunk(const uint8_t *data, uint8_t len) {
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x40);
  for (uint8_t i = 0; i < len; ++i) {
    Wire.write(data[i]);
  }
  Wire.endTransmission();
}

static void oledInit() {
  oledCmd(0xAE);
  oledCmd(0x20);
  oledCmd(0x00);
  oledCmd(0x21);
  oledCmd(0x00);
  oledCmd(0x7F);
  oledCmd(0x22);
  oledCmd(0x00);
  oledCmd(0x07);
  oledCmd(0xA1);
  oledCmd(0xC8);
  oledCmd(0xDA);
  oledCmd(0x12);
  oledCmd(0x81);
  oledCmd(0x7F);
  oledCmd(0xA4);
  oledCmd(0xA6);
  oledCmd(0x8D);
  oledCmd(0x14);
  oledCmd(0xAF);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin();
  Wire.setClock(400000U);

  Wire.beginTransmission(OLED_ADDR);
  const int ok = Wire.endTransmission();
  if (ok != 0) {
    Serial.println("OLED not found at 0x3C");
    return;
  }

  Serial.println("XIAO Expanded OLED sample");
  oledInit();
}

void loop() {
  static uint8_t pattern = 0xAA;
  uint8_t line[16];

  for (uint8_t i = 0; i < sizeof(line); ++i) {
    line[i] = (i & 1) ? pattern : static_cast<uint8_t>(~pattern);
  }

  oledCmd(0x21);
  oledCmd(0x00);
  oledCmd(0x7F);
  oledCmd(0x22);
  oledCmd(0x00);
  oledCmd(0x07);

  for (int i = 0; i < 64; ++i) {
    oledDataChunk(line, sizeof(line));
  }

  pattern ^= 0xFF;
  delay(500);
}
