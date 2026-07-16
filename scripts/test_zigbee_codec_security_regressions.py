#!/usr/bin/env python3
"""Exercise Zigbee reporting and CCM* boundary handling under sanitizers."""

from __future__ import annotations

import os
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


ARDUINO_STUB = r"""#pragma once
#include <stdint.h>
unsigned long millis();
"""


HARNESS = r"""
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "zigbee_security.h"
#include "zigbee_stack.h"

unsigned long millis() { return 0UL; }

using namespace xiao_nrf54l15;

static_assert(kZigbeeSecurityControlApsEncMic32 == 0x20U,
              "APS data security control changed on wire");
static_assert(kZigbeeSecurityControlApsKeyTransport == 0x30U,
              "APS transport-key security control changed on wire");
static_assert(kZigbeeSecurityControlNwkEncMic32 == 0x28U,
              "NWK security control changed on wire");

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

std::vector<uint8_t> reportingRecord(ZigbeeZclDataType type,
                                     std::initializer_list<uint8_t> change) {
  std::vector<uint8_t> record = {
      0x00U,  // Direction: reported attribute.
      0x34U, 0x12U,
      static_cast<uint8_t>(type),
      0x02U, 0x00U,
      0x3CU, 0x00U,
  };
  record.insert(record.end(), change.begin(), change.end());
  return record;
}

void requireAllTruncationsRejected(const std::vector<uint8_t>& record) {
  CHECK(record.size() >= 8U);
  for (size_t length = 1U; length < record.size(); ++length) {
    std::unique_ptr<uint8_t[]> exact(new uint8_t[length]);
    std::memcpy(exact.get(), record.data(), length);

    struct GuardedConfigurations {
      uint32_t before;
      ZigbeeReportingConfiguration values[1];
      uint32_t after;
    } guarded{0x13579BDFUL, {}, 0x2468ACE0UL};
    uint8_t count = 0xA5U;
    const bool parsed = ZigbeeCodec::parseConfigureReportingRequest(
        exact.get(), static_cast<uint8_t>(length), guarded.values, 1U, &count);
    CHECK(!parsed);
    CHECK(count == 0U);
    CHECK(guarded.before == 0x13579BDFUL);
    CHECK(guarded.after == 0x2468ACE0UL);
  }
}

void requireCompleteRecordAccepted(const std::vector<uint8_t>& record,
                                   ZigbeeZclDataType expectedType,
                                   uint32_t expectedChange) {
  struct GuardedConfigurations {
    uint32_t before;
    ZigbeeReportingConfiguration values[1];
    uint32_t after;
  } guarded{0x13579BDFUL, {}, 0x2468ACE0UL};
  uint8_t count = 0U;
  CHECK(ZigbeeCodec::parseConfigureReportingRequest(
      record.data(), static_cast<uint8_t>(record.size()), guarded.values, 1U,
      &count));
  CHECK(count == 1U);
  CHECK(guarded.values[0].used);
  CHECK(guarded.values[0].attributeId == 0x1234U);
  CHECK(guarded.values[0].dataType == expectedType);
  CHECK(guarded.values[0].minimumIntervalSeconds == 2U);
  CHECK(guarded.values[0].maximumIntervalSeconds == 60U);
  CHECK(guarded.values[0].reportableChange == expectedChange);
  CHECK(guarded.before == 0x13579BDFUL);
  CHECK(guarded.after == 0x2468ACE0UL);
}

void testConfigureReportingTruncations() {
  const auto unsigned8 = reportingRecord(ZigbeeZclDataType::kUint8, {0x7FU});
  const auto unsigned16 =
      reportingRecord(ZigbeeZclDataType::kUint16, {0x78U, 0x56U});
  const auto unsigned32 = reportingRecord(
      ZigbeeZclDataType::kUint32, {0x78U, 0x56U, 0x34U, 0x12U});
  const auto signed16 =
      reportingRecord(ZigbeeZclDataType::kInt16, {0x01U, 0x80U});

  requireAllTruncationsRejected(unsigned8);
  requireAllTruncationsRejected(unsigned16);
  requireAllTruncationsRejected(unsigned32);
  requireAllTruncationsRejected(signed16);
  requireCompleteRecordAccepted(unsigned8, ZigbeeZclDataType::kUint8, 0x7FU);
  requireCompleteRecordAccepted(unsigned16, ZigbeeZclDataType::kUint16,
                                0x5678U);
  requireCompleteRecordAccepted(unsigned32, ZigbeeZclDataType::kUint32,
                                0x12345678UL);
  requireCompleteRecordAccepted(signed16, ZigbeeZclDataType::kInt16, 0x8001U);

  // These exact six- and seven-byte allocations exercised the former OOB read.
  for (uint8_t length : {6U, 7U}) {
    std::unique_ptr<uint8_t[]> exact(new uint8_t[length]);
    std::memcpy(exact.get(), unsigned16.data(), length);
    ZigbeeReportingConfiguration value{};
    uint8_t count = 0xFFU;
    CHECK(!ZigbeeCodec::parseConfigureReportingRequest(
        exact.get(), length, &value, 1U, &count));
    CHECK(count == 0U);
  }

  // Direction 1 has a five-byte wire record, but this direction-0-only API
  // rejects it deliberately and without reading beyond any truncated prefix.
  const std::array<uint8_t, 5U> receivedAttribute = {
      0x01U, 0x34U, 0x12U, 0x3CU, 0x00U};
  for (size_t length = 1U; length <= receivedAttribute.size(); ++length) {
    std::unique_ptr<uint8_t[]> exact(new uint8_t[length]);
    std::memcpy(exact.get(), receivedAttribute.data(), length);
    ZigbeeReportingConfiguration value{};
    uint8_t count = 0xFFU;
    CHECK(!ZigbeeCodec::parseConfigureReportingRequest(
        exact.get(), static_cast<uint8_t>(length), &value, 1U, &count));
    CHECK(count == 0U);
  }
}

void testDiscreteReportingTypes() {
  const std::array<ZigbeeZclDataType, 4U> discreteTypes = {
      ZigbeeZclDataType::kBoolean,
      ZigbeeZclDataType::kBitmap8,
      ZigbeeZclDataType::kBitmap16,
      ZigbeeZclDataType::kBitmap32,
  };

  for (const ZigbeeZclDataType type : discreteTypes) {
    const auto record = reportingRecord(type, {});
    requireAllTruncationsRejected(record);
    requireCompleteRecordAccepted(record, type, 0U);

    ZigbeeReportingConfiguration configuration{};
    configuration.used = true;
    configuration.attributeId = 0x1234U;
    configuration.dataType = type;
    configuration.minimumIntervalSeconds = 2U;
    configuration.maximumIntervalSeconds = 60U;
    configuration.reportableChange = 0xA5A5A5A5UL;

    std::array<uint8_t, 127U> encoded{};
    uint8_t encodedLength = 0U;
    CHECK(ZigbeeCodec::buildConfigureReportingRequest(
        &configuration, 1U, 0x5AU, encoded.data(),
        static_cast<uint8_t>(encoded.size()), &encodedLength));
    ZigbeeZclFrame frame{};
    CHECK(ZigbeeCodec::parseZclFrame(encoded.data(), encodedLength, &frame));
    CHECK(frame.valid);
    CHECK(frame.payloadLength == 8U);
    requireCompleteRecordAccepted(
        std::vector<uint8_t>(frame.payload, frame.payload + frame.payloadLength),
        type, 0U);
  }
}

void fillSequence(uint8_t* data, size_t length, uint8_t seed) {
  for (size_t i = 0U; i < length; ++i) {
    data[i] = static_cast<uint8_t>(seed + static_cast<uint8_t>(i));
  }
}

void testCcmKnownAnswerVector() {
  // NIST CAVS 11.0 AES-CCM VTT128.rsp: Tlen=4, Nlen=13, Alen=32,
  // Plen=24, Count=0. This exercises Zigbee's exact CCM* parameter sizes.
  const std::array<uint8_t, 16U> key = {
      0x43U, 0xB1U, 0xA6U, 0xBCU, 0x8DU, 0x0DU, 0x22U, 0xD6U,
      0xD1U, 0xCAU, 0x95U, 0xC1U, 0x85U, 0x93U, 0xCCU, 0xA5U};
  const std::array<uint8_t, 13U> nonce = {
      0x98U, 0x82U, 0x57U, 0x8EU, 0x75U, 0x0BU, 0x96U,
      0x82U, 0xC6U, 0xCAU, 0x7FU, 0x8FU, 0x86U};
  const std::array<uint8_t, 32U> aad = {
      0x20U, 0x84U, 0xF3U, 0x86U, 0x1CU, 0x9AU, 0xD0U, 0xCCU,
      0xEEU, 0x7CU, 0x63U, 0xA7U, 0xE0U, 0x5AU, 0xECU, 0xE5U,
      0xDBU, 0x8BU, 0x34U, 0xBDU, 0x87U, 0x24U, 0xCCU, 0x06U,
      0xB4U, 0xCAU, 0x99U, 0xA7U, 0xF9U, 0xC4U, 0x91U, 0x4FU};
  const std::array<uint8_t, 24U> plaintext = {
      0xA2U, 0xB3U, 0x81U, 0xC7U, 0xD1U, 0x54U, 0x5CU, 0x40U,
      0x8FU, 0xE2U, 0x98U, 0x17U, 0xA2U, 0x1DU, 0xC4U, 0x35U,
      0xA1U, 0x54U, 0xC8U, 0x72U, 0x56U, 0x34U, 0x6BU, 0x05U};
  const std::array<uint8_t, 28U> expected = {
      0xCCU, 0x69U, 0xEDU, 0x76U, 0x98U, 0x5EU, 0x0EU, 0xD4U,
      0xC8U, 0x36U, 0x5AU, 0x72U, 0x77U, 0x5EU, 0x5AU, 0x19U,
      0xBFU, 0xCCU, 0xC7U, 0x1AU, 0xEBU, 0x11U, 0x6CU, 0x85U,
      0xA8U, 0xC7U, 0x46U, 0x77U};

  std::array<uint8_t, 28U> encrypted{};
  uint8_t encryptedLength = 0U;
  CHECK(ZigbeeSecurity::encryptCcmStar(
      key.data(), nonce.data(), aad.data(), aad.size(), plaintext.data(),
      plaintext.size(), encrypted.data(), encrypted.size(), &encryptedLength));
  CHECK(encryptedLength == expected.size());
  CHECK(encrypted == expected);

  std::array<uint8_t, 24U> decrypted{};
  uint8_t decryptedLength = 0U;
  CHECK(ZigbeeSecurity::decryptCcmStar(
      key.data(), nonce.data(), aad.data(), aad.size(), expected.data(),
      expected.size(), decrypted.data(), decrypted.size(), &decryptedLength));
  CHECK(decryptedLength == plaintext.size());
  CHECK(decrypted == plaintext);

  for (const size_t corruptIndex : {size_t{0U}, expected.size() - 1U}) {
    auto corrupted = expected;
    corrupted[corruptIndex] ^= 0x01U;
    std::array<uint8_t, 26U> rejected{};
    rejected.fill(0xD6U);
    const auto rejectedBefore = rejected;
    decryptedLength = 0xA5U;
    CHECK(!ZigbeeSecurity::decryptCcmStar(
        key.data(), nonce.data(), aad.data(), aad.size(), corrupted.data(),
        corrupted.size(), &rejected[1], plaintext.size(), &decryptedLength));
    CHECK(decryptedLength == 0U);
    CHECK(rejected == rejectedBefore);
  }
}

void testCcmCapacityBoundaries() {
  std::array<uint8_t, 16U> key{};
  std::array<uint8_t, 13U> nonce{};
  std::array<uint8_t, 255U> aad{};
  std::array<uint8_t, 252U> plaintext{};
  fillSequence(key.data(), key.size(), 0x10U);
  fillSequence(nonce.data(), nonce.size(), 0x40U);
  fillSequence(aad.data(), aad.size(), 0x70U);
  fillSequence(plaintext.data(), plaintext.size(), 0xA0U);

  std::array<uint8_t, 257U> encrypted{};
  encrypted.fill(0xCCU);
  uint8_t encryptedLength = 0xA5U;
  CHECK(ZigbeeSecurity::encryptCcmStar(
      key.data(), nonce.data(), aad.data(), static_cast<uint8_t>(aad.size()),
      plaintext.data(), 251U, &encrypted[1], 255U, &encryptedLength));
  CHECK(encryptedLength == 255U);
  CHECK(encrypted.front() == 0xCCU);
  CHECK(encrypted.back() == 0xCCU);

  std::array<uint8_t, 253U> decrypted{};
  decrypted.fill(0xDDU);
  uint8_t decryptedLength = 0xA5U;
  CHECK(ZigbeeSecurity::decryptCcmStar(
      key.data(), nonce.data(), aad.data(), static_cast<uint8_t>(aad.size()),
      &encrypted[1], encryptedLength, &decrypted[1], 251U, &decryptedLength));
  CHECK(decryptedLength == 251U);
  CHECK(std::memcmp(&decrypted[1], plaintext.data(), 251U) == 0);
  CHECK(decrypted.front() == 0xDDU);
  CHECK(decrypted.back() == 0xDDU);

  encrypted.fill(0xE1U);
  encryptedLength = 0xA5U;
  CHECK(!ZigbeeSecurity::encryptCcmStar(
      key.data(), nonce.data(), nullptr, 0U, plaintext.data(), 251U,
      &encrypted[1], 254U, &encryptedLength));
  CHECK(encryptedLength == 0U);
  for (uint8_t byte : encrypted) {
    CHECK(byte == 0xE1U);
  }

  encrypted.fill(0xE2U);
  encryptedLength = 0xA5U;
  CHECK(!ZigbeeSecurity::encryptCcmStar(
      key.data(), nonce.data(), nullptr, 0U, plaintext.data(), 252U,
      &encrypted[1], 255U, &encryptedLength));
  CHECK(encryptedLength == 0U);
  for (uint8_t byte : encrypted) {
    CHECK(byte == 0xE2U);
  }

  // The legacy signature remains source-compatible, but now rejects a result
  // whose 256-byte length cannot be represented by its uint8_t length output.
  encrypted.fill(0xE3U);
  encryptedLength = 0xA5U;
  CHECK(!ZigbeeSecurity::encryptCcmStar(
      key.data(), nonce.data(), nullptr, 0U, plaintext.data(), 252U,
      &encrypted[1], &encryptedLength));
  CHECK(encryptedLength == 0U);
  for (uint8_t byte : encrypted) {
    CHECK(byte == 0xE3U);
  }

  std::array<uint8_t, 6U> micOnly{};
  micOnly.fill(0xB4U);
  encryptedLength = 0U;
  CHECK(ZigbeeSecurity::encryptCcmStar(
      key.data(), nonce.data(), aad.data(), static_cast<uint8_t>(aad.size()),
      nullptr, 0U, &micOnly[1], 4U, &encryptedLength));
  CHECK(encryptedLength == 4U);
  CHECK(micOnly.front() == 0xB4U);
  CHECK(micOnly.back() == 0xB4U);

  uint8_t emptyOutput = 0x6DU;
  decryptedLength = 0xA5U;
  CHECK(ZigbeeSecurity::decryptCcmStar(
      key.data(), nonce.data(), aad.data(), static_cast<uint8_t>(aad.size()),
      &micOnly[1], 4U, &emptyOutput, 0U, &decryptedLength));
  CHECK(decryptedLength == 0U);
  CHECK(emptyOutput == 0x6DU);

  decrypted.fill(0xF4U);
  decryptedLength = 0xA5U;
  encrypted.fill(0x00U);
  CHECK(ZigbeeSecurity::encryptCcmStar(
      key.data(), nonce.data(), nullptr, 0U, plaintext.data(), 251U,
      encrypted.data(), 255U, &encryptedLength));
  CHECK(!ZigbeeSecurity::decryptCcmStar(
      key.data(), nonce.data(), nullptr, 0U, encrypted.data(), encryptedLength,
      &decrypted[1], 250U, &decryptedLength));
  CHECK(decryptedLength == 0U);
  for (uint8_t byte : decrypted) {
    CHECK(byte == 0xF4U);
  }
}

void testCcmAuthenticationAndAliasing() {
  std::array<uint8_t, 16U> key{};
  std::array<uint8_t, 13U> nonce{};
  std::array<uint8_t, 24U> plaintext{};
  fillSequence(key.data(), key.size(), 0x11U);
  fillSequence(nonce.data(), nonce.size(), 0x41U);
  fillSequence(plaintext.data(), plaintext.size(), 0x71U);

  std::array<uint8_t, 28U> reference{};
  uint8_t referenceLength = 0U;
  CHECK(ZigbeeSecurity::encryptCcmStar(
      key.data(), nonce.data(), nullptr, 0U, plaintext.data(),
      static_cast<uint8_t>(plaintext.size()), reference.data(),
      static_cast<uint8_t>(reference.size()), &referenceLength));
  CHECK(referenceLength == reference.size());

  std::array<uint8_t, 28U> inPlace{};
  std::memcpy(inPlace.data(), plaintext.data(), plaintext.size());
  uint8_t inPlaceLength = 0U;
  CHECK(ZigbeeSecurity::encryptCcmStar(
      key.data(), nonce.data(), nullptr, 0U, inPlace.data(),
      static_cast<uint8_t>(plaintext.size()), inPlace.data(),
      static_cast<uint8_t>(inPlace.size()), &inPlaceLength));
  CHECK(inPlaceLength == referenceLength);
  CHECK(inPlace == reference);

  uint8_t decryptedLength = 0U;
  CHECK(ZigbeeSecurity::decryptCcmStar(
      key.data(), nonce.data(), nullptr, 0U, inPlace.data(), inPlaceLength,
      inPlace.data(), static_cast<uint8_t>(plaintext.size()),
      &decryptedLength));
  CHECK(decryptedLength == plaintext.size());
  CHECK(std::memcmp(inPlace.data(), plaintext.data(), plaintext.size()) == 0);

  // Staging also makes partially overlapping input/output deterministic.
  std::array<uint8_t, 48U> overlap{};
  overlap.fill(0xC7U);
  std::memcpy(&overlap[12], plaintext.data(), plaintext.size());
  uint8_t overlapLength = 0U;
  CHECK(ZigbeeSecurity::encryptCcmStar(
      key.data(), nonce.data(), nullptr, 0U, &overlap[12],
      static_cast<uint8_t>(plaintext.size()), &overlap[8], referenceLength,
      &overlapLength));
  CHECK(overlapLength == referenceLength);
  CHECK(std::memcmp(&overlap[8], reference.data(), reference.size()) == 0);

  // Authentication failure must not release attacker-controlled plaintext.
  reference.back() ^= 0x80U;
  std::array<uint8_t, 26U> rejected{};
  rejected.fill(0xD3U);
  const auto rejectedBefore = rejected;
  decryptedLength = 0xA5U;
  CHECK(!ZigbeeSecurity::decryptCcmStar(
      key.data(), nonce.data(), nullptr, 0U, reference.data(), referenceLength,
      &rejected[1], static_cast<uint8_t>(plaintext.size()),
      &decryptedLength));
  CHECK(decryptedLength == 0U);
  CHECK(rejected == rejectedBefore);
}

void testCapacityAwareSecurityApis() {
  std::array<uint8_t, 16U> key{};
  fillSequence(key.data(), key.size(), 0x22U);

  ZigbeeNwkSecurityHeader nwkSecurity{};
  nwkSecurity.securityControl = kZigbeeSecurityControlNwkEncMic32;
  nwkSecurity.frameCounter = 0x01020304UL;
  nwkSecurity.sourceIeee = 0x00124B0001020304ULL;
  nwkSecurity.keySequence = 7U;

  std::array<uint8_t, 16U> guardedHeader{};
  guardedHeader.fill(0xA6U);
  const auto guardedHeaderBefore = guardedHeader;
  uint8_t encodedLength = 0xFFU;
  CHECK(!ZigbeeSecurity::buildNwkSecurityHeader(
      nwkSecurity, &guardedHeader[1], 13U, &encodedLength));
  CHECK(encodedLength == 0U);
  CHECK(guardedHeader == guardedHeaderBefore);
  CHECK(ZigbeeSecurity::buildNwkSecurityHeader(
      nwkSecurity, &guardedHeader[1], 14U, &encodedLength));
  CHECK(encodedLength == 14U);
  CHECK(guardedHeader.front() == 0xA6U);
  CHECK(guardedHeader.back() == 0xA6U);
  nwkSecurity.frameCounter = UINT32_MAX;
  CHECK(!ZigbeeSecurity::buildNwkSecurityHeader(
      nwkSecurity, &guardedHeader[1], 14U, &encodedLength));
  nwkSecurity.frameCounter = UINT32_MAX - 1U;
  CHECK(ZigbeeSecurity::buildNwkSecurityHeader(
      nwkSecurity, &guardedHeader[1], 14U, &encodedLength));
  nwkSecurity.frameCounter = 0x01020304UL;

  ZigbeeNetworkFrame nwk{};
  nwk.frameType = ZigbeeNwkFrameType::kData;
  nwk.destinationShort = 0x0000U;
  nwk.sourceShort = 0x1234U;
  nwk.radius = 5U;
  nwk.sequence = 9U;
  const std::array<uint8_t, 5U> payload = {1U, 2U, 3U, 4U, 5U};
  std::array<uint8_t, 127U> secured{};
  uint8_t securedLength = 0U;
  CHECK(ZigbeeSecurity::buildSecuredNwkFrame(
      nwk, nwkSecurity, key.data(), payload.data(), payload.size(),
      secured.data(), secured.size(), &securedLength));
  CHECK(securedLength > payload.size());
  CHECK(securedLength > 8U &&
        secured[8] == kZigbeeSecurityControlNwkEncMic32);

  std::array<uint8_t, 129U> rejectedFrame{};
  rejectedFrame.fill(0xE5U);
  const auto rejectedFrameBefore = rejectedFrame;
  encodedLength = 0xFFU;
  CHECK(!ZigbeeSecurity::buildSecuredNwkFrame(
      nwk, nwkSecurity, key.data(), payload.data(), payload.size(),
      &rejectedFrame[1], static_cast<uint8_t>(securedLength - 1U),
      &encodedLength));
  CHECK(encodedLength == 0U);
  CHECK(rejectedFrame == rejectedFrameBefore);

  ZigbeeNetworkFrame parsedNwk{};
  ZigbeeNwkSecurityHeader parsedSecurity{};
  std::array<uint8_t, 7U> rejectedPayload{};
  rejectedPayload.fill(0xB9U);
  const auto rejectedPayloadBefore = rejectedPayload;
  uint8_t parsedLength = 0xFFU;
  CHECK(!ZigbeeSecurity::parseSecuredNwkFrame(
      secured.data(), securedLength, key.data(), &parsedNwk, &parsedSecurity,
      &rejectedPayload[1], static_cast<uint8_t>(payload.size() - 1U),
      &parsedLength));
  CHECK(parsedLength == 0U);
  CHECK(!parsedNwk.valid);
  CHECK(!parsedSecurity.valid);
  CHECK(rejectedPayload == rejectedPayloadBefore);

  CHECK(ZigbeeSecurity::parseSecuredNwkFrame(
      secured.data(), securedLength, key.data(), &parsedNwk, &parsedSecurity,
      &rejectedPayload[1], payload.size(), &parsedLength));
  CHECK(parsedLength == payload.size());
  CHECK(std::memcmp(&rejectedPayload[1], payload.data(), payload.size()) == 0);
  for (const uint8_t reservedHighByteBit : {0x40U, 0x80U}) {
    auto reserved = secured;
    reserved[1] |= reservedHighByteBit;
    parsedNwk.valid = true;
    parsedSecurity.valid = true;
    parsedLength = 0xA5U;
    rejectedPayload.fill(0xB9U);
    CHECK(!ZigbeeSecurity::parseSecuredNwkFrame(
        reserved.data(), securedLength, key.data(), &parsedNwk,
        &parsedSecurity, &rejectedPayload[1], payload.size(), &parsedLength));
    CHECK(!parsedNwk.valid && !parsedSecurity.valid && parsedLength == 0U);
  }
}

void testMalformedCodecInputs() {
  // A 252-byte string at offset four requires 256 bytes. The former uint8_t
  // addition wrapped to zero and accepted a pointer beyond the allocation.
  std::array<uint8_t, 255U> stringReport{};
  stringReport[0] = 0x34U;
  stringReport[1] = 0x12U;
  stringReport[2] = static_cast<uint8_t>(ZigbeeZclDataType::kCharString);
  for (uint8_t declared : {252U, 253U, 254U, 255U}) {
    stringReport[3] = declared;
    ZigbeeAttributeReportRecord record{};
    uint8_t count = 0xA5U;
    CHECK(!ZigbeeCodec::parseAttributeReport(
        stringReport.data(), stringReport.size(), &record, 1U, &count));
    CHECK(count == 0U);
  }

  for (uint8_t rawType = 4U; rawType <= 7U; ++rawType) {
    const std::array<uint8_t, 3U> mac = {rawType, 0U, 0U};
    ZigbeeMacFrame parsed{};
    CHECK(!ZigbeeCodec::parseMacFrame(mac.data(), mac.size(), &parsed));
    CHECK(!parsed.valid);
  }

  for (uint8_t rawType : {2U, 3U}) {
    std::array<uint8_t, 8U> nwk{};
    nwk[0] = static_cast<uint8_t>(rawType | (2U << 2U));
    ZigbeeNetworkFrame parsed{};
    CHECK(!ZigbeeCodec::parseNwkFrame(nwk.data(), nwk.size(), &parsed));
    CHECK(!parsed.valid);
  }

  ZigbeeNetworkFrame extendedNwk{};
  extendedNwk.destinationShort = 0x1234U;
  extendedNwk.sourceShort = 0x5678U;
  extendedNwk.radius = 5U;
  extendedNwk.sequence = 6U;
  extendedNwk.extendedDestination = true;
  extendedNwk.destinationExtended = 0x00124B0001020304ULL;
  extendedNwk.extendedSource = true;
  extendedNwk.sourceExtended = 0x00124B0005060708ULL;
  std::array<uint8_t, 127U> extendedNwkWire{};
  uint8_t extendedNwkLength = 0U;
  CHECK(ZigbeeCodec::buildNwkFrame(
      extendedNwk, nullptr, 0U, extendedNwkWire.data(), extendedNwkWire.size(),
      &extendedNwkLength));
  CHECK(extendedNwkLength == 24U);
  struct GuardedNwkFrame {
    uint32_t before;
    ZigbeeNetworkFrame value;
    uint32_t after;
  } guardedNwk{0x12345678UL, {}, 0x89ABCDEFUL};
  for (uint8_t length = 0U; length < extendedNwkLength; ++length) {
    guardedNwk.value.valid = true;
    guardedNwk.value.destinationExtended = UINT64_MAX;
    CHECK(!ZigbeeCodec::parseNwkFrame(
        extendedNwkWire.data(), length, &guardedNwk.value));
    CHECK(!guardedNwk.value.valid &&
          guardedNwk.value.destinationExtended == 0U &&
          guardedNwk.before == 0x12345678UL &&
          guardedNwk.after == 0x89ABCDEFUL);
  }
  for (const uint8_t reservedHighByteBit : {0x40U, 0x80U}) {
    auto reservedNwk = extendedNwkWire;
    reservedNwk[1] |= reservedHighByteBit;
    guardedNwk.value.valid = true;
    CHECK(!ZigbeeCodec::parseNwkFrame(
        reservedNwk.data(), extendedNwkLength, &guardedNwk.value));
    CHECK(!guardedNwk.value.valid &&
          guardedNwk.before == 0x12345678UL &&
          guardedNwk.after == 0x89ABCDEFUL);
  }
  CHECK(ZigbeeCodec::parseNwkFrame(
      extendedNwkWire.data(), extendedNwkLength, &guardedNwk.value));
  CHECK(guardedNwk.value.valid &&
        guardedNwk.value.destinationExtended ==
            extendedNwk.destinationExtended &&
        guardedNwk.value.sourceExtended == extendedNwk.sourceExtended);

  for (uint8_t rawType : {2U, 3U}) {
    const std::array<uint8_t, 3U> zcl = {rawType, 0U, 0U};
    ZigbeeZclFrame parsed{};
    CHECK(!ZigbeeCodec::parseZclFrame(zcl.data(), zcl.size(), &parsed));
    CHECK(!parsed.valid);
  }

  std::array<uint8_t, 8U> unknownReporting = {
      0U, 0x34U, 0x12U, 0xFFU, 1U, 0U, 2U, 0U};
  ZigbeeReportingConfiguration reporting{};
  uint8_t reportingCount = 0xA5U;
  CHECK(!ZigbeeCodec::parseConfigureReportingRequest(
      unknownReporting.data(), unknownReporting.size(), &reporting, 1U,
      &reportingCount));
  CHECK(reportingCount == 0U);

  struct GuardedDiscoveredAttribute {
    uint32_t before;
    ZigbeeDiscoveredAttributeRecord values[2];
    uint32_t after;
  } discovered{0x11223344UL, {}, 0x55667788UL};
  discovered.values[0].attributeId = 0xBEEFU;
  discovered.values[0].dataType = ZigbeeZclDataType::kUint32;
  bool discoveryComplete = true;
  uint8_t discoveredCount = 0xA5U;
  const std::array<uint8_t, 4U> invalidDiscoveryComplete = {
      2U, 0x34U, 0x12U, static_cast<uint8_t>(ZigbeeZclDataType::kUint8)};
  CHECK(!ZigbeeCodec::parseDiscoverAttributesResponse(
      invalidDiscoveryComplete.data(), invalidDiscoveryComplete.size(),
      &discoveryComplete, discovered.values, 1U, &discoveredCount));
  CHECK(!discoveryComplete && discoveredCount == 0U);
  CHECK(discovered.before == 0x11223344UL &&
        discovered.after == 0x55667788UL);
  CHECK(discovered.values[0].attributeId == 0xBEEFU &&
        discovered.values[0].dataType == ZigbeeZclDataType::kUint32);

  auto invalidDataType = invalidDiscoveryComplete;
  invalidDataType[0] = 1U;
  invalidDataType[3] = 0xFFU;
  CHECK(!ZigbeeCodec::parseDiscoverAttributesResponse(
      invalidDataType.data(), invalidDataType.size(), &discoveryComplete,
      discovered.values, 1U, &discoveredCount));
  CHECK(!discoveryComplete && discoveredCount == 0U);
  CHECK(discovered.values[0].attributeId == 0xBEEFU &&
        discovered.values[0].dataType == ZigbeeZclDataType::kUint32);
  auto validUnsupportedType = invalidDiscoveryComplete;
  validUnsupportedType[0] = 1U;
  for (const uint8_t standardType : {0x30U, 0x4CU, 0x51U}) {
    validUnsupportedType[3] = standardType;
    CHECK(ZigbeeCodec::parseDiscoverAttributesResponse(
        validUnsupportedType.data(), validUnsupportedType.size(),
        &discoveryComplete, discovered.values, 1U, &discoveredCount));
    CHECK(discoveryComplete && discoveredCount == 1U &&
          discovered.values[0].attributeId == 0x1234U &&
          static_cast<uint8_t>(discovered.values[0].dataType) == standardType);
  }
  validUnsupportedType[3] = 0x49U;
  CHECK(!ZigbeeCodec::parseDiscoverAttributesResponse(
      validUnsupportedType.data(), validUnsupportedType.size(),
      &discoveryComplete, discovered.values, 1U, &discoveredCount));
  discovered.values[0].attributeId = 0xBEEFU;
  discovered.values[0].dataType = ZigbeeZclDataType::kUint32;

  struct GuardedExtendedAttribute {
    uint32_t before;
    ZigbeeDiscoveredExtendedAttributeRecord values[2];
    uint32_t after;
  } extended{0x89ABCDEFUL, {}, 0x76543210UL};
  extended.values[0].attributeId = 0xCAFEU;
  extended.values[0].dataType = ZigbeeZclDataType::kBitmap16;
  extended.values[0].accessControl = 0x07U;
  const std::array<uint8_t, 5U> invalidAccess = {
      1U, 0x34U, 0x12U, static_cast<uint8_t>(ZigbeeZclDataType::kUint8), 0x08U};
  CHECK(!ZigbeeCodec::parseDiscoverAttributesExtendedResponse(
      invalidAccess.data(), invalidAccess.size(), &discoveryComplete,
      extended.values, 1U, &discoveredCount));
  CHECK(!discoveryComplete && discoveredCount == 0U);
  CHECK(extended.before == 0x89ABCDEFUL &&
        extended.after == 0x76543210UL);
  CHECK(extended.values[0].attributeId == 0xCAFEU &&
        extended.values[0].dataType == ZigbeeZclDataType::kBitmap16 &&
        extended.values[0].accessControl == 0x07U);

  const std::array<uint8_t, 2U> invalidCommandDiscovery = {2U, 0x55U};
  std::array<uint8_t, 3U> commandIds = {0xC3U, 0xC3U, 0xC3U};
  const auto commandIdsBefore = commandIds;
  CHECK(!ZigbeeCodec::parseDiscoverCommandsResponse(
      invalidCommandDiscovery.data(), invalidCommandDiscovery.size(),
      &discoveryComplete, &commandIds[1], 1U, &discoveredCount));
  CHECK(!discoveryComplete && discoveredCount == 0U &&
        commandIds == commandIdsBefore);

  std::array<uint8_t, 255U> oversizedDiscovery{};
  oversizedDiscovery[0] = 1U;
  CHECK(!ZigbeeCodec::parseDiscoverAttributesResponse(
      oversizedDiscovery.data(), 253U, &discoveryComplete, discovered.values,
      UINT8_MAX, &discoveredCount));
  CHECK(!ZigbeeCodec::parseDiscoverAttributesExtendedResponse(
      oversizedDiscovery.data(), 253U, &discoveryComplete, extended.values,
      UINT8_MAX, &discoveredCount));
  CHECK(!ZigbeeCodec::parseDiscoverCommandsResponse(
      oversizedDiscovery.data(), oversizedDiscovery.size(), &discoveryComplete,
      &commandIds[1], UINT8_MAX, &discoveredCount));
  CHECK(discovered.values[0].attributeId == 0xBEEFU &&
        extended.values[0].attributeId == 0xCAFEU);
  CHECK(commandIds == commandIdsBefore);

  const std::array<uint8_t, 7U> duplicateAttributes = {
      1U, 0x34U, 0x12U, static_cast<uint8_t>(ZigbeeZclDataType::kUint8),
      0x34U, 0x12U, static_cast<uint8_t>(ZigbeeZclDataType::kUint16)};
  CHECK(!ZigbeeCodec::parseDiscoverAttributesResponse(
      duplicateAttributes.data(), duplicateAttributes.size(),
      &discoveryComplete, discovered.values, 2U, &discoveredCount));
  CHECK(!discoveryComplete && discoveredCount == 0U &&
        discovered.values[0].attributeId == 0xBEEFU);
  const std::array<uint8_t, 9U> descendingExtended = {
      1U, 0x35U, 0x12U, static_cast<uint8_t>(ZigbeeZclDataType::kUint8), 1U,
      0x34U, 0x12U, static_cast<uint8_t>(ZigbeeZclDataType::kUint16), 1U};
  CHECK(!ZigbeeCodec::parseDiscoverAttributesExtendedResponse(
      descendingExtended.data(), descendingExtended.size(),
      &discoveryComplete, extended.values, 2U, &discoveredCount));
  CHECK(!discoveryComplete && discoveredCount == 0U &&
        extended.values[0].attributeId == 0xCAFEU);
  const std::array<uint8_t, 3U> duplicateCommands = {1U, 0x22U, 0x22U};
  CHECK(!ZigbeeCodec::parseDiscoverCommandsResponse(
      duplicateCommands.data(), duplicateCommands.size(), &discoveryComplete,
      &commandIds[1], 2U, &discoveredCount));
  CHECK(commandIds == commandIdsBefore);

  ZigbeeDiscoveredAttributeRecord unorderedRecords[2] = {};
  unorderedRecords[0].attributeId = 2U;
  unorderedRecords[0].dataType = ZigbeeZclDataType::kUint8;
  unorderedRecords[1].attributeId = 1U;
  unorderedRecords[1].dataType = ZigbeeZclDataType::kUint16;
  std::array<uint8_t, 129U> discoveryOutput{};
  discoveryOutput.fill(0x9DU);
  const auto discoveryOutputBefore = discoveryOutput;
  uint8_t discoveryOutputLength = 0xA5U;
  CHECK(!ZigbeeCodec::buildDiscoverAttributesResponse(
      unorderedRecords, 2U, true, 1U, &discoveryOutput[1], 127U,
      &discoveryOutputLength));
  CHECK(discoveryOutputLength == 0U &&
        discoveryOutput == discoveryOutputBefore);

  discoveryOutputLength = 0xA5U;
  unorderedRecords[1].attributeId = unorderedRecords[0].attributeId;
  CHECK(!ZigbeeCodec::buildDiscoverAttributesResponse(
      unorderedRecords, 2U, true, 1U, &discoveryOutput[1], 127U,
      &discoveryOutputLength));
  CHECK(discoveryOutputLength == 0U &&
        discoveryOutput == discoveryOutputBefore);

  ZigbeeDiscoveredExtendedAttributeRecord unorderedExtendedRecords[2] = {};
  unorderedExtendedRecords[0].attributeId = 2U;
  unorderedExtendedRecords[0].dataType = ZigbeeZclDataType::kUint8;
  unorderedExtendedRecords[0].accessControl = 0x01U;
  unorderedExtendedRecords[1].attributeId = 1U;
  unorderedExtendedRecords[1].dataType = ZigbeeZclDataType::kUint16;
  unorderedExtendedRecords[1].accessControl = 0x05U;
  discoveryOutputLength = 0xA5U;
  CHECK(!ZigbeeCodec::buildDiscoverAttributesExtendedResponse(
      unorderedExtendedRecords, 2U, true, 1U, &discoveryOutput[1], 127U,
      &discoveryOutputLength));
  CHECK(discoveryOutputLength == 0U &&
        discoveryOutput == discoveryOutputBefore);

  discoveryOutputLength = 0xA5U;
  unorderedExtendedRecords[1].attributeId =
      unorderedExtendedRecords[0].attributeId;
  CHECK(!ZigbeeCodec::buildDiscoverAttributesExtendedResponse(
      unorderedExtendedRecords, 2U, true, 1U, &discoveryOutput[1], 127U,
      &discoveryOutputLength));
  CHECK(discoveryOutputLength == 0U &&
        discoveryOutput == discoveryOutputBefore);

  const uint8_t unorderedCommands[2] = {2U, 1U};
  const uint8_t duplicateCommandIds[2] = {2U, 2U};
  discoveryOutputLength = 0xA5U;
  CHECK(!ZigbeeCodec::buildDiscoverCommandsReceivedResponse(
      unorderedCommands, 2U, true, 1U, &discoveryOutput[1], 127U,
      &discoveryOutputLength));
  CHECK(discoveryOutputLength == 0U &&
        discoveryOutput == discoveryOutputBefore);

  discoveryOutputLength = 0xA5U;
  CHECK(!ZigbeeCodec::buildDiscoverCommandsReceivedResponse(
      duplicateCommandIds, 2U, true, 1U, &discoveryOutput[1], 127U,
      &discoveryOutputLength));
  CHECK(discoveryOutputLength == 0U &&
        discoveryOutput == discoveryOutputBefore);

  discoveryOutputLength = 0xA5U;
  CHECK(!ZigbeeCodec::buildDiscoverCommandsGeneratedResponse(
      unorderedCommands, 2U, true, 1U, &discoveryOutput[1], 127U,
      &discoveryOutputLength));
  CHECK(discoveryOutputLength == 0U &&
        discoveryOutput == discoveryOutputBefore);

  discoveryOutputLength = 0xA5U;
  CHECK(!ZigbeeCodec::buildDiscoverCommandsGeneratedResponse(
      duplicateCommandIds, 2U, true, 1U, &discoveryOutput[1], 127U,
      &discoveryOutputLength));
  CHECK(discoveryOutputLength == 0U &&
        discoveryOutput == discoveryOutputBefore);

  const uint8_t orderedCommands[2] = {1U, 2U};
  discoveryOutputLength = 0U;
  CHECK(ZigbeeCodec::buildDiscoverCommandsGeneratedResponse(
      orderedCommands, 2U, true, 1U, &discoveryOutput[1], 127U,
      &discoveryOutputLength));
  CHECK(discoveryOutputLength == 6U);

  const std::array<uint8_t, 9U> unknownReadReportingType = {
      0U, 0U, 0x34U, 0x12U, 0xFFU, 1U, 0U, 2U, 0U};
  ZigbeeReadReportingConfigurationResponseRecord readReporting{};
  CHECK(!ZigbeeCodec::parseReadReportingConfigurationResponse(
      unknownReadReportingType.data(), unknownReadReportingType.size(),
      &readReporting, 1U, &discoveredCount));
  CHECK(discoveredCount == 0U);

  const std::array<uint8_t, 6U> oversizedDescriptor = {
      1U, 0U, 0x34U, 0x12U, 0xFFU, 1U};
  ZigbeeZdoSimpleDescriptorResponseView descriptor{};
  CHECK(!ZigbeeCodec::parseZdoSimpleDescriptorResponse(
      oversizedDescriptor.data(), oversizedDescriptor.size(), &descriptor));
  CHECK(!descriptor.valid);

  ZigbeeHomeAutomationDevice device;
  ZigbeeBasicClusterConfig basic{};
  CHECK(device.configureOnOffLight(1U, 0x00124B0001020304ULL, 0x1234U,
                                   0x4567U, basic));
  std::array<uint8_t, 255U> request{};
  std::array<uint8_t, 127U> response{};
  uint8_t responseLength = 0U;

  request[0] = static_cast<uint8_t>(ZigbeeZclFrameType::kClusterSpecific);
  request[1] = 1U;
  request[2] = 0U;  // Add Group.
  request[5] = 253U;
  CHECK(device.handleZclRequest(kZigbeeClusterGroups, request.data(),
                                request.size(), response.data(),
                                static_cast<uint8_t>(response.size()),
                                &responseLength));
  for (const auto& entry : device.config().groups.entries) {
    CHECK(!entry.used);
  }

  request.fill(0U);
  request[0] = static_cast<uint8_t>(ZigbeeZclFrameType::kClusterSpecific);
  request[1] = 2U;
  request[2] = 2U;  // Get Group Membership.
  request[3] = 128U;
  CHECK(device.handleZclRequest(kZigbeeClusterGroups, request.data(),
                                request.size(), response.data(),
                                static_cast<uint8_t>(response.size()),
                                &responseLength));

  request.fill(0U);
  request[0] = static_cast<uint8_t>(ZigbeeZclFrameType::kClusterSpecific);
  request[1] = 3U;
  request[2] = 0U;  // Add Scene.
  request[8] = 250U;
  CHECK(device.handleZclRequest(kZigbeeClusterScenes, request.data(),
                                request.size(), response.data(),
                                static_cast<uint8_t>(response.size()),
                                &responseLength));
  for (const auto& entry : device.config().scenes.entries) {
    CHECK(!entry.used);
  }
}

void testSecuredBuilderLengthWraps() {
  std::array<uint8_t, 16U> key{};
  std::array<uint8_t, 255U> payload{};
  fillSequence(key.data(), key.size(), 0x20U);
  fillSequence(payload.data(), payload.size(), 0x60U);

  ZigbeeApsCommandFrame aps{};
  aps.frameType = ZigbeeApsFrameType::kCommand;
  aps.deliveryMode = kZigbeeApsDeliveryUnicast;
  aps.counter = 7U;
  aps.commandId = kZigbeeApsCommandUpdateDevice;
  ZigbeeApsSecurityHeader apsSecurity{};
  apsSecurity.securityControl = kZigbeeSecurityControlApsEncMic32;
  apsSecurity.frameCounter = 11U;
  apsSecurity.sourceIeee = 0x00124B0001020304ULL;

  std::array<uint8_t, 129U> output{};
  output.fill(0x7EU);
  uint8_t outputLength = 0xA5U;
  CHECK(!ZigbeeSecurity::buildSecuredApsCommandFrame(
      aps, apsSecurity, key.data(), payload.data(),
      static_cast<uint8_t>(payload.size()), &output[1], &outputLength));
  CHECK(output.front() == 0x7EU);
  CHECK(output.back() == 0x7EU);

  ZigbeeNetworkFrame nwk{};
  nwk.frameType = ZigbeeNwkFrameType::kData;
  nwk.destinationShort = 0x0000U;
  nwk.sourceShort = 0x1234U;
  nwk.radius = 5U;
  nwk.sequence = 9U;
  ZigbeeNwkSecurityHeader nwkSecurity{};
  nwkSecurity.securityControl = kZigbeeSecurityControlNwkEncMic32;
  nwkSecurity.frameCounter = 12U;
  nwkSecurity.sourceIeee = 0x00124B0001020304ULL;
  nwkSecurity.keySequence = 0U;

  output.fill(0x8EU);
  outputLength = 0xA5U;
  CHECK(!ZigbeeSecurity::buildSecuredNwkFrame(
      nwk, nwkSecurity, key.data(), payload.data(),
      static_cast<uint8_t>(payload.size()), &output[1], &outputLength));
  CHECK(output.front() == 0x8EU);
  CHECK(output.back() == 0x8EU);
}

void testAuxiliarySecurityHeaderContracts() {
  const auto nwkHeaderIsDefault = [](const ZigbeeNwkSecurityHeader& header) {
    return !header.valid && header.securityControl == 0x28U &&
           header.frameCounter == 0U && header.sourceIeee == 0U &&
           header.keySequence == 0U && header.micLength == 4U;
  };
  const auto apsHeaderIsDefault = [](const ZigbeeApsSecurityHeader& header) {
    return !header.valid && header.securityControl == 0x20U &&
           header.frameCounter == 0U && header.sourceIeee == 0U &&
           header.keySequence == 0U && header.micLength == 4U;
  };

  ZigbeeNwkSecurityHeader nwk{};
  nwk.frameCounter = 0x01020304UL;
  nwk.sourceIeee = 0x00124B0001020304ULL;
  nwk.keySequence = 7U;
  const std::array<uint8_t, 14U> expectedNwk = {
      0x28U, 0x04U, 0x03U, 0x02U, 0x01U, 0x04U, 0x03U,
      0x02U, 0x01U, 0x00U, 0x4BU, 0x12U, 0x00U, 0x07U};
  std::array<uint8_t, 16U> encoded{};
  uint8_t encodedLength = 0U;
  CHECK(ZigbeeSecurity::buildNwkSecurityHeader(
      nwk, encoded.data(), encoded.size(), &encodedLength));
  CHECK(encodedLength == expectedNwk.size());
  CHECK(std::memcmp(encoded.data(), expectedNwk.data(), expectedNwk.size()) ==
        0);

  for (uint8_t length = 0U; length < expectedNwk.size(); ++length) {
    ZigbeeNwkSecurityHeader parsed{};
    parsed.valid = true;
    parsed.frameCounter = 0xA5A5A5A5UL;
    uint8_t headerLength = 0xA5U;
    CHECK(!ZigbeeSecurity::parseNwkSecurityHeader(
        expectedNwk.data(), length, &parsed, &headerLength));
    CHECK(headerLength == 0U && nwkHeaderIsDefault(parsed));
  }

  ZigbeeNwkSecurityHeader parsedNwk{};
  uint8_t headerLength = 0U;
  CHECK(ZigbeeSecurity::parseNwkSecurityHeader(
      expectedNwk.data(), expectedNwk.size(), &parsedNwk, &headerLength));
  CHECK(parsedNwk.valid && headerLength == expectedNwk.size() &&
        parsedNwk.frameCounter == nwk.frameCounter &&
        parsedNwk.sourceIeee == nwk.sourceIeee &&
        parsedNwk.keySequence == nwk.keySequence);

  for (const uint8_t invalidControl : {0x2DU, 0x68U}) {
    auto invalid = expectedNwk;
    invalid[0] = invalidControl;
    parsedNwk.valid = true;
    headerLength = 0xA5U;
    CHECK(!ZigbeeSecurity::parseNwkSecurityHeader(
        invalid.data(), invalid.size(), &parsedNwk, &headerLength));
    CHECK(headerLength == 0U && nwkHeaderIsDefault(parsedNwk));
  }
  auto invalidNwk = expectedNwk;
  for (uint8_t i = 5U; i < 13U; ++i) {
    invalidNwk[i] = 0U;
  }
  CHECK(!ZigbeeSecurity::parseNwkSecurityHeader(
      invalidNwk.data(), invalidNwk.size(), &parsedNwk, &headerLength));
  CHECK(headerLength == 0U && nwkHeaderIsDefault(parsedNwk));
  invalidNwk = expectedNwk;
  for (uint8_t i = 5U; i < 13U; ++i) {
    invalidNwk[i] = 0xFFU;
  }
  CHECK(!ZigbeeSecurity::parseNwkSecurityHeader(
      invalidNwk.data(), invalidNwk.size(), &parsedNwk, &headerLength));
  CHECK(headerLength == 0U && nwkHeaderIsDefault(parsedNwk));
  invalidNwk = expectedNwk;
  invalidNwk[1] = 0xFFU;
  invalidNwk[2] = 0xFFU;
  invalidNwk[3] = 0xFFU;
  invalidNwk[4] = 0xFFU;
  CHECK(!ZigbeeSecurity::parseNwkSecurityHeader(
      invalidNwk.data(), invalidNwk.size(), &parsedNwk, &headerLength));
  CHECK(headerLength == 0U && nwkHeaderIsDefault(parsedNwk));

  nwk.frameCounter = UINT32_MAX;
  encoded.fill(0x6CU);
  const auto encodedBefore = encoded;
  encodedLength = 0xA5U;
  CHECK(!ZigbeeSecurity::buildNwkSecurityHeader(
      nwk, encoded.data(), encoded.size(), &encodedLength));
  CHECK(encodedLength == 0U && encoded == encodedBefore);
  nwk.frameCounter = UINT32_MAX - 1U;
  CHECK(ZigbeeSecurity::buildNwkSecurityHeader(
      nwk, encoded.data(), encoded.size(), &encodedLength));
  CHECK(ZigbeeSecurity::parseNwkSecurityHeader(
      encoded.data(), encodedLength, &parsedNwk, &headerLength));
  CHECK(parsedNwk.frameCounter == UINT32_MAX - 1U);

  ZigbeeApsSecurityHeader aps{};
  aps.frameCounter = 0x01020304UL;
  aps.sourceIeee = 0x00124B0001020304ULL;
  const std::array<uint8_t, 13U> expectedApsData = {
      0x20U, 0x04U, 0x03U, 0x02U, 0x01U, 0x04U, 0x03U,
      0x02U, 0x01U, 0x00U, 0x4BU, 0x12U, 0x00U};
  CHECK(ZigbeeSecurity::buildApsSecurityHeader(
      aps, encoded.data(), encoded.size(), &encodedLength));
  CHECK(encodedLength == expectedApsData.size());
  CHECK(std::memcmp(encoded.data(), expectedApsData.data(),
                    expectedApsData.size()) == 0);

  for (uint8_t length = 0U; length < expectedApsData.size(); ++length) {
    ZigbeeApsSecurityHeader parsed{};
    parsed.valid = true;
    parsed.frameCounter = 0x5A5A5A5AUL;
    headerLength = 0xA5U;
    CHECK(!ZigbeeSecurity::parseApsSecurityHeader(
        expectedApsData.data(), length, &parsed, &headerLength));
    CHECK(headerLength == 0U && apsHeaderIsDefault(parsed));
  }

  ZigbeeApsSecurityHeader parsedAps{};
  CHECK(ZigbeeSecurity::parseApsSecurityHeader(
      expectedApsData.data(), expectedApsData.size(), &parsedAps,
      &headerLength));
  CHECK(parsedAps.valid && headerLength == expectedApsData.size() &&
        parsedAps.securityControl == 0x20U);
  aps.securityControl = kZigbeeSecurityControlApsKeyTransport;
  CHECK(ZigbeeSecurity::buildApsSecurityHeader(
      aps, encoded.data(), encoded.size(), &encodedLength));
  CHECK(encodedLength == expectedApsData.size() && encoded[0] == 0x30U);
  CHECK(ZigbeeSecurity::parseApsSecurityHeader(
      encoded.data(), encodedLength, &parsedAps, &headerLength));
  CHECK(parsedAps.securityControl == 0x30U);

  for (const uint8_t invalidControl : {0x25U, 0x35U, 0x28U}) {
    auto invalid = expectedApsData;
    invalid[0] = invalidControl;
    parsedAps.valid = true;
    headerLength = 0xA5U;
    CHECK(!ZigbeeSecurity::parseApsSecurityHeader(
        invalid.data(), invalid.size(), &parsedAps, &headerLength));
    CHECK(headerLength == 0U && apsHeaderIsDefault(parsedAps));
  }
  auto invalidAps = expectedApsData;
  for (uint8_t i = 5U; i < invalidAps.size(); ++i) {
    invalidAps[i] = 0U;
  }
  CHECK(!ZigbeeSecurity::parseApsSecurityHeader(
      invalidAps.data(), invalidAps.size(), &parsedAps, &headerLength));
  CHECK(headerLength == 0U && apsHeaderIsDefault(parsedAps));
  invalidAps = expectedApsData;
  for (uint8_t i = 5U; i < invalidAps.size(); ++i) {
    invalidAps[i] = 0xFFU;
  }
  CHECK(!ZigbeeSecurity::parseApsSecurityHeader(
      invalidAps.data(), invalidAps.size(), &parsedAps, &headerLength));
  CHECK(headerLength == 0U && apsHeaderIsDefault(parsedAps));
  invalidAps = expectedApsData;
  invalidAps[1] = 0xFFU;
  invalidAps[2] = 0xFFU;
  invalidAps[3] = 0xFFU;
  invalidAps[4] = 0xFFU;
  CHECK(!ZigbeeSecurity::parseApsSecurityHeader(
      invalidAps.data(), invalidAps.size(), &parsedAps, &headerLength));
  CHECK(headerLength == 0U && apsHeaderIsDefault(parsedAps));
  aps.frameCounter = UINT32_MAX;
  encoded.fill(0x7DU);
  const auto apsEncodedBefore = encoded;
  encodedLength = 0xA5U;
  CHECK(!ZigbeeSecurity::buildApsSecurityHeader(
      aps, encoded.data(), encoded.size(), &encodedLength));
  CHECK(encodedLength == 0U && encoded == apsEncodedBefore);

  std::array<uint8_t, 13U> nonce{};
  CHECK(ZigbeeSecurity::buildNwkNonce(
      nwk.sourceIeee, 1U, 0x28U, nonce.data(), nonce.size()));
  CHECK(nonce[12] == 0x2DU);
  CHECK(ZigbeeSecurity::buildNwkNonce(
      nwk.sourceIeee, 1U, 0x20U, nonce.data(), nonce.size()));
  CHECK(nonce[12] == 0x25U);
  CHECK(ZigbeeSecurity::buildNwkNonce(
      nwk.sourceIeee, 1U, 0x30U, nonce.data(), nonce.size()));
  CHECK(nonce[12] == 0x35U);
  for (const uint8_t cryptoControl : {0x2DU, 0x25U, 0x35U}) {
    CHECK(ZigbeeSecurity::buildNwkNonce(
        nwk.sourceIeee, 1U, cryptoControl, nonce.data(), nonce.size()));
    CHECK(nonce[12] == cryptoControl);
  }
  nonce.fill(0xA6U);
  const auto nonceBefore = nonce;
  CHECK(!ZigbeeSecurity::buildNwkNonce(
      nwk.sourceIeee, 1U, 0xFFU, nonce.data(), nonce.size()));
  CHECK(!ZigbeeSecurity::buildNwkNonce(
      UINT64_MAX, 1U, 0x28U, nonce.data(), nonce.size()));
  CHECK(nonce == nonceBefore);
}

void testApsDataTupleContracts() {
  const std::array<uint8_t, 2U> payload = {0xAAU, 0x55U};
  ZigbeeApsDataFrame application{};
  application.destinationEndpoint = 1U;
  application.clusterId = kZigbeeClusterOnOff;
  application.profileId = kZigbeeProfileHomeAutomation;
  application.sourceEndpoint = 2U;
  application.ackRequested = true;
  application.counter = 3U;
  std::array<uint8_t, 32U> encoded{};
  uint8_t encodedLength = 0U;
  CHECK(ZigbeeCodec::buildApsDataFrame(
      application, payload.data(), payload.size(), encoded.data(),
      encoded.size(), &encodedLength));
  CHECK(encodedLength == 10U && encoded[0] == 0x40U);
  const auto validApplication = encoded;
  const uint8_t validApplicationLength = encodedLength;

  ZigbeeApsDataFrame parsed{};
  CHECK(ZigbeeCodec::parseApsDataFrame(
      encoded.data(), encodedLength, &parsed));
  CHECK(parsed.valid && parsed.destinationEndpoint == 1U &&
        parsed.sourceEndpoint == 2U && parsed.ackRequested &&
        parsed.payloadLength == payload.size());

  ZigbeeApsDataFrame zdo{};
  zdo.destinationEndpoint = 0U;
  zdo.clusterId = kZigbeeZdoActiveEndpointsRequest;
  zdo.profileId = kZigbeeProfileZdo;
  zdo.sourceEndpoint = 0U;
  zdo.ackRequested = true;
  zdo.counter = 4U;
  CHECK(ZigbeeCodec::buildApsDataFrame(
      zdo, nullptr, 0U, encoded.data(), encoded.size(), &encodedLength));
  CHECK(ZigbeeCodec::parseApsDataFrame(
      encoded.data(), encodedLength, &parsed));
  CHECK(parsed.valid && parsed.profileId == kZigbeeProfileZdo &&
        parsed.destinationEndpoint == 0U && parsed.sourceEndpoint == 0U);

  std::array<uint8_t, 34U> rejected{};
  rejected.fill(0xC5U);
  const auto rejectedBefore = rejected;
  uint8_t rejectedLength = 0xA5U;
  for (const uint8_t invalidEndpoint : {0U, 0xFFU}) {
    auto invalid = application;
    invalid.destinationEndpoint = invalidEndpoint;
    CHECK(!ZigbeeCodec::buildApsDataFrame(
        invalid, payload.data(), payload.size(), &rejected[1], 32U,
        &rejectedLength));
    CHECK(rejectedLength == 0U && rejected == rejectedBefore);
    invalid = application;
    invalid.sourceEndpoint = invalidEndpoint;
    CHECK(!ZigbeeCodec::buildApsDataFrame(
        invalid, payload.data(), payload.size(), &rejected[1], 32U,
        &rejectedLength));
    CHECK(rejectedLength == 0U && rejected == rejectedBefore);
  }
  auto invalidProfile = application;
  invalidProfile.profileId = kZigbeeProfileZdo;
  CHECK(!ZigbeeCodec::buildApsDataFrame(
      invalidProfile, payload.data(), payload.size(), &rejected[1], 32U,
      &rejectedLength));
  CHECK(rejectedLength == 0U && rejected == rejectedBefore);

  ZigbeeApsDataFrame group{};
  group.deliveryMode = kZigbeeApsDeliveryGroup;
  group.destinationGroup = 0x1234U;
  group.clusterId = kZigbeeClusterOnOff;
  group.profileId = kZigbeeProfileHomeAutomation;
  group.sourceEndpoint = 1U;
  group.counter = 5U;
  CHECK(ZigbeeCodec::buildApsDataFrame(
      group, payload.data(), payload.size(), encoded.data(), encoded.size(),
      &encodedLength));
  CHECK(encodedLength == 11U && encoded[0] == 0x0CU);
  const auto validGroup = encoded;
  const uint8_t validGroupLength = encodedLength;
  CHECK(ZigbeeCodec::parseApsDataFrame(
      encoded.data(), encodedLength, &parsed));
  CHECK(parsed.valid && parsed.deliveryMode == kZigbeeApsDeliveryGroup &&
        parsed.destinationGroup == 0x1234U && !parsed.ackRequested);
  group.ackRequested = true;
  CHECK(!ZigbeeCodec::buildApsDataFrame(
      group, payload.data(), payload.size(), &rejected[1], 32U,
      &rejectedLength));
  group.ackRequested = false;
  group.destinationGroup = 0U;
  CHECK(!ZigbeeCodec::buildApsDataFrame(
      group, payload.data(), payload.size(), &rejected[1], 32U,
      &rejectedLength));
  group.destinationGroup = 0x1234U;
  group.profileId = kZigbeeProfileZdo;
  CHECK(!ZigbeeCodec::buildApsDataFrame(
      group, payload.data(), payload.size(), &rejected[1], 32U,
      &rejectedLength));

  for (const uint8_t invalidBit : {0x10U, 0x20U, 0x80U}) {
    auto invalid = validApplication;
    invalid[0] |= invalidBit;
    parsed.valid = true;
    parsed.destinationEndpoint = 0xEEU;
    CHECK(!ZigbeeCodec::parseApsDataFrame(
        invalid.data(), validApplicationLength, &parsed));
    CHECK(!parsed.valid && parsed.destinationEndpoint == 0U);
  }
  for (const uint8_t invalidEndpoint : {0U, 0xFFU}) {
    auto invalid = validApplication;
    invalid[1] = invalidEndpoint;
    CHECK(!ZigbeeCodec::parseApsDataFrame(
        invalid.data(), validApplicationLength, &parsed));
    CHECK(!parsed.valid);
    invalid = validApplication;
    invalid[6] = invalidEndpoint;
    CHECK(!ZigbeeCodec::parseApsDataFrame(
        invalid.data(), validApplicationLength, &parsed));
    CHECK(!parsed.valid);
  }
  auto invalidGroup = validGroup;
  invalidGroup[0] |= 0x40U;
  CHECK(!ZigbeeCodec::parseApsDataFrame(
      invalidGroup.data(), validGroupLength, &parsed));
  invalidGroup = validGroup;
  invalidGroup[1] = 0U;
  invalidGroup[2] = 0U;
  CHECK(!ZigbeeCodec::parseApsDataFrame(
      invalidGroup.data(), validGroupLength, &parsed));
  invalidGroup = validGroup;
  invalidGroup[7] = 0xFFU;
  CHECK(!ZigbeeCodec::parseApsDataFrame(
      invalidGroup.data(), validGroupLength, &parsed));
}

void testApsAcknowledgementContracts() {
  ZigbeeApsAcknowledgementFrame commandAck{};
  commandAck.ackFormatCommand = true;
  commandAck.counter = 0x5AU;
  std::array<uint8_t, 16U> encoded{};
  uint8_t encodedLength = 0U;
  CHECK(ZigbeeCodec::buildApsAcknowledgementFrame(
      commandAck, encoded.data(), encoded.size(), &encodedLength));
  CHECK(encodedLength == 2U && encoded[0] == 0x12U &&
        encoded[1] == 0x5AU);

  ZigbeeApsAcknowledgementFrame parsed{};
  CHECK(ZigbeeCodec::parseApsAcknowledgementFrame(
      encoded.data(), encodedLength, &parsed));
  CHECK(parsed.valid && parsed.ackFormatCommand && parsed.counter == 0x5AU);

  ZigbeeApsAcknowledgementFrame dataAck{};
  dataAck.destinationEndpoint = 2U;
  dataAck.clusterId = kZigbeeClusterOnOff;
  dataAck.profileId = kZigbeeProfileHomeAutomation;
  dataAck.sourceEndpoint = 1U;
  dataAck.counter = 0x6BU;
  CHECK(ZigbeeCodec::buildApsAcknowledgementFrame(
      dataAck, encoded.data(), encoded.size(), &encodedLength));
  const std::array<uint8_t, 8U> expectedDataAck = {
      0x02U, 0x02U, 0x06U, 0x00U, 0x04U, 0x01U, 0x01U, 0x6BU};
  CHECK(encodedLength == expectedDataAck.size());
  CHECK(std::memcmp(encoded.data(), expectedDataAck.data(),
                    expectedDataAck.size()) == 0);
  CHECK(ZigbeeCodec::parseApsAcknowledgementFrame(
      encoded.data(), encodedLength, &parsed));
  CHECK(parsed.valid && !parsed.ackFormatCommand &&
        parsed.destinationEndpoint == 2U && parsed.sourceEndpoint == 1U &&
        parsed.profileId == kZigbeeProfileHomeAutomation);

  ZigbeeApsAcknowledgementFrame zdoAck{};
  zdoAck.destinationEndpoint = 0U;
  zdoAck.clusterId = kZigbeeZdoActiveEndpointsResponse;
  zdoAck.profileId = kZigbeeProfileZdo;
  zdoAck.sourceEndpoint = 0U;
  zdoAck.counter = 0x7CU;
  CHECK(ZigbeeCodec::buildApsAcknowledgementFrame(
      zdoAck, encoded.data(), encoded.size(), &encodedLength));
  CHECK(encodedLength == 8U);
  CHECK(ZigbeeCodec::parseApsAcknowledgementFrame(
      encoded.data(), encodedLength, &parsed));
  CHECK(parsed.valid && parsed.profileId == kZigbeeProfileZdo &&
        parsed.destinationEndpoint == 0U && parsed.sourceEndpoint == 0U);

  std::array<uint8_t, 18U> rejectedOutput{};
  rejectedOutput.fill(0xA9U);
  const auto rejectedBefore = rejectedOutput;
  uint8_t rejectedLength = 0xA5U;
  dataAck.profileId = kZigbeeProfileZdo;
  CHECK(!ZigbeeCodec::buildApsAcknowledgementFrame(
      dataAck, &rejectedOutput[1], 16U, &rejectedLength));
  CHECK(rejectedLength == 0U && rejectedOutput == rejectedBefore);
  dataAck.profileId = kZigbeeProfileHomeAutomation;
  dataAck.destinationEndpoint = 0U;
  CHECK(!ZigbeeCodec::buildApsAcknowledgementFrame(
      dataAck, &rejectedOutput[1], 16U, &rejectedLength));
  CHECK(rejectedLength == 0U && rejectedOutput == rejectedBefore);
  dataAck.destinationEndpoint = 0xFFU;
  CHECK(!ZigbeeCodec::buildApsAcknowledgementFrame(
      dataAck, &rejectedOutput[1], 16U, &rejectedLength));
  CHECK(rejectedLength == 0U && rejectedOutput == rejectedBefore);

  auto invalidEndpointWire = expectedDataAck;
  invalidEndpointWire[1] = 0xFFU;
  CHECK(!ZigbeeCodec::parseApsAcknowledgementFrame(
      invalidEndpointWire.data(), invalidEndpointWire.size(), &parsed));
  CHECK(!parsed.valid);

  const std::array<uint8_t, 2U> commandWire = {0x12U, 0x5AU};
  for (const uint8_t invalidBit : {0x20U, 0x40U, 0x80U}) {
    auto invalid = commandWire;
    invalid[0] |= invalidBit;
    parsed.valid = true;
    parsed.counter = 0xEEU;
    CHECK(!ZigbeeCodec::parseApsAcknowledgementFrame(
        invalid.data(), invalid.size(), &parsed));
    CHECK(!parsed.valid && parsed.counter == 0U &&
          !parsed.securityEnabled);
  }
  const std::array<uint8_t, 3U> commandTrailing = {0x12U, 0x5AU, 0U};
  CHECK(!ZigbeeCodec::parseApsAcknowledgementFrame(
      commandTrailing.data(), commandTrailing.size(), &parsed));
  for (uint8_t length = 2U; length < expectedDataAck.size(); ++length) {
    CHECK(!ZigbeeCodec::parseApsAcknowledgementFrame(
        expectedDataAck.data(), length, &parsed));
    CHECK(!parsed.valid);
  }
  std::array<uint8_t, 9U> dataTrailing{};
  std::memcpy(dataTrailing.data(), expectedDataAck.data(),
              expectedDataAck.size());
  CHECK(!ZigbeeCodec::parseApsAcknowledgementFrame(
      dataTrailing.data(), dataTrailing.size(), &parsed));

  ZigbeeApsDataFrame request{};
  request.deliveryMode = kZigbeeApsDeliveryUnicast;
  request.destinationEndpoint = 1U;
  request.clusterId = kZigbeeClusterOnOff;
  request.profileId = kZigbeeProfileHomeAutomation;
  request.sourceEndpoint = 2U;
  request.ackRequested = true;
  request.counter = 0x8DU;
  CHECK(ZigbeeCodec::buildApsDataAcknowledgement(
      request, encoded.data(), encoded.size(), &encodedLength));
  CHECK(ZigbeeCodec::parseApsAcknowledgementFrame(
      encoded.data(), encodedLength, &parsed));
  CHECK(parsed.destinationEndpoint == request.sourceEndpoint &&
        parsed.sourceEndpoint == request.destinationEndpoint);
  request.ackRequested = false;
  CHECK(!ZigbeeCodec::buildApsDataAcknowledgement(
      request, encoded.data(), encoded.size(), &encodedLength));
  request.ackRequested = true;
  request.deliveryMode = kZigbeeApsDeliveryBroadcast;
  CHECK(!ZigbeeCodec::buildApsDataAcknowledgement(
      request, encoded.data(), encoded.size(), &encodedLength));
  request.deliveryMode = kZigbeeApsDeliveryUnicast;
  request.securityEnabled = true;
  CHECK(!ZigbeeCodec::buildApsDataAcknowledgement(
      request, encoded.data(), encoded.size(), &encodedLength));
}

void testExactSecurityTuplesAndFixedOutputs() {
  std::array<uint8_t, 18U> guardedKey{};
  guardedKey.fill(0xA7U);
  uint8_t* const unknownKey = &guardedKey[1];
  CHECK(!ZigbeeSecurity::loadZigbeeAlliance09LinkKey(unknownKey));
  CHECK(guardedKey.front() == 0xA7U && guardedKey.back() == 0xA7U);
  CHECK(ZigbeeSecurity::loadZigbeeAlliance09LinkKey(unknownKey, 16U));
  CHECK(guardedKey.front() == 0xA7U && guardedKey.back() == 0xA7U);

  const uint8_t installCode[18] = {
      0x10U, 0xACU, 0x01U, 0x01U, 0x24U, 0x4BU, 0x00U, 0xA1U, 0xB2U,
      0xC3U, 0xD4U, 0xE5U, 0xF6U, 0x07U, 0x18U, 0x29U, 0x43U, 0x6AU};
  guardedKey.fill(0xB8U);
  CHECK(!ZigbeeSecurity::deriveInstallCodeLinkKey(
      installCode, sizeof(installCode), &guardedKey[1], 15U));
  CHECK(guardedKey.front() == 0xB8U && guardedKey.back() == 0xB8U);
  CHECK(ZigbeeSecurity::deriveInstallCodeLinkKey(
      installCode, sizeof(installCode), &guardedKey[1], 16U));
  CHECK(guardedKey.front() == 0xB8U && guardedKey.back() == 0xB8U);

  std::array<uint8_t, 15U> guardedNonce{};
  guardedNonce.fill(0xC9U);
  CHECK(!ZigbeeSecurity::buildNwkNonce(
      0x00124B0001020304ULL, 1U, kZigbeeSecurityControlNwkEncMic32,
      &guardedNonce[1], 12U));
  CHECK(guardedNonce.front() == 0xC9U && guardedNonce.back() == 0xC9U);
  CHECK(ZigbeeSecurity::buildNwkNonce(
      0x00124B0001020304ULL, 1U, kZigbeeSecurityControlNwkEncMic32,
      &guardedNonce[1], 13U));
  CHECK(guardedNonce.front() == 0xC9U && guardedNonce.back() == 0xC9U);
  CHECK(guardedNonce[13] == 0x2DU);
  CHECK(!ZigbeeSecurity::buildNwkNonce(
      0U, 1U, kZigbeeSecurityControlNwkEncMic32, &guardedNonce[1], 13U));
  CHECK(!ZigbeeSecurity::buildNwkNonce(
      0x00124B0001020304ULL, UINT32_MAX,
      kZigbeeSecurityControlNwkEncMic32, &guardedNonce[1], 13U));

  ZigbeeApsTransportKey transport{};
  transport.valid = true;
  transport.keyType = kZigbeeApsTransportKeyStandardNetworkKey;
  for (uint8_t i = 0U; i < sizeof(transport.key); ++i) {
    transport.key[i] = static_cast<uint8_t>(0x30U + i);
  }
  transport.keySequence = 3U;
  transport.destinationIeee = 0x00124B0001020304ULL;
  transport.sourceIeee = 0x00124B0005060708ULL;

  std::array<uint8_t, 64U> plainTransport{};
  uint8_t plainTransportLength = 0U;
  CHECK(ZigbeeCodec::buildApsTransportKeyCommand(
      transport, 8U, plainTransport.data(), plainTransport.size(),
      &plainTransportLength));
  CHECK(plainTransportLength == 37U && plainTransport[0] == 0x41U &&
        (plainTransport[0] & 0x10U) == 0U &&
        (plainTransport[0] & 0x40U) != 0U);
  ZigbeeApsTransportKey parsedPlainTransport{};
  uint8_t parsedPlainCounter = 0U;
  CHECK(ZigbeeCodec::parseApsTransportKeyCommand(
      plainTransport.data(), plainTransportLength, &parsedPlainTransport,
      &parsedPlainCounter));
  CHECK(parsedPlainTransport.valid && parsedPlainCounter == 8U);
  for (uint8_t length = 0U; length < plainTransportLength; ++length) {
    parsedPlainTransport.valid = true;
    parsedPlainTransport.sourceIeee = UINT64_MAX;
    parsedPlainCounter = 0xA5U;
    CHECK(!ZigbeeCodec::parseApsTransportKeyCommand(
        plainTransport.data(), length, &parsedPlainTransport,
        &parsedPlainCounter));
    CHECK(!parsedPlainTransport.valid &&
          parsedPlainTransport.sourceIeee == 0U &&
          parsedPlainCounter == 0U);
  }
  plainTransport[plainTransportLength] = 0U;
  CHECK(!ZigbeeCodec::parseApsTransportKeyCommand(
      plainTransport.data(), static_cast<uint8_t>(plainTransportLength + 1U),
      &parsedPlainTransport, &parsedPlainCounter));
  CHECK(!parsedPlainTransport.valid && parsedPlainCounter == 0U);

  auto invalidPlainTransport = plainTransport;
  invalidPlainTransport[3] = 0xFFU;
  parsedPlainTransport.valid = true;
  parsedPlainTransport.keyType = 0xFFU;
  parsedPlainCounter = 0xA5U;
  CHECK(!ZigbeeCodec::parseApsTransportKeyCommand(
      invalidPlainTransport.data(), plainTransportLength,
      &parsedPlainTransport, &parsedPlainCounter));
  CHECK(!parsedPlainTransport.valid &&
        parsedPlainTransport.keyType ==
            kZigbeeApsTransportKeyStandardNetworkKey &&
        parsedPlainTransport.sourceIeee == 0U && parsedPlainCounter == 0U);

  for (const uint8_t controlXor : {0x10U, 0x40U}) {
    auto invalidControl = plainTransport;
    invalidControl[0] ^= controlXor;
    ZigbeeApsCommandFrame parsedCommand{};
    CHECK(!ZigbeeCodec::parseApsCommandFrame(
        invalidControl.data(), plainTransportLength, &parsedCommand));
    CHECK(!parsedCommand.valid);
  }
  auto invalidTransport = transport;
  invalidTransport.keyType = 0xFFU;
  plainTransport.fill(0xD8U);
  const auto plainTransportBefore = plainTransport;
  plainTransportLength = 0xA5U;
  CHECK(!ZigbeeCodec::buildApsTransportKeyCommand(
      invalidTransport, 8U, plainTransport.data(), plainTransport.size(),
      &plainTransportLength));
  CHECK(plainTransportLength == 0U &&
        plainTransport == plainTransportBefore);

  ZigbeeApsSecurityHeader apsSecurity{};
  apsSecurity.securityControl = kZigbeeSecurityControlApsKeyTransport;
  apsSecurity.frameCounter = 7U;
  apsSecurity.sourceIeee = transport.sourceIeee;
  std::array<uint8_t, 127U> securedTransport{};
  uint8_t securedTransportLength = 0U;
  CHECK(ZigbeeSecurity::buildSecuredApsTransportKeyCommand(
      transport, apsSecurity, &guardedKey[1], 9U, securedTransport.data(),
      static_cast<uint8_t>(securedTransport.size()),
      &securedTransportLength));
  CHECK(securedTransportLength > 2U && securedTransport[0] == 0x61U &&
        securedTransport[2] == 0x30U);
  const auto validSecuredTransport = securedTransport;
  const uint8_t validSecuredTransportLength = securedTransportLength;
  ZigbeeApsTransportKey parsedTransport{};
  ZigbeeApsSecurityHeader parsedApsSecurity{};
  uint8_t parsedCounter = 0U;
  CHECK(ZigbeeSecurity::parseSecuredApsTransportKeyCommand(
      securedTransport.data(), securedTransportLength, &guardedKey[1],
      &parsedTransport, &parsedApsSecurity, &parsedCounter));
  CHECK(parsedTransport.valid && parsedCounter == 9U &&
        parsedTransport.keySequence == transport.keySequence &&
        std::memcmp(parsedTransport.key, transport.key,
                    sizeof(transport.key)) == 0);
  securedTransport[securedTransportLength - 1U] ^= 0x01U;
  CHECK(!ZigbeeSecurity::parseSecuredApsTransportKeyCommand(
      securedTransport.data(), securedTransportLength, &guardedKey[1],
      &parsedTransport, &parsedApsSecurity, &parsedCounter));
  CHECK(!parsedTransport.valid && !parsedApsSecurity.valid &&
        parsedCounter == 0U);

  apsSecurity.securityControl = kZigbeeSecurityControlApsEncMic32;
  securedTransport.fill(0xE9U);
  const auto wrongTupleBefore = securedTransport;
  securedTransportLength = 0xA5U;
  CHECK(!ZigbeeSecurity::buildSecuredApsTransportKeyCommand(
      transport, apsSecurity, &guardedKey[1], 10U, securedTransport.data(),
      static_cast<uint8_t>(securedTransport.size()),
      &securedTransportLength));
  CHECK(securedTransportLength == 0U && securedTransport == wrongTupleBefore);
  apsSecurity.securityControl = kZigbeeSecurityControlApsKeyTransport;
  invalidTransport.keyType = 0xFFU;
  CHECK(!ZigbeeSecurity::buildSecuredApsTransportKeyCommand(
      invalidTransport, apsSecurity, &guardedKey[1], 10U,
      securedTransport.data(), static_cast<uint8_t>(securedTransport.size()),
      &securedTransportLength));

  securedTransport = validSecuredTransport;
  securedTransportLength = validSecuredTransportLength;

  ZigbeeApsUpdateDevice update{};
  update.valid = true;
  update.deviceIeee = 0x00124B000A0B0C0DULL;
  update.deviceShort = 0x3344U;
  update.status = 1U;
  const std::array<uint8_t, 11U> invalidUpdatePayload = {
      0x0DU, 0x0CU, 0x0BU, 0x0AU, 0x00U, 0x4BU,
      0x12U, 0x00U, 0x44U, 0x33U, 0x04U};
  ZigbeeApsCommandFrame updateCommand{};
  updateCommand.frameType = ZigbeeApsFrameType::kCommand;
  updateCommand.deliveryMode = kZigbeeApsDeliveryUnicast;
  updateCommand.ackRequested = true;
  updateCommand.counter = 12U;
  updateCommand.commandId = kZigbeeApsCommandUpdateDevice;
  std::array<uint8_t, 32U> invalidPlainUpdate{};
  uint8_t invalidPlainUpdateLength = 0U;
  CHECK(ZigbeeCodec::buildApsCommandFrame(
      updateCommand, invalidUpdatePayload.data(), invalidUpdatePayload.size(),
      invalidPlainUpdate.data(), invalidPlainUpdate.size(),
      &invalidPlainUpdateLength));
  ZigbeeApsUpdateDevice rejectedUpdate{};
  uint8_t rejectedUpdateCounter = 0xA5U;
  CHECK(!ZigbeeCodec::parseApsUpdateDeviceCommand(
      invalidPlainUpdate.data(), invalidPlainUpdateLength, &rejectedUpdate,
      &rejectedUpdateCounter));
  CHECK(!rejectedUpdate.valid && rejectedUpdate.status == 0U &&
        rejectedUpdateCounter == 0U);
  auto invalidUpdate = update;
  invalidUpdate.status = 4U;
  invalidPlainUpdate.fill(0xABU);
  const auto invalidPlainUpdateBefore = invalidPlainUpdate;
  invalidPlainUpdateLength = 0xA5U;
  CHECK(!ZigbeeCodec::buildApsUpdateDeviceCommand(
      invalidUpdate, 12U, invalidPlainUpdate.data(), invalidPlainUpdate.size(),
      &invalidPlainUpdateLength));
  CHECK(invalidPlainUpdateLength == 0U &&
        invalidPlainUpdate == invalidPlainUpdateBefore);

  apsSecurity.securityControl = kZigbeeSecurityControlApsEncMic32;
  apsSecurity.frameCounter = 11U;
  std::array<uint8_t, 127U> securedUpdate{};
  uint8_t securedUpdateLength = 0U;
  CHECK(ZigbeeSecurity::buildSecuredApsUpdateDeviceCommand(
      update, apsSecurity, &guardedKey[1], 12U, securedUpdate.data(),
      securedUpdate.size(), &securedUpdateLength));
  CHECK(securedUpdateLength > 2U && securedUpdate[0] == 0x61U &&
        securedUpdate[2] == 0x20U);
  const auto validSecuredUpdate = securedUpdate;
  const uint8_t validSecuredUpdateLength = securedUpdateLength;
  ZigbeeApsUpdateDevice parsedUpdate{};
  parsedCounter = 0U;
  CHECK(ZigbeeSecurity::parseSecuredApsUpdateDeviceCommand(
      securedUpdate.data(), securedUpdateLength, &guardedKey[1], &parsedUpdate,
      &parsedApsSecurity, &parsedCounter));
  CHECK(parsedUpdate.valid && parsedUpdate.deviceIeee == update.deviceIeee &&
        parsedUpdate.deviceShort == update.deviceShort &&
        parsedUpdate.status == update.status && parsedCounter == 12U &&
        parsedApsSecurity.securityControl == 0x20U);

  std::array<uint8_t, 127U> invalidSecuredUpdate{};
  uint8_t invalidSecuredUpdateLength = 0U;
  CHECK(ZigbeeSecurity::buildSecuredApsCommandFrame(
      updateCommand, apsSecurity, &guardedKey[1], invalidUpdatePayload.data(),
      invalidUpdatePayload.size(), invalidSecuredUpdate.data(),
      invalidSecuredUpdate.size(), &invalidSecuredUpdateLength));
  rejectedUpdate.valid = true;
  rejectedUpdate.status = 0xFFU;
  parsedApsSecurity.valid = true;
  rejectedUpdateCounter = 0xA5U;
  CHECK(!ZigbeeSecurity::parseSecuredApsUpdateDeviceCommand(
      invalidSecuredUpdate.data(), invalidSecuredUpdateLength, &guardedKey[1],
      &rejectedUpdate, &parsedApsSecurity, &rejectedUpdateCounter));
  CHECK(!rejectedUpdate.valid && rejectedUpdate.status == 0U &&
        !parsedApsSecurity.valid && rejectedUpdateCounter == 0U);
  invalidSecuredUpdate.fill(0xBCU);
  const auto invalidSecuredUpdateBefore = invalidSecuredUpdate;
  invalidSecuredUpdateLength = 0xA5U;
  CHECK(!ZigbeeSecurity::buildSecuredApsUpdateDeviceCommand(
      invalidUpdate, apsSecurity, &guardedKey[1], 12U,
      invalidSecuredUpdate.data(), invalidSecuredUpdate.size(),
      &invalidSecuredUpdateLength));
  CHECK(invalidSecuredUpdateLength == 0U &&
        invalidSecuredUpdate == invalidSecuredUpdateBefore);

  for (const uint8_t controlXor : {0x10U, 0x40U}) {
    securedUpdate = validSecuredUpdate;
    securedUpdate[0] ^= controlXor;
    parsedUpdate.valid = true;
    parsedUpdate.deviceIeee = UINT64_MAX;
    parsedApsSecurity.valid = true;
    parsedCounter = 0xA5U;
    CHECK(!ZigbeeSecurity::parseSecuredApsUpdateDeviceCommand(
        securedUpdate.data(), validSecuredUpdateLength, &guardedKey[1],
        &parsedUpdate, &parsedApsSecurity, &parsedCounter));
    CHECK(!parsedUpdate.valid && parsedUpdate.deviceIeee == 0U &&
          !parsedApsSecurity.valid && parsedCounter == 0U);
  }
  securedUpdate = validSecuredUpdate;
  securedUpdate[validSecuredUpdateLength - 1U] ^= 0x01U;
  CHECK(!ZigbeeSecurity::parseSecuredApsUpdateDeviceCommand(
      securedUpdate.data(), validSecuredUpdateLength, &guardedKey[1],
      &parsedUpdate, &parsedApsSecurity, &parsedCounter));
  CHECK(!parsedUpdate.valid && !parsedApsSecurity.valid &&
        parsedCounter == 0U);

  apsSecurity.securityControl = kZigbeeSecurityControlApsKeyTransport;
  securedUpdate.fill(0xEAU);
  const auto updateWrongTupleBefore = securedUpdate;
  securedUpdateLength = 0xA5U;
  CHECK(!ZigbeeSecurity::buildSecuredApsUpdateDeviceCommand(
      update, apsSecurity, &guardedKey[1], 12U, securedUpdate.data(),
      securedUpdate.size(), &securedUpdateLength));
  CHECK(securedUpdateLength == 0U &&
        securedUpdate == updateWrongTupleBefore);

  apsSecurity.securityControl = kZigbeeSecurityControlApsEncMic32;
  apsSecurity.frameCounter = UINT32_MAX;
  CHECK(!ZigbeeSecurity::buildSecuredApsUpdateDeviceCommand(
      update, apsSecurity, &guardedKey[1], 12U, securedUpdate.data(),
      securedUpdate.size(), &securedUpdateLength));
  apsSecurity.frameCounter = 13U;

  ZigbeeApsSwitchKey switchKey{};
  switchKey.valid = true;
  switchKey.keySequence = 4U;
  std::array<uint8_t, 127U> securedSwitch{};
  uint8_t securedSwitchLength = 0U;
  CHECK(ZigbeeSecurity::buildSecuredApsSwitchKeyCommand(
      switchKey, apsSecurity, &guardedKey[1], 14U, securedSwitch.data(),
      securedSwitch.size(), &securedSwitchLength));
  CHECK(securedSwitchLength > 2U && securedSwitch[0] == 0x61U &&
        securedSwitch[2] == 0x20U);
  ZigbeeApsSwitchKey parsedSwitch{};
  CHECK(ZigbeeSecurity::parseSecuredApsSwitchKeyCommand(
      securedSwitch.data(), securedSwitchLength, &guardedKey[1], &parsedSwitch,
      &parsedApsSecurity, &parsedCounter));
  CHECK(parsedSwitch.valid && parsedSwitch.keySequence == switchKey.keySequence &&
        parsedCounter == 14U);
  securedSwitch[securedSwitchLength - 1U] ^= 0x01U;
  CHECK(!ZigbeeSecurity::parseSecuredApsSwitchKeyCommand(
      securedSwitch.data(), securedSwitchLength, &guardedKey[1], &parsedSwitch,
      &parsedApsSecurity, &parsedCounter));
  CHECK(!parsedSwitch.valid && !parsedApsSecurity.valid &&
        parsedCounter == 0U);

  apsSecurity.securityControl = kZigbeeSecurityControlApsKeyTransport;
  securedSwitch.fill(0xFBU);
  const auto switchWrongTupleBefore = securedSwitch;
  securedSwitchLength = 0xA5U;
  CHECK(!ZigbeeSecurity::buildSecuredApsSwitchKeyCommand(
      switchKey, apsSecurity, &guardedKey[1], 14U, securedSwitch.data(),
      securedSwitch.size(), &securedSwitchLength));
  CHECK(securedSwitchLength == 0U &&
        securedSwitch == switchWrongTupleBefore);

  ZigbeeApsCommandFrame securedCommand{};
  securedCommand.frameType = ZigbeeApsFrameType::kCommand;
  securedCommand.deliveryMode = kZigbeeApsDeliveryUnicast;
  securedCommand.ackRequested = false;
  securedCommand.counter = 15U;
  securedCommand.commandId = kZigbeeApsCommandUpdateDevice;
  apsSecurity.securityControl = kZigbeeSecurityControlApsEncMic32;
  apsSecurity.frameCounter = 15U;
  CHECK(!ZigbeeSecurity::buildSecuredApsCommandFrame(
      securedCommand, apsSecurity, &guardedKey[1], nullptr, 0U,
      securedUpdate.data(), securedUpdate.size(), &securedUpdateLength));

  const uint8_t officialInstallCode[18] = {
      0x88U, 0x77U, 0x66U, 0x55U, 0x44U, 0x33U, 0x22U, 0x11U, 0x11U,
      0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U, 0xD4U, 0x90U};
  const uint8_t officialDerivedKey[16] = {
      0xFAU, 0x80U, 0x81U, 0xCAU, 0xAAU, 0x41U, 0xD5U, 0xADU,
      0xE9U, 0xB5U, 0x65U, 0x87U, 0x99U, 0x26U, 0x8BU, 0x88U};
  uint8_t derivedKey[16] = {0U};
  CHECK(ZigbeeSecurity::calculateInstallCodeCrc(officialInstallCode, 16U) ==
        0x90D4U);
  CHECK(ZigbeeSecurity::deriveInstallCodeLinkKey(
      officialInstallCode, sizeof(officialInstallCode), derivedKey));
  CHECK(std::memcmp(derivedKey, officialDerivedKey,
                    sizeof(officialDerivedKey)) == 0);

  ZigbeeNwkSecurityHeader header{};
  header.sourceIeee = 0x00124B0001020304ULL;
  uint8_t encoded[16] = {0U};
  uint8_t encodedLength = 0U;
  header.securityControl =
      static_cast<uint8_t>(kZigbeeSecurityControlNwkEncMic32 | 0x40U);
  CHECK(!ZigbeeSecurity::buildNwkSecurityHeader(header, encoded,
                                                &encodedLength));
  header.securityControl = static_cast<uint8_t>(
      kZigbeeSecurityControlNwkEncMic32 & static_cast<uint8_t>(~0x20U));
  CHECK(!ZigbeeSecurity::buildNwkSecurityHeader(header, encoded,
                                                &encodedLength));

  ZigbeeApsCommandFrame command{};
  command.frameType = ZigbeeApsFrameType::kCommand;
  command.deliveryMode = kZigbeeApsDeliveryUnicast;
  command.securityEnabled = true;
  command.counter = 1U;
  command.commandId = kZigbeeApsCommandSwitchKey;
  CHECK(!ZigbeeCodec::buildApsCommandFrame(command, nullptr, 0U, encoded,
                                            &encodedLength));
  command.securityEnabled = false;
  command.ackRequested = false;
  std::memset(encoded, 0xA4, sizeof(encoded));
  uint8_t encodedBefore[sizeof(encoded)] = {0U};
  std::memcpy(encodedBefore, encoded, sizeof(encoded));
  encodedLength = 0xA5U;
  CHECK(!ZigbeeCodec::buildApsCommandFrame(command, nullptr, 0U, encoded,
                                            &encodedLength));
  CHECK(encodedLength == 0U &&
        std::memcmp(encoded, encodedBefore, sizeof(encoded)) == 0);
  command.ackRequested = true;
  CHECK(ZigbeeCodec::buildApsCommandFrame(command, nullptr, 0U, encoded,
                                           &encodedLength));
  CHECK(encodedLength == 3U && encoded[0] == 0x41U);
  const uint8_t fakeSecuredCommand[3] = {
      static_cast<uint8_t>(static_cast<uint8_t>(ZigbeeApsFrameType::kCommand) |
                           (1U << 5U)),
      1U, kZigbeeApsCommandSwitchKey};
  ZigbeeApsCommandFrame parsedCommand{};
  CHECK(!ZigbeeCodec::parseApsCommandFrame(
      fakeSecuredCommand, sizeof(fakeSecuredCommand), &parsedCommand));
  CHECK(!parsedCommand.securityEnabled && !parsedCommand.valid);
}

}  // namespace

int main() {
  testConfigureReportingTruncations();
  testDiscreteReportingTypes();
  testCcmKnownAnswerVector();
  testCcmCapacityBoundaries();
  testCcmAuthenticationAndAliasing();
  testCapacityAwareSecurityApis();
  testMalformedCodecInputs();
  testSecuredBuilderLengthWraps();
  testAuxiliarySecurityHeaderContracts();
  testApsDataTupleContracts();
  testApsAcknowledgementContracts();
  testExactSecurityTuplesAndFixedOutputs();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d Zigbee regression checks failed\n", g_failures);
    return 1;
  }
  std::puts("PASS Zigbee reporting truncation and type classification");
  std::puts("PASS Zigbee malformed-frame and enum rejection");
  std::puts("PASS Zigbee authenticated CCM* staging and aliasing");
  std::puts("PASS Zigbee capacity-aware security API canaries");
  return 0;
}
"""


def main() -> None:
    compiler = os.environ.get("CXX", "g++")
    if shutil.which(compiler) is None:
        raise SystemExit(f"C++ compiler not found: {compiler}")

    with tempfile.TemporaryDirectory(prefix="nrf54-zigbee-regression-") as temp:
        temporary = Path(temp)
        (temporary / "Arduino.h").write_text(ARDUINO_STUB, encoding="utf-8")
        harness = temporary / "zigbee_codec_security_test.cpp"
        harness.write_text(HARNESS, encoding="utf-8")
        binary = temporary / "zigbee_codec_security_test"

        command = [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-no-pie",
            "-ffunction-sections",
            "-fdata-sections",
            "-DNRF54L15_CLEAN_ZIGBEE_ENABLED=1",
            f"-I{temporary}",
            f"-I{SOURCE}",
            str(SOURCE / "zigbee_stack.cpp"),
            str(SOURCE / "zigbee_security.cpp"),
            str(harness),
            "-Wl,--gc-sections",
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
