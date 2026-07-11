/*
 * BleChannelSoundingVprHciBurst
 *
 * Sends N direct-HCI commands in rapid succession and verifies each command
 * completes with the expected status without wedging the VPR transport
 * (Parity item #5d, public API coverage).
 *
 * Note: BleCsControllerVprHost::direct* helpers intentionally pre-drain and
 * wait for command status, matching the safe public Arduino API. This example
 * is therefore a command-burst resilience test, not a raw async queue-fill
 * test. A true queue saturation test belongs below the public direct-HCI layer.
 *
 * The test sequence:
 *   1. Boot a fresh session.
 *   2. Send 10 direct-HCI commands back-to-back (create config, enable
 *      security, set params, remove config, reject params on removed config).
 *   3. Verify accepted and rejected commands return stable HCI status values.
 *   4. Verify the session is still usable after the burst.
 *
 * Serial output:  cs_vpr_hci_burst=PASS/FAIL sent=N success=N rejected=N
 */

#include <Arduino.h>

#include "ble_channel_sounding.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint16_t kConnHandle = 0x0041U;
constexpr uint8_t  kMaxPumpCount = 64U;
constexpr uint8_t  kCommandsToSend = 10U;

BleCsControllerVprHost gHost;

constexpr uint8_t kBleCsHciStatusSuccess = 0x00U;
constexpr uint8_t kBleCsHciStatusInvalidParams = 0x12U;

BleCsControllerCreateConfig makeCreateConfig(uint8_t configId) {
  BleCsControllerVprHostConfig cfg{};
  BleCsControllerVprHost::fillDemoConfig(&cfg);
  BleCsControllerCreateConfig createConfig = cfg.session.workflow.createConfig;
  createConfig.configId = configId;
  return createConfig;
}

BleCsProcedureParameters makeProcedureParams(uint8_t configId) {
  BleCsControllerVprHostConfig cfg{};
  BleCsControllerVprHost::fillDemoConfig(&cfg);
  BleCsProcedureParameters params = cfg.session.workflow.procedureParameters;
  params.configId = configId;
  params.maxProcedureCount = 1U;
  return params;
}

struct BurstStats {
  uint8_t sent = 0U;
  uint8_t success = 0U;
  uint8_t rejected = 0U;
  uint8_t usablePolls = 0U;
};

/* Send a batch through the public direct-HCI API and verify no command leaves
 * stale VPR state behind for the next command. */
BurstStats burstProbe() {
  BurstStats stats{};
  gHost.reset();
  BleCsControllerVprHostConfig config{};
  BleCsControllerVprHost::fillDemoConfig(&config);
  config.session.workflow.procedureParameters.maxProcedureCount = 1U;
  uint8_t pumpCount = 0U;
  bool ok = gHost.beginFreshHost(kConnHandle, config, kMaxPumpCount, &pumpCount);
  if (!ok || !gHost.ready()) {
    return stats;
  }

  /* Commands are intentionally mixed: config 2 create/security/params/remove
   * should succeed, while set params after removal should be rejected. */
  for (uint8_t i = 0U; i < kCommandsToSend; ++i) {
    uint8_t status = 0xFFU;
    bool cmdOk = false;
    uint8_t expectedStatus = kBleCsHciStatusSuccess;
    switch (i % 5U) {
      case 0U: {
        BleCsControllerCreateConfig createConfig = makeCreateConfig(2U);
        cmdOk = gHost.directCreateConfig(createConfig, &status);
        break;
      }
      case 1U: {
        cmdOk = gHost.directSecurityEnable(&status);
        break;
      }
      case 2U: {
        BleCsProcedureParameters params = makeProcedureParams(2U);
        cmdOk = gHost.directSetProcedureParameters(params, &status);
        break;
      }
      case 3U: {
        cmdOk = gHost.directRemoveConfig(2U, &status);
        break;
      }
      case 4U: {
        BleCsProcedureParameters params = makeProcedureParams(2U);
        cmdOk = gHost.directSetProcedureParameters(params, &status);
        expectedStatus = kBleCsHciStatusInvalidParams;
        break;
      }
    }
    if (!cmdOk) {
      break;
    }
    ++stats.sent;
    if (status == kBleCsHciStatusSuccess) {
      ++stats.success;
    } else if (status == expectedStatus) {
      ++stats.rejected;
    } else {
      break;
    }
  }

  for (uint8_t i = 0U; i < 16U; ++i) {
    if (!gHost.loopOnce() || gHost.failed()) break;
    ++stats.usablePolls;
  }
  return stats;
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
  Serial.println(F("BleChannelSoundingVprHciBurst"));

  const BurstStats stats = burstProbe();

  const bool allOk = (stats.sent == kCommandsToSend) && (stats.success == 8U) &&
                     (stats.rejected == 2U) && (stats.usablePolls > 0U) &&
                     !gHost.failed();
  Serial.print(F("cs_vpr_hci_burst="));
  Serial.print(allOk ? F("PASS") : F("FAIL"));
  Serial.print(F(" sent="));
  Serial.print(stats.sent);
  Serial.print(F(" success="));
  Serial.print(stats.success);
  Serial.print(F(" rejected="));
  Serial.print(stats.rejected);
  Serial.print(F(" polls="));
  Serial.print(stats.usablePolls);
  Serial.print(F(" failed="));
  Serial.println(gHost.failed() ? 1 : 0);
  Serial.println(F("--- done ---"));
}
