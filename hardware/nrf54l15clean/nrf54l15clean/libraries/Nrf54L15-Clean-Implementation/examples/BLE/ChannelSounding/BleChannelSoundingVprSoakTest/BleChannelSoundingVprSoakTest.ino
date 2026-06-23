/*
 * BleChannelSoundingVprSoakTest
 *
 * Long-running stability test for the VPR CS controller (Parity item #7a).
 *
 * The test sequence:
 *   1. Pump until at least 100 completed procedures — verify no state corruption
 *   2. Disconnect + reconnect cycle × 10 — verify clean recovery
 *   3. Create/remove config × 10 — verify slot reuse
 *   4. Verify the session is still usable after all stress
 *
 * Serial output:  cs_vpr_soak=PASS/FAIL procedures=N disconnects=N configs=N
 */

#include <Arduino.h>

#include "ble_channel_sounding.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint16_t kConnHandle = 0x0041U;
constexpr uint8_t  kMaxPumpCount = 64U;

BleCsControllerVprHost gHost;

constexpr uint8_t kBleCsHciStatusSuccess = 0x00U;

/* Helper: create a fresh demo config. */
BleCsControllerVprHostConfig makeDemoConfig() {
  BleCsControllerVprHostConfig cfg{};
  BleCsControllerVprHost::fillDemoConfig(&cfg);
  cfg.session.workflow.procedureParameters.maxProcedureCount = 1U;
  return cfg;
}

BleCsControllerCreateConfig makeCreateConfig(uint8_t configId) {
  BleCsControllerVprHostConfig cfg = makeDemoConfig();
  BleCsControllerCreateConfig createConfig = cfg.session.workflow.createConfig;
  createConfig.configId = configId;
  return createConfig;
}

/* ── Phase 1: Pump 500+ iterations ─────────────────────────────────── */
uint16_t testCompletedProcedures() {
  gHost.reset();
  BleCsControllerVprHostConfig config = makeDemoConfig();
  config.session.workflow.procedureParameters.maxProcedureCount = 100U;
  uint8_t pumpCount = 0U;
  bool ok = gHost.beginFreshHost(kConnHandle, config, kMaxPumpCount, &pumpCount);
  if (!ok || !gHost.ready()) return 0U;

  uint16_t iterations = 0U;
  const uint16_t targetProcedures = 100U;
  const uint16_t maxIterations = 4000U;

  while (iterations < maxIterations &&
         gHost.sessionState().completedProcedureCounter < targetProcedures) {
    const bool loopOk = gHost.loopOnce();
    if (!loopOk || gHost.failed()) break;
    iterations++;
  }
  if (!gHost.completedLocalResult().isComplete ||
      !gHost.completedPeerResult().isComplete) {
    return 0U;
  }
  return gHost.sessionState().completedProcedureCounter;
}

/* ── Phase 2: Disconnect + reconnect cycle × 10 ────────────────────── */
uint8_t testDisconnectReconnectCycles() {
  uint8_t cycles = 0U;
  const uint8_t targetCycles = 10U;

  for (uint8_t i = 0U; i < targetCycles; ++i) {
    /* Fresh session. */
    gHost.reset();
    BleCsControllerVprHostConfig config = makeDemoConfig();
    uint8_t pumpCount = 0U;
    bool ok = gHost.beginFreshHost(kConnHandle, config, kMaxPumpCount, &pumpCount);
    if (!ok || !gHost.ready()) break;

    /* Pump a few results. */
    for (uint8_t j = 0U; j < 8U; ++j) {
      const bool loopOk = gHost.loopOnce();
      if (!loopOk || gHost.failed()) break;
    }
    if (gHost.failed()) break;

    /* Disconnect via resetTransport. */
    ok = gHost.resetTransport(true);
    if (!ok) break;

    cycles++;
  }
  return cycles;
}

/* ── Phase 3: Create/remove config × 10 ────────────────────────────── */
uint8_t testConfigCreateRemoveCycles() {
  gHost.reset();
  BleCsControllerVprHostConfig config = makeDemoConfig();
  uint8_t pumpCount = 0U;
  bool ok = gHost.beginFreshHost(kConnHandle, config, kMaxPumpCount, &pumpCount);
  if (!ok || !gHost.ready()) return 0U;

  uint8_t cycles = 0U;
  const uint8_t targetCycles = 10U;

  for (uint8_t i = 0U; i < targetCycles; ++i) {
    /* Create config 2. */
    BleCsControllerCreateConfig createConfig = makeCreateConfig(2U);
    uint8_t createStatus = 0xFFU;
    ok = ok && gHost.directCreateConfig(createConfig, &createStatus);
    if (!ok || createStatus != kBleCsHciStatusSuccess) break;

    /* Remove config 2. */
    uint8_t removeStatus = 0xFFU;
    ok = ok && gHost.directRemoveConfig(2U, &removeStatus);
    if (!ok || removeStatus != kBleCsHciStatusSuccess) break;

    cycles++;
  }
  return cycles;
}

/* ── Phase 4: Final sanity check ───────────────────────────────────── */
bool testFinalSanity() {
  gHost.reset();
  BleCsControllerVprHostConfig config = makeDemoConfig();
  uint8_t pumpCount = 0U;
  bool ok = gHost.beginFreshHost(kConnHandle, config, kMaxPumpCount, &pumpCount);
  if (!ok || !gHost.ready()) return false;

  /* Pump 32 iterations — verify no corruption. */
  for (uint8_t i = 0U; i < 32U; ++i) {
    if (!gHost.loopOnce() || gHost.failed()) return false;
  }
  return gHost.ready() && !gHost.failed() &&
         gHost.sessionState().completedProcedureCounter > 0U &&
         gHost.completedLocalResult().isComplete &&
         gHost.completedPeerResult().isComplete;
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
  Serial.println(F("BleChannelSoundingVprSoakTest"));

  const uint16_t procedures = testCompletedProcedures();
  Serial.print(F("phase1=procedures="));
  Serial.println(procedures);

  const uint8_t disconnects = testDisconnectReconnectCycles();
  Serial.print(F("phase2=disconnects="));
  Serial.println(disconnects);

  const uint8_t configs = testConfigCreateRemoveCycles();
  Serial.print(F("phase3=configs="));
  Serial.println(configs);

  const bool finalOk = testFinalSanity();
  Serial.print(F("phase4=ok="));
  Serial.println(finalOk ? 1 : 0);

  const bool allOk = (procedures >= 100U) && (disconnects == 10U) &&
                     (configs == 10U) && finalOk;
  Serial.print(F("cs_vpr_soak="));
  Serial.print(allOk ? F("PASS") : F("FAIL"));
  Serial.print(F(" procedures="));
  Serial.print(procedures);
  Serial.print(F(" disconnects="));
  Serial.print(disconnects);
  Serial.print(F(" configs="));
  Serial.print(configs);
  Serial.print(F(" final="));
  Serial.println(finalOk ? 1 : 0);
  Serial.println(F("--- done ---"));
}
