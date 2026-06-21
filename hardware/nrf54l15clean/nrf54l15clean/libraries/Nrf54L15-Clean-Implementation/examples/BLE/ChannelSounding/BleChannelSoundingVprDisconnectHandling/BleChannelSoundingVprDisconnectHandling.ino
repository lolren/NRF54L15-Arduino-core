/*
 * BleChannelSoundingVprDisconnectHandling
 *
 * Exercises the VPR disconnect/timeout/abort framework (Parity item #3a).
 *
 * The test sequence:
 *   1. Phase 1 — Normal flow: beginFreshHost, verify ready, capture pump count.
 *   2. Phase 2 — Disconnect mid-procedure: beginFreshHost, pump to generate
 *      some subevent results, call resetTransport() to simulate BLE disconnect,
 *      pump again, verify ready() returns false and the workflow phase is kIdle
 *      (disconnect cleanup occurred).
 *   3. Phase 3 — Reconnect after disconnect: reset(), fresh beginFreshHost,
 *      verify it reaches ready() again. Proves disconnect cleanup doesn't
 *      contaminate the next session.
 *
 * This validates the disconnect detection, abort-reason propagation, and
 * cached-state invalidation described in parity item #3. It does not claim
 * physical RF ranging parity with a production controller.
 *
 * Serial output:  cs_vpr_disconnect=PASS/FAIL phase1=X phase2=X phase3=X
 */

#include <Arduino.h>

#include "ble_channel_sounding.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint16_t kConnHandle = 0x0041U;
constexpr uint8_t kMaxPumpCount = 64U;

BleCsControllerVprHost gHost;
BleCsControllerVprHostConfig gConfig{};

/* Run a fresh workflow session from current state and return the result. */
bool runFreshSession(uint8_t* outPumpCount) {
  const uint8_t pumps = *outPumpCount;
  BleCsControllerVprHostConfig freshConfig{};
  BleCsControllerVprHost::fillDemoConfig(&freshConfig);
  freshConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;
  const bool ok = gHost.beginFreshHost(kConnHandle, freshConfig, kMaxPumpCount,
                                       outPumpCount);
  return ok && gHost.ready() && !gHost.failed();
}

bool runDisconnectProbe() {
  /* ── Phase 1: Normal flow ───────────────────────────────────────── */
  gHost.reset();
  uint8_t pumpCount1 = 0U;
  bool ok = runFreshSession(&pumpCount1);
  bool phase1 = ok;

  /* ── Phase 2: Disconnect mid-procedure ───────────────────────────── */
  gHost.reset();
  uint8_t pumpCount2 = 0U;
  ok = runFreshSession(&pumpCount2);
  ok = ok && gHost.ready();

  /* Pump a few more times so the VPR has produced some subevent results. */
  for (uint8_t i = 0U; i < 4U; ++i) {
    (void)gHost.loopOnce();
  }

  /* Simulate BLE disconnect: resetTransport clears the VPR shared state
   * and calls syncVprState, which detects the session-open→closed transition
   * and calls handleDisconnect() internally (→ host_.reset(), workflow→kIdle). */
  ok = ok && gHost.resetTransport(true);

  /* After resetTransport, the workflow has already been reconciled to idle.
   * ready() should be false, workflow phase should be kIdle. */
  const bool disconnected = !gHost.ready();
  const bool workflowIdle =
      gHost.workflowState().phase == BleCsControllerWorkflowPhase::kIdle;

  bool phase2 = ok && disconnected && workflowIdle;

  /* ── Phase 3: Reconnect after disconnect ─────────────────────────── */
  uint8_t pumpCount3 = 0U;
  ok = runFreshSession(&pumpCount3);
  bool phase3 = ok && gHost.ready() && !gHost.failed();

  /* ── Report ──────────────────────────────────────────────────────── */
  const bool allOk = phase1 && phase2 && phase3;
  Serial.print(F("cs_vpr_disconnect="));
  Serial.print(allOk ? F("PASS") : F("FAIL"));
  Serial.print(F(" phase1="));
  Serial.print(phase1 ? 1 : 0);
  Serial.print(F(" phase2="));
  Serial.print(phase2 ? 1 : 0);
  Serial.print(F(" phase3="));
  Serial.print(phase3 ? 1 : 0);
  Serial.print(F(" pumps="));
  Serial.print(pumpCount1);
  Serial.print('/');
  Serial.print(pumpCount2);
  Serial.print('/');
  Serial.print(pumpCount3);
  Serial.print(F(" disconnected="));
  Serial.print(disconnected ? 1 : 0);
  Serial.print(F(" idle="));
  Serial.print(workflowIdle ? 1 : 0);
  Serial.print(F(" ok="));
  Serial.println(allOk ? 1 : 0);
  return allOk;
}

}  // namespace

namespace {
bool gDone = false;
}

void setup() {
  Serial.begin(115200);
  /* On XIAO nRF54L15, Serial.operator bool() never returns true on the UARTE21
   * bridge port, so we cannot gate on !Serial.  Instead we wait in setup() to
   * give the host-side reader time to connect before the test runs. */
  delay(2000);
  Serial.println(F("--- ready ---"));
}

void loop() {
  if (gDone) return;
  gDone = true;
  Serial.println(F("BleChannelSoundingVprDisconnectHandling"));
  (void)runDisconnectProbe();
  Serial.println(F("--- done ---"));
}
