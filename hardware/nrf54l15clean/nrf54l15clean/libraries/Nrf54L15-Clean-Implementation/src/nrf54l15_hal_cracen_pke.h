#pragma once

#include <stddef.h>
#include <stdint.h>

#include "nrf54l15_regs.h"

namespace xiao_nrf54l15 {

class CracenPke {
 public:
  inline static void enable() {
    wr(kEnable, rd(kEnable) | kPkeIkgEnableMask);
  }

  inline static void disable() {
    wr(kEnable, rd(kEnable) & ~kPkeIkgEnableMask);
  }

  inline static bool isEnabled() {
    return (rd(kEnable) & kPkeIkgEnableMask) != 0;
  }

  inline static bool ready() { return isEnabled() && !pkeBusy(); }

  inline static bool waitForReady(uint32_t spinLimit = 1000000UL) {
    while (spinLimit-- != 0) {
      if (ready()) return true;
    }
    return false;
  }

  inline static void enablePkeIkg() { enable(); }
  inline static void disablePkeIkg() { disable(); }

  inline static uint32_t pkeStatus() { return rdCore(kPkStatus); }

  inline static bool pkeBusy() {
    return (pkeStatus() & kPkBusyMask) != 0;
  }

  inline static bool pkeReady() { return ready(); }

  inline static bool waitPkeReady(uint32_t spinLimit = 1000000UL) {
    while (spinLimit-- != 0) {
      if (pkeReady()) return true;
    }
    return false;
  }

  inline static void writeData(uint32_t offset, uint32_t value) {
    if ((offset & 3UL) != 0 || offset >= pkeDataSize()) return;
    *reinterpret_cast<volatile uint32_t*>(kDataBase + offset) = value;
  }

  inline static void writeDataBlock(uint32_t offset, const uint32_t* words,
                                    size_t count) {
    if (words == nullptr || (offset & 3UL) != 0U ||
        offset > pkeDataSize() ||
        count > (pkeDataSize() - offset) / sizeof(uint32_t)) {
      return;
    }
    for (size_t i = 0; i < count; ++i) writeData(offset + i * 4, words[i]);
  }

  inline static void writeDataBytes(uint32_t offset, const uint8_t* bytes,
                                    size_t count) {
    if (bytes == nullptr || (offset & 3UL) != 0U || offset > pkeDataSize() ||
        count > pkeDataSize() - offset) {
      return;
    }
    for (size_t i = 0; i < count; i += 4) {
      uint32_t word = readData(offset + static_cast<uint32_t>(i));
      const size_t chunk = (count - i < 4) ? count - i : 4;
      const uint32_t preserveMask =
          (chunk == 4U) ? 0U : ~((1UL << (chunk * 8U)) - 1UL);
      word &= preserveMask;
      for (size_t byte = 0; byte < chunk; ++byte) {
        word |= static_cast<uint32_t>(bytes[i + byte]) << (byte * 8);
      }
      writeData(offset + static_cast<uint32_t>(i), word);
    }
  }

  inline static uint32_t readData(uint32_t offset) {
    if ((offset & 3UL) != 0 || offset >= pkeDataSize()) return 0;
    return *reinterpret_cast<const volatile uint32_t*>(kDataBase + offset);
  }

  inline static void readDataBlock(uint32_t offset, uint32_t* words,
                                   size_t count) {
    if (words == nullptr || (offset & 3UL) != 0U ||
        offset > pkeDataSize() ||
        count > (pkeDataSize() - offset) / sizeof(uint32_t)) {
      return;
    }
    for (size_t i = 0; i < count; ++i) words[i] = readData(offset + i * 4);
  }

