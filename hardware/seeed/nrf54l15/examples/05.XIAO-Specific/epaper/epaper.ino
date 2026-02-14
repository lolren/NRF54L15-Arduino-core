#include <Arduino.h>
#include <SPI.h>

/*
  E-paper transport test (SPI-level smoke test).
  This verifies wiring and basic SPI toggling; it is not a full display driver.
*/

#ifndef EPAPER_CS_PIN
#define EPAPER_CS_PIN PIN_SPI_SS
#endif

#ifndef EPAPER_DC_PIN
#define EPAPER_DC_PIN PIN_D1
#endif

#ifndef EPAPER_RST_PIN
#define EPAPER_RST_PIN PIN_D0
#endif

static void epdWriteCommand(uint8_t cmd) {
  digitalWrite(EPAPER_DC_PIN, LOW);
  digitalWrite(EPAPER_CS_PIN, LOW);
  SPI.transfer(cmd);
  digitalWrite(EPAPER_CS_PIN, HIGH);
}

static void epdWriteData(uint8_t data) {
  digitalWrite(EPAPER_DC_PIN, HIGH);
  digitalWrite(EPAPER_CS_PIN, LOW);
  SPI.transfer(data);
  digitalWrite(EPAPER_CS_PIN, HIGH);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(EPAPER_CS_PIN, OUTPUT);
  pinMode(EPAPER_DC_PIN, OUTPUT);
  pinMode(EPAPER_RST_PIN, OUTPUT);

  digitalWrite(EPAPER_CS_PIN, HIGH);
  digitalWrite(EPAPER_DC_PIN, HIGH);
  digitalWrite(EPAPER_RST_PIN, HIGH);

  SPI.begin(EPAPER_CS_PIN);
  SPI.beginTransaction(SPISettings(4000000U, MSBFIRST, SPI_MODE0));

  Serial.println("XIAO nRF54L15 E-paper SPI smoke test");

  digitalWrite(EPAPER_RST_PIN, LOW);
  delay(20);
  digitalWrite(EPAPER_RST_PIN, HIGH);
  delay(20);

  epdWriteCommand(0x12); // SWRESET on many controllers
  delay(10);
  epdWriteCommand(0x02); // POWER_OFF on many controllers
  epdWriteData(0x00);

  Serial.println("SPI command sequence sent.");
}

void loop() {
  delay(1000);
}
