/*
 * SPI Burst Transfer Example
 *
 * Connect MOSI (D10) to MISO (D9) and run.
 * Demonstrates buffer-level SPI transfers.
 */

#include <SPI.h>

static const uint8_t kTx[] = {0x3C, 0xA5, 0x5A, 0xC3, 0x00, 0xFF};
static uint8_t gRx[sizeof(kTx)];

void setup()
{
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  SPI.begin();
  Serial.println("SPI burst transfer test: connect D10 (MOSI) <-> D9 (MISO)");
}

void loop()
{
  memset(gRx, 0, sizeof(gRx));

  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  SPI.transfer(kTx, gRx, sizeof(kTx));
  SPI.endTransaction();

  bool pass = true;
  for (size_t i = 0; i < sizeof(kTx); ++i) {
    if (gRx[i] != kTx[i]) {
      pass = false;
    }
  }

  Serial.print("rx: ");
  for (size_t i = 0; i < sizeof(gRx); ++i) {
    if (gRx[i] < 16) {
      Serial.print('0');
    }
    Serial.print(gRx[i], HEX);
    Serial.print(' ');
  }
  Serial.println();
  Serial.println(pass ? "Burst PASS" : "Burst FAIL");
  Serial.println();

  delay(1000);
}
