#pragma once

#include <stddef.h>
#include <stdint.h>

namespace xiao_nrf54l15 {

// Unified random number generator for all Matter operations.
// Uses CRACEN hardware TRNG with no software PRNG fallback -
// if the hardware RNG is unavailable, operations fail rather
// than falling back to weak pseudo-random data.
class MatterRng {
 public:
  MatterRng() = default;

  // Initialize the CRACEN RNG. Call once at startup.
  bool begin(uint32_t spinLimit = 400000UL);
  void end();

  // Fill a buffer with cryptographically random bytes from CRACEN.
  // Returns true if all bytes were filled from the hardware RNG.
  bool getRandomBytes(uint8_t* buffer, size_t length,
                      uint32_t spinLimit = 400000UL);

  // Generate a random 32-bit value.
  uint32_t getRandomUint32(uint32_t spinLimit = 400000UL);

  // Generate a random 64-bit value.
  uint64_t getRandomUint64(uint32_t spinLimit = 400000UL);

  // Check if the RNG is healthy and available.
  bool isReady() const;

  // Check if the underlying hardware RNG is healthy.
  bool healthy() const;

 private:
  bool rngActive_ = false;
  bool rngHealthy_ = false;
};

}  // namespace xiao_nrf54l15
