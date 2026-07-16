#!/usr/bin/env python3
"""Validate atomic Zigbee codec capacities and truncated ZDO semantics."""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (
    ROOT
    / "hardware/nrf54l15clean/nrf54l15clean/libraries"
    / "Nrf54L15-Clean-Implementation/src"
)
STACK_CPP = SOURCE / "zigbee_stack.cpp"
STACK_H = SOURCE / "zigbee_stack.h"

CODEC_BUILDERS = {
    "buildMacFrame",
    "buildAssociationRequest",
    "buildAssociationResponse",
    "buildOrphanNotification",
    "buildCoordinatorRealignment",
    "buildBeaconFrame",
    "buildBeaconRequest",
    "buildDataRequest",
    "buildDataRequestShort",
    "buildNwkFrame",
    "buildNwkRejoinRequestCommand",
    "buildNwkRejoinResponseCommand",
    "buildNwkEndDeviceTimeoutRequestCommand",
    "buildNwkEndDeviceTimeoutResponseCommand",
    "buildApsDataFrame",
    "buildApsCommandFrame",
    "buildApsAcknowledgementFrame",
    "buildApsDataAcknowledgement",
    "buildApsTransportKeyCommand",
    "buildApsUpdateDeviceCommand",
    "buildApsSwitchKeyCommand",
    "buildZclFrame",
    "buildReadAttributesRequest",
    "buildWriteAttributesRequest",
    "buildWriteAttributesUndividedRequest",
    "buildDiscoverAttributesRequest",
    "buildDiscoverAttributesExtendedRequest",
    "buildDiscoverCommandsReceivedRequest",
    "buildDiscoverCommandsGeneratedRequest",
    "buildConfigureReportingRequest",
    "buildReadReportingConfigurationRequest",
    "buildReadAttributesResponse",
    "buildWriteAttributesResponse",
    "buildDiscoverAttributesResponse",
    "buildDiscoverAttributesExtendedResponse",
    "buildDiscoverCommandsReceivedResponse",
    "buildDiscoverCommandsGeneratedResponse",
    "buildConfigureReportingResponse",
    "buildReadReportingConfigurationResponse",
    "buildAttributeReport",
    "buildDefaultResponse",
    "buildZdoNetworkAddressRequest",
    "buildZdoIeeeAddressRequest",
    "buildZdoNodeDescriptorRequest",
    "buildZdoPowerDescriptorRequest",
    "buildZdoActiveEndpointsRequest",
    "buildZdoSimpleDescriptorRequest",
    "buildZdoMatchDescriptorRequest",
    "buildZdoBindRequest",
    "buildZdoUnbindRequest",
    "buildZdoMgmtLeaveRequest",
    "buildZdoMgmtPermitJoinRequest",
}

HA_OUTPUT_METHODS = {
    "handleZdoRequest",
    "handleZclRequest",
    "buildAttributeReport",
    "buildDueAttributeReport",
    "buildMgmtLqiResponse",
    "buildMgmtRtgResponse",
    "buildDeviceAnnounce",
}

ARDUINO_STUB = """#pragma once
#include <stdint.h>
unsigned long millis();
"""

