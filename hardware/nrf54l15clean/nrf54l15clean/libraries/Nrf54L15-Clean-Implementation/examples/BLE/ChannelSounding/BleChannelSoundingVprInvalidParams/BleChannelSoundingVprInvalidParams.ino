/*
 * BleChannelSoundingVprInvalidParams
 *
 * Exercises negative direct-HCI paths through the bundled VPR Channel Sounding
 * controller image. These commands are intentionally malformed but still pass
 * the public host-side builders, so the VPR image must reject them with normal
 * Bluetooth HCI status values instead of wedging the shared-memory transport.
 *
 * Expected serial output:
 *   cs_vpr_invalid_params=PASS statuses=12/12/12/12/C/0/0
 */

#include <Arduino.h>

#include "ble_channel_sounding.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint16_t kConnHandle = 0x0041U;
constexpr uint8_t kHciSuccess = 0x00U;
constexpr uint8_t kHciCommandDisallowed = 0x0CU;
constexpr uint8_t kHciInvalidParams = 0x12U;

BleCsControllerVprHost gHost;
BleCsControllerVprHostConfig gConfig{};

bool expectStatus(bool commandOk, uint8_t status, uint8_t expected) {
  return commandOk && status == expected;
}

bool runInvalidParamProbe() {
  gHost.reset();
  BleCsControllerVprHost::fillDemoConfig(&gConfig);
  gConfig.session.workflow.procedureEnable.enable = 0U;

  uint8_t pumpCount = 0U;
  bool ok = gHost.beginFreshHost(kConnHandle, gConfig, 48U, &pumpCount);
  ok = ok && gHost.ready() && !gHost.failed();

  BleCsControllerCreateConfig invalidCreate = gConfig.session.workflow.createConfig;
  invalidCreate.configId = 0U;
  uint8_t createStatus = 0xFFU;
  ok = ok && expectStatus(gHost.directCreateConfig(invalidCreate, &createStatus),
                          createStatus, kHciInvalidParams);

  BleCsProcedureParameters invalidProcedure =
      gConfig.session.workflow.procedureParameters;
  invalidProcedure.configId = 99U;
  uint8_t procedureStatus = 0xFFU;
  ok = ok && expectStatus(
                 gHost.directSetProcedureParameters(invalidProcedure,
                                                    &procedureStatus),
                 procedureStatus, kHciInvalidParams);

  BleCsProcedureEnable badEnable{};
  badEnable.configId = gConfig.session.workflow.createConfig.configId;
  badEnable.enable = 2U;
  uint8_t enableStatus = 0xFFU;
  ok = ok && expectStatus(gHost.directProcedureEnable(badEnable, &enableStatus),
                          enableStatus, kHciInvalidParams);

  uint8_t removeStatus = 0xFFU;
  ok = ok && expectStatus(gHost.directRemoveConfig(99U, &removeStatus),
                          removeStatus, kHciInvalidParams);

  BleCsTestEndComplete inactiveEnd{};
  uint8_t inactiveEndStatus = 0xFFU;
  ok = ok &&
       expectStatus(gHost.directStopTest(&inactiveEnd, &inactiveEndStatus),
                    inactiveEndStatus, kHciCommandDisallowed);

  BleCsTestParams validTest{};
  BleChannelSoundingRadio::fillValidChannelMap(validTest.channelMap);
  validTest.mainModeType = kBleCsMainMode2;
  validTest.role = 0U;
  validTest.csSyncPhy = 1U;
  validTest.maxNumSubevents = 1U;

  uint8_t validStartStatus = 0xFFU;
  ok = ok && expectStatus(gHost.directStartTest(validTest, &validStartStatus),
                          validStartStatus, kHciSuccess);

  BleCsTestEndComplete validEnd{};
  uint8_t validEndStatus = 0xFFU;
  ok = ok && gHost.directStopTest(&validEnd, &validEndStatus) &&
       validEndStatus == kHciSuccess && validEnd.status == kHciSuccess;

  Serial.print(F("cs_vpr_invalid_params="));
  Serial.print(ok ? F("PASS") : F("FAIL"));
  Serial.print(F(" pumps="));
  Serial.print(pumpCount);
  Serial.print(F(" statuses="));
  Serial.print(createStatus, HEX);
  Serial.print('/');
  Serial.print(procedureStatus, HEX);
  Serial.print('/');
  Serial.print(enableStatus, HEX);
  Serial.print('/');
  Serial.print(removeStatus, HEX);
  Serial.print('/');
  Serial.print(inactiveEndStatus, HEX);
  Serial.print('/');
  Serial.print(validStartStatus, HEX);
  Serial.print('/');
  Serial.println(validEndStatus, HEX);
  return ok;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500U) {
  }
  Serial.println(F("BleChannelSoundingVprInvalidParams"));
  (void)runInvalidParamProbe();
}

void loop() {}
