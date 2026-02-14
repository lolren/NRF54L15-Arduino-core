/*
 * SPI Loopback Example
 *
 * Connect MOSI (D10) to MISO (D9) to validate SPI transfers.
 */

#include <SPI.h>

void setup()
{
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  SPI.begin();
  Serial.println("SPI loopback test: connect D10 (MOSI) <-> D9 (MISO)");
}

void loop()
{
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));

  uint8_t tx8 = 0xA5;
  uint8_t rx8 = SPI.transfer(tx8);

  uint16_t tx16 = 0x5AA5;
  uint16_t rx16 = SPI.transfer16(tx16);

  SPI.endTransaction();

  Serial.print("8-bit tx=0x");
  Serial.print(tx8, HEX);
  Serial.print(" rx=0x");
  Serial.println(rx8, HEX);

  Serial.print("16-bit tx=0x");
  Serial.print(tx16, HEX);
  Serial.print(" rx=0x");
  Serial.println(rx16, HEX);

  if (rx8 == tx8 && rx16 == tx16) {
    Serial.println("Loopback PASS");
  } else {
    Serial.println("Loopback FAIL");
  }

  Serial.println();
  delay(1000);
}
