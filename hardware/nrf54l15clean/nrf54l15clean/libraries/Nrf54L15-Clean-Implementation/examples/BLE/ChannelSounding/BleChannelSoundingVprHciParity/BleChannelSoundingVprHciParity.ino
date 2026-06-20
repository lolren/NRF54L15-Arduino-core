/*
 * BleChannelSoundingVprHciParity
 *
 * Exercises the Bluetooth Channel Sounding HCI commands that Zephyr exposes
 * outside the normal connected-procedure workflow:
 *   - cached remote capabilities, version 1 and version 2
 *   - remote FAE read and cached FAE write
 *   - channel classification
 *   - CS Test and CS Test End
 *
 * This validates command/event transport through the bundled VPR image. It
 * does not claim physical RF ranging parity with a production controller.
 */

#include <Arduino.h>

#include "ble_channel_sounding.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint16_t kConnHandle = 0x0041U;

BleCsControllerVprHost gHost;
BleCsControllerVprHostConfig gConfig{};

bool statusOk(bool commandOk, uint8_t status) {
  return commandOk && status == 0U;
}

bool runParityProbe() {
  gHost.reset();
  BleCsControllerVprHost::fillDemoConfig(&gConfig);
  gConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;

  uint8_t pumpCount = 0U;
  bool ok = gHost.beginFreshHost(kConnHandle, gConfig, 48U, &pumpCount);
  ok = ok && gHost.ready() && !gHost.failed();

  BleCsControllerCapabilities capabilities =
      gHost.workflowState().remoteCapabilities;
  uint8_t cachedV1Status = 0xFFU;
  uint8_t cachedV2Status = 0xFFU;
  ok = ok && statusOk(
                 gHost.directWriteCachedRemoteSupportedCapabilities(
                     capabilities, &cachedV1Status),
                 cachedV1Status);
  ok = ok && statusOk(
                 gHost.directWriteCachedRemoteSupportedCapabilitiesV2(
                     capabilities, &cachedV2Status),
                 cachedV2Status);

  int8_t cachedFae[kBleCsFaeTableValueCount] = {0};
  for (size_t i = 0U; i < kBleCsFaeTableValueCount; ++i) {
    cachedFae[i] = static_cast<int8_t>((i % 17U) - 8);
  }
  uint8_t writeFaeStatus = 0xFFU;
  ok = ok && statusOk(
                 gHost.directWriteCachedRemoteFaeTable(cachedFae,
                                                       &writeFaeStatus),
                 writeFaeStatus);

  BleCsFaeTable remoteFae{};
  uint8_t readFaeStatus = 0xFFU;
  ok = ok && gHost.directReadRemoteFaeTable(&remoteFae, &readFaeStatus) &&
       readFaeStatus == 0U && remoteFae.valid &&
       remoteFae.connHandle == kConnHandle;

  BleCsChannelClassification classification{};
  BleChannelSoundingRadio::fillValidChannelMap(classification.channelMap);
  uint8_t classificationStatus = 0xFFU;
  ok = ok && statusOk(
                 gHost.directSetChannelClassification(classification,
                                                      &classificationStatus),
                 classificationStatus);

  BleCsTestParams testParams{};
  BleChannelSoundingRadio::fillValidChannelMap(testParams.channelMap);
  testParams.mainModeType = kBleCsMainMode2;
  testParams.role = 0U;
  testParams.csSyncPhy = 1U;
  testParams.maxNumSubevents = 1U;
  uint8_t testStartStatus = 0xFFU;
  ok = ok &&
       statusOk(gHost.directStartTest(testParams, &testStartStatus),
                testStartStatus);

  BleCsTestEndComplete testEnd{};
  uint8_t testEndCommandStatus = 0xFFU;
  ok = ok && gHost.directStopTest(&testEnd, &testEndCommandStatus) &&
       testEndCommandStatus == 0U && testEnd.status == 0U &&
       gHost.lastTestEndCompleteValid();

  Serial.print(F("cs_vpr_hci_parity="));
  Serial.print(ok ? F("PASS") : F("FAIL"));
  Serial.print(F(" pumps="));
  Serial.print(pumpCount);
  Serial.print(F(" status="));
  Serial.print(cachedV1Status, HEX);
  Serial.print('/');
  Serial.print(cachedV2Status, HEX);
  Serial.print('/');
  Serial.print(writeFaeStatus, HEX);
  Serial.print('/');
  Serial.print(readFaeStatus, HEX);
  Serial.print('/');
  Serial.print(classificationStatus, HEX);
  Serial.print('/');
  Serial.print(testStartStatus, HEX);
  Serial.print('/');
  Serial.print(testEndCommandStatus, HEX);
  Serial.print(F(" fae_valid="));
  Serial.print(remoteFae.valid ? 1 : 0);
  Serial.print(F(" fae_handle=0x"));
  Serial.print(remoteFae.connHandle, HEX);
  Serial.print(F(" test_end="));
  Serial.println(testEnd.status, HEX);
  return ok;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500U) {
  }
  Serial.println(F("BleChannelSoundingVprHciParity"));
  (void)runParityProbe();
}

void loop() {}
