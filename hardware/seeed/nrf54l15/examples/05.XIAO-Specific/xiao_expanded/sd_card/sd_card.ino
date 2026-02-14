#include <Arduino.h>
#include <SPI.h>

/*
  Expansion-base SD card SPI probe.
  Sends CMD0 and checks for an R1 idle response (0x01).
*/

#ifndef SD_CS_PIN
#define SD_CS_PIN PIN_SPI_SS
#endif

static uint8_t sdCommand(uint8_t cmd, uint32_t arg, uint8_t crc) {
  SPI.transfer(static_cast<uint8_t>(0x40U | cmd));
  SPI.transfer(static_cast<uint8_t>(arg >> 24));
  SPI.transfer(static_cast<uint8_t>(arg >> 16));
  SPI.transfer(static_cast<uint8_t>(arg >> 8));
  SPI.transfer(static_cast<uint8_t>(arg));
  SPI.transfer(crc);

  for (int i = 0; i < 10; ++i) {
    const uint8_t r = SPI.transfer(0xFF);
    if ((r & 0x80U) == 0) {
      return r;
    }
  }

  return 0xFF;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);

  SPI.begin(SD_CS_PIN);
  SPI.beginTransaction(SPISettings(400000U, MSBFIRST, SPI_MODE0));

  Serial.println("XIAO Expanded sd_card sample");

  for (int i = 0; i < 16; ++i) {
    SPI.transfer(0xFF);
  }

  digitalWrite(SD_CS_PIN, LOW);
  const uint8_t r1 = sdCommand(0, 0, 0x95);
  digitalWrite(SD_CS_PIN, HIGH);
  SPI.transfer(0xFF);

  Serial.print("CMD0 response: 0x");
  Serial.println(r1, HEX);
  if (r1 == 0x01) {
    Serial.println("SD card responded (idle state). SPI link looks good.");
  } else {
    Serial.println("No valid idle response. Check wiring, card, and CS pin.");
  }
}

void loop() {
  delay(1000);
}
