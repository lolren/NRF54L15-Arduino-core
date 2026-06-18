#include "Arduino.h"

Nrf52CompatNvmc g_nrf52_compat_nvmc = {
    0U,           // CONFIG = read-only (REN), any write to CONFIG is accepted
    NVMC_READY_READY_Ready,  // READY always ready
    {0U},         // RESERVED[128]
    0U,           // ERASEPAGE
    0U            // ERASEALL = NoOperation
};

Nrf52CompatUicr g_nrf52_compat_uicr = {
    0U,           // NFCPINS
    {0U, 0U},     // PSELRESET[2]
    0U,           // APPROTECT
    0U,           // DEBUGCTRL
    0U,           // RESERVED
    {0U}          // NRFFW[15]
};

SchedulerClass Scheduler;
HwPWMCompat HwPWM0;
HwPWMCompat HwPWM1;

extern "C" void sd_power_system_off(void) {
  // nRF52 sd_power_system_off() expects immediate power-off.
  // On nRF54L, use WFI sleep since true SYSTEM OFF needs LFXO.
  // Loop in WFI until next reset.
  __disable_irq();
  while (1) {
    __WFI();
  }
}

extern "C" void NVIC_SystemReset(void) {
  softReset();
}

extern "C" void enterOTADfu(void) {
  softReset();
}

extern "C" void enterSerialDfu(void) {
  softReset();
}

extern "C" void dbgPrintVersion(void) {
  Serial.print("nRF54L15 clean core ");
  Serial.println(NRF54L15_CLEAN_CORE_VERSION_STRING);
}

extern "C" void dbgMemInfo(void) {
  Serial.print("Heap total: ");
  Serial.println(dbgHeapTotal());
  Serial.print("Heap used: ");
  Serial.println(dbgHeapUsed());
  Serial.print("Free heap: ");
  Serial.println(getFreeHeapSize());
}

void SchedulerClass::startLoop(void (*fn)(void)) {
  loop_fn_ = fn;
}

void SchedulerClass::run(void) {
  if (loop_fn_ != nullptr) {
    loop_fn_();
  }
}

void HwPWMCompat::addPin(uint8_t pin) {
  pinMode(pin, OUTPUT);
}

void HwPWMCompat::setResolution(uint8_t bits) {
  resolution_bits_ = bits;
  analogWriteResolution(bits);
}

void HwPWMCompat::writePin(uint8_t pin, uint32_t value, bool invert) {
  if (resolution_bits_ == 0U) {
    analogWrite(pin, static_cast<int>(value));
    return;
  }

  const uint32_t max_value =
      (resolution_bits_ >= 31U) ? 0x7FFFFFFFUL : ((1UL << resolution_bits_) - 1UL);
  if (value > max_value) {
    value = max_value;
  }
  if (invert) {
    value = max_value - value;
  }
  analogWrite(pin, static_cast<int>(value));
}
