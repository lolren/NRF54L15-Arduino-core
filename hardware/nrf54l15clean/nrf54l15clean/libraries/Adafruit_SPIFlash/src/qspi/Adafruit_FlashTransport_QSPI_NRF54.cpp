#include "Adafruit_FlashTransport_QSPI_NRF54.h"

#if defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B)
#include <XiaoNrf54lm20QspiFlash.h>
#endif

namespace {

static constexpr uint8_t kCmdDeepPowerDown = 0xB9U;
static constexpr uint8_t kCmdSectorErase4k = 0x20U;
static constexpr uint32_t kDefaultClockHz = 32000000UL;

}  // namespace

Adafruit_FlashTransport_QSPI_NRF54::Adafruit_FlashTransport_QSPI_NRF54()
    : _clockHz(kDefaultClockHz) {}

Adafruit_FlashTransport_QSPI_NRF54::Adafruit_FlashTransport_QSPI_NRF54(
    int8_t sck, int8_t cs, int8_t io0, int8_t io1, int8_t io2, int8_t io3)
    : _clockHz(kDefaultClockHz) {
  (void)sck;
  (void)cs;
  (void)io0;
  (void)io1;
  (void)io2;
  (void)io3;
}

bool Adafruit_FlashTransport_QSPI_NRF54::begin() {
#if defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B)
  return XiaoQspiFlash.begin(_clockHz);
#else
  return false;
#endif
}

void Adafruit_FlashTransport_QSPI_NRF54::end() {
#if defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B)
  XiaoQspiFlash.end();
#endif
}

bool Adafruit_FlashTransport_QSPI_NRF54::runCommand(uint8_t command) {
#if defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B)
  if (command == kCmdDeepPowerDown) {
    return XiaoQspiFlash.deepPowerDown();
  }
  return XiaoQspiFlash.runCommand(command);
#else
  (void)command;
  return false;
#endif
}

bool Adafruit_FlashTransport_QSPI_NRF54::readCommand(uint8_t command,
                                                     uint8_t *response,
                                                     size_t length) {
#if defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B)
  return XiaoQspiFlash.readCommand(command, response, length);
#else
  (void)command;
  (void)response;
  (void)length;
  return false;
#endif
}

bool Adafruit_FlashTransport_QSPI_NRF54::writeCommand(uint8_t command,
                                                      const uint8_t *data,
                                                      size_t length) {
#if defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B)
  (void)data;
  (void)length;
  return XiaoQspiFlash.runCommand(command);
#else
  (void)command;
  (void)data;
  (void)length;
  return false;
#endif
}

bool Adafruit_FlashTransport_QSPI_NRF54::eraseCommand(uint8_t command,
                                                      uint32_t address) {
#if defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B)
  if (command == kCmdSectorErase4k) {
    return XiaoQspiFlash.eraseSector(address);
  }
  return false;
#else
  (void)command;
  (void)address;
  return false;
#endif
}

bool Adafruit_FlashTransport_QSPI_NRF54::readMemory(uint32_t address,
                                                    uint8_t *data,
                                                    size_t length) {
#if defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B)
  return XiaoQspiFlash.read(address, data, length);
#else
  (void)address;
  (void)data;
  (void)length;
  return false;
#endif
}

bool Adafruit_FlashTransport_QSPI_NRF54::writeMemory(uint32_t address,
                                                     const uint8_t *data,
                                                     size_t length) {
#if defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B)
  return XiaoQspiFlash.write(address, data, length);
#else
  (void)address;
  (void)data;
  (void)length;
  return false;
#endif
}
