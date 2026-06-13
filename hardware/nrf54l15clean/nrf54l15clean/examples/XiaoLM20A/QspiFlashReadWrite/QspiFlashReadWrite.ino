/*
  QspiFlashReadWrite

  Uses the core LM20A QSPI flash helper through the Adafruit_SPIFlash-compatible
  API. This exercises the same path library users should use.
*/

#include <Arduino.h>
#include <Adafruit_SPIFlash.h>
#include <Adafruit_FlashTransport_QSPI_NRF54.h>

#if !(defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B))
#error "QspiFlashReadWrite requires the XIAO nRF54LM20A board."
#endif

Adafruit_FlashTransport_QSPI_NRF54 flashTransport;
Adafruit_SPIFlash flash(&flashTransport);

static constexpr uint32_t kTestAddress = 0x000000UL;

static void printHexByte(uint8_t value) {
  if (value < 0x10) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

static void printBuffer(const char* label, const uint8_t* data, size_t length) {
  Serial.print(label);
  for (size_t i = 0; i < length; ++i) {
    Serial.print(' ');
    printHexByte(data[i]);
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("XIAO nRF54LM20A QSPI flash read/write");

  if (!flash.begin()) {
    Serial.println("Flash begin failed.");
    return;
  }

  uint8_t before[32] = {0};
  if (flash.readBuffer(kTestAddress, before, sizeof(before))) {
    printBuffer("Before erase:", before, sizeof(before));
  }

  Serial.println("Erasing sector 0...");
  if (!flash.eraseSector(kTestAddress)) {
    Serial.println("Erase failed.");
    return;
  }

  uint8_t erased[32] = {0};
  if (flash.readBuffer(kTestAddress, erased, sizeof(erased))) {
    printBuffer("After erase: ", erased, sizeof(erased));
  }

  uint8_t pattern[32] = {0};
  for (size_t i = 0; i < sizeof(pattern); ++i) {
    pattern[i] = static_cast<uint8_t>(i);
  }

  Serial.println("Writing 0x00..0x1F...");
  if (!flash.writeBuffer(kTestAddress, pattern, sizeof(pattern))) {
    Serial.println("Write failed.");
    return;
  }

  uint8_t after[32] = {0};
  if (flash.readBuffer(kTestAddress, after, sizeof(after))) {
    printBuffer("After write: ", after, sizeof(after));
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
  Serial.println("Flash in deep power-down.");
}

void loop() {
  delay(1000);
}
