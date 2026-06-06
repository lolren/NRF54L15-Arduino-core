/*
  ThreadExperimentalSleepyParent

  Minimal fixed-dataset parent for ThreadExperimentalSleepyChild.

  Flash this sketch to one board and ThreadExperimentalSleepyChild to a second
  board with Tools > Thread Core > Experimental Stage Core enabled. This sketch
  avoids Commissioner/Joiner traffic so sleepy child attach can be checked
  without MeshCoP noise.
*/

#include <nrf54_all.h>
#include "openthread_platform_nrf54l15.h"

#if !defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) || \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE == 0)
#error "Enable Tools > Thread Core > Experimental Stage Core"
#endif

namespace {

constexpr uint32_t kStatusPrintIntervalMs = 3000U;

xiao_nrf54l15::Nrf54ThreadExperimental gThread;
uint32_t gLastStatusPrintMs = 0U;

void printStatus() {
  Serial.print("sleepy_parent role=");
  Serial.print(gThread.roleName());
  Serial.print(" attached=");
  Serial.print(gThread.attached() ? 1 : 0);
  Serial.print(" rloc16=0x");
  Serial.print(gThread.rloc16(), HEX);
  Serial.print(" err=");
  Serial.println(static_cast<int>(gThread.lastError()));

  xiao_nrf54l15::OpenThreadPlatformSkeletonSnapshot snap = {};
  if (xiao_nrf54l15::OpenThreadPlatformSkeleton::snapshot(&snap)) {
    Serial.print("sleepy_parent channel=");
    Serial.println(snap.radioChannel);
    Serial.print("sleepy_parent pan=0x");
    Serial.println(snap.panId, HEX);
    Serial.print("sleepy_parent radio_state=");
    Serial.println(static_cast<int>(snap.radioState));
    Serial.print("sleepy_parent rx_on_when_idle=");
    Serial.println(snap.radioRxOnWhenIdle ? 1 : 0);
  }
  Serial.println();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) {
  }

  otOperationalDataset dataset = {};
  gThread.buildDemoDataset(&dataset);
  gThread.setActiveDataset(dataset);

  if (gThread.beginAsRouter(true)) {
    Serial.println("sleepy_parent boot=ok");
  } else {
    Serial.println("sleepy_parent boot=failed");
  }
}

void loop() {
  gThread.process();

  if (Serial && (millis() - gLastStatusPrintMs) >= kStatusPrintIntervalMs) {
    gLastStatusPrintMs = millis();
    printStatus();
  }
}
