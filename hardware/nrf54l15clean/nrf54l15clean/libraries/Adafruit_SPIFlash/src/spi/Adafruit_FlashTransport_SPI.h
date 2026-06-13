#ifndef ADAFRUIT_FLASHTRANSPORT_SPI_H
#define ADAFRUIT_FLASHTRANSPORT_SPI_H

#include <Adafruit_FlashTransport.h>
#include <SPI.h>

class Adafruit_FlashTransport_SPI : public Adafruit_FlashTransport {
public:
  Adafruit_FlashTransport_SPI(int8_t cs, SPIClass *spi = &SPI);

  bool begin() override;
  void end() override;
  bool runCommand(uint8_t command) override;
  bool readCommand(uint8_t command, uint8_t *response, size_t length) override;
  bool writeCommand(uint8_t command, const uint8_t *data, size_t length) override;
  bool eraseCommand(uint8_t command, uint32_t address) override;
  bool readMemory(uint32_t address, uint8_t *data, size_t length) override;
  bool writeMemory(uint32_t address, const uint8_t *data, size_t length) override;

private:
  bool writeEnable();
  bool waitReady(uint32_t timeoutMs);
  void select();
  void deselect();
  void writeAddress(uint32_t address);

  int8_t _cs;
  SPIClass *_spi;
  SPISettings _settings;
};

#endif
