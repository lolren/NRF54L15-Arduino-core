#pragma once

#include <stdint.h>

#include "zigbee_stack.h"

namespace xiao_nrf54l15 {

constexpr uint8_t kZigbeeSecurityLevelEncMic32 = 0x05U;
constexpr uint8_t kZigbeeSecurityKeyIdData = 0x00U;
constexpr uint8_t kZigbeeSecurityKeyIdNetwork = 0x08U;
constexpr uint8_t kZigbeeSecurityKeyIdKeyTransport = 0x10U;
constexpr uint8_t kZigbeeSecurityExtendedNonce = 0x20U;
// Zigbee writes APS security-level bits as zero on the wire; CCM* still uses
// ENC-MIC-32 internally after the auxiliary header has been authenticated.
constexpr uint8_t kZigbeeSecurityControlApsEncMic32 =
    static_cast<uint8_t>(kZigbeeSecurityKeyIdData |
                         kZigbeeSecurityExtendedNonce);
constexpr uint8_t kZigbeeSecurityControlApsKeyTransport =
    static_cast<uint8_t>(kZigbeeSecurityKeyIdKeyTransport |
                         kZigbeeSecurityExtendedNonce);
constexpr uint8_t kZigbeeSecurityControlNwkEncMic32 =
    static_cast<uint8_t>(kZigbeeSecurityKeyIdNetwork |
                         kZigbeeSecurityExtendedNonce);
static_assert(kZigbeeSecurityControlApsEncMic32 == 0x20U,
              "APS data security control must match the Zigbee wire value");
static_assert(kZigbeeSecurityControlApsKeyTransport == 0x30U,
              "APS key-transport control must match the Zigbee wire value");
static_assert(kZigbeeSecurityControlNwkEncMic32 == 0x28U,
              "NWK security control must match the Zigbee wire value");

template <typename T>
struct ZigbeeSecurityOutputCapacity;

template <size_t N>
struct ZigbeeSecurityOutputCapacity<uint8_t[N]> {
  static constexpr uint8_t value =
      static_cast<uint8_t>(N > 255U ? 255U : N);
};

template <>
struct [[deprecated("pass an explicit Zigbee security output capacity")]]
    ZigbeeSecurityOutputCapacity<uint8_t*> {
#if defined(NRF54L15_CLEAN_ZIGBEE_REQUIRE_EXPLICIT_OUTPUT_CAPACITY)
  // No value in strict repository builds: every pointer must carry capacity.
#else
  static constexpr uint8_t value = 0U;
#endif
};

template <typename Output>
constexpr uint8_t zigbeeSecurityOutputCapacity() {
  return ZigbeeSecurityOutputCapacity<
      typename ZigbeeRemoveCv<
          typename ZigbeeRemoveReference<Output>::Type>::Type>::value;
}

struct ZigbeeNwkSecurityHeader {
  bool valid = false;
  uint8_t securityControl = kZigbeeSecurityControlNwkEncMic32;
  uint32_t frameCounter = 0U;
  uint64_t sourceIeee = 0U;
  uint8_t keySequence = 0U;
  uint8_t micLength = 4U;
};

struct ZigbeeApsSecurityHeader {
  bool valid = false;
  uint8_t securityControl = kZigbeeSecurityControlApsEncMic32;
  uint32_t frameCounter = 0U;
  uint64_t sourceIeee = 0U;
  uint8_t keySequence = 0U;
  uint8_t micLength = 4U;
};

class ZigbeeSecurity {
 public:
  template <typename ZigbeeOutput>
  static bool loadZigbeeAlliance09LinkKey(ZigbeeOutput&& outKey) {
    return loadZigbeeAlliance09LinkKey(
        outKey, zigbeeSecurityOutputCapacity<ZigbeeOutput>());
  }
  static bool loadZigbeeAlliance09LinkKey(uint8_t* outKey,
                                          uint8_t outKeyCapacity);
  static uint16_t calculateInstallCodeCrc(const uint8_t* installCode,
                                          uint8_t lengthWithoutCrc);
  static bool validateInstallCode(const uint8_t* installCode, uint8_t length);
  template <typename ZigbeeOutput>
  static bool deriveInstallCodeLinkKey(const uint8_t* installCode,
                                       uint8_t length,
                                       ZigbeeOutput&& outKey) {
    return deriveInstallCodeLinkKey(
        installCode, length, outKey,
        zigbeeSecurityOutputCapacity<ZigbeeOutput>());
  }
  static bool deriveInstallCodeLinkKey(const uint8_t* installCode,
                                       uint8_t length, uint8_t* outKey,
                                       uint8_t outKeyCapacity);