HARNESS = r"""
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "zigbee_stack.h"

unsigned long millis() { return 1000UL; }

using namespace xiao_nrf54l15;

namespace {

int g_failures = 0;

#define CHECK(expression)                                                     \
  do {                                                                        \
    if (!(expression)) {                                                      \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,           \
                   #expression);                                              \
      ++g_failures;                                                           \
    }                                                                         \
  } while (false)

template <typename Builder>
void requireAtomicCapacity(const char* name, Builder builder) {
  std::array<uint8_t, 129U> full{};
  full.fill(0xA5U);
  uint8_t fullLength = 0xEEU;
  CHECK(builder(&full[1], 127U, &fullLength));
  CHECK(fullLength > 0U);
  CHECK(full[0] == 0xA5U);
  CHECK(full[128] == 0xA5U);
  if (fullLength == 0U) {
    std::fprintf(stderr, "FAIL %s produced no bytes\n", name);
    ++g_failures;
    return;
  }

  const auto expected = full;

  std::array<uint8_t, 129U> shortOutput{};
  shortOutput.fill(0x5AU);
  const auto shortBefore = shortOutput;
  uint8_t shortLength = 0xEEU;
  CHECK(!builder(&shortOutput[1], static_cast<uint8_t>(fullLength - 1U),
                 &shortLength));
  CHECK(shortLength == 0U);
  CHECK(shortOutput == shortBefore);

  std::array<uint8_t, 129U> zeroOutput{};
  zeroOutput.fill(0x3CU);
  const auto zeroBefore = zeroOutput;
  uint8_t zeroLength = 0xEEU;
  CHECK(!builder(&zeroOutput[1], 0U, &zeroLength));
  CHECK(zeroLength == 0U);
  CHECK(zeroOutput == zeroBefore);

  std::array<uint8_t, 129U> exact{};
  exact.fill(0xC3U);
  uint8_t exactLength = 0xEEU;
  CHECK(builder(&exact[1], fullLength, &exactLength));
  CHECK(exactLength == fullLength);
  CHECK(exact[0] == 0xC3U);
  CHECK(exact[128] == 0xC3U);
  CHECK(std::memcmp(&exact[1], &expected[1], fullLength) == 0);
}

ZigbeeHomeAutomationDevice makeDevice() {
  ZigbeeHomeAutomationDevice device;
  ZigbeeBasicClusterConfig basic{};
  CHECK(device.configureOnOffLight(1U, 0x00124B0001020304ULL, 0x1234U,
                                   0x4567U, basic));
  return device;
}

bool sameNeighbor(const ZigbeeNeighborTableEntry& left,
                  const ZigbeeNeighborTableEntry& right) {
  return left.used == right.used &&
         left.extendedPanId == right.extendedPanId &&
         left.ieeeAddress == right.ieeeAddress &&
         left.networkAddress == right.networkAddress &&
         left.deviceType == right.deviceType &&
         left.deviceTypeUnknown == right.deviceTypeUnknown &&
         left.rxOnWhenIdle == right.rxOnWhenIdle &&
         left.rxOnWhenIdleUnknown == right.rxOnWhenIdleUnknown &&
         left.relationship == right.relationship &&
         left.permitJoin == right.permitJoin && left.depth == right.depth &&
         left.lqi == right.lqi;
}

bool sameRoute(const ZigbeeRoutingTableEntry& left,
               const ZigbeeRoutingTableEntry& right) {
  return left.used == right.used &&
         left.destinationAddress == right.destinationAddress &&
         left.status == right.status &&
         left.memoryConstrained == right.memoryConstrained &&
         left.manyToOne == right.manyToOne &&
         left.routeRecordRequired == right.routeRecordRequired &&
         left.nextHopAddress == right.nextHopAddress;
}

void testFamilyCapacityCanaries() {
  const std::array<uint8_t, 3U> payload = {0x11U, 0x22U, 0x33U};

  ZigbeeMacFrame mac{};
  mac.frameType = ZigbeeMacFrameType::kData;
  mac.sequence = 0x41U;
  requireAtomicCapacity("MAC", [&](uint8_t* out, uint8_t capacity,
                                    uint8_t* length) {
    return ZigbeeCodec::buildMacFrame(mac, payload.data(), payload.size(), out,
                                      capacity, length);
  });

  ZigbeeNetworkFrame nwk{};
  nwk.frameType = ZigbeeNwkFrameType::kData;
  nwk.destinationShort = 0x1234U;
  nwk.sourceShort = 0x5678U;
  nwk.radius = 5U;
  nwk.sequence = 9U;
  requireAtomicCapacity("NWK", [&](uint8_t* out, uint8_t capacity,
                                    uint8_t* length) {
    return ZigbeeCodec::buildNwkFrame(nwk, payload.data(), payload.size(), out,
                                      capacity, length);
  });

  ZigbeeApsDataFrame aps{};
  aps.destinationEndpoint = 1U;
  aps.clusterId = kZigbeeClusterOnOff;
  aps.profileId = kZigbeeProfileHomeAutomation;
  aps.sourceEndpoint = 1U;
  aps.counter = 7U;
  requireAtomicCapacity("APS", [&](uint8_t* out, uint8_t capacity,
                                    uint8_t* length) {
    return ZigbeeCodec::buildApsDataFrame(
        aps, payload.data(), payload.size(), out, capacity, length);
  });

  requireAtomicCapacity("ZCL", [&](uint8_t* out, uint8_t capacity,
                                    uint8_t* length) {
    return ZigbeeCodec::buildDefaultResponse(3U, true, 1U, 0U, out, capacity,
                                             length);
  });

  const std::array<uint16_t, 2U> inputClusters = {kZigbeeClusterBasic,
                                                  kZigbeeClusterOnOff};
  const std::array<uint16_t, 1U> outputClusters = {kZigbeeClusterIdentify};
  requireAtomicCapacity("ZDO", [&](uint8_t* out, uint8_t capacity,
                                    uint8_t* length) {
    return ZigbeeCodec::buildZdoMatchDescriptorRequest(
        1U, 0x1234U, kZigbeeProfileHomeAutomation, inputClusters.data(),
        inputClusters.size(), outputClusters.data(), outputClusters.size(), out,
        capacity, length);
  });

  requireAtomicCapacity("HA announce", [&](uint8_t* out, uint8_t capacity,
                                            uint8_t* length) {
    auto device = makeDevice();
    return device.buildDeviceAnnounce(8U, out, capacity, length);
  });

  requireAtomicCapacity("HA ZDO handler", [&](uint8_t* out, uint8_t capacity,
                                               uint8_t* length) {
    auto device = makeDevice();
    const std::array<uint8_t, 3U> request = {4U, 0x34U, 0x12U};
    uint16_t responseCluster = 0U;
    return device.handleZdoRequest(kZigbeeZdoActiveEndpointsRequest,
                                   request.data(), request.size(),
                                   &responseCluster, out, capacity, length);
  });

  ZigbeeNeighborTableEntry neighbor{};
  neighbor.used = true;
  neighbor.networkAddress = 0x1234U;
  neighbor.ieeeAddress = 0x00124B0001020304ULL;
  neighbor.extendedPanId = 0x0102030405060708ULL;
  neighbor.depth = 0U;
  requireAtomicCapacity("HA management", [&](uint8_t* out, uint8_t capacity,
                                              uint8_t* length) {
    auto device = makeDevice();
    return device.buildMgmtLqiResponse(2U, 0U, &neighbor, 1U, out, capacity,
                                       length);
  });
}

void testArrayInferenceAndRawPointerCompatibility() {
  uint8_t exact[8] = {0xD4U, 0xD4U, 0xD4U, 0xD4U,
                      0xD4U, 0xD4U, 0xD4U, 0xD4U};
  uint8_t length = 0xEEU;
  CHECK(ZigbeeCodec::buildBeaconRequest(1U, exact, &length));
  CHECK(length == sizeof(exact));

  uint8_t shortArray[7] = {0xB7U, 0xB7U, 0xB7U, 0xB7U,
                           0xB7U, 0xB7U, 0xB7U};
  const std::array<uint8_t, 7U> shortBefore = {
      shortArray[0], shortArray[1], shortArray[2], shortArray[3],
      shortArray[4], shortArray[5], shortArray[6]};
  length = 0xEEU;
  CHECK(!ZigbeeCodec::buildBeaconRequest(1U, shortArray, &length));
  CHECK(length == 0U);
  CHECK(std::memcmp(shortArray, shortBefore.data(), shortBefore.size()) == 0);

  uint8_t legacyStorage[127] = {0U};
  std::memset(legacyStorage, 0x91, sizeof(legacyStorage));
  const std::array<uint8_t, 127U> legacyBefore = [] {
    std::array<uint8_t, 127U> value{};
    value.fill(0x91U);
    return value;
  }();
  uint8_t* legacyPointer = legacyStorage;
  length = 0xEEU;
  CHECK(!ZigbeeCodec::buildBeaconRequest(1U, legacyPointer, &length));
  CHECK(length == 0U);
  CHECK(std::memcmp(legacyStorage, legacyBefore.data(), legacyBefore.size()) ==
        0);
}

void testZdoResponseForms() {
  std::array<uint8_t, 17U> node = {
      1U, 0U, 0x34U, 0x12U, 1U, 0x40U, 0x8EU, 0x34U, 0x12U,
      82U, 82U, 0U, 0U, 0U, 82U, 0U, 0U};
  for (uint8_t length = 4U; length < node.size(); ++length) {
    ZigbeeZdoNodeDescriptorResponseView parsed{};
    CHECK(!ZigbeeCodec::parseZdoNodeDescriptorResponse(node.data(), length,
                                                        &parsed));
    CHECK(!parsed.valid);
  }
  ZigbeeZdoNodeDescriptorResponseView nodeParsed{};
  CHECK(ZigbeeCodec::parseZdoNodeDescriptorResponse(
      node.data(), node.size(), &nodeParsed));
  CHECK(nodeParsed.valid);
  CHECK(nodeParsed.frequencyBand == 0x08U);
  CHECK(nodeParsed.maxOutgoingTransferSize == 82U);

  node[4] = 3U;
  nodeParsed.valid = true;
  nodeParsed.transactionSequence = 0xA5U;
  CHECK(!ZigbeeCodec::parseZdoNodeDescriptorResponse(
      node.data(), node.size(), &nodeParsed));
  CHECK(!nodeParsed.valid && nodeParsed.transactionSequence == 0U &&
        nodeParsed.logicalType == 0U);
  node[4] = 0x20U;
  CHECK(!ZigbeeCodec::parseZdoNodeDescriptorResponse(
      node.data(), node.size(), &nodeParsed));
  CHECK(!nodeParsed.valid);
  node[4] = 1U;
  node[5] = 0x41U;
  CHECK(!ZigbeeCodec::parseZdoNodeDescriptorResponse(
      node.data(), node.size(), &nodeParsed));
  node[5] = 0x10U;
  CHECK(!ZigbeeCodec::parseZdoNodeDescriptorResponse(
      node.data(), node.size(), &nodeParsed));
  node[5] = 0U;
  CHECK(!ZigbeeCodec::parseZdoNodeDescriptorResponse(
      node.data(), node.size(), &nodeParsed));
  node[5] = 0x80U;
  CHECK(ZigbeeCodec::parseZdoNodeDescriptorResponse(
      node.data(), node.size(), &nodeParsed));
  CHECK(nodeParsed.frequencyBand == 0x10U);
  node[5] = 0x40U;
  node[6] = 0x9EU;
  CHECK(!ZigbeeCodec::parseZdoNodeDescriptorResponse(
      node.data(), node.size(), &nodeParsed));
  node[6] = 0x8EU;
  node[16] = 0x04U;
  CHECK(!ZigbeeCodec::parseZdoNodeDescriptorResponse(
      node.data(), node.size(), &nodeParsed));
  node[16] = 0U;
  node[9] = 0x80U;
  CHECK(!ZigbeeCodec::parseZdoNodeDescriptorResponse(
      node.data(), node.size(), &nodeParsed));
  node[9] = 82U;
  node[11] = 0x80U;
  CHECK(!ZigbeeCodec::parseZdoNodeDescriptorResponse(
      node.data(), node.size(), &nodeParsed));
  node[11] = 0U;
  node[13] = 0x01U;
  CHECK(!ZigbeeCodec::parseZdoNodeDescriptorResponse(
      node.data(), node.size(), &nodeParsed));
  node[13] = 0U;
  node[15] = 0x80U;
  CHECK(!ZigbeeCodec::parseZdoNodeDescriptorResponse(
      node.data(), node.size(), &nodeParsed));
  node[15] = 0U;

  auto nodeDevice = makeDevice();
  const std::array<uint8_t, 3U> nodeRequest = {9U, 0x34U, 0x12U};
  std::array<uint8_t, 127U> nodeResponse{};
  uint16_t nodeResponseCluster = 0U;
  uint8_t nodeResponseLength = 0U;
  CHECK(nodeDevice.handleZdoRequest(
      kZigbeeZdoNodeDescriptorRequest, nodeRequest.data(), nodeRequest.size(),
      &nodeResponseCluster, nodeResponse.data(), nodeResponse.size(),
      &nodeResponseLength));
  CHECK(nodeResponseCluster == kZigbeeZdoNodeDescriptorResponse &&
        nodeResponseLength == 17U && nodeResponse[5] == 0x40U &&
        nodeResponse[6] == 0x84U);
  CHECK(ZigbeeCodec::parseZdoNodeDescriptorResponse(
      nodeResponse.data(), nodeResponseLength, &nodeParsed));
  CHECK(nodeParsed.frequencyBand == 0x08U);

  ZigbeeHomeAutomationDevice router;
  ZigbeeHomeAutomationDevice coordinator;
  ZigbeeHomeAutomationDevice batteryEndDevice;
  ZigbeeBasicClusterConfig roleBasic{};
  CHECK(router.configureOnOffLight(
      1U, 1U, 1U, 1U, roleBasic, 0U, ZigbeeLogicalType::kRouter));
  CHECK(coordinator.configureOnOffLight(
      1U, 1U, 1U, 1U, roleBasic, 0U, ZigbeeLogicalType::kCoordinator));
  CHECK(batteryEndDevice.configureTemperatureSensor(
      1U, 1U, 1U, 1U, roleBasic, 0U, ZigbeeLogicalType::kEndDevice));
  CHECK(router.macCapabilityFlags() == 0x8EU);
  CHECK(coordinator.macCapabilityFlags() == 0x8FU);
  CHECK(batteryEndDevice.macCapabilityFlags() == 0x80U);

  const std::array<uint8_t, 4U> nodeFailure = {2U, 0x81U, 0x34U, 0x12U};
  nodeParsed = ZigbeeZdoNodeDescriptorResponseView{};
  CHECK(ZigbeeCodec::parseZdoNodeDescriptorResponse(
      nodeFailure.data(), nodeFailure.size(), &nodeParsed));
  CHECK(nodeParsed.valid && nodeParsed.status == 0x81U);

  const std::array<uint8_t, 4U> activeFailure = {3U, 0x81U, 0x34U, 0x12U};
  ZigbeeZdoActiveEndpointsResponseView active{};
  CHECK(ZigbeeCodec::parseZdoActiveEndpointsResponse(
      activeFailure.data(), activeFailure.size(), &active));
  CHECK(active.valid && active.endpointCount == 0U);

  const std::array<uint8_t, 4U> activeTruncated = {3U, 0U, 0x34U, 0x12U};
  active = ZigbeeZdoActiveEndpointsResponseView{};
  CHECK(!ZigbeeCodec::parseZdoActiveEndpointsResponse(
      activeTruncated.data(), activeTruncated.size(), &active));
  CHECK(!active.valid);

  const std::array<uint8_t, 5U> simpleFailure = {
      4U, 0x82U, 0x34U, 0x12U, 0U};
  ZigbeeZdoSimpleDescriptorResponseView simple{};
  CHECK(ZigbeeCodec::parseZdoSimpleDescriptorResponse(
      simpleFailure.data(), simpleFailure.size(), &simple));
  CHECK(simple.valid && simple.status == 0x82U);

  const std::array<uint8_t, 5U> simpleTruncated = {4U, 0U, 0x34U, 0x12U, 8U};
  simple = ZigbeeZdoSimpleDescriptorResponseView{};
  CHECK(!ZigbeeCodec::parseZdoSimpleDescriptorResponse(
      simpleTruncated.data(), simpleTruncated.size(), &simple));
  CHECK(!simple.valid);
}

void testSecuredMacRejected() {
  const std::array<uint8_t, 3U> securedData = {0x09U, 0U, 1U};
  ZigbeeMacFrame parsed{};
  CHECK(!ZigbeeCodec::parseMacFrame(securedData.data(), securedData.size(),
                                    &parsed));
  CHECK(parsed.securityEnabled);
  CHECK(!parsed.valid);
}

void testHaStateAtomicOnCapacityFailure() {
  auto device = makeDevice();
  CHECK(!device.onOff());
  const std::array<uint8_t, 3U> onRequest = {
      static_cast<uint8_t>(ZigbeeZclFrameType::kClusterSpecific), 1U, 1U};

  std::array<uint8_t, 127U> expected{};
  uint8_t expectedLength = 0U;
  auto probe = device;
  CHECK(probe.handleZclRequest(kZigbeeClusterOnOff, onRequest.data(),
                               onRequest.size(), expected.data(),
                               static_cast<uint8_t>(expected.size()),
                               &expectedLength));
  CHECK(expectedLength > 0U);

  std::array<uint8_t, 127U> rejected{};
  rejected.fill(0x6DU);
  const auto rejectedBefore = rejected;
  uint8_t rejectedLength = 0xEEU;
  CHECK(!device.handleZclRequest(
      kZigbeeClusterOnOff, onRequest.data(), onRequest.size(), rejected.data(),
      static_cast<uint8_t>(expectedLength - 1U), &rejectedLength));
  CHECK(rejectedLength == 0U);
  CHECK(rejected == rejectedBefore);
  CHECK(!device.onOff());

  CHECK(device.handleZclRequest(kZigbeeClusterOnOff, onRequest.data(),
                                onRequest.size(), rejected.data(),
                                expectedLength, &rejectedLength));
  CHECK(device.onOff());
}

void testEnumValidationAndStateAtomicity() {
  auto device = makeDevice();
  ZigbeeBasicClusterConfig replacement{};
  replacement.applicationVersion = 0x77U;
  const uint64_t originalIeee = device.config().ieeeAddress;
  const uint16_t originalNwk = device.config().nwkAddress;
  const uint16_t originalPan = device.config().panId;
  const uint8_t originalEndpoint =
      device.config().endpointDescriptors[0].endpoint;
  const ZigbeeLogicalType originalType = device.config().logicalType;
  std::array<uint8_t, sizeof(ZigbeeHomeAutomationConfig)> configSnapshot{};
  std::memcpy(configSnapshot.data(), &device.config(), configSnapshot.size());
  const auto invalidLogicalType = static_cast<ZigbeeLogicalType>(0xFFU);
  const auto configurationUnchanged = [&]() {
    return device.config().ieeeAddress == originalIeee &&
           device.config().nwkAddress == originalNwk &&
           device.config().panId == originalPan &&
           device.config().endpointDescriptors[0].endpoint == originalEndpoint &&
           device.config().logicalType == originalType &&
           device.config().basic.applicationVersion != 0x77U &&
           std::memcmp(configSnapshot.data(), &device.config(),
                       configSnapshot.size()) == 0;
  };

  CHECK(!device.configureOnOffLight(2U, 1U, 2U, 3U, replacement, 4U,
                                    invalidLogicalType));
  CHECK(configurationUnchanged());
  CHECK(!device.configureOnOffLightSwitch(2U, 1U, 2U, 3U, replacement, 4U,
                                          invalidLogicalType));
  CHECK(configurationUnchanged());
  CHECK(!device.configureDimmableLight(2U, 1U, 2U, 3U, replacement, 4U,
                                       invalidLogicalType));
  CHECK(configurationUnchanged());
  CHECK(!device.configureColorDimmableLight(2U, 1U, 2U, 3U, replacement, 4U,
                                            invalidLogicalType));
  CHECK(configurationUnchanged());
  CHECK(!device.configureExtendedColorLight(2U, 1U, 2U, 3U, replacement, 4U,
                                             invalidLogicalType));
  CHECK(configurationUnchanged());
  CHECK(!device.configureTemperatureSensor(2U, 1U, 2U, 3U, replacement, 4U,
                                            invalidLogicalType));
  CHECK(configurationUnchanged());
  CHECK(!device.configureTemperatureHumiditySensor(
      2U, 1U, 2U, 3U, replacement, 4U, invalidLogicalType));
  CHECK(configurationUnchanged());

  ZigbeeNeighborTableEntry neighbor{};
  neighbor.extendedPanId = 0x0102030405060708ULL;
  neighbor.ieeeAddress = 0x00124B0001020304ULL;
  neighbor.networkAddress = 0x2244U;
  neighbor.deviceType = ZigbeeLogicalType::kRouter;
  neighbor.rxOnWhenIdle = true;
  neighbor.relationship = ZigbeeNeighborRelationship::kSibling;
  neighbor.permitJoin = ZigbeePermitJoinState::kAccepting;
  neighbor.depth = 3U;
  neighbor.lqi = 200U;
  CHECK(device.setNeighborTableEntry(0U, neighbor));
  const ZigbeeNeighborTableEntry acceptedNeighbor =
      device.neighborTableEntries()[0];
  std::array<uint8_t, 127U> managementWire{};
  uint8_t managementWireLength = 0U;
  CHECK(device.buildMgmtLqiResponse(
      1U, 0U, &acceptedNeighbor, 1U, managementWire.data(),
      managementWire.size(), &managementWireLength));
  CHECK(managementWireLength == 27U && managementWire[23] == 0x25U &&
        managementWire[24] == 0x01U);
  ZigbeeNeighborTableEntry unknownNeighbor = acceptedNeighbor;
  unknownNeighbor.deviceTypeUnknown = true;
  unknownNeighbor.rxOnWhenIdleUnknown = true;
  CHECK(device.buildMgmtLqiResponse(
      1U, 0U, &unknownNeighbor, 1U, managementWire.data(),
      managementWire.size(), &managementWireLength));
  CHECK(managementWireLength == 27U && managementWire[23] == 0x2BU);

  ZigbeeNeighborTableEntry invalidNeighbor = acceptedNeighbor;
  invalidNeighbor.deviceType = static_cast<ZigbeeLogicalType>(0xFFU);
  CHECK(!device.setNeighborTableEntry(0U, invalidNeighbor));
  CHECK(sameNeighbor(device.neighborTableEntries()[0], acceptedNeighbor));
  invalidNeighbor = acceptedNeighbor;
  invalidNeighbor.relationship =
      static_cast<ZigbeeNeighborRelationship>(0xFFU);
  CHECK(!device.setNeighborTableEntry(0U, invalidNeighbor));
  CHECK(sameNeighbor(device.neighborTableEntries()[0], acceptedNeighbor));
  invalidNeighbor = acceptedNeighbor;
  invalidNeighbor.permitJoin = static_cast<ZigbeePermitJoinState>(0xFFU);
  CHECK(!device.setNeighborTableEntry(0U, invalidNeighbor));
  CHECK(sameNeighbor(device.neighborTableEntries()[0], acceptedNeighbor));
  invalidNeighbor = acceptedNeighbor;
  invalidNeighbor.depth = static_cast<uint8_t>(kZigbeeNwkMaximumDepth + 1U);
  CHECK(!device.setNeighborTableEntry(0U, invalidNeighbor));
  CHECK(sameNeighbor(device.neighborTableEntries()[0], acceptedNeighbor));

  std::array<uint8_t, 129U> managementOutput{};
  managementOutput.fill(0x6BU);
  const auto managementBefore = managementOutput;
  uint8_t managementLength = 0xA5U;
  CHECK(!device.buildMgmtLqiResponse(
      1U, 0U, &invalidNeighbor, 1U, &managementOutput[1], 127U,
      &managementLength));
  CHECK(managementLength == 0U && managementOutput == managementBefore);

  ZigbeeRoutingTableEntry route{};
  route.destinationAddress = 0x3344U;
  route.status = ZigbeeRouteStatus::kActive;
  route.memoryConstrained = true;
  route.manyToOne = true;
  route.routeRecordRequired = true;
  route.nextHopAddress = 0x4455U;
  CHECK(device.setRoutingTableEntry(0U, route));
  const ZigbeeRoutingTableEntry acceptedRoute = device.routingTableEntries()[0];
  CHECK(device.buildMgmtRtgResponse(
      1U, 0U, &acceptedRoute, 1U, managementWire.data(),
      managementWire.size(), &managementWireLength));
  CHECK(managementWireLength == 10U && managementWire[7] == 0x38U);
  ZigbeeRoutingTableEntry invalidRoute = acceptedRoute;
  invalidRoute.status = static_cast<ZigbeeRouteStatus>(0xFFU);
  CHECK(!device.setRoutingTableEntry(0U, invalidRoute));
  CHECK(sameRoute(device.routingTableEntries()[0], acceptedRoute));
  managementOutput.fill(0x7CU);
  const auto routeManagementBefore = managementOutput;
  managementLength = 0xA5U;
  CHECK(!device.buildMgmtRtgResponse(
      1U, 0U, &invalidRoute, 1U, &managementOutput[1], 127U,
      &managementLength));
  CHECK(managementLength == 0U &&
        managementOutput == routeManagementBefore);
}

}  // namespace

int main() {
  testFamilyCapacityCanaries();
  testArrayInferenceAndRawPointerCompatibility();
  testZdoResponseForms();
  testSecuredMacRejected();
  testHaStateAtomicOnCapacityFailure();
  testEnumValidationAndStateAtomicity();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d Zigbee capacity checks failed\n", g_failures);
    return 1;
  }
  std::puts("PASS atomic capacity canaries across MAC/NWK/APS/ZCL/ZDO/HA");
  std::puts("PASS ZDO failure/success forms and secured-MAC rejection");
  std::puts("PASS HA state and output commit atomically");
  std::puts("PASS HA and management-table enums reject without state changes");
  return 0;
}
"""


