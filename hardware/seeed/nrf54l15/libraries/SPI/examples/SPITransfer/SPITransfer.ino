#include <Arduino.h>
#include <SPI.h>

void setup() {
  Serial.begin(115200);
  SPI.begin(PIN_SPI_SS);
  Serial.println("SPI transfer demo");
}

void loop() {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  uint8_t response = SPI.transfer(0x55);
  SPI.endTransaction();

  Serial.print("TX 0x55 -> RX 0x");
  if (response < 16) {
    Serial.print("0");
  }
  Serial.println(response, HEX);

  delay(1000);
}
