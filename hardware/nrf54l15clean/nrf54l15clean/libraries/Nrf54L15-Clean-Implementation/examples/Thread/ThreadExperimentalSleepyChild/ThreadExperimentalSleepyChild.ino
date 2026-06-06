/*
  ThreadExperimentalSleepyChild

  Demonstrates sleepy end device (SED) behavior on nRF54L15.

  SWD markers in gSleepyMarkers[]. Locate the symbol in the ELF/map before
  reading it with pyOCD; the absolute address depends on the linker layout.
    [0] = started
    [1] = attached
    [2] = role (0=disabled, 1=detached, 2=child, 3=router, 4=leader)
    [3] = rloc16
    [4] = poll_period
    [5] = rx_on_when_idle (0=sleepy, 1=always-on)
    [6] = loop_count
    [7] = attach_attempts
    [8] = setup_phase (0-5 for diagnostics)
    [9] = dataset_configured
    [10] = last_error
    [11] = dataset_restore_error
*/

#include <nrf54_all.h>
#include "openthread_platform_nrf54l15.h"

#if !defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) || \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE == 0)
#error "Enable Tools > Thread Core > Experimental Stage Core"
#endif

namespace {

constexpr uint32_t kStatusPrintIntervalMs = 3000U;
constexpr uint32_t kPollPeriodMs = 3000U;

xiao_nrf54l15::Nrf54ThreadExperimental gThread;
uint32_t gLastStatusPrintMs = 0U;

__attribute__((used, section(".noinit"))) volatile uint32_t gSleepyMarkers[16] = {0};

#define M_STARTED      gSleepyMarkers[0]
#define M_ATTACHED     gSleepyMarkers[1]
#define M_ROLE         gSleepyMarkers[2]
#define M_RLOC16       gSleepyMarkers[3]
#define M_POLL_PERIOD  gSleepyMarkers[4]
#define M_RX_ON_IDLE   gSleepyMarkers[5]
#define M_LOOP_COUNT   gSleepyMarkers[6]
#define M_ATTACH_ATTEMPTS gSleepyMarkers[7]
#define M_SETUP_PHASE  gSleepyMarkers[8]
#define M_DATASET_CFG  gSleepyMarkers[9]
#define M_LAST_ERR     gSleepyMarkers[10]
#define M_RESTORE_ERR  gSleepyMarkers[11]

void updateMarkers() {
  M_STARTED = gThread.started() ? 1U : 0U;
  M_ATTACHED = gThread.attached() ? 1U : 0U;
  M_ROLE = static_cast<uint32_t>(gThread.role());
  M_RLOC16 = gThread.rloc16();
  M_POLL_PERIOD = gThread.getPollPeriod();

  xiao_nrf54l15::OpenThreadPlatformSkeletonSnapshot snap = {};
  if (xiao_nrf54l15::OpenThreadPlatformSkeleton::snapshot(&snap)) {
    M_RX_ON_IDLE = snap.radioRxOnWhenIdle ? 1U : 0U;
  }

  M_DATASET_CFG = gThread.datasetConfigured() ? 1U : 0U;
  M_LAST_ERR = static_cast<uint32_t>(gThread.lastError());
  M_RESTORE_ERR = static_cast<uint32_t>(gThread.datasetRestoreError());

  xiao_nrf54l15::Nrf54ThreadExperimental::AttachDiagnostics diag = {};
  if (gThread.getAttachDiagnostics(&diag)) {
    M_ATTACH_ATTEMPTS = diag.attachAttempts;
  }
}

void printPlatformDebug() {
  xiao_nrf54l15::OpenThreadPlatformSkeletonSnapshot snapshot = {};
  if (!xiao_nrf54l15::OpenThreadPlatformSkeleton::snapshot(&snapshot)) {
    return;
  }
  for (size_t i = 0; i < 4U; ++i) {
    if (snapshot.recentLogLines[i][0] == '\0') {
      continue;
    }
    Serial.print("sleepy_child otlog[");
    Serial.print(i);
    Serial.print("]=");
    Serial.println(snapshot.recentLogLines[i]);
  }
  for (size_t i = 0; i < 4U; ++i) {
    if (snapshot.recentMleLogLines[i][0] == '\0') {
      continue;
    }
    Serial.print("sleepy_child mlelog[");
    Serial.print(i);
    Serial.print("]=");
    Serial.println(snapshot.recentMleLogLines[i]);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);

  M_SETUP_PHASE = 1;
  bool ok = gThread.beginAsSleepyChild(true);
  M_SETUP_PHASE = ok ? 2 : 0xFF;
  if (!ok) {
    M_LAST_ERR = static_cast<uint32_t>(gThread.lastError());
    updateMarkers();
    return;
  }

  // Use the same helper as ThreadExperimentalCommissioner so both boards share
  // identical network key, PAN ID, channel, PSKc, and security policy values.
  M_SETUP_PHASE = 3;
  otOperationalDataset dataset = {};
  gThread.buildDemoDataset(&dataset);
  ok = gThread.setActiveDataset(dataset);
  M_SETUP_PHASE = ok ? 4 : 0xFE;
  if (!ok) {
    M_LAST_ERR = static_cast<uint32_t>(gThread.lastError());
    updateMarkers();
    return;
  }

  // Now call process() to apply the dataset and enable Thread
  gThread.process();
  delay(100);
  gThread.process();
  delay(100);
  gThread.process();
  M_SETUP_PHASE = 5;
  updateMarkers();

  ok = gThread.setPollPeriod(kPollPeriodMs);
  M_SETUP_PHASE = ok ? 8 : 0xFD;
  updateMarkers();
}

void loop() {
  gThread.process();
  M_LOOP_COUNT++;
  updateMarkers();

  if (Serial && (millis() - gLastStatusPrintMs >= kStatusPrintIntervalMs)) {
    gLastStatusPrintMs = millis();
    Serial.print("sleepy_child role=");
    Serial.println(gThread.roleName());
    Serial.print("sleepy_child attached=");
    Serial.println(gThread.attached() ? 1 : 0);
    xiao_nrf54l15::OpenThreadPlatformSkeletonSnapshot snap = {};
    if (xiao_nrf54l15::OpenThreadPlatformSkeleton::snapshot(&snap)) {
      Serial.print("sleepy_child channel=");
      Serial.println(snap.radioChannel);
      Serial.print("sleepy_child pan=0x");
      Serial.println(snap.panId, HEX);
      Serial.print("sleepy_child short=0x");
      Serial.println(snap.shortAddress, HEX);
      Serial.print("sleepy_child radio_state=");
      Serial.println(static_cast<int>(snap.radioState));
      Serial.print("sleepy_child rx_on_when_idle=");
      Serial.println(snap.radioRxOnWhenIdle ? 1 : 0);
    }
    printPlatformDebug();
    Serial.println();
  }
}
