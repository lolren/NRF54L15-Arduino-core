#pragma once

#include <stddef.h>
#include <stdint.h>

namespace xiao_nrf54l15 {

// nRF54 ICACHE is a unified, write-around instruction/data cache. CPU stores
// reach memory directly, so there are no dirty cache lines to write back.
class Cache {
 public:
  inline static void enable() {
    writeReg(kEnable, 1);
    instructionBarrier();
  }

  inline static void disable() {
    dataBarrier();
    writeReg(kEnable, 0);
    instructionBarrier();
  }

  inline static bool isEnabled() { return readReg(kEnable) != 0; }

  inline static void invalidateDataCache() { invalidateAll(); }

  // ICACHE uses write-around allocation. A data barrier is the required DMA
  // hand-off; the peripheral has no clean/write-back task.
  inline static void cleanDataCache() { dataBarrier(); }

  inline static void cleanInvalidateDataCache() { invalidateAll(); }

  inline static void invalidateInstructionCache() { invalidateAll(); }

  inline static void invalidateDataCacheLine(uint32_t addr) {
    invalidateLine(addr);
  }

  inline static void cleanDataCacheLine(uint32_t) { dataBarrier(); }

  inline static void cleanInvalidateDataCacheLine(uint32_t addr) {
    invalidateLine(addr);
  }

  inline static void invalidateInstructionCacheLine(uint32_t addr) {
    invalidateLine(addr);
  }

  inline static void cleanForDma(const void* ptr, size_t len) {
    if (ptr == nullptr || len == 0) return;
    dataBarrier();
  }

  inline static void invalidateForDma(void* ptr, size_t len) {
    if (ptr == nullptr || len == 0) return;
    forEachLine(ptr, len, invalidateLine);
  }

  inline static void cleanInvalidateForDma(void* ptr, size_t len) {
    invalidateForDma(ptr, len);
  }

  inline static bool isDataInvalidatePending() { return isBusy(); }
  inline static bool isDataCleanPending() { return false; }
  inline static bool isInstrInvalidatePending() { return isBusy(); }

  inline static uint32_t readReg(uint32_t offset) {
    return *reinterpret_cast<const volatile uint32_t*>(kBase + offset);
  }

  inline static void writeReg(uint32_t offset, uint32_t value) {
    *reinterpret_cast<volatile uint32_t*>(kBase + offset) = value;
  }

 private:
  static constexpr uintptr_t kBase = 0xE0082000UL;
  static constexpr uint32_t kTasksInvalidateCache = 0x008;
  static constexpr uint32_t kTasksInvalidateLine = 0x014;
  static constexpr uint32_t kStatus = 0x400;
  static constexpr uint32_t kEnable = 0x404;
  static constexpr uint32_t kLineAddr = 0x410;
  static constexpr uintptr_t kLineSize = 32;
  static constexpr uint32_t kDefaultSpinLimit = 1000000UL;

  inline static bool isBusy() { return (readReg(kStatus) & 1UL) != 0; }

  inline static bool waitReady(uint32_t spinLimit = kDefaultSpinLimit) {
    while (spinLimit-- != 0) {
      if (!isBusy()) return true;
    }
    return false;
  }

  inline static void invalidateAll() {
    dataBarrier();
    if (!waitReady()) return;
    writeReg(kTasksInvalidateCache, 1);
    (void)waitReady();
    instructionBarrier();
  }

  inline static void invalidateLine(uint32_t addr) {
    dataBarrier();
    if (!waitReady()) return;
    writeReg(kLineAddr, addr & ~static_cast<uint32_t>(kLineSize - 1));
    writeReg(kTasksInvalidateLine, 1);
    (void)waitReady();
    instructionBarrier();
  }

  inline static void forEachLine(void* ptr, size_t len,
                                 void (*operation)(uint32_t)) {
    const uintptr_t first = reinterpret_cast<uintptr_t>(ptr) & ~(kLineSize - 1);
    const uintptr_t lastByte = reinterpret_cast<uintptr_t>(ptr) + len - 1;
    const uintptr_t last = lastByte & ~(kLineSize - 1);
    for (uintptr_t addr = first;; addr += kLineSize) {
      operation(static_cast<uint32_t>(addr));
      if (addr == last) break;
    }
  }

  inline static void dataBarrier() {
#if defined(__arm__) || defined(__thumb__)
    __asm volatile("dsb 0xF" ::: "memory");
#else
    __asm volatile("" ::: "memory");
#endif
  }

  inline static void instructionBarrier() {
#if defined(__arm__) || defined(__thumb__)
    __asm volatile("isb 0xF" ::: "memory");
#else
    __asm volatile("" ::: "memory");
#endif
  }
};

}  // namespace xiao_nrf54l15
