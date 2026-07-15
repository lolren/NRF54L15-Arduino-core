#include <nrf54_all.h>

#include <CHIPError.h>
#include <CHIPProjectConfig.h>
#include <SystemLayer.h>
#include <SystemLayerImplArduino.h>

namespace {

chip::System::LayerImpl gSystemLayer;
uint32_t gPassCount = 0U;
uint32_t gFailCount = 0U;
uint32_t gTimerCallbacks = 0U;
uint32_t gWorkCallbacks = 0U;
uint32_t gPlatformTimerCallbacks = 0U;

void check(bool passed, const char* name) {
  Serial.print(passed ? "[PASS] " : "[FAIL] ");
  Serial.println(name);
  if (passed) {
    ++gPassCount;
  } else {
    ++gFailCount;
  }
}

void onTimer(chip::System::Layer*, void*) { ++gTimerCallbacks; }

void onWork(chip::System::Layer*, void*) { ++gWorkCallbacks; }

void onPlatformTimer(chip::System::Layer*, void*) {
  ++gPlatformTimerCallbacks;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) delay(10);

  Serial.println("=== CHIP System Layer Test ===");
  CHIP_ERROR error = gSystemLayer.Init();
  check(error == CHIP_NO_ERROR && gSystemLayer.IsInitialized(),
        "System layer initialization");
  if (error != CHIP_NO_ERROR) return;

  const uint64_t millisecondsBefore = chip::System::Clock::GetMilliseconds();
  const uint64_t microsecondsBefore = chip::System::Clock::GetMicroseconds();
  delay(2);
  check(chip::System::Clock::GetMilliseconds() >= millisecondsBefore + 1U &&
            chip::System::Clock::GetMicroseconds() > microsecondsBefore,
        "64-bit monotonic clocks advance");

  error = gSystemLayer.StartTimer(100U, onTimer, nullptr);
  const uint32_t remaining = gSystemLayer.GetRemainingTime(onTimer, nullptr);
  check(error == CHIP_NO_ERROR &&
            gSystemLayer.IsTimerActive(onTimer, nullptr) &&
            remaining > 0U && remaining <= 100U,
        "StartTimer and remaining time");

  error = gSystemLayer.ExtendTimerTo(150U, onTimer, nullptr);
  check(error == CHIP_NO_ERROR &&
            gSystemLayer.GetRemainingTime(onTimer, nullptr) > 100U,
        "ExtendTimerTo only moves deadline later");
  gSystemLayer.CancelTimer(onTimer, nullptr);
  check(!gSystemLayer.IsTimerActive(onTimer, nullptr), "CancelTimer");

  error = gSystemLayer.ScheduleWork(onWork, nullptr);
  if (error == CHIP_NO_ERROR) error = gSystemLayer.ScheduleWork(onWork, nullptr);
  if (error == CHIP_NO_ERROR) error = gSystemLayer.HandleEvents();
  check(error == CHIP_NO_ERROR && gWorkCallbacks == 2U,
        "ScheduleWork preserves duplicate callbacks");

  error = gSystemLayer.StartTimer(5U, onTimer, nullptr);
  delay(10);
  if (error == CHIP_NO_ERROR) error = gSystemLayer.HandleEvents();
  check(error == CHIP_NO_ERROR && gTimerCallbacks == 1U,
        "Expired timer dispatch");

  chip::System::Layer::PlatformTimer* platformTimer = nullptr;
  error = gSystemLayer.NewTimer(onPlatformTimer, nullptr, &platformTimer);
  if (error == CHIP_NO_ERROR) error = platformTimer->Arm(5000U);
  delay(10);
  if (error == CHIP_NO_ERROR) error = gSystemLayer.HandleEvents();
  check(error == CHIP_NO_ERROR && gPlatformTimerCallbacks == 1U,
        "Microsecond PlatformTimer arm and dispatch");

  if (platformTimer != nullptr) {
    error = platformTimer->Arm(5000U);
    platformTimer->Disarm();
    delay(10);
    if (error == CHIP_NO_ERROR) error = gSystemLayer.HandleEvents();
    check(error == CHIP_NO_ERROR && gPlatformTimerCallbacks == 1U,
          "PlatformTimer disarm");
    gSystemLayer.FreeTimer(platformTimer);
  } else {
    check(false, "PlatformTimer disarm");
  }

  gSystemLayer.Shutdown();
  check(!gSystemLayer.IsInitialized(), "System layer shutdown");
  Serial.print("system_result pass=");
  Serial.print(gPassCount);
  Serial.print(" fail=");
  Serial.println(gFailCount);
}

void loop() {}
