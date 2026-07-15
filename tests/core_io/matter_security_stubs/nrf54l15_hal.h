#pragma once

#include <stddef.h>
#include <stdint.h>

namespace xiao_nrf54l15 {

class CracenRng {
 public:
  explicit CracenRng(uint32_t = 0U, uint32_t = 0U) {}

  bool begin(uint32_t = 400000UL) {
    active_ = true;
    return true;
  }

  void end() { active_ = false; }

  bool fill(void* data, size_t length, uint32_t = 400000UL) {
    if (data == nullptr && length != 0U) {
      return false;
    }
    uint8_t* bytes = static_cast<uint8_t*>(data);
    for (size_t i = 0U; i < length; ++i) {
      uint64_t& value = state();
      value ^= value << 13U;
      value ^= value >> 7U;
      value ^= value << 17U;
      bytes[i] = static_cast<uint8_t>(value >> 24U);
    }
    return true;
  }

  bool randomWord(uint32_t* outWord, uint32_t spinLimit = 400000UL) {
    return fill(outWord, sizeof(*outWord), spinLimit);
  }

  bool healthy() const { return true; }
  bool active() const { return active_; }

 private:
  static uint64_t& state() {
    static uint64_t value = 0x4D41545445525452ULL;
    return value;
  }

  bool active_ = false;
};

class Ecb {
 public:
  explicit Ecb(uint32_t = 0U) {}

  bool encryptBlock(const uint8_t key[16], const uint8_t plaintext[16],
                    uint8_t ciphertext[16], uint32_t = 200000UL) {
    if (key == nullptr || plaintext == nullptr || ciphertext == nullptr) {
      return false;
    }
    uint8_t carry = 0xA7U;
    for (size_t i = 0U; i < 16U; ++i) {
      carry = static_cast<uint8_t>(carry + key[(i * 5U) & 0x0FU] +
                                   plaintext[(i * 3U) & 0x0FU] + i);
      ciphertext[i] = static_cast<uint8_t>(
          carry ^ key[(i + 7U) & 0x0FU] ^ plaintext[i]);
    }
    return true;
  }
};

}  // namespace xiao_nrf54l15
