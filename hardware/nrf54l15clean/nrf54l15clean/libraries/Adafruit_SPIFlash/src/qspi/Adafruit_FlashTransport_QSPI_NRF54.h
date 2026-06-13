#ifndef ADAFRUIT_FLASHTRANSPORT_QSPI_NRF54_H
#define ADAFRUIT_FLASHTRANSPORT_QSPI_NRF54_H

#include <Adafruit_FlashTransport.h>

class Adafruit_FlashTransport_QSPI_NRF54 : public Adafruit_FlashTransport {
public:
  Adafruit_FlashTransport_QSPI_NRF54();
  Adafruit_FlashTransport_QSPI_NRF54(int8_t sck, int8_t cs, int8_t io0,
                                     int8_t io1, int8_t io2, int8_t io3);

  bool begin() override;
  void end() override;
  bool runCommand(uint8_t command) override;
  bool readCommand(uint8_t command, uint8_t *response, size_t length) override;
  bool writeCommand(uint8_t command, const uint8_t *data, size_t length) override;
  bool eraseCommand(uint8_t command, uint32_t address) override;
  bool readMemory(uint32_t address, uint8_t *data, size_t length) override;
  bool writeMemory(uint32_t address, const uint8_t *data, size_t length) override;

private:
  uint32_t _clockHz;
};

#endif
