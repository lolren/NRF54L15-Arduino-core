#pragma once

#include <stdint.h>
#include "nrf54l15_regs.h"

namespace xiao_nrf54l15 {

// Periodic advertising requires a BLE controller to construct AUX_SYNC_IND,
// schedule secondary channels, and maintain the periodic event counter. A
// one-shot raw RADIO packet is not periodic advertising, so this legacy class
// deliberately fails closed until the controller exposes that feature.
class BlePeriodicAdvertising {
 public:
  explicit BlePeriodicAdvertising(uint32_t radioBase = nrf54l15::RADIO_BASE)
      : radioBase_(radioBase) {}

  inline static constexpr bool supported() { return false; }

  inline bool begin(const uint8_t* advData, uint8_t advDataLen,
                    uint16_t intervalMs) {
    (void)advData;
    (void)advDataLen;
    (void)intervalMs;
    return false;
  }

  inline void end() {}

  inline bool setData(const uint8_t* data, uint8_t len) {
    (void)data;
    (void)len;
    return false;
  }

  inline uint16_t intervalMs() const { return 30U; }

  inline bool setIntervalMs(uint16_t intervalMs) {
    (void)intervalMs;
    return false;
  }

  inline int8_t txPowerDbm() const { return 4; }

  inline bool setTxPowerDbm(int8_t dbm) {
    (void)dbm;
    return false;
  }

  inline bool isActive() const { return false; }
  inline uint32_t packetCount() const { return 0U; }
  inline uint32_t base() const { return radioBase_; }

 private:
  uint32_t radioBase_;
};

}  // namespace xiao_nrf54l15
