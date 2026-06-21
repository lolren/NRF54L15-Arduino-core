/*
 * BleChannelSoundingVprCachedCapabilities
 *
 * Exercises the per-connection cached capability and FAE state that the
 * BleCsControllerVprHost maintains on the Arduino host side (independent of
 * the VPR's limited RAM).
 *
 * The test sequence:
 *   1. beginFreshHost — drives the connected workflow to completion; the
 *      workflow's internal state records the remote capabilities read during
 *      the exchange, but the VprHost-level caching accessors are not populated
 *      by the workflow path (only by directReadRemoteSupportedCapabilities /
 *      directWrite* or the auxiliary event drain).
 *   2. Verify that cachedRemoteCapabilitiesV1/V2 return false before any
 *      direct read or write has populated them.
 *   3. directReadRemoteSupportedCapabilities — the VPR emits a Command Status
 *      followed by an LE Meta Read Remote Supported Capabilities Complete V2
 *      event (0x38). The auxiliary event drain catches and caches it.
 *   4. Verify cachedRemoteCapabilitiesV2 returns valid data (v1 stays invalid
 *      because the VPR emits the V2 event for both the v1 and V2 reads).
 *   5. directWriteCachedRemoteSupportedCapabilities with a custom v1 payload
 *      — caches v1 locally on success.
 *   6. Verify cachedRemoteCapabilitiesV1 returns the custom data.
 *   7. directWriteCachedRemoteSupportedCapabilitiesV2 with a custom v2 payload
 *      — caches v2 locally on success.
 *   8. Verify cachedRemoteCapabilitiesV2 returns the custom data (overwriting
 *      the VPR-read data from step 3).
 *   9. directWriteCachedRemoteFaeTable with a custom FAE table — cached
 *      locally on success.
 *  10. Verify lastRemoteFaeTable returns the written data.
 *  11. Reset the VprHost.
 *  12. Verify all cached capabilities and the FAE table are invalidated.
 *
 * This validates the host-side caching lifecycle described in parity item #2
 * (per-connection cached capability and FAE state management). It does not
 * claim RF ranging parity with a production controller.
 */

#include <Arduino.h>

#include "ble_channel_sounding.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint16_t kConnHandle = 0x0041U;
constexpr uint8_t kExpectedNumConfig = 4U;
constexpr uint16_t kExpectedMaxProcedures = 8U;
constexpr uint8_t kExpectedNumAntennas = 2U;
constexpr uint8_t kExpectedMaxAntennaPaths = 4U;

BleCsControllerVprHost gHost;
BleCsControllerVprHostConfig gConfig{};

/* Builds a v1 BleCsControllerCapabilities with distinctive test values that
 * differ from the VPR's default/local capabilities. */
BleCsControllerCapabilities makeCustomV1Caps() {
  BleCsControllerCapabilities caps{};
  caps.valid = true;
  caps.isV2 = false;
  caps.status = 0U;
  caps.connHandle = kConnHandle;
  caps.numConfigSupported = 2U;
  caps.maxConsecutiveProceduresSupported = 4U;
  caps.numAntennasSupported = 1U;
  caps.maxAntennaPathsSupported = 2U;
  caps.initiatorSupported = true;
  caps.reflectorSupported = false;
  caps.mode3Supported = true;
  caps.rttCapability = 1U;
  caps.rttAaOnlyN = 2U;
  caps.rttSoundingN = 3U;
  caps.rttRandomPayloadN = 4U;
  caps.nadmSoundingCapability = 100U;
  caps.nadmRandomCapability = 200U;
  caps.csSync2mPhySupported = true;
  caps.csSync2m2btPhySupported = true;
  caps.csWithoutFaeSupported = false;
  caps.chselAlg3cSupported = true;
  caps.pbrFromRttSoundingSeqSupported = false;
  caps.csIptReflectorSupported = true;
  caps.tIp1TimesSupported = 0x003CU;
  caps.tIp2TimesSupported = 0x001EU;
  caps.tFcsTimesSupported = 0x00F0U;
  caps.tPmTimesSupported = 0x00F0U;
  caps.tSwTimeSupported = 32U;
  caps.txSnrCapability = 3U;
  caps.tIp2IptTimesSupported = 0x001EU;
  caps.tSwIptTimeSupported = 32U;
  return caps;
}

/* Builds a V2 BleCsControllerCapabilities with values that differ from both
 * the VPR's defaults and the custom v1 values above. */
