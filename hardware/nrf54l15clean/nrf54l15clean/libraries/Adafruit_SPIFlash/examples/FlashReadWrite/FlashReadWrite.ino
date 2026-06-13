/*
  FlashReadWrite

  Erases one 4 KB sector, writes a small pattern, verifies it, then puts the
  XIAO nRF54LM20A onboard flash into deep power-down.
*/

#include <Arduino.h>
#include <Adafruit_SPIFlash.h>
#include <Adafruit_FlashTransport_QSPI_NRF54.h>

#if !(defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B))
#error "FlashReadWrite requires the XIAO nRF54LM20A board."
#endif

Adafruit_FlashTransport_QSPI_NRF54 flashTransport;
Adafruit_SPIFlash flash(&flashTransport);

static constexpr uint32_t kTestAddress = 0x000000UL;

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
  Serial.println("Adafruit_SPIFlash-compatible LM20A read/write test");

  if (!flash.begin()) {
    Serial.println("Flash begin failed.");
    return;
  }

  uint8_t before[16] = {0};
  if (flash.readBuffer(kTestAddress, before, sizeof(before))) {
    Serial.print("Before:");
    for (uint8_t value : before) {
      Serial.print(' ');
      printHex2(value);
    }
    Serial.println();
  }

  Serial.println("Erasing sector 0...");
  if (!flash.eraseSector(kTestAddress)) {
    Serial.println("Erase failed.");
    return;
  }

  uint8_t pattern[16] = {0};
  for (size_t i = 0; i < sizeof(pattern); ++i) {
    pattern[i] = static_cast<uint8_t>(0xA0U + i);
  }

  Serial.println("Writing pattern...");
  if (!flash.writeBuffer(kTestAddress, pattern, sizeof(pattern))) {
    Serial.println("Write failed.");
    return;
  }

  uint8_t after[16] = {0};
  if (flash.readBuffer(kTestAddress, after, sizeof(after))) {
    Serial.print("After: ");
    for (uint8_t value : after) {
      Serial.print(' ');
      printHex2(value);
    }
    Serial.println();
  }

  bool matched = true;
  for (size_t i = 0; i < sizeof(pattern); ++i) {
    if (after[i] != pattern[i]) {
      matched = false;
      break;
    }
  }
  Serial.println(matched ? "Verify OK." : "Verify failed.");

  flash.runCommand(0xB9);
  flash.end();
  Serial.println("Flash parked in deep power-down.");
}

void loop() {
  delay(1000);
}
