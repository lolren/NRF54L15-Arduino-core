#ifndef ADAFRUIT_FLASHTRANSPORT_H
#define ADAFRUIT_FLASHTRANSPORT_H

#include <Arduino.h>

class Adafruit_FlashTransport {
public:
  virtual ~Adafruit_FlashTransport() {}

  virtual bool begin() = 0;
  virtual void end() = 0;
  virtual bool runCommand(uint8_t command) = 0;
  virtual bool readCommand(uint8_t command, uint8_t *response, size_t length) = 0;
  virtual bool writeCommand(uint8_t command, const uint8_t *data, size_t length) = 0;
  virtual bool eraseCommand(uint8_t command, uint32_t address) = 0;
  virtual bool readMemory(uint32_t address, uint8_t *data, size_t length) = 0;
  virtual bool writeMemory(uint32_t address, const uint8_t *data, size_t length) = 0;
};

#endif
