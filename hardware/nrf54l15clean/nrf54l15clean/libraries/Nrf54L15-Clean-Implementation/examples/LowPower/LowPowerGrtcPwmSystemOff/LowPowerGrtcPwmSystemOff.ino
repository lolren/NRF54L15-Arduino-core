#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

#if !defined(PIN_GRTC_PWM)
#error "This board variant must expose PIN_GRTC_PWM for the fixed P0.03 GRTC PWM output."
#endif

static constexpr uint8_t kGrtcPwmPin = PIN_GRTC_PWM;
static constexpr char kGrtcPwmPinLabel[] = "PIN_GRTC_PWM / P0.03";

static constexpr uint8_t kDuty8 = 128U;
static constexpr uint32_t kSleepMs = 5000UL;
static constexpr uint32_t kDebugDetachSettleMs = 2000UL;
static constexpr uintptr_t kDhcsrAddress = 0xE000EDF0UL;
static constexpr uint32_t kDhcsrDebugEnableMask = 1UL;

static GrtcPwm g_pwm;
static PowerManager g_power;
__attribute__((section(".noinit"))) static uint32_t g_bootCount;

bool wokeFromGrtcOrSystemOff(uint32_t resetReason) {
  return ((resetReason & RESET_RESETREAS_OFF_Msk) != 0U) ||
         ((resetReason & RESET_RESETREAS_GRTC_Msk) != 0U);
}

}  // namespace

void setup() {
  const uint32_t resetReason = nrf54ResetReason();
  const uint32_t resetReasonRegister = NRF_RESET->RESETREAS;
  const uint32_t dhcsr =
      *reinterpret_cast<volatile const uint32_t*>(kDhcsrAddress);
  const uint32_t systemOffAbortStage = nrf54SystemOffAbortStage();
  nrf54ClearSystemOffAbortStage();
  ++g_bootCount;

  Serial.begin(115200);
  delay(250);

  Serial.println("LowPowerGrtcPwmSystemOff");
  Serial.print("boot=");
  Serial.println(g_bootCount);
  Serial.print("wake_from_grtc_or_off=");
  Serial.println(wokeFromGrtcOrSystemOff(resetReason) ? 1 : 0);
  Serial.print("system_off_abort_stage=");
  Serial.println(systemOffAbortStage);
  Serial.print("reset_reason_snapshot=0x");
  Serial.println(resetReason, HEX);
  Serial.print("reset_reason_register=0x");
  Serial.println(resetReasonRegister, HEX);
  Serial.print("reset_reason_off=");
  Serial.print((resetReason & RESET_RESETREAS_OFF_Msk) != 0U ? 1 : 0);
  Serial.print(" grtc=");
  Serial.print((resetReason & RESET_RESETREAS_GRTC_Msk) != 0U ? 1 : 0);
  Serial.print(" dif=");
  Serial.print((resetReason & RESET_RESETREAS_DIF_Msk) != 0U ? 1 : 0);
  Serial.print(" sreq=");
  Serial.println((resetReason & RESET_RESETREAS_SREQ_Msk) != 0U ? 1 : 0);
  Serial.print("debug_c_debugen=");
  Serial.println((dhcsr & kDhcsrDebugEnableMask) != 0U ? 1 : 0);
  if ((resetReason & RESET_RESETREAS_DIF_Msk) != 0U &&
      !wokeFromGrtcOrSystemOff(resetReason)) {
    // A pyOCD reset starts the external Debug Interface. Give the probe time
    // to release its power request before testing a real timed System OFF.
    Serial.print("debug_interface_settle_ms=");
    Serial.println(kDebugDetachSettleMs);
    Serial.flush();
    delay(kDebugDetachSettleMs);
  }
  Serial.print("pin=");
  Serial.println(kGrtcPwmPinLabel);
  Serial.print("frequency_hz=");
  Serial.println(GrtcPwm::frequencyHz());
  Serial.println("Observe the fixed P0.03 waveform while the MCU drops into SYSTEM OFF.");
  Serial.println("On XIAO Sense this pin is shared with the Wire1/IMU route, so the IMU/mic rail is turned off first.");

  (void)BoardControl::setBatterySenseEnabled(false);
  (void)BoardControl::setImuMicEnabled(false);
  delay(5);

  if (!g_pwm.beginArduinoPin(kGrtcPwmPin, kDuty8, GrtcClockSource::kLfxo, true)) {
    Serial.println("GRTC PWM begin failed");
    while (true) {
      delay(1000);
    }
  }

  delay(100);
  Serial.print("Entering SYSTEM OFF for ");
  Serial.print(kSleepMs);
  Serial.println(" ms");
  Serial.flush();
  g_power.systemOffTimedWakeMs(kSleepMs);
}

void loop() {}