  // Accepts an on-wire or crypto-expanded security control and always emits
  // the CCM nonce's crypto-expanded security-level value.
  template <typename ZigbeeOutput>
  static bool buildNwkNonce(uint64_t sourceIeee, uint32_t frameCounter,
                            uint8_t securityControl,
                            ZigbeeOutput&& outNonce) {
    return buildNwkNonce(sourceIeee, frameCounter, securityControl, outNonce,
                         zigbeeSecurityOutputCapacity<ZigbeeOutput>());
  }
  static bool buildNwkNonce(uint64_t sourceIeee, uint32_t frameCounter,
                            uint8_t securityControl, uint8_t* outNonce,
                            uint8_t outNonceCapacity);
  template <typename ZigbeeOutput>
  static bool buildNwkSecurityHeader(const ZigbeeNwkSecurityHeader& header,
                                     ZigbeeOutput&& outHeader,
                                     uint8_t* outHeaderLength) {
    return buildNwkSecurityHeader(
        header, outHeader, zigbeeSecurityOutputCapacity<ZigbeeOutput>(),
        outHeaderLength);
  }
  static bool buildNwkSecurityHeader(const ZigbeeNwkSecurityHeader& header,
                                     uint8_t* outHeader,
                                     uint8_t outHeaderCapacity,
                                     uint8_t* outHeaderLength);
  static bool parseNwkSecurityHeader(const uint8_t* data, uint8_t length,
                                     ZigbeeNwkSecurityHeader* outHeader,
                                     uint8_t* outHeaderLength);
  template <typename ZigbeeOutput>
  static bool buildApsSecurityHeader(const ZigbeeApsSecurityHeader& header,
                                     ZigbeeOutput&& outHeader,
                                     uint8_t* outHeaderLength) {
    return buildApsSecurityHeader(
        header, outHeader, zigbeeSecurityOutputCapacity<ZigbeeOutput>(),
        outHeaderLength);
  }
  static bool buildApsSecurityHeader(const ZigbeeApsSecurityHeader& header,
                                     uint8_t* outHeader,
                                     uint8_t outHeaderCapacity,
                                     uint8_t* outHeaderLength);
  static bool parseApsSecurityHeader(const uint8_t* data, uint8_t length,
                                     ZigbeeApsSecurityHeader* outHeader,
                                     uint8_t* outHeaderLength);

  template <typename ZigbeeOutput>
  static bool encryptCcmStar(
      const uint8_t key[16], const uint8_t nonce[13], const uint8_t* aad,
      uint8_t aadLength, const uint8_t* plaintext, uint8_t plaintextLength,
      ZigbeeOutput&& outCiphertextWithMic,
      uint8_t* outCiphertextWithMicLength) {
    return encryptCcmStar(
        key, nonce, aad, aadLength, plaintext, plaintextLength,
        outCiphertextWithMic,
        zigbeeSecurityOutputCapacity<ZigbeeOutput>(),
        outCiphertextWithMicLength);
  }
  static bool encryptCcmStar(const uint8_t key[16], const uint8_t nonce[13],
                             const uint8_t* aad, uint8_t aadLength,
                             const uint8_t* plaintext, uint8_t plaintextLength,
                             uint8_t* outCiphertextWithMic,
                             uint8_t outCiphertextWithMicCapacity,
                             uint8_t* outCiphertextWithMicLength);
  template <typename ZigbeeOutput>
  static bool decryptCcmStar(
      const uint8_t key[16], const uint8_t nonce[13], const uint8_t* aad,
      uint8_t aadLength, const uint8_t* ciphertextWithMic,
      uint8_t ciphertextWithMicLength, ZigbeeOutput&& outPlaintext,
      uint8_t* outPlaintextLength) {
    return decryptCcmStar(
        key, nonce, aad, aadLength, ciphertextWithMic,
        ciphertextWithMicLength, outPlaintext,
        zigbeeSecurityOutputCapacity<ZigbeeOutput>(), outPlaintextLength);
  }
  static bool decryptCcmStar(const uint8_t key[16], const uint8_t nonce[13],
                             const uint8_t* aad, uint8_t aadLength,
                             const uint8_t* ciphertextWithMic,
                             uint8_t ciphertextWithMicLength,
                             uint8_t* outPlaintext,
                             uint8_t outPlaintextCapacity,
                             uint8_t* outPlaintextLength);

