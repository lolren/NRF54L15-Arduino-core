#include "Adafruit_SPIFlash.h"

#include <string.h>

namespace {

static constexpr uint8_t kCmdReadJedecId = 0x9FU;
static constexpr uint8_t kCmdReadStatus = 0x05U;
static constexpr uint8_t kCmdSectorErase4k = 0x20U;
static constexpr uint8_t kCmdChipErase = 0xC7U;

static const SPIFlash_Device_t kDefaultFlashDevices[] = {
    PY25Q64HA,
    PY25Q64HA_ALT_JEDEC,
    MX25R6435F,
};

static constexpr size_t kDefaultFlashDeviceCount =
    sizeof(kDefaultFlashDevices) / sizeof(kDefaultFlashDevices[0]);

static bool jedecMatches(const SPIFlash_Device_t &device,
                         const uint8_t id[3]) {
  return device.manufacturer_id == id[0] &&
         device.memory_type == id[1] &&
         device.capacity == id[2];
}

}  // namespace

Adafruit_SPIFlash::Adafruit_SPIFlash(Adafruit_FlashTransport *transport)
    : _transport(transport), _device{}, _begun(false),
      _fsCache(nullptr), _fsCacheBase(0U), _fsCacheValid(false),
      _fsCacheDirty(false) {}

Adafruit_SPIFlash::~Adafruit_SPIFlash() {
  end();
}

bool Adafruit_SPIFlash::begin(const SPIFlash_Device_t *devices, size_t count) {
  if (_transport == nullptr) {
    return false;
  }

  if (devices == nullptr || count == 0U) {
    devices = kDefaultFlashDevices;
    count = kDefaultFlashDeviceCount;
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
      _fsCacheValid = false;
      _fsCacheDirty = false;
      return true;
    }
  }

  _transport->end();
  return false;
}

void Adafruit_SPIFlash::end() {
  (void)flushFsCache();
  delete[] _fsCache;
  _fsCache = nullptr;
  _fsCacheBase = 0U;
  _fsCacheValid = false;
  _fsCacheDirty = false;

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

bool Adafruit_SPIFlash::blockRangeToAddress(uint32_t block, size_t count,
                                            uint32_t *address,
                                            size_t *length) const {
  if (address == nullptr || length == nullptr) {
    return false;
  }

  if (count == 0U) {
    *address = 0U;
    *length = 0U;
    return true;
  }

  const uint64_t start =
      static_cast<uint64_t>(block) * kFilesystemBlockSize;
  const uint64_t bytes =
      static_cast<uint64_t>(count) * kFilesystemBlockSize;
  const uint64_t flashSize = static_cast<uint64_t>(size());

  if (start > 0xFFFFFFFFULL || bytes > 0xFFFFFFFFULL ||
      (start + bytes) > flashSize) {
    return false;
  }

  *address = static_cast<uint32_t>(start);
  *length = static_cast<size_t>(bytes);
  return true;
}

bool Adafruit_SPIFlash::ensureFsCache() {
  if (_fsCache != nullptr) {
    return true;
  }

  _fsCache = new uint8_t[kFilesystemEraseSize];
  return _fsCache != nullptr;
}

bool Adafruit_SPIFlash::flushFsCache() {
  if (!_fsCacheValid || !_fsCacheDirty) {
    return true;
  }
  if (!_begun || _transport == nullptr || _fsCache == nullptr) {
    return false;
  }

  if (!eraseSector(_fsCacheBase) || !waitUntilReady()) {
    return false;
  }
  if (!writeBuffer(_fsCacheBase, _fsCache, kFilesystemEraseSize) ||
      !waitUntilReady()) {
    return false;
  }

  _fsCacheDirty = false;
  return true;
}

bool Adafruit_SPIFlash::loadFsCache(uint32_t baseAddress) {
  if (_fsCacheValid && _fsCacheBase == baseAddress) {
    return true;
  }
  if (!flushFsCache() || !ensureFsCache()) {
    return false;
  }
  if (!readBuffer(baseAddress, _fsCache, kFilesystemEraseSize)) {
    return false;
  }

  _fsCacheBase = baseAddress;
  _fsCacheValid = true;
  _fsCacheDirty = false;
  return true;
}

bool Adafruit_SPIFlash::isBusy() {
  if (!_begun || _transport == nullptr) {
    return false;
  }

  uint8_t status = 0x01U;
  if (!_transport->readCommand(kCmdReadStatus, &status, 1U)) {
    return true;
  }
  return (status & 0x01U) != 0U;
}

uint32_t Adafruit_SPIFlash::sectorCount() {
  return size() / kFilesystemBlockSize;
}

bool Adafruit_SPIFlash::syncDevice() {
  return flushFsCache() && waitUntilReady();
}

bool Adafruit_SPIFlash::readSector(uint32_t block, uint8_t *dst) {
  return readSectors(block, dst, 1U);
}

bool Adafruit_SPIFlash::readSectors(uint32_t block, uint8_t *dst, size_t ns) {
  if (dst == nullptr) {
    return false;
  }

  uint32_t address = 0U;
  size_t length = 0U;
  if (!blockRangeToAddress(block, ns, &address, &length)) {
    return false;
  }
  if (length == 0U) {
    return true;
  }

  return readBuffer(address, dst, length);
}

bool Adafruit_SPIFlash::writeSector(uint32_t block, const uint8_t *src) {
  return writeSectors(block, src, 1U);
}

bool Adafruit_SPIFlash::writeSectors(uint32_t block, const uint8_t *src,
                                     size_t ns) {
  if (src == nullptr) {
    return false;
  }

  uint32_t address = 0U;
  size_t length = 0U;
  if (!blockRangeToAddress(block, ns, &address, &length)) {
    return false;
  }
  if (length == 0U) {
    return true;
  }

  const uint8_t *cursor = src;
  uint32_t currentAddress = address;
  size_t remaining = length;
  while (remaining > 0U) {
    const uint32_t cacheBase =
        currentAddress & ~(kFilesystemEraseSize - 1UL);
    if (!loadFsCache(cacheBase)) {
      return false;
    }

    const size_t cacheOffset =
        static_cast<size_t>(currentAddress - cacheBase);
    const size_t cacheRoom =
        static_cast<size_t>(kFilesystemEraseSize) - cacheOffset;
    const size_t chunk = (remaining < cacheRoom) ? remaining : cacheRoom;

    memcpy(&_fsCache[cacheOffset], cursor, chunk);
    _fsCacheDirty = true;

    currentAddress += static_cast<uint32_t>(chunk);
    cursor += chunk;
    remaining -= chunk;
  }

  return flushFsCache();
}
