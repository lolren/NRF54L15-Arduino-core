#ifndef ADAFRUIT_SPIFLASH_H
#define ADAFRUIT_SPIFLASH_H

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "Adafruit_FlashTransport.h"
#include "flash_devices.h"

// Match Adafruit SdFat's FsBlockDeviceInterface without forcing SdFat to be
// installed for sketches that only use raw flash reads/writes.
#ifndef FsBlockDeviceInterface_h
#define FsBlockDeviceInterface_h
class FsBlockDeviceInterface {
public:
  virtual ~FsBlockDeviceInterface() {}
  virtual void end() {}
  virtual bool isBusy() = 0;
  virtual bool readSector(uint32_t sector, uint8_t *dst) = 0;
  virtual bool readSectors(uint32_t sector, uint8_t *dst, size_t ns) = 0;
  virtual uint32_t sectorCount() = 0;
  virtual bool syncDevice() = 0;
  virtual bool writeSector(uint32_t sector, const uint8_t *src) = 0;
  virtual bool writeSectors(uint32_t sector, const uint8_t *src, size_t ns) = 0;
  virtual bool syncBlocks() { return syncDevice(); }
  virtual bool readBlock(uint32_t block, uint8_t *dst) {
    return readSector(block, dst);
  }
  virtual bool readBlocks(uint32_t block, uint8_t *dst, size_t nb) {
    return readSectors(block, dst, nb);
  }
  virtual bool writeBlock(uint32_t block, const uint8_t *src) {
    return writeSector(block, src);
  }
  virtual bool writeBlocks(uint32_t block, const uint8_t *src, size_t nb) {
    return writeSectors(block, src, nb);
  }
};
#endif

class Adafruit_SPIFlash : public FsBlockDeviceInterface {
public:
  explicit Adafruit_SPIFlash(Adafruit_FlashTransport *transport);
  ~Adafruit_SPIFlash();

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

  bool isBusy();
  uint32_t sectorCount();
  bool syncDevice();
  bool readSector(uint32_t block, uint8_t *dst);
  bool readSectors(uint32_t block, uint8_t *dst, size_t ns);
  bool writeSector(uint32_t block, const uint8_t *src);
  bool writeSectors(uint32_t block, const uint8_t *src, size_t ns);

private:
  static const uint32_t kFilesystemBlockSize = 512UL;
  static const uint32_t kFilesystemEraseSize = 4096UL;

  Adafruit_FlashTransport *_transport;
  SPIFlash_Device_t _device;
  bool _begun;

  uint8_t *_fsCache;
  uint32_t _fsCacheBase;
  bool _fsCacheValid;
  bool _fsCacheDirty;

  bool blockRangeToAddress(uint32_t block, size_t count, uint32_t *address,
                           size_t *length) const;
  bool ensureFsCache();
  bool loadFsCache(uint32_t baseAddress);
  bool flushFsCache();
};

#endif
