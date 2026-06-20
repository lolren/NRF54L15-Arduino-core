#include <Arduino.h>

#include <string.h>

#include "ble_channel_sounding.h"

using namespace xiao_nrf54l15;

namespace {

uint32_t g_checks = 0U;
uint32_t g_failures = 0U;

void expect(bool condition, const char* label) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    Serial.print("FAIL: ");
    Serial.println(label);
  }
}

void testCsTestCommands() {
  BleCsTestParams params{};
  memset(params.channelMap, 0xAA, sizeof(params.channelMap));

  BleCsHciCommand command{};
  expect(BleChannelSoundingRadio::buildHciTestCommand(params, &command),
         "build default CS Test");
  expect(command.opcode == 0x2095U, "CS Test opcode");
  expect(command.payloadLen == 43U, "default CS Test payload length");
  expect(command.payload[8U] == 0xE2U &&
             command.payload[9U] == 0x04U &&
             command.payload[10U] == 0x00U,
         "24-bit subevent length");
  expect(command.payload[29U] == 13U, "channel-map override length");
  expect(memcmp(command.payload + 30U, params.channelMap,
                sizeof(params.channelMap)) == 0,
         "channel-map override bytes");

  params.overrideConfig = kBleCsTestSupportedOverrideMask;
  params.overrideChannelListLen = kBleCsMaxTestChannelCount;
  for (size_t i = 0U; i < kBleCsMaxTestChannelCount; ++i) {
    params.overrideChannelList[i] = static_cast<uint8_t>(i);
  }
  params.overrideMainModeSteps = 7U;
  params.overrideTpmToneExtension = 3U;
  params.overrideToneAntennaPermutationIndex = 5U;
  params.overrideCsSyncAccessAddressInitiator = 0x11223344UL;
  params.overrideCsSyncAccessAddressReflector = 0x55667788UL;
  params.overrideSsMarkerPositions[0] = 4U;
  params.overrideSsMarkerPositions[1] = 0xFFU;
  params.overrideSsMarkerValue = 2U;
  params.overrideCsSyncPayloadPattern = 0x80U;
  memset(params.overrideCsSyncUserPayload, 0x5A,
         sizeof(params.overrideCsSyncUserPayload));

  expect(BleChannelSoundingRadio::buildHciTestCommand(params, &command),
         "build maximum CS Test");
  expect(command.payloadLen == 99U, "maximum CS Test payload length");
  expect(command.payload[29U] == 69U, "maximum override length");
  expect(command.payload[30U] == kBleCsMaxTestChannelCount,
         "channel-list count");

  params.overrideConfig = 0x0002U;
  expect(!BleChannelSoundingRadio::buildHciTestCommand(params, &command),
         "reject reserved override bit");

  expect(BleChannelSoundingRadio::buildHciTestEndCommand(&command),
         "build CS Test End");
  expect(command.opcode == 0x2096U && command.payloadLen == 0U,
         "CS Test End layout");

  const uint8_t testEndEvent[] = {0x00U};
  BleCsTestEndComplete testEnd{};
  expect(BleChannelSoundingRadio::parseHciTestEndCompleteEvent(
             testEndEvent, sizeof(testEndEvent), &testEnd),
         "parse CS Test End event");
  expect(testEnd.status == 0U, "CS Test End status");
}