BleCsControllerCapabilities makeCustomV2Caps() {
  BleCsControllerCapabilities caps{};
  caps.valid = true;
  caps.isV2 = true;
  caps.status = 0U;
  caps.connHandle = kConnHandle;
  caps.numConfigSupported = 3U;
  caps.maxConsecutiveProceduresSupported = 6U;
  caps.numAntennasSupported = 2U;
  caps.maxAntennaPathsSupported = 3U;
  caps.initiatorSupported = false;
  caps.reflectorSupported = true;
  caps.mode3Supported = true;
  caps.rttCapability = 2U;
  caps.rttAaOnlyN = 1U;
  caps.rttSoundingN = 2U;
  caps.rttRandomPayloadN = 5U;
  caps.nadmSoundingCapability = 150U;
  caps.nadmRandomCapability = 250U;
  caps.csSync2mPhySupported = true;
  caps.csSync2m2btPhySupported = false;
  caps.csWithoutFaeSupported = true;
  caps.chselAlg3cSupported = false;
  caps.pbrFromRttSoundingSeqSupported = true;
  caps.csIptReflectorSupported = false;
  caps.tIp1TimesSupported = 0x001EU;
  caps.tIp2TimesSupported = 0x003CU;
  caps.tFcsTimesSupported = 0x0078U;
  caps.tPmTimesSupported = 0x0078U;
  caps.tSwTimeSupported = 24U;
  caps.txSnrCapability = 2U;
  caps.tIp2IptTimesSupported = 0x003CU;
  caps.tSwIptTimeSupported = 24U;
  return caps;
}

/* Build a custom FAE table that differs from the VPR's default zero table. */
void fillCustomFaeTable(int8_t faeTable[kBleCsFaeTableValueCount]) {
  for (size_t i = 0U; i < kBleCsFaeTableValueCount; ++i) {
    /* Pattern: ..., -5, -3, -1, 1, 3, 5, ... so each value is distinct. */
    faeTable[i] = static_cast<int8_t>(static_cast<int>(i) * 2 - 71);
  }
}

/* Returns true when commandStatus is 0 and the conduit-level ok is true. */
bool cmdOk(bool conduitOk, uint8_t commandStatus) {
  return conduitOk && commandStatus == 0U;
}

