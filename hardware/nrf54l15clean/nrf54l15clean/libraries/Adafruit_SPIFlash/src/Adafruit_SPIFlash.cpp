#include "Adafruit_SPIFlash.h"

namespace {

static constexpr uint8_t kCmdReadJedecId = 0x9FU;
static constexpr uint8_t kCmdReadStatus = 0x05U;
static constexpr uint8_t kCmdSectorErase4k = 0x20U;
static constexpr uint8_t kCmdChipErase = 0xC7U;

static bool jedecMatches(const SPIFlash_Device_t &device,
                         const uint8_t id[3]) {
  return device.manufacturer_id == id[0] &&
         device.memory_type == id[1] &&
         device.capacity == id[2];
}

}  // namespace

Adafruit_SPIFlash::Adafruit_SPIFlash(Adafruit_FlashTransport *transport)
    : _transport(transport), _device{}, _begun(false) {}

bool Adafruit_SPIFlash::begin(const SPIFlash_Device_t *devices, size_t count) {
  if (_transport == nullptr || devices == nullptr || count == 0U) {
    return false;
  }

  if (!_transport->begin()) {
    return false;
  }

  uint8_t id[3] = {0U, 0U, 0U};
  if (!_transport->readCommand(kCmdReadJedecId, id, sizeof(id))) {
    _transport->end();
    return false;
  }

  for (size_t i = 0; i < count; ++i) {
    if (jedecMatches(devices[i], id)) {
      _device = devices[i];
      _begun = true;
      return true;
    }
  }

  _transport->end();
  return false;
}

void Adafruit_SPIFlash::end() {
  if (_transport != nullptr) {
    _transport->end();
  }
  _begun = false;
}

uint32_t Adafruit_SPIFlash::size() const {
  return _begun ? _device.total_size : 0U;
}

uint32_t Adafruit_SPIFlash::readJEDECID() {
  if (_transport == nullptr) {
    return 0U;
  }

  uint8_t id[3] = {0U, 0U, 0U};
  if (!_transport->readCommand(kCmdReadJedecId, id, sizeof(id))) {
    return 0U;
  }

  return (static_cast<uint32_t>(id[0]) << 16U) |
         (static_cast<uint32_t>(id[1]) << 8U) |
         static_cast<uint32_t>(id[2]);
}

bool Adafruit_SPIFlash::getJEDECID(uint8_t *manufacturer,
                                   uint8_t *memoryType,
                                   uint8_t *capacity) {
  if (_transport == nullptr || manufacturer == nullptr ||
      memoryType == nullptr || capacity == nullptr) {
    return false;
  }

  uint8_t id[3] = {0U, 0U, 0U};
  if (!_transport->readCommand(kCmdReadJedecId, id, sizeof(id))) {
    return false;
  }

  *manufacturer = id[0];
  *memoryType = id[1];
  *capacity = id[2];
  return true;
}

bool Adafruit_SPIFlash::readBuffer(uint32_t address,
                                   uint8_t *buffer,
                                   size_t length) {
  return _begun && _transport != nullptr &&
         _transport->readMemory(address, buffer, length);
}

bool Adafruit_SPIFlash::writeBuffer(uint32_t address,
                                    const uint8_t *buffer,
                                    size_t length) {
  return _begun && _transport != nullptr &&
         _transport->writeMemory(address, buffer, length);
}

bool Adafruit_SPIFlash::eraseSector(uint32_t address) {
  return _begun && _transport != nullptr &&
         _transport->eraseCommand(kCmdSectorErase4k, address);
}

bool Adafruit_SPIFlash::eraseChip() {
  return _begun && _transport != nullptr &&
         _transport->runCommand(kCmdChipErase) &&
         waitUntilReady(60000UL);
}

bool Adafruit_SPIFlash::waitUntilReady(uint32_t timeoutMs) {
  if (!_begun || _transport == nullptr) {
    return false;
  }

  const uint32_t startMs = millis();
  uint8_t status = 0x01U;
  while ((status & 0x01U) != 0U) {
    if (!_transport->readCommand(kCmdReadStatus, &status, 1U)) {
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

bool Adafruit_SPIFlash::runCommand(uint8_t command) {
  return _transport != nullptr && _transport->runCommand(command);
}