void testFaeCommands() {
  int8_t fae[kBleCsFaeTableValueCount] = {};
  for (size_t i = 0U; i < kBleCsFaeTableValueCount; ++i) {
    fae[i] = static_cast<int8_t>(static_cast<int>(i) - 36);
  }

  BleCsHciCommand command{};
  expect(BleChannelSoundingRadio::buildHciWriteCachedRemoteFaeTableCommand(
             0x1234U, fae, &command),
         "build cached FAE");
  expect(command.opcode == 0x208FU && command.payloadLen == 74U,
         "cached FAE command layout");
  expect(command.payload[0U] == 0x34U && command.payload[1U] == 0x12U,
         "cached FAE handle");
  expect(memcmp(command.payload + 2U, fae, sizeof(fae)) == 0,
         "cached FAE values");

  uint8_t event[3U + kBleCsFaeTableValueCount] = {};
  event[1U] = 0x34U;
  event[2U] = 0x12U;
  memcpy(event + 3U, fae, sizeof(fae));
  BleCsFaeTable parsed{};
  expect(BleChannelSoundingRadio::parseHciReadRemoteFaeTableCompleteEvent(
             event, sizeof(event), &parsed),
         "parse remote FAE event");
  expect(parsed.valid && parsed.connHandle == 0x1234U,
         "remote FAE event header");
  expect(memcmp(parsed.values, fae, sizeof(fae)) == 0,
         "remote FAE event values");
}

void testCachedCapabilitiesCommands() {
  BleCsControllerCapabilities caps{};
  caps.numConfigSupported = 4U;
  caps.maxConsecutiveProceduresSupported = 0x1234U;
  caps.numAntennasSupported = 2U;
  caps.maxAntennaPathsSupported = 4U;
  caps.initiatorSupported = true;
  caps.reflectorSupported = true;
  caps.mode3Supported = true;
  caps.rttCapability = 0x07U;
  caps.rttAaOnlyN = 1U;
  caps.rttSoundingN = 2U;
  caps.rttRandomPayloadN = 3U;
  caps.nadmSoundingCapability = 0x1122U;
  caps.nadmRandomCapability = 0x3344U;
  caps.csSync2mPhySupported = true;
  caps.csSync2m2btPhySupported = true;
  caps.csWithoutFaeSupported = true;
  caps.chselAlg3cSupported = true;
  caps.pbrFromRttSoundingSeqSupported = true;
  caps.csIptReflectorSupported = true;
  caps.tIp1TimesSupported = 0x0102U;
  caps.tIp2TimesSupported = 0x0304U;
  caps.tFcsTimesSupported = 0x0506U;
  caps.tPmTimesSupported = 0x0708U;
  caps.tSwTimeSupported = 10U;
  caps.txSnrCapability = 11U;
  caps.tIp2IptTimesSupported = 0x090AU;
  caps.tSwIptTimeSupported = 12U;

  BleCsHciCommand command{};
  expect(BleChannelSoundingRadio::
             buildHciWriteCachedRemoteSupportedCapabilitiesCommand(
                 0x1234U, caps, &command),
         "build cached capabilities v1");
  expect(command.opcode == 0x208BU && command.payloadLen == 30U,
         "cached capabilities v1 layout");
  expect(command.payload[7U] == 0x03U && command.payload[8U] == 0x01U,
         "cached capability role and mode masks");

  expect(BleChannelSoundingRadio::
             buildHciWriteCachedRemoteSupportedCapabilitiesV2Command(
                 0x1234U, caps, &command),
         "build cached capabilities v2");
  expect(command.opcode == 0x20A6U && command.payloadLen == 33U,
         "cached capabilities v2 layout");
  expect(command.payload[18U] == 0x1EU && command.payload[19U] == 0x00U,
         "cached capabilities v2 subfeatures");
  expect(command.payload[30U] == 0x0AU &&
             command.payload[31U] == 0x09U &&
             command.payload[32U] == 12U,
         "cached capabilities v2 IPT fields");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);

  static_assert(kBleCsHciOpSetChannelClassification == 0x2092U,
                "LE CS channel-classification opcode mismatch");
  static_assert(kBleCsHciEvtReadRemoteFaeTableComplete == 0x2DU,
                "LE CS FAE event mismatch");
  static_assert(kBleCsMaxHciCommandPayloadBytes == 128U,
                "LE CS command payload capacity mismatch");

  testCsTestCommands();
  testFaeCommands();
  testCachedCapabilitiesCommands();

  Serial.print("checks=");
  Serial.print(g_checks);
  Serial.print(" failures=");
  Serial.println(g_failures);
}

void loop() {
  delay(1000);
}
