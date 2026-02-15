#include <Arduino.h>
#include <SPI.h>

/*
  XIAO nRF54L15 board test: SPI loopback.

  Wiring:
  - D10 (MOSI) -> D9 (MISO)
  - D8 is SCK
  - D2 is CS (managed by this sketch)
*/

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  delay(1000);

  SPI.begin(PIN_SPI_SS);
  Serial.println("XIAO nRF54L15 SPI loopback test");
  Serial.println("Connect MOSI (D10) to MISO (D9).");
}

void loop() {
  uint8_t failures = 0;

  SPI.beginTransaction(SPISettings(1000000U, MSBFIRST, SPI_MODE0));
  for (uint8_t tx = 0; tx < 16; ++tx) {
    const uint8_t expected = static_cast<uint8_t>(tx * 17U);
    const uint8_t rx = SPI.transfer(expected);
    if (rx != expected) {
      ++failures;
      Serial.print("Mismatch tx=0x");
      Serial.print(expected, HEX);
      Serial.print(" rx=0x");
      Serial.println(rx, HEX);
    }
  }
  SPI.endTransaction();

  if (failures == 0) {
    Serial.println("SPI loopback PASS");
    digitalWrite(LED_BUILTIN, LOW);
    delay(80);
    digitalWrite(LED_BUILTIN, HIGH);
  } else {
    Serial.print("SPI loopback FAIL, mismatches=");
    Serial.println(failures);
    digitalWrite(LED_BUILTIN, HIGH);
  }

  delay(1000);
}