  inline static void readDataBytes(uint32_t offset, uint8_t* bytes,
                                   size_t count) {
    if (bytes == nullptr || (offset & 3UL) != 0U || offset > pkeDataSize() ||
        count > pkeDataSize() - offset) {
      return;
    }
    for (size_t i = 0; i < count; i += 4) {
      const uint32_t word = readData(offset + static_cast<uint32_t>(i));
      const size_t chunk = (count - i < 4) ? count - i : 4;
      for (size_t byte = 0; byte < chunk; ++byte) {
        bytes[i + byte] = static_cast<uint8_t>(word >> (byte * 8));
      }
    }
  }

  inline static void writeCode(uint32_t offset, uint32_t value) {
    if ((offset & 3UL) != 0 || offset >= pkeCodeSize()) return;
    *reinterpret_cast<volatile uint32_t*>(kCodeBase + offset) = value;
  }

  inline static void writeCodeBlock(uint32_t offset, const uint32_t* words,
                                    size_t count) {
    if (words == nullptr || (offset & 3UL) != 0U ||
        offset > pkeCodeSize() ||
        count > (pkeCodeSize() - offset) / sizeof(uint32_t)) {
      return;
    }
    for (size_t i = 0; i < count; ++i) writeCode(offset + i * 4, words[i]);
  }

  inline static bool issueCommand(uint32_t command) {
    if (!ready()) return false;
    wrCore(kPkCommand, command);
    wrCore(kPkControl, kPkStartMask);
    return true;
  }

  inline static bool pkeIkgEvent(bool clear = true) {
    const bool set = rd(kEventsPkeIkg) != 0;
    if (set && clear) {
      wrCore(kPkControl, kPkClearIrqMask);
      wrCore(kIkgPkeControl, kIkgPkeClearIrqMask);
      wr(kEventsPkeIkg, 0);
    }
    return set;
  }

  inline static void enablePkeIkgInterrupt(bool enable = true) {
    wr(enable ? kIntenSet : kIntenClr, kPkeIkgEnableMask);
  }

  inline static constexpr size_t pkeDataSize() {
#if defined(NRF54LM20A_XXAA) || defined(NRF54LM20B_XXAA)
    return 15U * 512U;
#else
    return 8192U;
#endif
  }
  inline static constexpr size_t pkeCodeSize() { return 16384; }

 private:
  static constexpr uintptr_t kBase = nrf54l15::CRACEN_BASE;
  static constexpr uintptr_t kCoreBase = nrf54l15::CRACENCORE_BASE;
  static constexpr uintptr_t kDataBase = kCoreBase + 0x8000;
  static constexpr uintptr_t kCodeBase = kCoreBase + 0xC000;
  static constexpr uint32_t kEventsPkeIkg = 0x108;
  static constexpr uint32_t kIntenSet = 0x304;
  static constexpr uint32_t kIntenClr = 0x308;
  static constexpr uint32_t kEnable = 0x400;
  static constexpr uint32_t kPkeIkgEnableMask = 1UL << 2;
  static constexpr uint32_t kPkStatus = 0x200C;
  static constexpr uint32_t kPkBusyMask = 1UL << 16;
  static constexpr uint32_t kPkCommand = 0x2004;
  static constexpr uint32_t kPkControl = 0x2008;
  static constexpr uint32_t kPkStartMask = 1UL << 0;
  static constexpr uint32_t kPkClearIrqMask = 1UL << 1;
  static constexpr uint32_t kIkgPkeControl = 0x301C;
  static constexpr uint32_t kIkgPkeClearIrqMask = 1UL << 1;

  inline static uint32_t rd(uint32_t offset) {
    return *reinterpret_cast<const volatile uint32_t*>(kBase + offset);
  }

  inline static void wr(uint32_t offset, uint32_t value) {
    *reinterpret_cast<volatile uint32_t*>(kBase + offset) = value;
  }

  inline static uint32_t rdCore(uint32_t offset) {
    return *reinterpret_cast<const volatile uint32_t*>(kCoreBase + offset);
  }

  inline static void wrCore(uint32_t offset, uint32_t value) {
    *reinterpret_cast<volatile uint32_t*>(kCoreBase + offset) = value;
  }
};

}  // namespace xiao_nrf54l15