def validate_source_contract() -> None:
    source = STACK_CPP.read_text(encoding="utf-8")
    header = STACK_H.read_text(encoding="utf-8")

    unchecked = set(
        re.findall(r"^bool (build[A-Za-z0-9_]+)Unchecked\(", source, re.MULTILINE)
    )
    wrapped = set(
        re.findall(
            r"NRF54_ZIGBEE_DEFINE_OUTPUT_BUILDER\(\s*(build[A-Za-z0-9_]+)",
            source,
        )
    )
    wrapped.add("buildWriteAttributesRequest")
    if unchecked != CODEC_BUILDERS or wrapped != CODEC_BUILDERS:
        raise AssertionError(
            "codec capacity migration mismatch:\n"
            f"unchecked missing={sorted(CODEC_BUILDERS - unchecked)}\n"
            f"unchecked extra={sorted(unchecked - CODEC_BUILDERS)}\n"
            f"wrapped missing={sorted(CODEC_BUILDERS - wrapped)}\n"
            f"wrapped extra={sorted(wrapped - CODEC_BUILDERS)}"
        )

    for name in HA_OUTPUT_METHODS:
        if f"ZigbeeHomeAutomationDevice::{name}(" not in source:
            raise AssertionError(f"missing safe HA output definition: {name}")
        if f"ZigbeeHomeAutomationDevice::{name}Unchecked(" not in source:
            raise AssertionError(f"missing staged HA implementation: {name}")

    required_header_tokens = (
        "kZigbeeCodecMaximumOutputLength = 127U",
        "uint8_t outCapacity",
        "ZigbeeOutputCapacity<uint8_t[N]>",
        'deprecated("pass an explicit Zigbee output capacity")',
    )
    for token in required_header_tokens:
        if token not in header:
            raise AssertionError(f"missing output API contract: {token}")


def main() -> None:
    validate_source_contract()
    compiler = os.environ.get("CXX", "g++")
    if shutil.which(compiler) is None:
        raise SystemExit(f"C++ compiler not found: {compiler}")

    with tempfile.TemporaryDirectory(prefix="nrf54-zigbee-capacity-") as temp:
        temporary = Path(temp)
        (temporary / "Arduino.h").write_text(ARDUINO_STUB, encoding="utf-8")
        harness = temporary / "zigbee_codec_capacity_test.cpp"
        harness.write_text(HARNESS, encoding="utf-8")
        binary = temporary / "zigbee_codec_capacity_test"
        command = [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-no-pie",
            "-DNRF54L15_CLEAN_ZIGBEE_ENABLED=1",
            f"-I{temporary}",
            f"-I{SOURCE}",
            str(STACK_CPP),
            str(harness),
            "-o",
            str(binary),
        ]
        subprocess.run(command, cwd=ROOT, check=True)
        environment = os.environ.copy()
        environment["ASAN_OPTIONS"] = "abort_on_error=1:detect_leaks=0"
        environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        subprocess.run([str(binary)], cwd=ROOT, env=environment, check=True)


if __name__ == "__main__":
    main()
