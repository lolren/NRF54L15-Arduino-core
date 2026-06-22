/*
 * BleChannelSoundingVprResetClearsConfigs
 *
 * Verifies that resetting the bundled VPR Channel Sounding controller clears
 * retained config slots and cached host-visible link state. The probe creates
 * multiple configs, resets the VPR transport, checks the cached state is empty,
 * then starts a fresh host session and checks only the boot config exists.
 *
 * Expected serial output:
 *   cs_vpr_reset_clears_configs=PASS before=4 reset=0 fresh=1 ...
 */

#include <Arduino.h>

#include "ble_channel_sounding.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint16_t kConnHandle = 0x0042U;
constexpr uint8_t kHciSuccess = 0x00U;

BleCsControllerVprHost gHost;
BleCsControllerVprHostConfig gConfig{};

void fillChannelMap(uint8_t* channelMap, size_t channelMapLen, uint8_t seed) {
  if (channelMap == nullptr) {
    return;
  }
  memset(channelMap, 0, channelMapLen);
  for (uint8_t i = 0U; i < 4U; ++i) {
    const uint8_t channel = static_cast<uint8_t>((seed + (i * 9U)) % 40U);
    if ((channel >> 3U) < channelMapLen) {
      channelMap[channel >> 3U] |=
          static_cast<uint8_t>(1U << (channel & 0x07U));
    }
  }
}

BleCsControllerCreateConfig makeConfig(uint8_t configId, uint8_t seed) {
  BleCsControllerCreateConfig config = gConfig.session.workflow.createConfig;
  config.configId = configId;
  config.minMainModeSteps = 3U;
  config.maxMainModeSteps = 3U;
  config.mainModeRepetition = 1U;
  fillChannelMap(config.channelMap, sizeof(config.channelMap), seed);
  return config;
}

bool createConfig(uint8_t configId, uint8_t seed, uint8_t* outStatus) {
  if (outStatus == nullptr) {
    return false;
  }
  *outStatus = 0xFFU;
  const BleCsControllerCreateConfig config = makeConfig(configId, seed);
  return gHost.directCreateConfig(config, outStatus) &&
         *outStatus == kHciSuccess;
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

bool resetStateLooksEmpty(const BleCsControllerVprHostState& state) {
  return !state.linkSessionOpen &&
         state.linkStoredConfigCount == 0U &&
         state.linkConfigId == 0U &&
         state.linkSlot0ConfigId == 0U &&
         state.linkSlot1ConfigId == 0U &&
         state.linkPreviousConfigId == 0U &&
         !state.linkPreviousSlotInUse;
}

bool runResetClearsConfigsProbe() {
  gHost.reset();
  BleCsControllerVprHost::fillDemoConfig(&gConfig);
  gConfig.session.workflow.procedureEnable.enable = 0U;

  uint8_t firstBeginPolls = 0U;
  bool ok = gHost.beginFreshHost(kConnHandle, gConfig, 48U, &firstBeginPolls);
  ok = ok && gHost.ready() && !gHost.failed();

  const uint8_t baseConfigId = gHost.workflowState().configComplete.configId;
  uint8_t createStatus[3] = {0xFFU, 0xFFU, 0xFFU};
  uint8_t count4Polls = 0U;
  uint8_t secondBeginPolls = 0U;

  ok = ok && createConfig(static_cast<uint8_t>(baseConfigId + 1U), 3U,
                          &createStatus[0]);
  ok = ok && createConfig(static_cast<uint8_t>(baseConfigId + 2U), 11U,
                          &createStatus[1]);
  ok = ok && createConfig(static_cast<uint8_t>(baseConfigId + 3U), 21U,
                          &createStatus[2]);
  ok = ok && pollStoredCount(4U, 48U, &count4Polls);
  const BleCsControllerVprHostState beforeReset = gHost.vprState();

  ok = ok && gHost.resetTransport(true);
  const BleCsControllerVprHostState afterReset = gHost.vprState();

  ok = ok && gHost.beginFreshHost(kConnHandle, gConfig, 48U, &secondBeginPolls);
  const BleCsControllerVprHostState afterFreshBegin = gHost.vprState();

  const bool statusOk = createStatus[0] == kHciSuccess &&
                        createStatus[1] == kHciSuccess &&
                        createStatus[2] == kHciSuccess;
  const bool stateOk = beforeReset.linkStoredConfigCount == 4U &&
                       resetStateLooksEmpty(afterReset) &&
                       gHost.ready() && !gHost.failed() &&
                       afterFreshBegin.linkSessionOpen &&
                       afterFreshBegin.linkStoredConfigCount == 1U &&
                       afterFreshBegin.linkConfigId == baseConfigId &&
                       afterFreshBegin.linkSlot0ConfigId == baseConfigId &&
                       afterFreshBegin.linkSlot1ConfigId == 0U &&
                       afterFreshBegin.linkPreviousConfigId == 0U;

  ok = ok && statusOk && stateOk;

  Serial.print(F("cs_vpr_reset_clears_configs="));
  Serial.print(ok ? F("PASS") : F("FAIL"));
  Serial.print(F(" before="));
  Serial.print(beforeReset.linkStoredConfigCount);
  Serial.print(F(" reset="));
  Serial.print(afterReset.linkStoredConfigCount);
  Serial.print(F(" fresh="));
  Serial.print(afterFreshBegin.linkStoredConfigCount);
  Serial.print(F(" status="));
  Serial.print(createStatus[0], HEX);
  Serial.print('/');
  Serial.print(createStatus[1], HEX);
  Serial.print('/');
  Serial.print(createStatus[2], HEX);
  Serial.print(F(" ids="));
  Serial.print(beforeReset.linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(beforeReset.linkSlot1ConfigId);
  Serial.print(F(" fresh_ids="));
  Serial.print(afterFreshBegin.linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(afterFreshBegin.linkSlot1ConfigId);
  Serial.print(F(" polls="));
  Serial.print(firstBeginPolls);
  Serial.print('/');
  Serial.print(count4Polls);
  Serial.print('/');
  Serial.println(secondBeginPolls);
  return ok;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) {
  }
  Serial.println(F("BleChannelSoundingVprResetClearsConfigs"));
  (void)runResetClearsConfigsProbe();
}

void loop() {}
