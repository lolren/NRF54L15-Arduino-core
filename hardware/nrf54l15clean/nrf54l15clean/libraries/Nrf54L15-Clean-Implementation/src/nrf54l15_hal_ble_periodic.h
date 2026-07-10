#pragma once

#include <stdint.h>
#include <string.h>

#include "nrf54l15_regs.h"

namespace xiao_nrf54l15 {

// Periodic advertising requires a BLE controller to construct AUX_SYNC_IND,
// schedule secondary channels, and maintain the periodic event counter. A
// one-shot raw RADIO packet is not periodic advertising, so this legacy class
// deliberately fails closed until the controller exposes that feature.
class BlePeriodicAdvertising {
 public:
  explicit BlePeriodicAdvertising(uint32_t radioBase = nrf54l15::RADIO_BASE)
      : radioBase_(radioBase), advDataLen_(0), intervalMs_(30),
        txPowerDbm_(4), packetCount_(0), active_(false) {}

  inline static constexpr bool supported() { return false; }

  inline bool begin(const uint8_t* advData, uint8_t advDataLen,
                    uint16_t intervalMs) {
    end();
    if (radioBase_ == 0 || intervalMs < 3 ||
        (advDataLen != 0 && advData == nullptr)) {
      return false;
    }
    (void)setData(advData, advDataLen);
    intervalMs_ = intervalMs;
    return false;
  }

  inline void end() { active_ = false; }

  inline bool setData(const uint8_t* data, uint8_t len) {
    if (active_ || (len != 0 && data == nullptr)) return false;
    if (len != 0) memcpy(advData_, data, len);
    advDataLen_ = len;
    return true;
  }

  inline uint16_t intervalMs() const { return intervalMs_; }

  inline bool setIntervalMs(uint16_t intervalMs) {
    if (intervalMs < 3 || active_) return false;
    intervalMs_ = intervalMs;
    return true;
  }

  inline int8_t txPowerDbm() const { return txPowerDbm_; }

  inline bool setTxPowerDbm(int8_t dbm) {
    if (dbm < -40 || dbm > 8 || active_) return false;
    txPowerDbm_ = dbm;
    return true;
  }

  inline bool isActive() const { return active_; }
  inline uint32_t packetCount() const { return packetCount_; }
  inline uint32_t base() const { return radioBase_; }

 private:
  uint32_t radioBase_;
  uint8_t advData_[255];
  uint8_t advDataLen_;
  uint16_t intervalMs_;
  int8_t txPowerDbm_;
  uint32_t packetCount_;
  bool active_;
};

}  // namespace xiao_nrf54l15
