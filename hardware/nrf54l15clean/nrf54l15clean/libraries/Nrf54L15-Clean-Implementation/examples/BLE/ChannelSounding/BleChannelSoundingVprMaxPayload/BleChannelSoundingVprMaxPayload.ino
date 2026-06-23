/*
 * BleChannelSoundingVprMaxPayload
 *
 * Verifies CS subevent result payload reassembly at the largest synthetic
 * payload currently emitted by the bundled VPR CS Test stream (Parity item #7c).
 *
 * The test sequence:
 *   1. Boot a fresh session with connected procedure disabled.
 *   2. Start standalone LE CS Test.
 *   3. Drain CS Subevent Result + Continue packets.
 *   4. Verify the host reassembled handle 0x0FFF, config 0, 8 reported
 *      mode-2 steps, and 64 bytes of step data.
 *
 * Serial output:  cs_vpr_max_payload=PASS/FAIL procedures=N steps=N bytes=N
 */

#include <Arduino.h>

#include "ble_channel_sounding.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint16_t kConnHandle = 0x0041U;
constexpr uint8_t  kMaxPumpCount = 64U;
constexpr uint16_t kBleCsHciTestConnHandle = 0x0FFFU;
constexpr uint16_t kTargetProcedures = 3U;
constexpr uint16_t kExpectedSteps = 8U;
constexpr uint16_t kExpectedStepBytes = 64U;

BleCsControllerVprHost gHost;

BleCsControllerVprHostConfig makeDemoConfig() {
  BleCsControllerVprHostConfig cfg{};
  BleCsControllerVprHost::fillDemoConfig(&cfg);
  cfg.session.workflow.procedureEnable.enable = 0U;
  return cfg;
}

}  // namespace

namespace {
bool gDone = false;
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println(F("--- ready ---"));
}

void loop() {
  if (gDone) return;
  gDone = true;
  Serial.println(F("BleChannelSoundingVprMaxPayload"));

  gHost.reset();
  BleCsControllerVprHostConfig config = makeDemoConfig();
  uint8_t pumpCount = 0U;
  bool ok = gHost.beginFreshHost(kConnHandle, config, kMaxPumpCount, &pumpCount);
  if (!ok || !gHost.ready()) {
    Serial.println(F("cs_vpr_max_payload=FAIL begin=0"));
    Serial.println(F("--- done ---"));
    return;
  }

  BleCsTestParams testParams{};
  BleChannelSoundingRadio::fillValidChannelMap(testParams.channelMap);
  testParams.mainModeType = kBleCsMainMode2;
  testParams.role = 0U;
  testParams.csSyncPhy = 1U;
  testParams.maxNumSubevents = 1U;

  uint8_t startStatus = 0xFFU;
  ok = gHost.directStartTest(testParams, &startStatus) && startStatus == 0U;

  const uint32_t deadline = millis() + 8000UL;
  while (ok && gHost.testResultCount() < kTargetProcedures && millis() < deadline) {
    ok = gHost.drainPendingControllerEvents();
  }

  const BleCsSubeventResult& result = gHost.lastTestResult();
  const bool resultOk =
      gHost.lastTestResultValid() &&
      result.header.connHandle == kBleCsHciTestConnHandle &&
      result.header.configId == 0U &&
      result.header.numStepsReported == kExpectedSteps &&
      result.stepData != nullptr &&
      result.stepDataLen == kExpectedStepBytes &&
      result.isComplete &&
      !result.isPartial &&
      result.header.procedureAbortReason == 0U &&
      result.header.subeventAbortReason == 0U;

  BleCsTestEndComplete testEnd{};
  uint8_t endStatus = 0xFFU;
  ok = ok && resultOk && gHost.directStopTest(&testEnd, &endStatus) &&
       endStatus == 0U && testEnd.status == 0U;

  const bool allOk = ok && gHost.testResultCount() >= kTargetProcedures &&
                     !gHost.failed() && gHost.ready();
  Serial.print(F("cs_vpr_max_payload="));
  Serial.print(allOk ? F("PASS") : F("FAIL"));
  Serial.print(F(" procedures="));
  Serial.print(gHost.testResultCount());
  Serial.print(F(" steps="));
  Serial.print(result.header.numStepsReported);
  Serial.print(F(" bytes="));
  Serial.print(result.stepDataLen);
  Serial.print(F(" start=0x"));
  Serial.print(startStatus, HEX);
  Serial.print(F(" end=0x"));
  Serial.print(endStatus, HEX);
  Serial.print(F(" failed="));
  Serial.println(gHost.failed() ? 1 : 0);
  Serial.println(F("--- done ---"));
}
