/*
 * BleChannelSoundingVprPeerPduInjection
 *
 * Deterministically exercises the VPR Channel Sounding peer-exchange state
 * machine by injecting raw CS LL Control PDU payloads through a test-only
 * vendor HCI command. This validates the host/controller ordering framework:
 * CS_RSP -> CS_CFG -> CS_SEC_RSP -> CS_PROC_RSP -> CS_START -> CS_ABORT.
 *
 * This does not perform over-air LL Control PDU exchange yet; it proves that
 * the controller-side state transitions and timeout/abort bookkeeping are
 * testable before the real radio LL transport is wired.
 *
 * Serial output: cs_vpr_peer_pdu_injection=PASS/FAIL ...
 */

#include <Arduino.h>

#include "ble_channel_sounding.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint16_t kConnHandle = 0x0041U;

constexpr uint8_t kHciStatusSuccess = 0x00U;
constexpr uint8_t kHciStatusInvalidParams = 0x12U;

constexpr uint8_t kLlCsRsp = 0x2DU;
constexpr uint8_t kLlCsCfg = 0x2EU;
constexpr uint8_t kLlCsProcRsp = 0x30U;
constexpr uint8_t kLlCsSecRsp = 0x32U;
constexpr uint8_t kLlCsStart = 0x33U;
constexpr uint8_t kLlCsAbort = 0x35U;

BleCsControllerVprHost gHost;
BleCsControllerVprHostConfig gConfig{};

void fillPdu(uint8_t* pdu, size_t len, uint8_t opcode, uint8_t payloadLen) {
  if (pdu == nullptr || len < 2U) {
    return;
  }
  memset(pdu, 0, len);
  pdu[0] = opcode;
  pdu[1] = payloadLen;
  if (len >= 3U) {
    pdu[2] = 1U;
  }
}

bool bootControlledHost() {
  BleCsControllerVprHost::fillDemoConfig(&gConfig);
  gConfig.builtInPeerDemo.enabled = false;
  gConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;
  return gHost.resetTransport(true) &&
         gHost.loadDefaultTransportImage() &&
         gHost.bootTransport() &&
         gHost.beginHost(kConnHandle, gConfig) &&
         !gHost.failed();
}

bool readStage(uint8_t expectedStage, BleCsVprPeerExchangeState* outState) {
  if (outState == nullptr) {
    return false;
  }
  return gHost.directReadPeerExchangeStateForTest(outState) &&
         outState->valid &&
         outState->status == kHciStatusSuccess &&
         outState->currentStage == expectedStage;
}

bool injectExpect(const uint8_t* pdu,
                  size_t pduLen,
                  uint8_t expectedPrevious,
                  uint8_t expectedCurrent,
                  BleCsVprPeerExchangeState* outState) {
  if (outState == nullptr) {
    return false;
  }
  return gHost.directInjectPeerPduForTest(pdu, pduLen, outState) &&
         outState->valid &&
         outState->status == kHciStatusSuccess &&
         outState->previousStage == expectedPrevious &&
         outState->currentStage == expectedCurrent;
}

void markProgress(uint32_t* progressMask, uint8_t step) {
  if (progressMask != nullptr && step < 32U) {
    *progressMask |= (1UL << step);
  }
}

