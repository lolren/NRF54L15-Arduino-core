#include "Adafruit_FlashTransport_SPI.h"

namespace {

static constexpr uint8_t kCmdWriteEnable = 0x06U;
static constexpr uint8_t kCmdReadStatus = 0x05U;
static constexpr uint8_t kCmdReadData = 0x03U;
static constexpr uint8_t kCmdPageProgram = 0x02U;
static constexpr uint8_t kCmdSectorErase4k = 0x20U;
static constexpr size_t kPageSize = 256U;

}  // namespace

Adafruit_FlashTransport_SPI::Adafruit_FlashTransport_SPI(int8_t cs,
                                                         SPIClass *spi)
    : _cs(cs), _spi(spi), _settings(8000000UL, MSBFIRST, SPI_MODE0) {}

bool Adafruit_FlashTransport_SPI::begin() {
  if (_spi == nullptr || _cs < 0) {
    return false;
  }
  pinMode(_cs, OUTPUT);
  digitalWrite(_cs, HIGH);
  _spi->begin();
  return true;
}

void Adafruit_FlashTransport_SPI::end() {
  if (_spi != nullptr) {
    _spi->end();
  }
}

bool Adafruit_FlashTransport_SPI::runCommand(uint8_t command) {
  if (_spi == nullptr) {
    return false;
  }
  _spi->beginTransaction(_settings);
  select();
  _spi->transfer(command);
  deselect();
  _spi->endTransaction();
  return true;
}

bool Adafruit_FlashTransport_SPI::readCommand(uint8_t command,
                                              uint8_t *response,
                                              size_t length) {
  if (_spi == nullptr || (length > 0U && response == nullptr)) {
    return false;
  }
  _spi->beginTransaction(_settings);
  select();
  _spi->transfer(command);
  for (size_t i = 0; i < length; ++i) {
    response[i] = _spi->transfer(0xFFU);
  }
  deselect();
  _spi->endTransaction();
  return true;
}

bool Adafruit_FlashTransport_SPI::writeCommand(uint8_t command,
                                               const uint8_t *data,
                                               size_t length) {
  if (_spi == nullptr || (length > 0U && data == nullptr)) {
    return false;
  }
  if (!writeEnable()) {
    return false;
  }
  _spi->beginTransaction(_settings);
  select();
  _spi->transfer(command);
  for (size_t i = 0; i < length; ++i) {
    _spi->transfer(data[i]);
  }
  deselect();
  _spi->endTransaction();
  return waitReady(100UL);
}

bool Adafruit_FlashTransport_SPI::eraseCommand(uint8_t command,
                                               uint32_t address) {
  if (_spi == nullptr || !writeEnable()) {
    return false;
  }
  _spi->beginTransaction(_settings);
  select();
  _spi->transfer(command);
  writeAddress(address);
  deselect();
  _spi->endTransaction();
  return waitReady(command == kCmdSectorErase4k ? 1000UL : 60000UL);
}

bool Adafruit_FlashTransport_SPI::readMemory(uint32_t address,
                                             uint8_t *data,
                                             size_t length) {
  if (_spi == nullptr || data == nullptr) {
    return false;
  }
  if (!waitReady(100UL)) {
    return false;
  }
  _spi->beginTransaction(_settings);
  select();
  _spi->transfer(kCmdReadData);
  writeAddress(address);
  for (size_t i = 0; i < length; ++i) {
    data[i] = _spi->transfer(0xFFU);
  }
  deselect();
  _spi->endTransaction();
  return true;
}

bool Adafruit_FlashTransport_SPI::writeMemory(uint32_t address,
                                              const uint8_t *data,
                                              size_t length) {
  if (_spi == nullptr || data == nullptr) {
    return false;
  }
  size_t offset = 0U;
  while (offset < length) {
    const size_t pageRemaining = kPageSize - ((address + offset) & (kPageSize - 1U));
    size_t chunk = length - offset;
    if (chunk > pageRemaining) {
      chunk = pageRemaining;
    }
    if (!writeEnable()) {
      return false;
    }
    _spi->beginTransaction(_settings);
    select();
    _spi->transfer(kCmdPageProgram);
    writeAddress(address + offset);
    for (size_t i = 0; i < chunk; ++i) {
      _spi->transfer(data[offset + i]);
    }
    deselect();
    _spi->endTransaction();
    if (!waitReady(100UL)) {
      return false;
    }
    offset += chunk;
  }
  return true;
}

bool Adafruit_FlashTransport_SPI::writeEnable() {
  return runCommand(kCmdWriteEnable);
}

bool Adafruit_FlashTransport_SPI::waitReady(uint32_t timeoutMs) {
  const uint32_t startMs = millis();
  uint8_t status = 0x01U;
  while ((status & 0x01U) != 0U) {
    if (!readCommand(kCmdReadStatus, &status, 1U)) {
      return false;
    }
    if ((millis() - startMs) > timeoutMs) {
      return false;
    }
    if ((status & 0x01U) != 0U) {
      delay(1);
    }
  }
  return true;
}

void Adafruit_FlashTransport_SPI::select() {
  digitalWrite(_cs, LOW);
}

void Adafruit_FlashTransport_SPI::deselect() {
  digitalWrite(_cs, HIGH);
}

void Adafruit_FlashTransport_SPI::writeAddress(uint32_t address) {
  _spi->transfer(static_cast<uint8_t>((address >> 16U) & 0xFFU));
  _spi->transfer(static_cast<uint8_t>((address >> 8U) & 0xFFU));
  _spi->transfer(static_cast<uint8_t>(address & 0xFFU));
}
