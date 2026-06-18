/*
  HighSpeedSpi32MHzProbe

  Shows the dedicated HS-SPI path.

  nRF54L15 boards:
    SPI uses the normal Arduino SPI pins on a serial-fabric SPIM, 8 MHz max.
    SPI_HS uses SPIM00 on the P2 high-speed route and can request 32 MHz.
    On XIAO nRF54L15, D8/D9/D10 are the same P2 SCK/MISO/MOSI pins and
    HS_SS is P2.05, shared with RF_SW_CTL.

  XIAO nRF54LM20A:
    SPI uses the XIAO header pins on a serial-fabric SPIM and is limited to 8 MHz.
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
  Serial.println("Default SPI: D8/D9/D10 on serial-fabric SPIM, 8 MHz max.");
  Serial.println("SPI_HS: onboard QSPI/HS pads on SPIM00, 32 MHz capable.");
#elif defined(ARDUINO_NRF54L15)
  Serial.println("Board: nRF54L15");
  Serial.println("Default SPI: Arduino SPI pins on serial-fabric SPIM, 8 MHz max.");
  Serial.println("SPI_HS: P2.x HS pins on SPIM00, 32 MHz capable.");
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
