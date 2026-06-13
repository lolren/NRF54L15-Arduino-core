/*
  HighSpeedSpi32MHzProbe

  Shows the dedicated HS-SPI path.

  XIAO nRF54L15:
    SPI and SPI_HS both use SPIM00 on D8/D9/D10 and can request 32 MHz.

  XIAO nRF54LM20A:
    SPI uses the XIAO header pins on SPIM22 and is limited to 8 MHz.
    SPI_HS uses SPIM00 on the onboard QSPI flash pads and can request 32 MHz.
    Do not wire external devices to SPI_HS unless you deliberately use those pads.
*/

#include <Arduino.h>
#include <SPI.h>

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("HighSpeedSpi32MHzProbe");

#if defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B)
  Serial.println("Board: XIAO nRF54LM20A");
  Serial.println("Default SPI: D8/D9/D10 on SPIM22, 8 MHz max.");
  Serial.println("SPI_HS: onboard QSPI/HS pads on SPIM00, 32 MHz capable.");
#elif defined(ARDUINO_NRF54L15)
  Serial.println("Board: nRF54L15");
  Serial.println("SPI_HS aliases SPI on SPIM00, 32 MHz capable.");
#else
  Serial.println("Board: unknown nRF54 clean-core target.");
#endif

  SPI_HS.begin();
  SPI_HS.beginTransaction(SPISettings(SPI_CLOCK_32M, MSBFIRST, SPI_MODE0));
  const uint8_t rx = SPI_HS.transfer(0xFF);
  SPI_HS.endTransaction();
  SPI_HS.end();

  Serial.print("32 MHz transfer complete, rx=0x");
  if (rx < 0x10) {
    Serial.print('0');
  }
  Serial.println(rx, HEX);
  Serial.println("Done.");
}

void loop() {
  delay(1000);
}
