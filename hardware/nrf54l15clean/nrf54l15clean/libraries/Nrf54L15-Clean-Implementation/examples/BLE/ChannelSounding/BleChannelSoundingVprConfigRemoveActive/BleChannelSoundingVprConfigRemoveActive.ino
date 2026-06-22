/*
 * BleChannelSoundingVprConfigRemoveActive
 *
 * Exercises retained CS configuration removal in the bundled VPR Channel
 * Sounding controller image. The probe creates a second config, selects the
 * original config again, removes that selected/original config, verifies that
 * the alternate config is promoted, runs the promoted config, and confirms the
 * removed config can no longer be enabled.
 *
 * Expected serial output:
 *   cs_vpr_config_remove=PASS statuses=0/0/0/0/0/0/12
 */

#include <Arduino.h>

#include "ble_channel_sounding.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint16_t kConnHandle = 0x0041U;
constexpr uint8_t kHciSuccess = 0x00U;
constexpr uint8_t kHciInvalidParams = 0x12U;
constexpr uint8_t kAltChannels[] = {6U, 18U, 30U, 39U};

BleCsControllerVprHost gHost;
BleCsControllerVprHostConfig gConfig{};

void fillAltChannelMap(uint8_t* channelMap, size_t channelMapLen) {
  if (channelMap == nullptr) {
    return;
  }
  memset(channelMap, 0, channelMapLen);
  for (size_t i = 0U; i < sizeof(kAltChannels); ++i) {
    const uint8_t channel = kAltChannels[i];
    if ((channel >> 3U) < channelMapLen) {
      channelMap[channel >> 3U] |= static_cast<uint8_t>(1U << (channel & 0x07U));
    }
  }
}

bool runConfigRemoveProbe() {
  gHost.reset();
  BleCsControllerVprHost::fillDemoConfig(&gConfig);
  gConfig.session.workflow.procedureEnable.enable = 0U;
  gConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;
  gConfig.session.workflow.procedureParameters.maxProcedureLen = 16U;
  gConfig.session.workflow.procedureParameters.minSubeventLen = 0x000100UL;
  gConfig.session.workflow.procedureParameters.maxSubeventLen = 0x000100UL;

  uint8_t pumpCount = 0U;
  bool ok = gHost.beginFreshHost(kConnHandle, gConfig, 48U, &pumpCount);
  ok = ok && gHost.ready() && !gHost.failed();

  const uint8_t baseConfigId = gHost.workflowState().configComplete.configId;
  BleCsControllerCreateConfig altConfig = gConfig.session.workflow.createConfig;
  altConfig.configId = static_cast<uint8_t>(baseConfigId + 1U);
  altConfig.rttType = 0U;
  altConfig.minMainModeSteps = 4U;
  altConfig.maxMainModeSteps = 4U;
  fillAltChannelMap(altConfig.channelMap, sizeof(altConfig.channelMap));

  BleCsProcedureParameters baseParams = gConfig.session.workflow.procedureParameters;
  BleCsProcedureParameters altParams = baseParams;
  altParams.configId = altConfig.configId;

  uint8_t createStatus = 0xFFU;
  uint8_t securityStatus = 0xFFU;
  uint8_t setAltStatus = 0xFFU;
  uint8_t setBaseStatus = 0xFFU;
  uint8_t removeBaseStatus = 0xFFU;
  uint8_t runAltStatus = 0xFFU;
  uint8_t runRemovedStatus = 0xFFU;
  uint8_t createPolls = 0U;
  uint8_t baseSelectPolls = 0U;
  uint8_t promotedPolls = 0U;
  uint8_t runAltPolls = 0U;

  ok = ok && gHost.directCreateConfig(altConfig, &createStatus);
  ok = ok && gHost.directSecurityEnable(&securityStatus);
  ok = ok && gHost.directSetProcedureParameters(altParams, &setAltStatus);
  ok = ok &&
       gHost.pollUntilSelectedState(altConfig.configId, 2U, true, 32U,
                                    &createPolls);
  const BleCsControllerVprHostState armedAltState = gHost.vprState();

  ok = ok && gHost.directSetProcedureParameters(baseParams, &setBaseStatus);
  ok = ok &&
       gHost.pollUntilSelectedState(baseConfigId, 2U, true, 32U,
                                    &baseSelectPolls);
  const BleCsControllerVprHostState baseSelectedState = gHost.vprState();

  ok = ok && gHost.directRemoveConfig(baseConfigId, &removeBaseStatus);
  ok = ok &&
       gHost.pollUntilSelectedState(altConfig.configId, 1U, true, 32U,
                                    &promotedPolls);
  const BleCsControllerVprHostState promotedState = gHost.vprState();

  ok = ok && gHost.directProcedureEnable(altConfig.configId, true,
                                         &runAltStatus);
  ok = ok && gHost.pollUntilStoppedOnConfig(altConfig.configId, 96U,
                                            &runAltPolls);
  const BleCsSubeventResult altLocal = gHost.completedLocalResult();
  const BleCsSubeventResult altPeer = gHost.completedPeerResult();

  ok = ok && gHost.directProcedureEnable(baseConfigId, true, &runRemovedStatus);

  ok = ok && createStatus == kHciSuccess && securityStatus == kHciSuccess &&
       setAltStatus == kHciSuccess && setBaseStatus == kHciSuccess &&
       removeBaseStatus == kHciSuccess && runAltStatus == kHciSuccess &&
       runRemovedStatus == kHciInvalidParams &&
       armedAltState.linkConfigId == altConfig.configId &&
       armedAltState.linkSelectedConfigRunnable &&
       baseSelectedState.linkConfigId == baseConfigId &&
       baseSelectedState.linkSelectedConfigRunnable &&
       promotedState.linkConfigId == altConfig.configId &&
       promotedState.linkStoredConfigCount == 1U &&
       promotedState.linkSelectedConfigRunnable &&
       altLocal.header.configId == altConfig.configId &&
       altPeer.header.configId == altConfig.configId &&
       !gHost.failed();

  Serial.print(F("cs_vpr_config_remove="));
  Serial.print(ok ? F("PASS") : F("FAIL"));
  Serial.print(F(" pumps="));
  Serial.print(pumpCount);
  Serial.print(F(" statuses="));
  Serial.print(createStatus, HEX);
  Serial.print('/');
  Serial.print(securityStatus, HEX);
  Serial.print('/');
  Serial.print(setAltStatus, HEX);
  Serial.print('/');
  Serial.print(setBaseStatus, HEX);
  Serial.print('/');
  Serial.print(removeBaseStatus, HEX);
  Serial.print('/');
  Serial.print(runAltStatus, HEX);
  Serial.print('/');
  Serial.print(runRemovedStatus, HEX);
  Serial.print(F(" polls="));
  Serial.print(createPolls);
  Serial.print('/');
  Serial.print(baseSelectPolls);
  Serial.print('/');
  Serial.print(promotedPolls);
  Serial.print('/');
  Serial.print(runAltPolls);
  Serial.print(F(" cfg="));
  Serial.print(baseConfigId);
  Serial.print("->");
  Serial.println(altConfig.configId);
  return ok;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500U) {
  }
  Serial.println(F("BleChannelSoundingVprConfigRemoveActive"));
  (void)runConfigRemoveProbe();
}

void loop() {}
