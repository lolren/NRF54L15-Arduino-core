#include <assert.h>
#include <stdint.h>

#include <SystemLayerImplArduino.h>

namespace {

unsigned long gNowMs = 0U;
unsigned long gNowUs = 0U;
uint32_t gWorkCalls = 0U;
uint32_t gTimerCalls = 0U;
uint32_t gCancelledTimerCalls = 0U;
uint32_t gPlatformTimerCalls = 0U;
CHIP_ERROR gNestedHandleResult = CHIP_NO_ERROR;

void rescheduleWork(chip::System::Layer * layer, void *) {
  ++gWorkCalls;
  if (gWorkCalls == 1U) {
    assert(layer->ScheduleWork(rescheduleWork, nullptr) == CHIP_NO_ERROR);
    gNestedHandleResult = layer->HandleEvents();
  }
}

void cancelledTimer(chip::System::Layer *, void *) {
  ++gCancelledTimerCalls;
}

void rescheduleTimer(chip::System::Layer * layer, void *) {
  ++gTimerCalls;
  layer->CancelTimer(cancelledTimer, nullptr);
  if (gTimerCalls == 1U) {
    assert(layer->StartTimer(0U, rescheduleTimer, nullptr) == CHIP_NO_ERROR);
  }
}

void platformTimer(chip::System::Layer *, void *) {
  ++gPlatformTimerCalls;
}

}  // namespace

unsigned long millis() { return gNowMs; }
unsigned long micros() { return gNowUs; }

int main() {
  chip::System::LayerImplArduino layer;
  assert(layer.Init() == CHIP_NO_ERROR);

  assert(layer.ScheduleWork(rescheduleWork, nullptr) == CHIP_NO_ERROR);
  assert(layer.HandleEvents() == CHIP_NO_ERROR);
  assert(gWorkCalls == 1U);
  assert(gNestedHandleResult == CHIP_ERROR_INCORRECT_STATE);
  assert(layer.HandleEvents() == CHIP_NO_ERROR);
  assert(gWorkCalls == 2U);

  assert(layer.StartTimer(0U, rescheduleTimer, nullptr) == CHIP_NO_ERROR);
  assert(layer.StartTimer(0U, cancelledTimer, nullptr) == CHIP_NO_ERROR);
  assert(layer.HandleEvents() == CHIP_NO_ERROR);
  assert(gTimerCalls == 1U);
  assert(gCancelledTimerCalls == 0U);
  assert(layer.HandleEvents() == CHIP_NO_ERROR);
  assert(gTimerCalls == 2U);

  gNowMs = 0xFFFFFFF0UL;
  assert(layer.StartTimer(32U, cancelledTimer, nullptr) == CHIP_NO_ERROR);
  assert(layer.GetRemainingTime(cancelledTimer, nullptr) == 32U);
  gNowMs = 0x00000010UL;
  assert(layer.HandleEvents() == CHIP_NO_ERROR);
  assert(gCancelledTimerCalls == 1U);

  chip::System::Layer::PlatformTimer * timer = nullptr;
  assert(layer.NewTimer(platformTimer, nullptr, &timer) == CHIP_NO_ERROR);
  assert(timer != nullptr);
  gNowMs = 100U;
  assert(timer->Arm(1U) == CHIP_NO_ERROR);
  assert(layer.HandleEvents() == CHIP_NO_ERROR);
  assert(gPlatformTimerCalls == 0U);
  gNowMs = 101U;
  assert(layer.HandleEvents() == CHIP_NO_ERROR);
  assert(gPlatformTimerCalls == 1U);
  layer.FreeTimer(timer);

  layer.Shutdown();
  return 0;
}
