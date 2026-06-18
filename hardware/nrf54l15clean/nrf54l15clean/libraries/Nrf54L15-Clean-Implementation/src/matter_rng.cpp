#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && NRF54L15_CLEAN_MATTER_CORE_ENABLE
#include "matter_rng.h"

#include <string.h>
#include "nrf54l15_hal.h"

namespace xiao_nrf54l15 {

bool MatterRng::begin(uint32_t spinLimit) {
  CracenRng rng;
  if (!rng.begin(spinLimit)) {
    rng.end();
    rngActive_ = false;
    rngHealthy_ = false;
    return false;
  }

  rngHealthy_ = rng.healthy();
  rngActive_ = rng.active();
  rng.end();  // Release the hardware; re-open on each fill() call
  return rngActive_ && rngHealthy_;
}

void MatterRng::end() {
  rngActive_ = false;
  rngHealthy_ = false;
}

bool MatterRng::getRandomBytes(uint8_t* buffer, size_t length,
                                uint32_t spinLimit) {
  if (length == 0U) {
    return true;
  }
  if (buffer == nullptr) {
    return false;
  }

  CracenRng rng;
  if (!rng.begin(spinLimit)) {
    rng.end();
    return false;
  }

  const bool ok = rng.fill(buffer, length, spinLimit);
  rng.end();
  return ok;
}

uint32_t MatterRng::getRandomUint32(uint32_t spinLimit) {
  CracenRng rng;
  uint32_t word = 0U;

  if (rng.begin(spinLimit)) {
    rng.randomWord(&word, spinLimit);
    rng.end();
  }

  return word;
}

uint64_t MatterRng::getRandomUint64(uint32_t spinLimit) {
  uint8_t bytes[8] = {0};
  if (!getRandomBytes(bytes, sizeof(bytes), spinLimit)) {
    return 0ULL;
  }

  uint64_t value = 0ULL;
  for (size_t i = 0; i < 8; ++i) {
    value |= static_cast<uint64_t>(bytes[i]) << (i * 8U);
  }
  return value;
}

bool MatterRng::isReady() const {
  return rngActive_ && rngHealthy_;
}

bool MatterRng::healthy() const {
  return rngHealthy_;
}

}  // namespace xiao_nrf54l15
#endif // NRF54L15_CLEAN_MATTER_CORE_ENABLE
