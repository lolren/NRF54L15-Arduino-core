/*
 * BleChannelSoundingVprMultiConfig
 *
 * Exercises the bundled VPR Channel Sounding controller's retained config
 * table beyond the old two-primary-slot path. It creates five configs, proves
 * config 1 can still be selected/run after configs 3-5 are created, removes a
 * middle config, reuses the freed slot, and verifies the removed config is
 * rejected.
 *
 * Expected serial output:
 *   cs_vpr_multi_config=PASS count=5>4>5 status=...
 */

#include <Arduino.h>

#include "ble_channel_sounding.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint16_t kConnHandle = 0x0041U;
constexpr uint8_t kHciSuccess = 0x00U;
constexpr uint8_t kHciInvalidParams = 0x12U;

BleCsControllerVprHost gHost;
BleCsControllerVprHostConfig gConfig{};

void fillSparseChannelMap(uint8_t* channelMap, size_t channelMapLen,
                          uint8_t seed) {
  if (channelMap == nullptr) {
    return;
  }
  memset(channelMap, 0, channelMapLen);
  for (uint8_t i = 0U; i < 4U; ++i) {
    const uint8_t channel =
        static_cast<uint8_t>((seed + (i * 11U)) % 40U);
    if ((channel >> 3U) < channelMapLen) {
      channelMap[channel >> 3U] |=
          static_cast<uint8_t>(1U << (channel & 0x07U));
    }
  }
}

BleCsControllerCreateConfig makeConfig(uint8_t configId, uint8_t seed) {
  BleCsControllerCreateConfig config = gConfig.session.workflow.createConfig;
  config.configId = configId;
  config.rttType = 0U;
  config.minMainModeSteps = static_cast<uint8_t>(3U + (seed & 0x01U));
  config.maxMainModeSteps = config.minMainModeSteps;
  config.mainModeRepetition = 1U;
  fillSparseChannelMap(config.channelMap, sizeof(config.channelMap), seed);
  return config;
}

BleCsProcedureParameters makeParams(uint8_t configId, uint8_t seed) {
  BleCsProcedureParameters params = gConfig.session.workflow.procedureParameters;
  params.configId = configId;
  params.maxProcedureCount = 1U;
  params.maxProcedureLen = static_cast<uint16_t>(12U + seed);
  params.minProcedureInterval = 80U;
  params.maxProcedureInterval = 100U;
  params.minSubeventLen = 0x000100UL;
  params.maxSubeventLen = 0x000100UL;
  return params;
}

bool createSecureRunnableConfig(uint8_t configId, uint8_t seed,
                                uint8_t* outCreateStatus,
                                uint8_t* outSecurityStatus,
                                uint8_t* outParamsStatus) {
  if (outCreateStatus == nullptr || outSecurityStatus == nullptr ||
      outParamsStatus == nullptr) {
    return false;
  }

  *outCreateStatus = 0xFFU;
  *outSecurityStatus = 0xFFU;
  *outParamsStatus = 0xFFU;
  BleCsControllerCreateConfig config = makeConfig(configId, seed);
  BleCsProcedureParameters params = makeParams(configId, seed);
  return gHost.directCreateConfig(config, outCreateStatus) &&
         *outCreateStatus == kHciSuccess &&
         gHost.directSecurityEnable(outSecurityStatus) &&
         *outSecurityStatus == kHciSuccess &&
         gHost.directSetProcedureParameters(params, outParamsStatus) &&
         *outParamsStatus == kHciSuccess;
}

bool pollStoredCount(uint8_t expectedCount, uint8_t maxPolls,
                     uint8_t* outPolls) {
  if (outPolls != nullptr) {
    *outPolls = 0U;
  }
  while (!gHost.failed()) {
    if (gHost.vprState().linkStoredConfigCount == expectedCount) {
      return true;
    }
    if (outPolls != nullptr && *outPolls >= maxPolls) {
      break;
    }
    if (!gHost.poll()) {
      return false;
    }
    if (outPolls != nullptr) {
      *outPolls = static_cast<uint8_t>(*outPolls + 1U);
    }
  }
  return false;
}

