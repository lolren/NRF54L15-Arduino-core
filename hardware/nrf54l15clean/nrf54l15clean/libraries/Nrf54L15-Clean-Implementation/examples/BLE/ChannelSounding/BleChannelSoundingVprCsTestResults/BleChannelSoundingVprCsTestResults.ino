/*
 * BleChannelSoundingVprCsTestResults
 *
 * Exercises the standalone CS Test result stream that a Bluetooth controller
 * emits while LE CS Test mode is active: CS Subevent Result (HCI LE Meta
 * subevent 0x31) and CS Subevent Result Continue (0x32) events on the reserved
 * test connection handle 0x0FFF, continuing until LE CS Test End.
 *
 * This validates that the bundled VPR image streams standards-shaped test
 * results (handle 0x0FFF, config_id 0, start_acl_conn_event 0, matching Zephyr
 * host/cs.c test-mode handling) and that the host reassembles the initial +
 * continuation events and counts completed procedures independently of the
 * connected-procedure session. It also checks that a second LE CS Test while one
 * is already active is rejected with Command Disallowed (0x0C).
 *
 * This proves HCI/controller transport parity for the test result stream. It
 * does not claim physical RF ranging parity with a production controller; the
 * emitted step data is synthetic.
 */

#include <Arduino.h>

#include "ble_channel_sounding.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint16_t kConnHandle = 0x0041U;
constexpr uint16_t kTargetProcedures = 3U;
/* BLE_CS_HCI_STATUS_COMMAND_DISALLOWED, as returned by the VPR image. */
constexpr uint8_t kBleCsHciStatusCommandDisallowed = 0x0CU;
/* HCI LE CS Test reserved connection handle (BT_HCI_LE_CS_TEST_CONN_HANDLE). */
constexpr uint16_t kBleCsHciTestConnHandle = 0x0FFFU;

BleCsControllerVprHost gHost;
BleCsControllerVprHostConfig gConfig{};

bool runTestResultStreamProbe() {
  gHost.reset();
  BleCsControllerVprHost::fillDemoConfig(&gConfig);

  uint8_t pumpCount = 0U;
  bool ok = gHost.beginFreshHost(kConnHandle, gConfig, 48U, &pumpCount);
  ok = ok && gHost.ready() && !gHost.failed();

  BleCsTestParams testParams{};
  BleChannelSoundingRadio::fillValidChannelMap(testParams.channelMap);
  testParams.mainModeType = kBleCsMainMode2;
  testParams.role = 0U;
  testParams.csSyncPhy = 1U;
  testParams.maxNumSubevents = 1U;

  uint8_t startStatus = 0xFFU;
  ok = ok && gHost.directStartTest(testParams, &startStatus) && startStatus == 0U;

  /* Drain the standalone result stream via the direct controller path. Do not
   * use poll()/loopOnce() here: those feed the stream/ingress path bound to the
   * connected-procedure session, whereas test results live on the direct path
   * and are collected on handle 0x0FFF. */
  const uint32_t deadline = millis() + 8000UL;
  while (ok && gHost.testResultCount() < kTargetProcedures && millis() < deadline) {
    ok = ok && gHost.drainPendingControllerEvents();
    if (gHost.lastTestResultValid()) {
      ok = ok && (gHost.lastTestResult().header.connHandle == kBleCsHciTestConnHandle);
      ok = ok && (gHost.lastTestResult().header.configId == 0U);
    }
  }
  ok = ok && (gHost.testResultCount() >= kTargetProcedures);

  /* Snapshot before the second directStartTest, which resets the collector. */
  const uint16_t procedures = gHost.testResultCount();
  const uint16_t handle =
      gHost.lastTestResultValid() ? gHost.lastTestResult().header.connHandle : 0U;

  /* A second CS Test while one is already active must be rejected. */
  uint8_t secondStartStatus = 0x00U;
  ok = ok && gHost.directStartTest(testParams, &secondStartStatus) &&
       secondStartStatus == kBleCsHciStatusCommandDisallowed;

  BleCsTestEndComplete testEnd{};
  uint8_t testEndCommandStatus = 0xFFU;
  ok = ok && gHost.directStopTest(&testEnd, &testEndCommandStatus) &&
       testEndCommandStatus == 0U && testEnd.status == 0U &&
       gHost.lastTestEndCompleteValid();
  Serial.print(F("cs_vpr_test_results="));
  Serial.print(ok ? F("PASS") : F("FAIL"));
  Serial.print(F(" procedures="));
  Serial.print(procedures);
  Serial.print(F(" handle=0x"));
  Serial.print(handle, HEX);
  Serial.print(F(" start=0x"));
  Serial.print(startStatus, HEX);
  Serial.print(F(" second_start=0x"));
  Serial.print(secondStartStatus, HEX);
  Serial.print(F(" end=0x"));
  Serial.println(testEndCommandStatus, HEX);
  return ok;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500U) {
  }
  Serial.println(F("BleChannelSoundingVprCsTestResults"));
  (void)runTestResultStreamProbe();
}

void loop() {}
