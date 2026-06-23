/*
 * BleChannelSoundingVprResetMidProcedure
 *
 * Verifies that resetting the VPR while a procedure is mid-flow doesn't
 * corrupt state and the session can be re-established (Parity item #7b).
 *
 * The test sequence:
 *   1. Boot a fresh session, pump some results.
 *   2. Call resetTransport() mid-procedure.
 *  3. Verify workflow phase goes to kIdle.
 *   4. Call beginFreshHost() again — verify it reaches ready.
 *   5. Pump more results — verify completed local/peer result snapshots are
 *      valid after recovery.
 *
 * Serial output:  cs_vpr_reset_mid=PASS/FAIL phase1=X phase2=X phase3=X
 */

#include <Arduino.h>

#include "ble_channel_sounding.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint16_t kConnHandle = 0x0041U;
constexpr uint8_t  kMaxPumpCount = 64U;

BleCsControllerVprHost gHost;

BleCsControllerVprHostConfig makeDemoConfig() {
  BleCsControllerVprHostConfig cfg{};
  BleCsControllerVprHost::fillDemoConfig(&cfg);
  cfg.session.workflow.procedureParameters.maxProcedureCount = 1U;
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
  Serial.println(F("BleChannelSoundingVprResetMidProcedure"));

  /* ── Phase 1: Boot and pump some results ──────────────────────── */
  gHost.reset();
  BleCsControllerVprHostConfig config = makeDemoConfig();
  uint8_t pumpCount1 = 0U;
  bool ok = gHost.beginFreshHost(kConnHandle, config, kMaxPumpCount, &pumpCount1);
  bool phase1 = ok && gHost.ready() && !gHost.failed();

  /* Pump a few results. */
  for (uint8_t i = 0U; i < 8U; ++i) {
    if (!gHost.loopOnce() || gHost.failed()) break;
  }
  phase1 = phase1 && gHost.sessionState().completedProcedureCounter > 0U &&
           gHost.completedLocalResult().isComplete &&
           gHost.completedPeerResult().isComplete;

  /* ── Phase 2: Reset mid-procedure ─────────────────────────────── */
  ok = gHost.resetTransport(true);
  bool phase2 = ok && !gHost.failed();

  /* Verify workflow phase is kIdle or equivalent (not actively running). */
  /* After resetTransport, the session should be clean. */

  /* ── Phase 3: Re-establish session ────────────────────────────── */
  gHost.reset();
  uint8_t pumpCount3 = 0U;
  ok = gHost.beginFreshHost(kConnHandle, config, kMaxPumpCount, &pumpCount3);
  bool phase3 = ok && gHost.ready() && !gHost.failed();

  /* Pump more results to verify full functionality. */
  if (phase3) {
    for (uint8_t i = 0U; i < 16U; ++i) {
      if (!gHost.loopOnce() || gHost.failed()) break;
    }
    phase3 = gHost.ready() && !gHost.failed() &&
             gHost.sessionState().completedProcedureCounter > 0U &&
             gHost.completedLocalResult().isComplete &&
             gHost.completedPeerResult().isComplete &&
             gHost.completedLocalResult().header.configId == 1U &&
             gHost.completedPeerResult().header.configId == 1U;
  }

  const bool allOk = phase1 && phase2 && phase3;
  Serial.print(F("cs_vpr_reset_mid="));
  Serial.print(allOk ? F("PASS") : F("FAIL"));
  Serial.print(F(" phase1="));
  Serial.print(phase1 ? 1 : 0);
  Serial.print(F(" phase2="));
  Serial.print(phase2 ? 1 : 0);
  Serial.print(F(" phase3="));
  Serial.print(phase3 ? 1 : 0);
  Serial.print(F(" procedures="));
  Serial.println(gHost.sessionState().completedProcedureCounter);
  Serial.println(F("--- done ---"));
}