bool runMultiConfigProbe() {
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
  const uint8_t config2 = static_cast<uint8_t>(baseConfigId + 1U);
  const uint8_t config3 = static_cast<uint8_t>(baseConfigId + 2U);
  const uint8_t config4 = static_cast<uint8_t>(baseConfigId + 3U);
  const uint8_t config5 = static_cast<uint8_t>(baseConfigId + 4U);
  const uint8_t config6 = static_cast<uint8_t>(baseConfigId + 5U);

  uint8_t createStatus[5] = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};
  uint8_t securityStatus[5] = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};
  uint8_t paramsStatus[5] = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};
  uint8_t selectBaseStatus = 0xFFU;
  uint8_t runBaseStatus = 0xFFU;
  uint8_t removeConfig3Status = 0xFFU;
  uint8_t removedConfig3SelectStatus = 0xFFU;
  uint8_t count5Polls = 0U;
  uint8_t count4Polls = 0U;
  uint8_t count5AgainPolls = 0U;
  uint8_t runBasePolls = 0U;

  ok = ok && createSecureRunnableConfig(config2, 2U, &createStatus[0],
                                        &securityStatus[0], &paramsStatus[0]);
  ok = ok && createSecureRunnableConfig(config3, 7U, &createStatus[1],
                                        &securityStatus[1], &paramsStatus[1]);
  ok = ok && createSecureRunnableConfig(config4, 13U, &createStatus[2],
                                        &securityStatus[2], &paramsStatus[2]);
  ok = ok && createSecureRunnableConfig(config5, 19U, &createStatus[3],
                                        &securityStatus[3], &paramsStatus[3]);
  ok = ok && pollStoredCount(5U, 48U, &count5Polls);
  const BleCsControllerVprHostState afterFive = gHost.vprState();

  BleCsProcedureParameters baseParams = gConfig.session.workflow.procedureParameters;
  baseParams.configId = baseConfigId;
  baseParams.maxProcedureCount = 1U;
  baseParams.maxProcedureLen = 16U;
  baseParams.minSubeventLen = 0x000100UL;
  baseParams.maxSubeventLen = 0x000100UL;
  ok = ok && gHost.directSetProcedureParameters(baseParams, &selectBaseStatus);
  ok = ok && selectBaseStatus == kHciSuccess &&
       gHost.pollUntilSelectedState(baseConfigId, 5U, true, 48U, nullptr);
  const BleCsControllerVprHostState baseSelected = gHost.vprState();

  ok = ok && gHost.directProcedureEnable(baseConfigId, true, &runBaseStatus);
  const bool runBaseComplete =
      (runBaseStatus == kHciSuccess) &&
      gHost.pollUntilStoppedOnConfig(baseConfigId, 128U, &runBasePolls);
  const BleCsControllerVprHostState afterBaseRun = gHost.vprState();
  ok = ok && runBaseComplete;

  ok = ok && gHost.directRemoveConfig(config3, &removeConfig3Status);
  ok = ok && removeConfig3Status == kHciSuccess &&
       pollStoredCount(4U, 48U, &count4Polls);
  const BleCsControllerVprHostState afterRemove = gHost.vprState();

  ok = ok && createSecureRunnableConfig(config6, 23U, &createStatus[4],
                                        &securityStatus[4], &paramsStatus[4]);
  ok = ok && pollStoredCount(5U, 48U, &count5AgainPolls);
  const BleCsControllerVprHostState afterReuse = gHost.vprState();

  BleCsProcedureParameters removedParams = makeParams(config3, 7U);
  ok = ok &&
       gHost.directSetProcedureParameters(removedParams, &removedConfig3SelectStatus);

  const bool statusOk =
      createStatus[0] == kHciSuccess && createStatus[1] == kHciSuccess &&
      createStatus[2] == kHciSuccess && createStatus[3] == kHciSuccess &&
      createStatus[4] == kHciSuccess &&
      securityStatus[0] == kHciSuccess && securityStatus[1] == kHciSuccess &&
      securityStatus[2] == kHciSuccess && securityStatus[3] == kHciSuccess &&
      securityStatus[4] == kHciSuccess &&
      paramsStatus[0] == kHciSuccess && paramsStatus[1] == kHciSuccess &&
      paramsStatus[2] == kHciSuccess && paramsStatus[3] == kHciSuccess &&
      paramsStatus[4] == kHciSuccess &&
      selectBaseStatus == kHciSuccess && runBaseStatus == kHciSuccess &&
      removeConfig3Status == kHciSuccess &&
      removedConfig3SelectStatus == kHciInvalidParams;

  const bool stateOk =
      afterFive.linkStoredConfigCount == 5U &&
      afterFive.linkSlot0ConfigId == baseConfigId &&
      afterFive.linkSlot1ConfigId == config2 &&
      !afterFive.linkPreviousSlotInUse &&
      baseSelected.linkConfigId == baseConfigId &&
      baseSelected.linkStoredConfigCount == 5U &&
      afterRemove.linkStoredConfigCount == 4U &&
      afterReuse.linkStoredConfigCount == 5U &&
      afterReuse.linkConfigId == config6 &&
      afterReuse.linkLastEvictedConfigId == 0U;

  ok = ok && statusOk && stateOk && !gHost.failed();

  Serial.print(F("cs_vpr_multi_config="));
  Serial.print(ok ? F("PASS") : F("FAIL"));
  Serial.print(F(" count="));
  Serial.print(afterFive.linkStoredConfigCount);
  Serial.print('>');
  Serial.print(afterRemove.linkStoredConfigCount);
  Serial.print('>');
  Serial.print(afterReuse.linkStoredConfigCount);
  Serial.print(F(" status="));
  for (uint8_t i = 0U; i < 5U; ++i) {
    if (i != 0U) {
      Serial.print(',');
    }
    Serial.print(createStatus[i], HEX);
    Serial.print('/');
    Serial.print(securityStatus[i], HEX);
    Serial.print('/');
    Serial.print(paramsStatus[i], HEX);
  }
  Serial.print(F(" base="));
  Serial.print(selectBaseStatus, HEX);
  Serial.print('/');
  Serial.print(runBaseStatus, HEX);
  Serial.print(F(" remove="));
  Serial.print(removeConfig3Status, HEX);
  Serial.print(F(" removed_select="));
  Serial.print(removedConfig3SelectStatus, HEX);
  Serial.print(F(" polls="));
  Serial.print(pumpCount);
  Serial.print('/');
  Serial.print(count5Polls);
  Serial.print('/');
  Serial.print(runBasePolls);
  Serial.print('/');
  Serial.print(count4Polls);
  Serial.print('/');
  Serial.print(count5AgainPolls);
  Serial.print(F(" visible="));
  Serial.print(afterFive.linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(afterFive.linkSlot1ConfigId);
  Serial.print('/');
  Serial.print(afterFive.linkPreviousConfigId);
  Serial.print(F(" active="));
  Serial.print(afterFive.linkConfigId);
  Serial.print('>');
  Serial.print(baseSelected.linkConfigId);
  Serial.print('>');
  Serial.print(afterReuse.linkConfigId);
  Serial.print(F(" after_run="));
  Serial.print(afterBaseRun.linkConfigId);
  Serial.print('/');
  Serial.print(afterBaseRun.linkProcedureEnabled ? 1 : 0);
  Serial.print('/');
  Serial.print(afterBaseRun.linkSessionOpen ? 1 : 0);
  Serial.print('/');
  Serial.print(afterBaseRun.linkSecurityEnabled ? 1 : 0);
  Serial.print('/');
  Serial.print(afterBaseRun.linkProcedureParamsApplied ? 1 : 0);
  Serial.print(F(" evicted="));
  Serial.println(afterReuse.linkLastEvictedConfigId);
  return ok;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) {
  }
  Serial.println(F("BleChannelSoundingVprMultiConfig"));
  (void)runMultiConfigProbe();
}

void loop() {}