  template <typename ZigbeeOutput>
  static bool buildSecuredNwkFrame(
      const ZigbeeNetworkFrame& frame,
      const ZigbeeNwkSecurityHeader& security, const uint8_t key[16],
      const uint8_t* payload, uint8_t payloadLength, ZigbeeOutput&& outFrame,
      uint8_t* outLength) {
    return buildSecuredNwkFrame(
        frame, security, key, payload, payloadLength, outFrame,
        zigbeeSecurityOutputCapacity<ZigbeeOutput>(), outLength);
  }
  static bool buildSecuredNwkFrame(const ZigbeeNetworkFrame& frame,
                                   const ZigbeeNwkSecurityHeader& security,
                                   const uint8_t key[16],
                                   const uint8_t* payload,
                                   uint8_t payloadLength, uint8_t* outFrame,
                                   uint8_t outFrameCapacity,
                                   uint8_t* outLength);
  template <typename ZigbeeOutput>
  static bool parseSecuredNwkFrame(
      const uint8_t* frame, uint8_t length, const uint8_t key[16],
      ZigbeeNetworkFrame* outFrame, ZigbeeNwkSecurityHeader* outSecurity,
      ZigbeeOutput&& outPayload, uint8_t* outPayloadLength,
      uint64_t defaultSourceIeee = 0U) {
    return parseSecuredNwkFrame(
        frame, length, key, outFrame, outSecurity, outPayload,
        zigbeeSecurityOutputCapacity<ZigbeeOutput>(), outPayloadLength,
        defaultSourceIeee);
  }
  static bool parseSecuredNwkFrame(
      const uint8_t* frame, uint8_t length, const uint8_t key[16],
      ZigbeeNetworkFrame* outFrame, ZigbeeNwkSecurityHeader* outSecurity,
      uint8_t* outPayload, uint8_t outPayloadCapacity,
      uint8_t* outPayloadLength, uint64_t defaultSourceIeee = 0U);
  template <typename ZigbeeOutput>
  static bool buildSecuredApsCommandFrame(
      const ZigbeeApsCommandFrame& frame,
      const ZigbeeApsSecurityHeader& security, const uint8_t key[16],
      const uint8_t* payload, uint8_t payloadLength, ZigbeeOutput&& outFrame,
      uint8_t* outLength) {
    return buildSecuredApsCommandFrame(
        frame, security, key, payload, payloadLength, outFrame,
        zigbeeSecurityOutputCapacity<ZigbeeOutput>(), outLength);
  }
  static bool buildSecuredApsCommandFrame(
      const ZigbeeApsCommandFrame& frame,
      const ZigbeeApsSecurityHeader& security, const uint8_t key[16],
      const uint8_t* payload, uint8_t payloadLength, uint8_t* outFrame,
      uint8_t outFrameCapacity, uint8_t* outLength);
  template <typename ZigbeeOutput>
  static bool parseSecuredApsCommandFrame(
      const uint8_t* frame, uint8_t length, const uint8_t key[16],
      ZigbeeApsCommandFrame* outFrame, ZigbeeApsSecurityHeader* outSecurity,
      ZigbeeOutput&& outPayload, uint8_t* outPayloadLength) {
    return parseSecuredApsCommandFrame(
        frame, length, key, outFrame, outSecurity, outPayload,
        zigbeeSecurityOutputCapacity<ZigbeeOutput>(), outPayloadLength);
  }
  static bool parseSecuredApsCommandFrame(
      const uint8_t* frame, uint8_t length, const uint8_t key[16],
      ZigbeeApsCommandFrame* outFrame, ZigbeeApsSecurityHeader* outSecurity,
      uint8_t* outPayload, uint8_t outPayloadCapacity,
      uint8_t* outPayloadLength);
  template <typename ZigbeeOutput>
  static bool buildSecuredApsTransportKeyCommand(
      const ZigbeeApsTransportKey& transportKey,
      const ZigbeeApsSecurityHeader& security, const uint8_t key[16],
      uint8_t counter, ZigbeeOutput&& outFrame, uint8_t* outLength) {
    return buildSecuredApsTransportKeyCommand(
        transportKey, security, key, counter, outFrame,
        zigbeeSecurityOutputCapacity<ZigbeeOutput>(), outLength);
  }
  static bool buildSecuredApsTransportKeyCommand(
      const ZigbeeApsTransportKey& transportKey,
      const ZigbeeApsSecurityHeader& security, const uint8_t key[16],
      uint8_t counter, uint8_t* outFrame, uint8_t outFrameCapacity,
      uint8_t* outLength);
  static bool parseSecuredApsTransportKeyCommand(
      const uint8_t* frame, uint8_t length, const uint8_t key[16],
      ZigbeeApsTransportKey* outTransportKey,
      ZigbeeApsSecurityHeader* outSecurity, uint8_t* outCounter);
  template <typename ZigbeeOutput>
  static bool buildSecuredApsUpdateDeviceCommand(
      const ZigbeeApsUpdateDevice& updateDevice,
      const ZigbeeApsSecurityHeader& security, const uint8_t key[16],
      uint8_t counter, ZigbeeOutput&& outFrame, uint8_t* outLength) {
    return buildSecuredApsUpdateDeviceCommand(
        updateDevice, security, key, counter, outFrame,
        zigbeeSecurityOutputCapacity<ZigbeeOutput>(), outLength);
  }
  static bool buildSecuredApsUpdateDeviceCommand(
      const ZigbeeApsUpdateDevice& updateDevice,
      const ZigbeeApsSecurityHeader& security, const uint8_t key[16],
      uint8_t counter, uint8_t* outFrame, uint8_t outFrameCapacity,
      uint8_t* outLength);
  static bool parseSecuredApsUpdateDeviceCommand(
      const uint8_t* frame, uint8_t length, const uint8_t key[16],
      ZigbeeApsUpdateDevice* outUpdateDevice,
      ZigbeeApsSecurityHeader* outSecurity, uint8_t* outCounter);
  template <typename ZigbeeOutput>
  static bool buildSecuredApsSwitchKeyCommand(
      const ZigbeeApsSwitchKey& switchKey,
      const ZigbeeApsSecurityHeader& security, const uint8_t key[16],
      uint8_t counter, ZigbeeOutput&& outFrame, uint8_t* outLength) {
    return buildSecuredApsSwitchKeyCommand(
        switchKey, security, key, counter, outFrame,
        zigbeeSecurityOutputCapacity<ZigbeeOutput>(), outLength);
  }
  static bool buildSecuredApsSwitchKeyCommand(
      const ZigbeeApsSwitchKey& switchKey,
      const ZigbeeApsSecurityHeader& security, const uint8_t key[16],
      uint8_t counter, uint8_t* outFrame, uint8_t outFrameCapacity,
      uint8_t* outLength);
  static bool parseSecuredApsSwitchKeyCommand(
      const uint8_t* frame, uint8_t length, const uint8_t key[16],
      ZigbeeApsSwitchKey* outSwitchKey, ZigbeeApsSecurityHeader* outSecurity,
      uint8_t* outCounter);
};

}  // namespace xiao_nrf54l15
