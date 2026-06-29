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
 *   cs_vpr_security_material=PASS pre_flags=0x0 pre_params=C post_flags=0x7 ...
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

bool prepareDirectConfiguredHost(BleCsControllerVprWorkflowStartStatus* outStatus) {
  gHost.reset();
  BleCsControllerVprHost::fillDemoConfig(&gConfig);
  gConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;
  gConfig.session.workflow.procedureEnable.enable = 0U;

  BleCsControllerVprWorkflowStartStatus status{};
  bool ok = gHost.resetTransport(true) &&
            gHost.loadDefaultTransportImage() &&
            gHost.bootTransport() &&
            gHost.beginHost(kConnHandle, gConfig) &&
            gHost.directStartConfiguredWorkflow(false, &status);

  if (outStatus != nullptr) {
    *outStatus = status;
  }

  return ok &&
         status.readRemoteSupportedCapabilities == kHciSuccess &&
         status.setDefaultSettings == kHciSuccess &&
         status.createConfig == kHciSuccess &&
         status.securityEnable == kHciSuccess &&
         status.setProcedureParameters == kHciSuccess;
}

bool runInvalidParamProbe() {
  BleCsControllerVprWorkflowStartStatus workflowStatus{};
  bool ok = prepareDirectConfiguredHost(&workflowStatus);

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
  Serial.print(F(" phase="));
  Serial.print(static_cast<uint8_t>(gHost.workflowState().phase));
  Serial.print(F(" last_op=0x"));
  Serial.print(gHost.workflowState().lastOpcode, HEX);
  Serial.print(F(" last_status=0x"));
  Serial.print(gHost.workflowState().lastStatus, HEX);
  Serial.print(F(" wf="));
  Serial.print(workflowStatus.readRemoteSupportedCapabilities, HEX);
  Serial.print('/');
  Serial.print(workflowStatus.setDefaultSettings, HEX);
  Serial.print('/');
  Serial.print(workflowStatus.createConfig, HEX);
  Serial.print('/');
  Serial.print(workflowStatus.securityEnable, HEX);
  Serial.print('/');
  Serial.print(workflowStatus.setProcedureParameters, HEX);
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

bool runSecurityMaterialProbe() {
  BleCsControllerVprWorkflowStartStatus workflowStatus{};
  bool ok = prepareDirectConfiguredHost(&workflowStatus);

  // Config 1 was prepared by the normal workflow. Use a fresh config ID so the
  // negative check proves security material is bound per config, not globally.
  BleCsControllerCreateConfig createConfig = gConfig.session.workflow.createConfig;
  BleCsProcedureParameters procedureParams =
      gConfig.session.workflow.procedureParameters;
  BleCsProcedureEnable procedureEnable = gConfig.session.workflow.procedureEnable;
  createConfig.configId = 2U;
  procedureParams.configId = createConfig.configId;
  procedureEnable.configId = createConfig.configId;
  procedureEnable.enable = 1U;

  uint8_t createStatus = 0xFFU;
  ok = ok && expectStatus(gHost.directCreateConfig(createConfig, &createStatus),
                          createStatus, kHciSuccess);

  BleCsVprSecurityMaterialState preSecurity{};
  ok = ok && gHost.directReadSecurityMaterialForTest(&preSecurity) &&
       preSecurity.valid && preSecurity.status == kHciSuccess &&
       !preSecurity.materialValid && preSecurity.materialToken == 0U;

  uint8_t preParamsStatus = 0xFFU;
  ok = ok && expectStatus(gHost.directSetProcedureParameters(procedureParams,
                                                             &preParamsStatus),
                          preParamsStatus, kHciCommandDisallowed);

  uint8_t securityStatus = 0xFFU;
  ok = ok && expectStatus(gHost.directSecurityEnable(&securityStatus),
                          securityStatus, kHciSuccess);

  BleCsVprSecurityMaterialState postSecurity{};
  ok = ok && gHost.directReadSecurityMaterialForTest(&postSecurity) &&
       postSecurity.valid && postSecurity.status == kHciSuccess &&
       postSecurity.materialValid && postSecurity.controllerOwned &&
       postSecurity.boundToConfig && postSecurity.materialValidRaw != 0U &&
       postSecurity.connHandle == kConnHandle &&
       postSecurity.configId == createConfig.configId &&
       postSecurity.drbgNonce != 0U && postSecurity.materialToken != 0U &&
       postSecurity.sessionCounter != 0U;

  uint8_t postParamsStatus = 0xFFU;
  ok = ok && expectStatus(gHost.directSetProcedureParameters(procedureParams,
                                                             &postParamsStatus),
                          postParamsStatus, kHciSuccess);

  uint8_t enableStatus = 0xFFU;
  ok = ok && expectStatus(gHost.directProcedureEnable(procedureEnable,
                                                      &enableStatus),
                          enableStatus, kHciSuccess);

  Serial.print(F("cs_vpr_security_material="));
  Serial.print(ok ? F("PASS") : F("FAIL"));
  Serial.print(F(" phase="));
  Serial.print(static_cast<uint8_t>(gHost.workflowState().phase));
  Serial.print(F(" last_op=0x"));
  Serial.print(gHost.workflowState().lastOpcode, HEX);
  Serial.print(F(" last_status=0x"));
  Serial.print(gHost.workflowState().lastStatus, HEX);
  Serial.print(F(" wf="));
  Serial.print(workflowStatus.readRemoteSupportedCapabilities, HEX);
  Serial.print('/');
  Serial.print(workflowStatus.setDefaultSettings, HEX);
  Serial.print('/');
  Serial.print(workflowStatus.createConfig, HEX);
  Serial.print('/');
  Serial.print(workflowStatus.securityEnable, HEX);
  Serial.print('/');
  Serial.print(workflowStatus.setProcedureParameters, HEX);
  Serial.print(F(" create="));
  Serial.print(createStatus, HEX);
  Serial.print(F(" pre_flags=0x"));
  Serial.print(preSecurity.flags, HEX);
  Serial.print(F(" pre_params="));
  Serial.print(preParamsStatus, HEX);
  Serial.print(F(" sec_status="));
  Serial.print(securityStatus, HEX);
  Serial.print(F(" post_flags=0x"));
  Serial.print(postSecurity.flags, HEX);
  Serial.print(F(" post_conn=0x"));
  Serial.print(postSecurity.connHandle, HEX);
  Serial.print(F(" post_cfg="));
  Serial.print(postSecurity.configId);
  Serial.print(F(" post_nonce=0x"));
  Serial.print(postSecurity.drbgNonce, HEX);
  Serial.print(F(" post_token=0x"));
  Serial.print(postSecurity.materialToken, HEX);
  Serial.print(F(" post_ctr="));
  Serial.print(postSecurity.sessionCounter);
  Serial.print(F(" post_params="));
  Serial.print(postParamsStatus, HEX);
  Serial.print(F(" enable="));
  Serial.println(enableStatus, HEX);
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
  (void)runSecurityMaterialProbe();
}

void loop() {}
