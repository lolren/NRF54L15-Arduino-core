/*
  FlashInfo

  Adafruit_SPIFlash-compatible JEDEC probe for the XIAO nRF54LM20A onboard
  P25Q64/PY25Q64 QSPI flash. The transport uses the dedicated LM20A flash pads,
  not the normal XIAO SPI header.
*/

#include <Arduino.h>
#include <Adafruit_SPIFlash.h>
#include <Adafruit_FlashTransport_QSPI_NRF54.h>

#if !(defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B))
#error "FlashInfo requires the XIAO nRF54LM20A board."
#endif

Adafruit_FlashTransport_QSPI_NRF54 flashTransport;
Adafruit_SPIFlash flash(&flashTransport);

static void printHex2(uint8_t value) {
  if (value < 0x10) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("Adafruit_SPIFlash-compatible LM20A flash info");

  if (!flash.begin()) {
    Serial.println("Flash begin failed.");
    return;
  }

  uint8_t manufacturer = 0;
  uint8_t memoryType = 0;
  uint8_t capacity = 0;
  if (flash.getJEDECID(&manufacturer, &memoryType, &capacity)) {
    Serial.print("JEDEC ID: ");
    printHex2(manufacturer);
    Serial.print(' ');
    printHex2(memoryType);
    Serial.print(' ');
    printHex2(capacity);
    Serial.println();
  }

  Serial.print("Detected size: ");
  Serial.print(flash.size());
  Serial.println(" bytes");

  flash.runCommand(0xB9);  // Deep power-down for low-current idle.
  flash.end();
  Serial.println("Flash parked in deep power-down.");
}

void loop() {
  delay(1000);
}
