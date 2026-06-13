#ifndef ADAFRUIT_SPIFLASH_H
#define ADAFRUIT_SPIFLASH_H

#include <Arduino.h>

#include "Adafruit_FlashTransport.h"
#include "flash_devices.h"

class Adafruit_SPIFlash {
public:
  explicit Adafruit_SPIFlash(Adafruit_FlashTransport *transport);

  bool begin(const SPIFlash_Device_t *devices = possible_devices,
             size_t count = EXTERNAL_FLASH_DEVICE_COUNT);
  void end();

  uint32_t size() const;
  uint32_t readJEDECID();
  bool getJEDECID(uint8_t *manufacturer, uint8_t *memoryType, uint8_t *capacity);
  bool readBuffer(uint32_t address, uint8_t *buffer, size_t length);
  bool writeBuffer(uint32_t address, const uint8_t *buffer, size_t length);
  bool eraseSector(uint32_t address);
  bool eraseChip();
  bool waitUntilReady(uint32_t timeoutMs = 100UL);
  bool runCommand(uint8_t command);

private:
  Adafruit_FlashTransport *_transport;
  SPIFlash_Device_t _device;
  bool _begun;
};

#endif