bool runCachingLifecycleProbe() {
  /* ── Phase 1: begin, confirm caches are empty ─────────────────── */
  gHost.reset();
  BleCsControllerVprHost::fillDemoConfig(&gConfig);
  gConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;

  uint8_t pumpCount = 0U;
  bool ok = gHost.beginFreshHost(kConnHandle, gConfig, 48U, &pumpCount);
  ok = ok && gHost.ready() && !gHost.failed();

  /* Pre-read check: caches should be invalid (the workflow path does not
   * populate them). */
  BleCsControllerCapabilities preCaps{};
  ok = ok && !gHost.cachedRemoteCapabilitiesV1(&preCaps);
  ok = ok && !gHost.cachedRemoteCapabilitiesV2(&preCaps);

  /* ── Phase 2: read via direct path, populates V2 cache ────────── */
  uint8_t readStatus = 0xFFU;
  ok = ok && cmdOk(gHost.directReadRemoteSupportedCapabilities(&readStatus),
                   readStatus);

  /* After the direct read, the V2 cache should contain whatever the VPR
   * reports (its local/default capabilities via the 0x38 meta event). */
  BleCsControllerCapabilities readCaps{};
  ok = ok && gHost.cachedRemoteCapabilitiesV2(&readCaps) && readCaps.valid &&
       readCaps.connHandle == kConnHandle;
  /* V1 should still be invalid — the VPR only emits the V2 event. */
  ok = ok && !gHost.cachedRemoteCapabilitiesV1(&preCaps);

  /* ── Phase 3: write custom v1, verify cache ───────────────────── */
  const BleCsControllerCapabilities customV1 = makeCustomV1Caps();
  uint8_t writeV1Status = 0xFFU;
  ok = ok &&
       cmdOk(gHost.directWriteCachedRemoteSupportedCapabilities(customV1,
                                                                 &writeV1Status),
             writeV1Status);

  BleCsControllerCapabilities cachedV1{};
  ok = ok && gHost.cachedRemoteCapabilitiesV1(&cachedV1) && cachedV1.valid &&
       cachedV1.numConfigSupported == customV1.numConfigSupported &&
       cachedV1.maxConsecutiveProceduresSupported ==
           customV1.maxConsecutiveProceduresSupported &&
       cachedV1.numAntennasSupported == customV1.numAntennasSupported &&
       cachedV1.initiatorSupported == customV1.initiatorSupported &&
       cachedV1.reflectorSupported == customV1.reflectorSupported &&
       cachedV1.rttCapability == customV1.rttCapability &&
       cachedV1.nadmSoundingCapability == customV1.nadmSoundingCapability &&
       cachedV1.csSync2mPhySupported == customV1.csSync2mPhySupported;

  /* ── Phase 4: write custom v2, verify cache ───────────────────── */
  const BleCsControllerCapabilities customV2 = makeCustomV2Caps();
  uint8_t writeV2Status = 0xFFU;
  ok = ok &&
       cmdOk(gHost.directWriteCachedRemoteSupportedCapabilitiesV2(
                 customV2, &writeV2Status),
             writeV2Status);

  BleCsControllerCapabilities cachedV2{};
  ok = ok && gHost.cachedRemoteCapabilitiesV2(&cachedV2) && cachedV2.valid &&
       cachedV2.numConfigSupported == customV2.numConfigSupported &&
       cachedV2.maxConsecutiveProceduresSupported ==
           customV2.maxConsecutiveProceduresSupported &&
       cachedV2.numAntennasSupported == customV2.numAntennasSupported &&
       cachedV2.initiatorSupported == customV2.initiatorSupported &&
       cachedV2.reflectorSupported == customV2.reflectorSupported &&
       cachedV2.mode3Supported == customV2.mode3Supported &&
       cachedV2.rttCapability == customV2.rttCapability &&
       cachedV2.nadmRandomCapability == customV2.nadmRandomCapability &&
       cachedV2.csWithoutFaeSupported == customV2.csWithoutFaeSupported &&
       cachedV2.chselAlg3cSupported == customV2.chselAlg3cSupported;

  /* V1 cache should still hold the customV1 data (independent of V2). */
  BleCsControllerCapabilities v1AfterV2{};
  ok = ok && gHost.cachedRemoteCapabilitiesV1(&v1AfterV2) &&
       v1AfterV2.numConfigSupported == customV1.numConfigSupported;

  /* ── Phase 5: write custom FAE, verify cache ──────────────────── */
  int8_t customFae[kBleCsFaeTableValueCount] = {0};
  fillCustomFaeTable(customFae);
  uint8_t writeFaeStatus = 0xFFU;
  ok = ok && cmdOk(
                 gHost.directWriteCachedRemoteFaeTable(customFae,
                                                        &writeFaeStatus),
                 writeFaeStatus);

  ok = ok && gHost.lastRemoteFaeTableValid() &&
       gHost.lastRemoteFaeTable().valid &&
       gHost.lastRemoteFaeTable().connHandle == kConnHandle &&
       gHost.lastRemoteFaeTable().status == 0U;

  /* Verify a few FAE values rather than all 72. */
  const BleCsFaeTable& faeTable = gHost.lastRemoteFaeTable();
  ok = ok &&
       faeTable.values[0] == customFae[0] &&
       faeTable.values[17] == customFae[17] &&
       faeTable.values[35] == customFae[35] &&
       faeTable.values[53] == customFae[53] &&
       faeTable.values[71] == customFae[71];

  /* ── Phase 6: reset, verify invalidation ──────────────────────── */
  gHost.reset();

  BleCsControllerCapabilities postResetCaps{};
  ok = ok && !gHost.cachedRemoteCapabilitiesV1(&postResetCaps);
  ok = ok && !gHost.cachedRemoteCapabilitiesV2(&postResetCaps);
  ok = ok && !gHost.lastRemoteFaeTableValid();
  ok = ok && !gHost.lastTestEndCompleteValid();
  ok = ok && !gHost.ready() && !gHost.failed();

  /* ── Report ───────────────────────────────────────────────────── */
  Serial.print(F("cs_vpr_cached_caps="));
  Serial.print(ok ? F("PASS") : F("FAIL"));
  Serial.print(F(" pumps="));
  Serial.print(pumpCount);
  Serial.print(F(" pre_v1="));
  Serial.print(!gHost.cachedRemoteCapabilitiesV1(&preCaps) ? 1 : 0);
  Serial.print(F(" pre_v2="));
  Serial.print(!gHost.cachedRemoteCapabilitiesV2(&preCaps) ? 1 : 0);
  Serial.print(F(" read="));
  Serial.print(readStatus, HEX);
  Serial.print(F(" wr_v1="));
  Serial.print(writeV1Status, HEX);
  Serial.print(F(" wr_v2="));
  Serial.print(writeV2Status, HEX);
  Serial.print(F(" wr_fae="));
  Serial.print(writeFaeStatus, HEX);
  Serial.print(F(" fae_v0="));
  Serial.print(faeTable.values[0]);
  Serial.print(F(" fae_v71="));
  Serial.print(faeTable.values[71]);
  Serial.print(F(" post_v1="));
  Serial.print(!gHost.cachedRemoteCapabilitiesV1(&postResetCaps) ? 1 : 0);
  Serial.print(F(" post_v2="));
  Serial.print(!gHost.cachedRemoteCapabilitiesV2(&postResetCaps) ? 1 : 0);
  Serial.print(F(" post_fae="));
  Serial.print(gHost.lastRemoteFaeTableValid() ? 1 : 0);
  Serial.print(F(" post_te="));
  Serial.print(gHost.lastTestEndCompleteValid() ? 1 : 0);
  Serial.print(F(" ok="));
  Serial.println(ok ? 1 : 0);
  return ok;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500U) {
  }
  Serial.println(F("BleChannelSoundingVprCachedCapabilities"));
  (void)runCachingLifecycleProbe();
}

void loop() {}
