/*
 * BleChannelSoundingVprEdgeCases
 *
 * Exercises contradictory command sequences and edge-case parameters
 * that can confuse the CS controller state machine (Parity item #5c).
 *
 * The test sequence:
 *   1. Remove non-existent config → Invalid Params (0x12)
 *   2. Create config 2, remove it, then set procedure params on removed
 *      config 2 → should fail (config no longer exists)
 *   3. Procedure enable/disable cycles (5 cycles) — verify state machine
 *      handles repeated transitions correctly
 *   4. Create config with configId=0 → Invalid Params (0x12)
 *   5. Security enable without prior caps read → should not corrupt state
 *
 * Serial output:  cs_vpr_edge_cases=PASS/FAIL e1=X e2=X e3=X e4=X e5=X
 */

#include <Arduino.h>

#include "ble_channel_sounding.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint16_t kConnHandle = 0x0041U;
constexpr uint8_t  kMaxPumpCount = 64U;

BleCsControllerVprHost gHost;

/* HCI CS status codes (Bluetooth Core Spec). */
constexpr uint8_t kBleCsHciStatusSuccess       = 0x00U;
constexpr uint8_t kBleCsHciStatusCommandDisallowed = 0x0CU;
constexpr uint8_t kBleCsHciStatusInvalidParams = 0x12U;

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

BleCsProcedureParameters makeProcedureParams(uint8_t configId) {
  BleCsControllerVprHostConfig cfg = makeDemoConfig();
  BleCsProcedureParameters params = cfg.session.workflow.procedureParameters;
  params.configId = configId;
  params.maxProcedureCount = 1U;
  return params;
}

/* ── Edge 1: Remove non-existent config ─────────────────────────────── */
bool testRemoveNonExistentConfig() {
  gHost.reset();
  BleCsControllerVprHostConfig config = makeDemoConfig();
  uint8_t pumpCount = 0U;
  bool ok = gHost.beginFreshHost(kConnHandle, config, kMaxPumpCount, &pumpCount);
  if (!ok || !gHost.ready()) return false;

  uint8_t status = 0xFFU;
  ok = ok && gHost.directRemoveConfig(99U, &status);
  return ok && status == kBleCsHciStatusInvalidParams;
}

/* ── Edge 2: Set params on a removed config ─────────────────────────── */
bool testParamsOnRemovedConfig() {
  gHost.reset();
  BleCsControllerVprHostConfig config = makeDemoConfig();
  uint8_t pumpCount = 0U;
  bool ok = gHost.beginFreshHost(kConnHandle, config, kMaxPumpCount, &pumpCount);
  if (!ok || !gHost.ready()) return false;

  /* Create config 2. */
  BleCsControllerCreateConfig createConfig = makeCreateConfig(2U);
  uint8_t createStatus = 0xFFU;
  ok = ok && gHost.directCreateConfig(createConfig, &createStatus);
  ok = ok && createStatus == kBleCsHciStatusSuccess;

  /* Remove config 2. */
  uint8_t removeStatus = 0xFFU;
  ok = ok && gHost.directRemoveConfig(2U, &removeStatus);
  ok = ok && removeStatus == kBleCsHciStatusSuccess;

  /* Try to set procedure params for removed config 2.
   * Zephyr rejects this as invalid parameters because the config no longer
   * exists; keep this exact so regressions are visible. */
  BleCsProcedureParameters params = makeProcedureParams(2U);
  uint8_t setStatus = 0xFFU;
  ok = ok && gHost.directSetProcedureParameters(params, &setStatus);
  return ok && setStatus == kBleCsHciStatusInvalidParams;
}

/* ── Edge 3: Procedure enable/disable cycles ───────────────────────── */
bool testEnableDisableCycles() {
  gHost.reset();
  BleCsControllerVprHostConfig config = makeDemoConfig();
  uint8_t pumpCount = 0U;
  bool ok = gHost.beginFreshHost(kConnHandle, config, kMaxPumpCount, &pumpCount);
  if (!ok || !gHost.ready()) return false;

  /* The demo config already enables the procedure via beginFreshHost.
   * We can disable and re-enable to test the cycles. */
  for (uint8_t i = 0U; i < 5U; ++i) {
    /* Disable. */
    uint8_t status = 0xFFU;
    ok = ok && gHost.directProcedureEnable(1U, false, &status);
    if (!ok || status != kBleCsHciStatusSuccess) return false;

    /* Enable. */
    ok = ok && gHost.directProcedureEnable(1U, true, &status);
    if (!ok || status != kBleCsHciStatusSuccess) return false;
  }
  return ok;
}

/* ── Edge 4: Create config with configId=0 ────────────────────────── */
bool testCreateConfigZero() {
  gHost.reset();
  BleCsControllerVprHostConfig config = makeDemoConfig();
  uint8_t pumpCount = 0U;
  bool ok = gHost.beginFreshHost(kConnHandle, config, kMaxPumpCount, &pumpCount);
  if (!ok || !gHost.ready()) return false;

  BleCsControllerCreateConfig createConfig = makeCreateConfig(0U);
  uint8_t status = 0xFFU;
  ok = ok && gHost.directCreateConfig(createConfig, &status);
  return ok && status == kBleCsHciStatusInvalidParams;
}

/* ── Edge 5: Security enable without prior caps read ───────────────── */
bool testSecurityBeforeCaps() {
  gHost.reset();
  BleCsControllerVprHostConfig config = makeDemoConfig();
  uint8_t pumpCount = 0U;
  bool ok = gHost.beginFreshHost(kConnHandle, config, kMaxPumpCount, &pumpCount);
  if (!ok || !gHost.ready()) return false;

  /* Enable security directly, without reading remote caps first. */
  uint8_t status = 0xFFU;
  ok = ok && gHost.directSecurityEnable(&status);
  if (!ok ||
      (status != kBleCsHciStatusSuccess &&
       status != kBleCsHciStatusCommandDisallowed)) {
    return false;
  }

  /* Verify the session is still usable. */
  uint8_t pumpCount2 = 0U;
  while (pumpCount2 < 16U) {
    const bool loopOk = gHost.loopOnce();
    if (!loopOk) break;
    pumpCount2++;
    if (gHost.failed()) return false;
  }
  return !gHost.failed();
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
  Serial.println(F("BleChannelSoundingVprEdgeCases"));

  const bool e1 = testRemoveNonExistentConfig();
  const bool e2 = testParamsOnRemovedConfig();
  const bool e3 = testEnableDisableCycles();
  const bool e4 = testCreateConfigZero();
  const bool e5 = testSecurityBeforeCaps();

  const bool allOk = e1 && e2 && e3 && e4 && e5;
  Serial.print(F("cs_vpr_edge_cases="));
  Serial.print(allOk ? F("PASS") : F("FAIL"));
  Serial.print(F(" e1="));
  Serial.print(e1 ? 1 : 0);
  Serial.print(F(" e2="));
  Serial.print(e2 ? 1 : 0);
  Serial.print(F(" e3="));
  Serial.print(e3 ? 1 : 0);
  Serial.print(F(" e4="));
  Serial.print(e4 ? 1 : 0);
  Serial.print(F(" e5="));
  Serial.println(e5 ? 1 : 0);
  Serial.println(F("--- done ---"));
}