bool runPeerPduProbe(BleCsVprPeerExchangeState* outFinalState,
                     BleCsVprPeerExchangeState* outInvalidState,
                     uint32_t* outProgressMask,
                     uint8_t* outLastCommandStatus) {
  if (outFinalState == nullptr || outInvalidState == nullptr) {
    return false;
  }
  *outFinalState = BleCsVprPeerExchangeState{};
  *outInvalidState = BleCsVprPeerExchangeState{};
  if (outProgressMask != nullptr) {
    *outProgressMask = 0U;
  }
  if (outLastCommandStatus != nullptr) {
    *outLastCommandStatus = 0xFFU;
  }

  bool ok = bootControlledHost();
  if (ok) markProgress(outProgressMask, 0U);

  uint8_t status = 0xFFU;
  ok = ok && gHost.directReadRemoteSupportedCapabilities(&status) &&
       status == kHciStatusSuccess;
  if (outLastCommandStatus != nullptr) *outLastCommandStatus = status;
  if (ok) markProgress(outProgressMask, 1U);
  ok = ok && gHost.directSetDefaultSettings(
                 gConfig.session.workflow.defaultSettings, &status) &&
       status == kHciStatusSuccess;
  if (outLastCommandStatus != nullptr) *outLastCommandStatus = status;
  if (ok) markProgress(outProgressMask, 2U);
  ok = ok && gHost.directCreateConfig(
                 gConfig.session.workflow.createConfig, &status) &&
       status == kHciStatusSuccess;
  if (outLastCommandStatus != nullptr) *outLastCommandStatus = status;
  if (ok) markProgress(outProgressMask, 3U);

  BleCsVprPeerExchangeState state{};
  ok = ok && readStage(kBleCsVprPeerStageAwaitingCsRsp, &state);
  *outFinalState = state;
  if (ok) markProgress(outProgressMask, 4U);

  uint8_t csRsp[4];
  fillPdu(csRsp, sizeof(csRsp), kLlCsRsp, 2U);
  ok = ok && injectExpect(csRsp, sizeof(csRsp),
                          kBleCsVprPeerStageAwaitingCsRsp,
                          kBleCsVprPeerStageAwaitingCsCfg, &state);
  *outFinalState = state;
  if (ok) markProgress(outProgressMask, 5U);

  uint8_t csCfg[23];
  fillPdu(csCfg, sizeof(csCfg), kLlCsCfg, 21U);
  ok = ok && injectExpect(csCfg, sizeof(csCfg),
                          kBleCsVprPeerStageAwaitingCsCfg,
                          kBleCsVprPeerStageIdle, &state);
  *outFinalState = state;
  if (ok) markProgress(outProgressMask, 6U);

  ok = ok && gHost.directSecurityEnable(&status) &&
       status == kHciStatusSuccess;
  if (outLastCommandStatus != nullptr) *outLastCommandStatus = status;
  if (ok) markProgress(outProgressMask, 7U);
  ok = ok && readStage(kBleCsVprPeerStageAwaitingSecRsp, &state);
  *outFinalState = state;
  if (ok) markProgress(outProgressMask, 8U);

  uint8_t csSecRsp[3];
  fillPdu(csSecRsp, sizeof(csSecRsp), kLlCsSecRsp, 1U);
  ok = ok && injectExpect(csSecRsp, sizeof(csSecRsp),
                          kBleCsVprPeerStageAwaitingSecRsp,
                          kBleCsVprPeerStageIdle, &state);
  *outFinalState = state;
  if (ok) markProgress(outProgressMask, 9U);

  ok = ok && gHost.directSetProcedureParameters(
                 gConfig.session.workflow.procedureParameters, &status) &&
       status == kHciStatusSuccess;
  if (outLastCommandStatus != nullptr) *outLastCommandStatus = status;
  if (ok) markProgress(outProgressMask, 10U);
  ok = ok && readStage(kBleCsVprPeerStageAwaitingProcRsp, &state);
  *outFinalState = state;
  if (ok) markProgress(outProgressMask, 11U);

  uint8_t csProcRsp[21];
  fillPdu(csProcRsp, sizeof(csProcRsp), kLlCsProcRsp, 19U);
  ok = ok && injectExpect(csProcRsp, sizeof(csProcRsp),
                          kBleCsVprPeerStageAwaitingProcRsp,
                          kBleCsVprPeerStageAwaitingStart, &state);
  *outFinalState = state;
  if (ok) markProgress(outProgressMask, 12U);

  ok = ok && gHost.directProcedureEnable(
                 gConfig.session.workflow.procedureEnable, &status) &&
       status == kHciStatusSuccess;
  if (outLastCommandStatus != nullptr) *outLastCommandStatus = status;
  if (ok) markProgress(outProgressMask, 13U);
  ok = ok && readStage(kBleCsVprPeerStageAwaitingStart, &state);
  *outFinalState = state;
  if (ok) markProgress(outProgressMask, 14U);

  uint8_t csStart[16];
  fillPdu(csStart, sizeof(csStart), kLlCsStart, 14U);
  ok = ok && injectExpect(csStart, sizeof(csStart),
                          kBleCsVprPeerStageAwaitingStart,
                          kBleCsVprPeerStageProcedureActive, &state);
  *outFinalState = state;
  if (ok) markProgress(outProgressMask, 15U);

  uint8_t csAbort[3] = {kLlCsAbort, 1U, 0x42U};
  ok = ok && injectExpect(csAbort, sizeof(csAbort),
                          kBleCsVprPeerStageProcedureActive,
                          kBleCsVprPeerStageIdle, &state);
  ok = ok && state.procedureAbortReason == 0x42U &&
       state.subeventAbortReason == 0x42U;
  *outFinalState = state;
  if (ok) markProgress(outProgressMask, 16U);

  uint8_t malformedPdu[3] = {kLlCsRsp, 2U, 0U};
  ok = ok && gHost.directInjectPeerPduForTest(
                 malformedPdu, sizeof(malformedPdu), outInvalidState) &&
       outInvalidState->valid &&
       outInvalidState->status == kHciStatusInvalidParams;
  if (ok) markProgress(outProgressMask, 17U);

  return ok;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500U) {
  }

  Serial.println(F("BleChannelSoundingVprPeerPduInjection"));
  BleCsVprPeerExchangeState finalState{};
  BleCsVprPeerExchangeState invalidState{};
  uint32_t progressMask = 0U;
  uint8_t lastCommandStatus = 0xFFU;
  const bool ok = runPeerPduProbe(&finalState, &invalidState,
                                  &progressMask, &lastCommandStatus);

  Serial.print(F("cs_vpr_peer_pdu_injection="));
  Serial.print(ok ? F("PASS") : F("FAIL"));
  Serial.print(F(" progress=0x"));
  Serial.print(progressMask, HEX);
  Serial.print(F(" final_stage="));
  Serial.print(finalState.currentStage);
  Serial.print(F(" prev_stage="));
  Serial.print(finalState.previousStage);
  Serial.print(F(" state_status=0x"));
  Serial.print(finalState.status, HEX);
  Serial.print(F(" hci_status=0x"));
  Serial.print(lastCommandStatus, HEX);
  Serial.print(F(" valid="));
  Serial.print(finalState.valid ? 1 : 0);
  Serial.print(F(" abort=0x"));
  Serial.print(finalState.procedureAbortReason, HEX);
  Serial.print(F(" invalid_status=0x"));
  Serial.println(invalidState.status, HEX);
}

void loop() {}
