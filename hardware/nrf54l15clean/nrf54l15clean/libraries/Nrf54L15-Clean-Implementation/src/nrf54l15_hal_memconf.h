#pragma once

#include <stdint.h>

#include "nrf54l15_regs.h"

namespace xiao_nrf54l15 {

// MEMCONF controls each RAM section through POWER[n].CONTROL and POWER[n].RET.
// It does not expose task, event, interrupt, status, or RRAM protection blocks.
class Memconf {
 public:
  static constexpr uint32_t kUnsupportedProtectionStatus = 0xFFFFFFFFUL;

  inline static bool powerOnRamSection(uint8_t section) {
    return setPower(section, true);
  }

  inline static bool powerOffRamSection(uint8_t section) {
    return setPower(section, false);
  }

  // Retained for source compatibility. Since MEMCONF has no transition events,
  // these methods report the current configured state and never clear hardware.
  inline static bool ramOnEvent(uint8_t section, bool clear = true) {
    (void)clear;
    return isRamSectionPowered(section);
  }

  inline static bool ramOffEvent(uint8_t section, bool clear = true) {
    (void)clear;
    return validSection(section) && !isRamSectionPowered(section);
  }

  inline static uint32_t ramSectionStatus() {
    uint32_t mask = 0;
    for (uint8_t section = 0; section < kRamSectionCount; ++section) {
      if ((readReg(kPower0Control) & sectionMask(section)) != 0) {
        mask |= 1UL << section;
      }
    }
    return mask;
  }

  inline static bool isRamSectionPowered(uint8_t section) {
    return validSection(section) &&
           ((readReg(kPower0Control) & sectionMask(section)) != 0);
  }

  inline static bool setRamSectionRetention(uint8_t section, bool retain) {
    if (!validSection(section)) return false;
    const uint32_t mask = sectionMask(section);
    updateBits(kPower0Retention, mask, retain);
    if (sectionHasSecondRetentionBank(section)) {
      updateBits(kPower0Retention2, mask, retain);
    }
    return true;
  }

  inline static bool ramSectionRetentionEnabled(uint8_t section) {
    if (!validSection(section)) return false;
    const uint32_t mask = sectionMask(section);
    if ((readReg(kPower0Retention) & mask) == 0) return false;
    return !sectionHasSecondRetentionBank(section) ||
           (readReg(kPower0Retention2) & mask) != 0;
  }

  // RRAM access protection belongs to RRAMC/KMU, not MEMCONF.
  inline static uint32_t nvmcReadProtection() {
    return kUnsupportedProtectionStatus;
  }
  inline static uint32_t nvmcWriteProtection() {
    return kUnsupportedProtectionStatus;
  }

  inline static constexpr bool ramTransitionInterruptsSupported() {
    return false;
  }

  inline static bool enableRamOnInterrupt(uint8_t, bool = true) {
    return false;
  }
  inline static bool enableRamOffInterrupt(uint8_t, bool = true) {
    return false;
  }

  inline static uint32_t readReg(uint32_t offset) {
    return *reinterpret_cast<const volatile uint32_t*>(kBase + offset);
  }

  inline static void writeReg(uint32_t offset, uint32_t value) {
    *reinterpret_cast<volatile uint32_t*>(kBase + offset) = value;
  }

 private:
  static constexpr uintptr_t kBase = nrf54l15::MEMCONF_BASE;
#if defined(NRF54LM20A_XXAA) || defined(NRF54LM20B_XXAA)
  static constexpr uint8_t kRamSectionCount = 16;
#else
  static constexpr uint8_t kRamSectionCount = 8;
#endif
  static constexpr uint32_t kPower0Control = 0x500;
  static constexpr uint32_t kPower0Retention = 0x508;
  static constexpr uint32_t kPower0Retention2 = 0x50C;

  inline static bool validSection(uint8_t section) {
    return kBase != 0 && section < kRamSectionCount;
  }

  inline static uint32_t sectionMask(uint8_t section) {
    return 1UL << section;
  }

  inline static bool sectionHasSecondRetentionBank(uint8_t section) {
#if defined(NRF54LM20A_XXAA) || defined(NRF54LM20B_XXAA)
    (void)section;
    return false;
#else
    return section == 7U;
#endif
  }

  inline static void updateBits(uint32_t offset, uint32_t mask, bool set) {
    const uint32_t value = readReg(offset);
    writeReg(offset, set ? (value | mask) : (value & ~mask));
  }

  inline static bool setPower(uint8_t section, bool enabled) {
    if (!validSection(section)) return false;
    updateBits(kPower0Control, sectionMask(section), enabled);
    return true;
  }
};

}  // namespace xiao_nrf54l15
