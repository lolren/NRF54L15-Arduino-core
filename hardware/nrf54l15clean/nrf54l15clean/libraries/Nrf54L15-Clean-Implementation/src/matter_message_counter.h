#pragma once

#include <stdint.h>

namespace xiao_nrf54l15 {

// Tracks a 16-bit serial number using RFC 1982 half-range ordering. Callers
// should commit a value only after the surrounding message is structurally
// valid and belongs to the active exchange.
class MatterMessageCounter16 {
 public:
  bool canAccept(uint16_t value) const {
    if (!initialized_) {
      return true;
    }
    const uint16_t delta = static_cast<uint16_t>(value - highest_);
    return delta != 0U && delta < 0x8000U;
  }

  bool accept(uint16_t value) {
    if (!canAccept(value)) {
      return false;
    }
    highest_ = value;
    initialized_ = true;
    return true;
  }

  void reset() {
    highest_ = 0U;
    initialized_ = false;
  }

  bool initialized() const { return initialized_; }
  uint16_t highest() const { return highest_; }

 private:
  uint16_t highest_ = 0U;
  bool initialized_ = false;
};

}  // namespace xiao_nrf54l15
