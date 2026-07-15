#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && NRF54L15_CLEAN_MATTER_CORE_ENABLE
#include "matter_pase_commissioning.h"

#include <Arduino.h>
#include <string.h>

namespace xiao_nrf54l15 {
namespace {

constexpr uint16_t kProtocolSecureChannel =
    static_cast<uint16_t>(MatterMessageProtocol::kSecureChannel);

bool readUint16Le(const uint8_t* data, size_t offset,
                  size_t maxLength, uint16_t* outValue) {
  if (data == nullptr || outValue == nullptr ||
      (offset + 2U) > maxLength) {
    return false;
  }
  *outValue = static_cast<uint16_t>(data[offset]) |
              (static_cast<uint16_t>(data[offset + 1U]) << 8U);
  return true;
}

bool readUint32Le(const uint8_t* data, size_t offset,
                  size_t maxLength, uint32_t* outValue) {
  if (data == nullptr || outValue == nullptr ||
      (offset + 4U) > maxLength) {
    return false;
  }
  *outValue = static_cast<uint32_t>(data[offset]) |
              (static_cast<uint32_t>(data[offset + 1U]) << 8U) |
              (static_cast<uint32_t>(data[offset + 2U]) << 16U) |
              (static_cast<uint32_t>(data[offset + 3U]) << 24U);
  return true;
}

void writeUint16Le(uint16_t value, uint8_t* out, size_t offset) {
  out[offset] = static_cast<uint8_t>(value & 0xFFU);
  out[offset + 1U] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

void writeUint32Le(uint32_t value, uint8_t* out, size_t offset) {
  out[offset] = static_cast<uint8_t>(value & 0xFFU);
  out[offset + 1U] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
  out[offset + 2U] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
  out[offset + 3U] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

// Domain separators for this explicitly non-standard PASE-like surface.
constexpr char kSpake2pContextProliferation[] = "SPAKE2P Key Salt";
constexpr char kSpake2pContextAlpha[] = "PASE-EXPERIMENTAL-CONFIRM-A";
constexpr char kSpake2pContextBeta[] = "PASE-EXPERIMENTAL-CONFIRM-B";
constexpr char kSpake2pKeyAlpha[] = "PASE-EXPERIMENTAL-KCA";
constexpr char kSpake2pKeyBeta[] = "PASE-EXPERIMENTAL-KCB";
constexpr size_t kPbkdfPayloadSize =
    32U + 2U + 2U + 1U + kMatterSpake2pSaltSize + 4U;

bool constantTimeEqual(const uint8_t* lhs, const uint8_t* rhs, size_t length) {
  if ((lhs == nullptr || rhs == nullptr) && length != 0U) {
    return false;
  }
  uint8_t difference = 0U;
  for (size_t i = 0U; i < length; ++i) {
    difference |= static_cast<uint8_t>(lhs[i] ^ rhs[i]);
  }
  return difference == 0U;
}

void secureZero(void* data, size_t length) {
  volatile uint8_t* bytes = static_cast<volatile uint8_t*>(data);
  while (length > 0U) {
    *bytes++ = 0U;
    --length;
  }
}

bool allZero(const uint8_t* data, size_t length) {
  if (data == nullptr) {
    return true;
  }
  uint8_t combined = 0U;
  for (size_t i = 0U; i < length; ++i) {
    combined |= data[i];
  }
  return combined == 0U;
}

bool validPbkdfRequestPayload(const uint8_t* payload, uint16_t length) {
  if (payload == nullptr || length != kPbkdfPayloadSize ||
      allZero(payload, 32U)) {
    return false;
  }

  size_t offset = 32U;
  uint16_t sessionId = 0U;
  uint16_t passcodeId = 0U;
  if (!readUint16Le(payload, offset, length, &sessionId)) {
    return false;
  }
  offset += 2U;
  if (!readUint16Le(payload, offset, length, &passcodeId)) {
    return false;
  }
  offset += 2U;
  if (sessionId == 0U || passcodeId != 0U || payload[offset++] != 1U) {
    return false;
  }
  offset += kMatterSpake2pSaltSize;
  uint32_t iterations = 0U;
  return readUint32Le(payload, offset, length, &iterations) &&
         iterations >= kMatterSpake2pMinPbkdf2Iterations &&
         iterations <= kMatterSpake2pMaxPbkdf2Iterations;
}

size_t buildConfirmationInput(const MatterPaseSessionState& session,
                              bool localIsInitiator, bool confirmationA,
                              uint8_t* output, size_t capacity) {
  const char* domain =
      confirmationA ? kSpake2pContextAlpha : kSpake2pContextBeta;
  const size_t domainLength = strlen(domain);
  const size_t required =
      domainLength + (2U * kMatterSpake2pPointSize) + 64U + 4U;
  if (output == nullptr || capacity < required) {
    return 0U;
  }

  const uint16_t initiatorSessionId =
      localIsInitiator ? session.localSessionId : session.peerSessionId;
  const uint16_t responderSessionId =
      localIsInitiator ? session.peerSessionId : session.localSessionId;
  size_t offset = 0U;
  memcpy(output + offset, domain, domainLength);
  offset += domainLength;

  if (confirmationA) {
    memcpy(output + offset, session.X, sizeof(session.X));
    offset += sizeof(session.X);
    memcpy(output + offset, session.Y, sizeof(session.Y));
    offset += sizeof(session.Y);
    memcpy(output + offset, session.initiateRandom,
           sizeof(session.initiateRandom));
    offset += sizeof(session.initiateRandom);
    memcpy(output + offset, session.respondRandom,
           sizeof(session.respondRandom));
    offset += sizeof(session.respondRandom);
    writeUint16Le(initiatorSessionId, output, offset);
    offset += 2U;
    writeUint16Le(responderSessionId, output, offset);
    offset += 2U;
  } else {
    memcpy(output + offset, session.Y, sizeof(session.Y));
    offset += sizeof(session.Y);
    memcpy(output + offset, session.X, sizeof(session.X));
    offset += sizeof(session.X);
    memcpy(output + offset, session.respondRandom,
           sizeof(session.respondRandom));
    offset += sizeof(session.respondRandom);
    memcpy(output + offset, session.initiateRandom,
           sizeof(session.initiateRandom));
    offset += sizeof(session.initiateRandom);
    writeUint16Le(responderSessionId, output, offset);
    offset += 2U;
    writeUint16Le(initiatorSessionId, output, offset);
    offset += 2U;
  }
  return offset;
}

// Derive w0s, w1s from passcode using Matter's formula:
// w0s = PBKDF2(passcode, salt || "SPAKE2P Key Salt", iterations, 32)
// w1s = PBKDF2(passcode, salt || w0s || "SPAKE2P Key Salt", iterations, 32)
// Then reduce mod n.
bool spake2pDeriveWS(
    uint32_t passcode,
    const uint8_t salt[kMatterSpake2pSaltSize],
    uint32_t iterations,
    uint8_t outW0[kMatterSpake2pW0Length],
    uint8_t outW1[kMatterSpake2pW1Length]) {
  if (!matterSetupPinValid(passcode) || salt == nullptr || outW0 == nullptr ||
      outW1 == nullptr || iterations < kMatterSpake2pMinPbkdf2Iterations ||
      iterations > kMatterSpake2pMaxPbkdf2Iterations) {
    return false;
  }

  // Convert passcode to byte representation
  uint8_t passcodeBytes[16] = {0};
  size_t passcodeLen = 0U;
  {
    uint32_t temp = passcode;
    uint8_t digits[16] = {0};
    size_t digitCount = 0U;
    while (temp > 0U) {
      digits[digitCount++] = static_cast<uint8_t>('0' + (temp % 10U));
      temp /= 10U;
    }
    // Reverse to get correct order
    passcodeLen = digitCount;
    for (size_t i = 0; i < digitCount; ++i) {
      passcodeBytes[i] = digits[digitCount - 1U - i];
    }
  }

  const char* keySaltStr = kSpake2pContextProliferation;
  const size_t keySaltLen = strlen(keySaltStr);

  // w0s = PBKDF2(passcode, salt || keySalt, iterations, hashLen)
  uint8_t saltWithContext[kMatterSpake2pSaltSize + 32] = {0};
  memcpy(saltWithContext, salt, kMatterSpake2pSaltSize);
  memcpy(saltWithContext + kMatterSpake2pSaltSize, keySaltStr, keySaltLen);

  uint8_t w0Raw[kMatterSpake2pHashSize] = {0};
  if (!MatterPbkdf2::deriveKey(passcodeBytes, passcodeLen,
                                saltWithContext,
                                kMatterSpake2pSaltSize + keySaltLen,
                                iterations, sizeof(w0Raw), w0Raw)) {
    return false;
  }

  // Treat raw bytes as a 256-bit number, reduce mod n
  // The PBKDF2 output is hashLen bytes. Pad to 32 then reduce.
  uint8_t w0Padded[32] = {0};
  memcpy(w0Padded, w0Raw, sizeof(w0Raw));
  Secp256r1::BigNum256 w0Full;
  Secp256r1::bnFromBytes(w0Padded, &w0Full);
  // Simple mod: the raw bytes from PBKDF2 are already < 2^256
  // We need to ensure < n. n is ~2^256 so most values are fine.
  // But let's do it properly: reduce mod n
  const Secp256r1::BigNum256 nBn = Secp256r1::orderN();

  // If w0Full >= n, subtract n
  if (Secp256r1::bnCompare(w0Full, nBn) >= 0) {
    Secp256r1::bnSub(w0Full, nBn, &w0Full);
  }
  // If w0Full == 0, set to 1
  if (Secp256r1::bnIsZero(w0Full)) {
    Secp256r1::bnSetOne(&w0Full);
  }

  Secp256r1::bnToBytes(w0Full, outW0);

  // w1s = PBKDF2(passcode, salt || w0s || keySalt, iterations, hashLen)
  uint8_t saltWithW0[kMatterSpake2pSaltSize + kMatterSpake2pW0Length + 32] = {0};
  memcpy(saltWithW0, salt, kMatterSpake2pSaltSize);
  memcpy(saltWithW0 + kMatterSpake2pSaltSize, outW0, kMatterSpake2pW0Length);
  memcpy(saltWithW0 + kMatterSpake2pSaltSize + kMatterSpake2pW0Length,
         keySaltStr, keySaltLen);

  uint8_t w1Raw[kMatterSpake2pHashSize] = {0};
  if (!MatterPbkdf2::deriveKey(passcodeBytes, passcodeLen,
                                saltWithW0,
                                kMatterSpake2pSaltSize +
                                    kMatterSpake2pW0Length + keySaltLen,
                                iterations, sizeof(w1Raw), w1Raw)) {
    return false;
  }

  // Reduce w1 mod n similarly
  uint8_t w1Padded[32] = {0};
  memcpy(w1Padded, w1Raw, sizeof(w1Raw));
  Secp256r1::BigNum256 w1Full;
  Secp256r1::bnFromBytes(w1Padded, &w1Full);
  if (Secp256r1::bnCompare(w1Full, nBn) >= 0) {
    Secp256r1::bnSub(w1Full, nBn, &w1Full);
  }
  if (Secp256r1::bnIsZero(w1Full)) {
    Secp256r1::bnSetOne(&w1Full);
  }
  Secp256r1::bnToBytes(w1Full, outW1);

  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool MatterPaseCommissioning::beginAsCommissionee(
    MatterPaseTransport* platform,
    CommissioningCallback callback, void* context) {
  if (platform == nullptr || session_.active) {
    return false;
  }

  session_ = MatterPaseSessionState{};
  verifier_ = MatterSpake2pVerifier{};
  peerAddr_ = otIp6Address{};
  peerPort_ = 0U;
  peerExchangeId_ = 0U;
  peerMessageId_ = 0U;
  peerBound_ = false;
  peerMessageCounter_.reset();
  expectedMessage_ = ExpectedMessage::kPbkdfParamRequest;

  platform_ = platform;
  callback_ = callback;
  callbackContext_ = context;
  initiator_ = false;

  localExchangeId_ = nextExchangeId();
  localMessageId_ = 0U;
  session_.active = true;
  session_.initiator = false;
  session_.setupPinCode = setupPinCode_;
  session_.passcodeId = 0U;
  session_.state = MatterCommissioningState::kIdle;

  if (!platform_->setReceiveCallback(handleUdpReceive, this)) {
    platform_->setReceiveCallback(nullptr, nullptr);
    platform_ = nullptr;
    callback_ = nullptr;
    callbackContext_ = nullptr;
    session_ = MatterPaseSessionState{};
    return false;
  }
  return true;
}

bool MatterPaseCommissioning::beginAsCommissioner(
    MatterPaseTransport* platform,
    CommissioningCallback callback, void* context) {
  if (platform == nullptr || session_.active) {
    return false;
  }

  session_ = MatterPaseSessionState{};
  verifier_ = MatterSpake2pVerifier{};
  peerAddr_ = otIp6Address{};
  peerPort_ = 0U;
  peerExchangeId_ = 0U;
  peerMessageId_ = 0U;
  peerBound_ = false;
  peerMessageCounter_.reset();
  expectedMessage_ = ExpectedMessage::kNone;

  platform_ = platform;
  callback_ = callback;
  callbackContext_ = context;
  initiator_ = true;

  localExchangeId_ = nextExchangeId();
  localMessageId_ = 0U;
  session_.active = true;
  session_.initiator = true;
  session_.setupPinCode = setupPinCode_;
  session_.passcodeId = 0U;
  session_.state = MatterCommissioningState::kIdle;

  if (!platform_->setReceiveCallback(handleUdpReceive, this)) {
    platform_->setReceiveCallback(nullptr, nullptr);
    platform_ = nullptr;
    callback_ = nullptr;
    callbackContext_ = nullptr;
    session_ = MatterPaseSessionState{};
    return false;
  }
  return true;
}

void MatterPaseCommissioning::end() {
  if (platform_ != nullptr) {
    platform_->setReceiveCallback(nullptr, nullptr);
  }
  platform_ = nullptr;
  callback_ = nullptr;
  callbackContext_ = nullptr;
  session_ = MatterPaseSessionState{};
  session_.active = false;
  localExchangeId_ = 0U;
  localMessageId_ = 0U;
  peerExchangeId_ = 0U;
  peerMessageId_ = 0U;
  peerPort_ = 0U;
  peerAddr_ = otIp6Address{};
  peerBound_ = false;
  peerMessageCounter_.reset();
  expectedMessage_ = ExpectedMessage::kNone;
  initiator_ = false;
  verifier_ = MatterSpake2pVerifier{};
}

void MatterPaseCommissioning::process() {
  // Message handling is callback-driven from the platform UDP receive
}

bool MatterPaseCommissioning::active() const {
  return session_.active;
}

MatterCommissioningState MatterPaseCommissioning::state() const {
  return session_.state;
}

const char* MatterPaseCommissioning::stateName() const {
  return stateName(session_.state);
}

bool MatterPaseCommissioning::setPasscode(uint32_t passcode) {
  if (session_.active || !matterSetupPinValid(passcode)) {
    return false;
  }
  setupPinCode_ = passcode;
  return true;
}

bool MatterPaseCommissioning::setDiscriminator(uint16_t discriminator) {
  if (session_.active || !matterDiscriminatorValid(discriminator)) {
    return false;
  }
  discriminator_ = discriminator;
  return true;
}

bool MatterPaseCommissioning::deriveVerifier(
    uint32_t passcode,
    const uint8_t salt[kMatterSpake2pSaltSize],
    uint32_t iterations,
    MatterSpake2pVerifier* outVerifier) {
  if (outVerifier == nullptr || salt == nullptr) {
    return false;
  }
  if (!matterSetupPinValid(passcode) || iterations == 0U) {
    return false;
  }

  *outVerifier = MatterSpake2pVerifier{};
  memcpy(outVerifier->salt, salt, kMatterSpake2pSaltSize);
  outVerifier->iterations = iterations;

  // Derive w0 and w1 from passcode into local buffers
  uint8_t localW0[kMatterSpake2pW0Length] = {0};
  uint8_t localW1[kMatterSpake2pW1Length] = {0};
  if (!spake2pDeriveWS(passcode, salt, iterations, localW0, localW1)) {
    return false;
  }

  memcpy(outVerifier->w0, localW0, sizeof(outVerifier->w0));

  // L = w1 * G
  Secp256r1Scalar w1Scalar;
  memcpy(w1Scalar.bytes, localW1, sizeof(w1Scalar.bytes));
  Secp256r1Point Lpoint;
  if (!Secp256r1::scalarMultiplyBase(w1Scalar, &Lpoint)) {
    return false;
  }
  Secp256r1::encodeUncompressed(Lpoint, outVerifier->L);

  outVerifier->valid = true;
  return true;
}

bool MatterPaseCommissioning::sendPbkdfParamRequest(
    const otIp6Address& peerAddr, uint16_t peerPort,
    uint32_t setupPinCode) {
  if (platform_ == nullptr || !session_.active || !initiator_ ||
      session_.state != MatterCommissioningState::kIdle || peerPort == 0U ||
      !matterSetupPinValid(setupPinCode)) {
    return false;
  }

  setupPinCode_ = setupPinCode;
  session_.setupPinCode = setupPinCode;
  peerAddr_ = peerAddr;
  peerPort_ = peerPort;

  // Generate random salt for PBKDF2.
  if (!generateRandom(session_.salt, sizeof(session_.salt))) {
    fail(MatterCommissioningError::kCryptoFailed);
    return false;
  }
  session_.pbkdf2Iterations = kMatterSpake2pPbkdf2Iterations;

  // Derive w0 and w1 from passcode
  if (!spake2pDeriveWS(setupPinCode, session_.salt,
                       session_.pbkdf2Iterations,
                       session_.w0, session_.w1)) {
    fail(MatterCommissioningError::kCryptoFailed);
    return false;
  }

  // Compute L = w1 * G for the verifier
  Secp256r1Scalar w1Scalar;
  memcpy(w1Scalar.bytes, session_.w1, sizeof(w1Scalar.bytes));
  Secp256r1Point Lpoint;
  if (!Secp256r1::scalarMultiplyBase(w1Scalar, &Lpoint)) {
    fail(MatterCommissioningError::kCryptoFailed);
    return false;
  }
  Secp256r1::encodeUncompressed(Lpoint, session_.L);

  // Store in verifier for later use
  memcpy(verifier_.w0, session_.w0, sizeof(verifier_.w0));
  memcpy(verifier_.L, session_.L, sizeof(verifier_.L));
  memcpy(verifier_.salt, session_.salt, sizeof(verifier_.salt));
  verifier_.iterations = session_.pbkdf2Iterations;
  verifier_.valid = true;

  if (!generateRandom(session_.initiateRandom, sizeof(session_.initiateRandom))) {
    fail(MatterCommissioningError::kCryptoFailed);
    return false;
  }
  session_.localSessionId = static_cast<uint16_t>(
      (session_.initiateRandom[0] << 8U) | session_.initiateRandom[1]);
  if (session_.localSessionId == 0U) {
    session_.localSessionId = 1U;
  }
  session_.passcodeId = 0U;

  // Build PBKDF param request
  uint8_t payload[128] = {0};
  size_t offset = 0U;

  memcpy(&payload[offset], session_.initiateRandom,
         sizeof(session_.initiateRandom));
  offset += sizeof(session_.initiateRandom);
  writeUint16Le(session_.localSessionId, payload, offset);
  offset += 2U;
  writeUint16Le(session_.passcodeId, payload, offset);
  offset += 2U;

  // Include SPAKE2+ parameters: salt, iterations
  payload[offset++] = 1U;  // hasPbkdfParameters
  memcpy(&payload[offset], session_.salt, sizeof(session_.salt));
  offset += sizeof(session_.salt);
  writeUint32Le(session_.pbkdf2Iterations, payload, offset);
  offset += 4U;

  MatterMessageHeader header = {};
  header.exchangeFlags =
      static_cast<uint8_t>(MatterMessageExchangeFlags::kInitiator);
  header.sessionType = 0U;
  header.messageId = nextMessageId();
  header.exchangeId = localExchangeId_;
  header.protocolId = kProtocolSecureChannel;
  header.protocolOpcode =
      static_cast<uint8_t>(MatterMessageType::kPBKDFParamRequest);

  bindPeer(peerAddr, peerPort, localExchangeId_);
  const bool ok = sendMessage(peerAddr, peerPort, header, payload,
                              static_cast<uint16_t>(offset));
  if (ok) {
    expectedMessage_ = ExpectedMessage::kPbkdfParamResponse;
    advanceState(MatterCommissioningState::kPasePbkdfParamsSent);
  } else {
    peerAddr_ = otIp6Address{};
    peerPort_ = 0U;
    peerExchangeId_ = 0U;
    peerBound_ = false;
    peerMessageCounter_.reset();
    expectedMessage_ = ExpectedMessage::kNone;
  }
  return ok;
}

bool MatterPaseCommissioning::sendPbkdfParamResponse(
    const otIp6Address& peerAddr, uint16_t peerPort) {
  if (platform_ == nullptr || !session_.active || initiator_ ||
      session_.state != MatterCommissioningState::kIdle || !peerBound_ ||
      !peerMatches(peerAddr, peerPort)) {
    return false;
  }

  if (!generateRandom(session_.respondRandom, sizeof(session_.respondRandom))) {
    fail(MatterCommissioningError::kCryptoFailed);
    return false;
  }
  session_.localSessionId = static_cast<uint16_t>(
      (session_.respondRandom[0] << 8U) | session_.respondRandom[1]);
  if (session_.localSessionId == 0U) {
    session_.localSessionId = 1U;
  }

  // Derive SPAKE2+ keys from passcode using received salt
  if (!spake2pDeriveWS(session_.setupPinCode, session_.salt,
                       session_.pbkdf2Iterations,
                       session_.w0, session_.w1)) {
    fail(MatterCommissioningError::kCryptoFailed);
    return false;
  }

  Secp256r1Scalar w1Scalar = {};
  Secp256r1Point verifierPoint = {};
  memcpy(w1Scalar.bytes, session_.w1, sizeof(w1Scalar.bytes));
  if (!Secp256r1::scalarMultiplyBase(w1Scalar, &verifierPoint)) {
    fail(MatterCommissioningError::kCryptoFailed);
    return false;
  }
  Secp256r1::encodeUncompressed(verifierPoint, session_.L);

  // Build PBKDF param response
  uint8_t payload[128] = {0};
  size_t offset = 0U;

  memcpy(&payload[offset], session_.respondRandom,
         sizeof(session_.respondRandom));
  offset += sizeof(session_.respondRandom);
  writeUint16Le(session_.localSessionId, payload, offset);
  offset += 2U;
  writeUint16Le(session_.passcodeId, payload, offset);
  offset += 2U;

  // Include SPAKE2+ parameters confirmation
  payload[offset++] = 1U;  // hasPbkdfParameters
  memcpy(&payload[offset], session_.salt, sizeof(session_.salt));
  offset += sizeof(session_.salt);
  writeUint32Le(session_.pbkdf2Iterations, payload, offset);
  offset += 4U;

  MatterMessageHeader header = {};
  header.exchangeFlags = 0U;
  header.sessionType = 0U;
  header.messageId = nextMessageId();
  header.exchangeId = peerExchangeId_;
  header.protocolId = kProtocolSecureChannel;
  header.protocolOpcode =
      static_cast<uint8_t>(MatterMessageType::kPBKDFParamResponse);

  if (!sendMessage(peerAddr, peerPort, header, payload,
                   static_cast<uint16_t>(offset))) {
    fail(MatterCommissioningError::kTransportFailed);
    return false;
  }
  expectedMessage_ = ExpectedMessage::kSpake2p1;
  advanceState(MatterCommissioningState::kPaseSpake2pInProgress);
  return true;
}

bool MatterPaseCommissioning::initiateSpake2p(
    const otIp6Address& peerAddr, uint16_t peerPort) {
  if (platform_ == nullptr || !session_.active || !initiator_ ||
      session_.state != MatterCommissioningState::kPasePbkdfParamsSent ||
      !peerMatches(peerAddr, peerPort)) {
    return false;
  }

  peerAddr_ = peerAddr;
  peerPort_ = peerPort;

  // Initiator (commissioner/prover) computes X = (x + w0)*G.
  // In this experimental PASE-like exchange:
  // - Initiator sends spake2p1 with X
  // - Responder sends spake2p2 with Y + cB
  // - Initiator sends spake2p3 with cA

  if (!computeSpake2pX()) {
    fail(MatterCommissioningError::kCryptoFailed);
    return false;
  }

  // Build spake2p1 message containing X
  MatterMessageHeader header = {};
  header.exchangeFlags =
      static_cast<uint8_t>(MatterMessageExchangeFlags::kInitiator);
  header.sessionType = 0U;
  header.messageId = nextMessageId();
  header.exchangeId = localExchangeId_;
  header.protocolId = kProtocolSecureChannel;
  header.protocolOpcode =
      static_cast<uint8_t>(MatterMessageType::kPaseSpake2p1);

  const bool ok = sendMessage(peerAddr, peerPort, header,
                              session_.X, sizeof(session_.X));
  if (ok) {
    expectedMessage_ = ExpectedMessage::kSpake2p2;
    advanceState(MatterCommissioningState::kPaseSpake2pInProgress);
  } else {
    fail(MatterCommissioningError::kTransportFailed);
  }
  return ok;
}

bool MatterPaseCommissioning::getSharedSecret(
    uint8_t outSharedSecret[kMatterSpake2pHashSize]) const {
  if (outSharedSecret == nullptr ||
      session_.state != MatterCommissioningState::kPaseComplete) {
    return false;
  }
  memcpy(outSharedSecret, session_.sharedSecret, kMatterSpake2pHashSize);
  return true;
}

// ---------------------------------------------------------------------------
// SPAKE2+ cryptographic operations
// ---------------------------------------------------------------------------

bool MatterPaseCommissioning::computeSpake2pX() {
  // Initiator/commissioner (prover) computes:
  // X = x*G + w0*G = (x + w0)*G
  Secp256r1Scalar xScalar;
  if (!Secp256r1::generateRandomScalar(&xScalar)) return false;

  // x + w0 (mod n)
  Secp256r1::BigNum256 xBn, w0Bn, sumBn;
  Secp256r1::bnFromBytes(xScalar.bytes, &xBn);
  Secp256r1::bnFromBytes(session_.w0, &w0Bn);
  Secp256r1::bnModAddN(xBn, w0Bn, &sumBn);

  Secp256r1Scalar scalarXW0;
  Secp256r1::bnToBytes(sumBn, scalarXW0.bytes);

  Secp256r1Point Xpoint;
  if (!Secp256r1::scalarMultiplyBase(scalarXW0, &Xpoint)) {
    return false;
  }

  // Store ephemeral scalar for later Z computation
  memcpy(session_.ephemeralScalar, xScalar.bytes, sizeof(xScalar.bytes));

  Secp256r1::encodeUncompressed(Xpoint, session_.X);
  return true;
}

bool MatterPaseCommissioning::computeSpake2pY() {
  // Responder/commissionee (verifier) computes:
  // Y = (y + w0)*G
  Secp256r1Scalar yScalar;
  if (!Secp256r1::generateRandomScalar(&yScalar)) return false;

  // y + w0 (mod n)
  Secp256r1::BigNum256 yBn, w0Bn, sumBn;
  Secp256r1::bnFromBytes(yScalar.bytes, &yBn);
  Secp256r1::bnFromBytes(session_.w0, &w0Bn);
  Secp256r1::bnModAddN(yBn, w0Bn, &sumBn);

  Secp256r1Scalar scalarYW0;
  Secp256r1::bnToBytes(sumBn, scalarYW0.bytes);

  Secp256r1Point Ypoint;
  if (!Secp256r1::scalarMultiplyBase(scalarYW0, &Ypoint)) {
    return false;
  }

  // Store ephemeral scalar for later Z computation
  memcpy(session_.ephemeralScalar, yScalar.bytes, sizeof(yScalar.bytes));

  Secp256r1::encodeUncompressed(Ypoint, session_.Y);
  return true;
}

bool MatterPaseCommissioning::computeSpake2pZ(bool responderVerifier) {
  // Both sides compute:
  // Z = x * (Y - w0*G)  (prover uses the responder's Y)
  // Z = y * (X - w0*G)  (verifier uses the initiator's X)
  // Since X = (x+w0)*G and Y = (y+w0)*G:
  //   Z = x*y*G = y*x*G (same for both!)
  //
  // V = w1*(Y-w0*G) for the prover and y*L for the verifier.

  // Decode peer's point
  const uint8_t* peerPoint =
      responderVerifier ? session_.X : session_.Y;
  Secp256r1Point peerP;
  if (!Secp256r1::decodeUncompressed(peerPoint, &peerP)) {
    return false;
  }

  // Compute w0*G
  Secp256r1Scalar w0Scalar;
  memcpy(w0Scalar.bytes, session_.w0, sizeof(w0Scalar.bytes));
  Secp256r1Point w0G;
  if (!Secp256r1::scalarMultiplyBase(w0Scalar, &w0G)) {
    return false;
  }

  // Compute peerMinusW0 = peerP - w0*G = peerP + (-w0*G)
  // To negate: (x, y) -> (x, p - y)
  Secp256r1Point negW0G;
  memcpy(negW0G.x, w0G.x, sizeof(negW0G.x));
  Secp256r1::BigNum256 pVal = Secp256r1::primeP();
  Secp256r1::BigNum256 yNeg;
  Secp256r1::bnFromBytes(w0G.y, &yNeg);
  Secp256r1::bnSub(pVal, yNeg, &yNeg);
  Secp256r1::bnToBytes(yNeg, negW0G.y);

  Secp256r1Point peerMinusW0;
  if (!Secp256r1::pointAdd(peerP, negW0G, &peerMinusW0)) {
    return false;
  }

  // Z = ephemeralScalar * peerMinusW0
  // Reuse the ephemeral scalar (x or y) stored during X/Y computation
  Secp256r1Scalar ephemeral;
  memcpy(ephemeral.bytes, session_.ephemeralScalar, sizeof(ephemeral.bytes));

  Secp256r1Point Zpoint;
  if (!Secp256r1::scalarMultiply(ephemeral, peerMinusW0, &Zpoint)) {
    return false;
  }
  Secp256r1::encodeUncompressed(Zpoint, session_.Z);

  Secp256r1Point Vpoint = {};
  if (responderVerifier) {
    // The verifier has y and L = w1*G, so V = y*L.
    Secp256r1Point verifierPoint = {};
    if (!Secp256r1::decodeUncompressed(session_.L, &verifierPoint) ||
        !Secp256r1::scalarMultiply(ephemeral, verifierPoint, &Vpoint)) {
      return false;
    }
  } else {
    // The prover knows w1, so V = w1*(Y-w0*G).
    Secp256r1Scalar w1Scalar = {};
    memcpy(w1Scalar.bytes, session_.w1, sizeof(w1Scalar.bytes));
    if (!Secp256r1::scalarMultiply(w1Scalar, peerMinusW0, &Vpoint)) {
      return false;
    }
  }
  Secp256r1::encodeUncompressed(Vpoint, session_.V);

  return true;
}

bool MatterPaseCommissioning::deriveSharedSecret() {
  // SharedSecret = SHA256(Z || V || w0)
  uint8_t concat[kMatterSpake2pPointSize * 2 + kMatterSpake2pW0Length] = {0};
  size_t offset = 0U;
  memcpy(concat + offset, session_.Z, sizeof(session_.Z));
  offset += sizeof(session_.Z);
  memcpy(concat + offset, session_.V, sizeof(session_.V));
  offset += sizeof(session_.V);
  memcpy(concat + offset, session_.w0, sizeof(session_.w0));
  offset += sizeof(session_.w0);

  MatterPbkdf2::sha256(concat, offset, session_.sharedSecret);

  // Derive independent session and confirmation keys.
  const char* sessionKeysContext = "PASE-EXPERIMENTAL-SESSION";
  MatterPbkdf2::hmacSha256(
      session_.sharedSecret, sizeof(session_.sharedSecret),
      reinterpret_cast<const uint8_t*>(sessionKeysContext),
      strlen(sessionKeysContext),
      session_.ke);
  MatterPbkdf2::hmacSha256(
      session_.sharedSecret, sizeof(session_.sharedSecret),
      reinterpret_cast<const uint8_t*>(kSpake2pKeyAlpha),
      strlen(kSpake2pKeyAlpha), session_.kcA);
  MatterPbkdf2::hmacSha256(
      session_.sharedSecret, sizeof(session_.sharedSecret),
      reinterpret_cast<const uint8_t*>(kSpake2pKeyBeta),
      strlen(kSpake2pKeyBeta), session_.kcB);

  return true;
}

bool MatterPaseCommissioning::generateConfirmationA() {
  uint8_t input[256] = {0};
  const size_t inputLength =
      buildConfirmationInput(session_, initiator_, true, input, sizeof(input));
  if (inputLength == 0U) {
    return false;
  }
  MatterPbkdf2::hmacSha256(session_.kcA, sizeof(session_.kcA),
                           input, inputLength, session_.cA);

  return true;
}

bool MatterPaseCommissioning::generateConfirmationB() {
  uint8_t input[256] = {0};
  const size_t inputLength =
      buildConfirmationInput(session_, initiator_, false, input, sizeof(input));
  if (inputLength == 0U) {
    return false;
  }
  MatterPbkdf2::hmacSha256(session_.kcB, sizeof(session_.kcB),
                           input, inputLength, session_.cB);

  return true;
}

bool MatterPaseCommissioning::verifyConfirmationB() {
  // Save received cB before it gets overwritten by re-computation
  uint8_t receivedCB[kMatterSpake2pConfirmationSize] = {0};
  memcpy(receivedCB, session_.cB, sizeof(receivedCB));

  // Re-compute cB
  if (!generateConfirmationB()) {
    return false;
  }

  uint8_t computedCB[kMatterSpake2pConfirmationSize] = {0};
  memcpy(computedCB, session_.cB, sizeof(computedCB));

  // Restore received cB
  memcpy(session_.cB, receivedCB, sizeof(receivedCB));

  return constantTimeEqual(computedCB, receivedCB, sizeof(computedCB));
}

bool MatterPaseCommissioning::verifyConfirmationA() {
  uint8_t receivedCA[kMatterSpake2pConfirmationSize] = {0};
  memcpy(receivedCA, session_.cA, sizeof(receivedCA));

  if (!generateConfirmationA()) {
    return false;
  }

  uint8_t computedCA[kMatterSpake2pConfirmationSize] = {0};
  memcpy(computedCA, session_.cA, sizeof(computedCA));

  memcpy(session_.cA, receivedCA, sizeof(session_.cA));

  return constantTimeEqual(computedCA, receivedCA, sizeof(computedCA));
}

// ---------------------------------------------------------------------------
// UDP receive and message handling
// ---------------------------------------------------------------------------

void MatterPaseCommissioning::handleUdpReceive(
    void* context, const uint8_t* payload, uint16_t length,
    const otIp6Address& source, uint16_t sourcePort) {
  if (context == nullptr) {
    return;
  }
  static_cast<MatterPaseCommissioning*>(context)->handleMessage(
      payload, length, source, sourcePort);
}

void MatterPaseCommissioning::handleMessage(
    const uint8_t* payload, uint16_t length,
    const otIp6Address& source, uint16_t sourcePort) {
  if (!session_.active ||
      session_.state == MatterCommissioningState::kFailed ||
      session_.state == MatterCommissioningState::kPaseComplete) {
    return;
  }

  MatterMessageHeader header = {};
  size_t payloadOffset = 0U;
  if (!parseMessageHeader(payload, length, &header, &payloadOffset)) {
    return;
  }

  if (!messageExpected(header, source, sourcePort)) {
    return;
  }

  const uint16_t appLength =
      static_cast<uint16_t>(length > payloadOffset ? length - payloadOffset
                                                    : 0U);
  const uint8_t* appPayload =
      appLength > 0U ? &payload[payloadOffset] : nullptr;
  if (!messagePayloadValid(header, appPayload, appLength) ||
      !peerMessageCounter_.accept(header.messageId)) {
    return;
  }

  // Commit the peer only after the complete first request is structurally
  // valid. A malformed unauthenticated datagram must not claim the window.
  if (!peerBound_) {
    bindPeer(source, sourcePort, header.exchangeId);
  }
  peerMessageId_ = header.messageId;

  switch (static_cast<MatterMessageType>(header.protocolOpcode)) {
    case MatterMessageType::kPBKDFParamRequest:
      handlePbkdfParamRequest(appPayload, appLength, source, sourcePort);
      break;
    case MatterMessageType::kPBKDFParamResponse:
      handlePbkdfParamResponse(appPayload, appLength, source, sourcePort);
      break;
    case MatterMessageType::kPaseSpake2p1:
      handleSpake2p1(appPayload, appLength, source, sourcePort);
      break;
    case MatterMessageType::kPaseSpake2p2:
      handleSpake2p2(appPayload, appLength, source, sourcePort);
      break;
    case MatterMessageType::kPaseSpake2p3:
      handleSpake2p3(appPayload, appLength);
      break;
    default:
      break;
  }
}

void MatterPaseCommissioning::handlePbkdfParamRequest(
    const uint8_t* payload, uint16_t length,
    const otIp6Address& source, uint16_t sourcePort) {
  if (payload == nullptr || initiator_ || length != kPbkdfPayloadSize) {
    fail(MatterCommissioningError::kInvalidMessage);
    return;
  }

  uint8_t initiateRandom[sizeof(session_.initiateRandom)] = {0};
  uint8_t salt[sizeof(session_.salt)] = {0};
  memcpy(initiateRandom, payload, sizeof(initiateRandom));
  size_t offset = sizeof(session_.initiateRandom);

  // Parse initiator session ID
  uint16_t initiatorSessionId = 0U;
  if (!readUint16Le(payload, offset, length, &initiatorSessionId)) {
    return;
  }
  offset += 2U;

  // Parse passcode ID
  uint16_t passcodeId = 0U;
  if (!readUint16Le(payload, offset, length, &passcodeId)) {
    return;
  }
  offset += 2U;

  if (initiatorSessionId == 0U || passcodeId != 0U || payload[offset++] != 1U) {
    fail(MatterCommissioningError::kInvalidMessage);
    return;
  }

  memcpy(salt, &payload[offset], sizeof(salt));
  offset += sizeof(salt);
  uint32_t iterations = 0U;
  if (!readUint32Le(payload, offset, length, &iterations) ||
      iterations < kMatterSpake2pMinPbkdf2Iterations ||
      iterations > kMatterSpake2pMaxPbkdf2Iterations) {
    fail(MatterCommissioningError::kInvalidMessage);
    return;
  }

  memcpy(session_.initiateRandom, initiateRandom, sizeof(initiateRandom));
  memcpy(session_.salt, salt, sizeof(salt));
  session_.peerSessionId = initiatorSessionId;
  session_.passcodeId = passcodeId;
  session_.pbkdf2Iterations = iterations;

  // Send PBKDF param response
  (void)sendPbkdfParamResponse(source, sourcePort);
}

void MatterPaseCommissioning::handlePbkdfParamResponse(
    const uint8_t* payload, uint16_t length,
    const otIp6Address& source, uint16_t sourcePort) {
  if (payload == nullptr || !initiator_ || length != kPbkdfPayloadSize) {
    fail(MatterCommissioningError::kInvalidMessage);
    return;
  }

  uint8_t respondRandom[sizeof(session_.respondRandom)] = {0};
  memcpy(respondRandom, payload, sizeof(respondRandom));
  size_t offset = sizeof(session_.respondRandom);

  // Parse responder session ID
  uint16_t responderSessionId = 0U;
  if (!readUint16Le(payload, offset, length, &responderSessionId)) {
    return;
  }
  offset += 2U;

  uint16_t passcodeId = 0U;
  if (!readUint16Le(payload, offset, length, &passcodeId)) {
    fail(MatterCommissioningError::kInvalidMessage);
    return;
  }
  offset += 2U;

  if (responderSessionId == 0U || passcodeId != session_.passcodeId ||
      payload[offset++] != 1U ||
      !constantTimeEqual(&payload[offset], session_.salt,
                         sizeof(session_.salt))) {
    fail(MatterCommissioningError::kInvalidMessage);
    return;
  }
  offset += sizeof(session_.salt);

  uint32_t iterations = 0U;
  if (!readUint32Le(payload, offset, length, &iterations) ||
      iterations != session_.pbkdf2Iterations) {
    fail(MatterCommissioningError::kInvalidMessage);
    return;
  }

  memcpy(session_.respondRandom, respondRandom, sizeof(respondRandom));
  session_.peerSessionId = responderSessionId;
  (void)initiateSpake2p(source, sourcePort);
}

void MatterPaseCommissioning::handleSpake2p1(
    const uint8_t* payload, uint16_t length,
    const otIp6Address& source, uint16_t sourcePort) {
  if (payload == nullptr || initiator_ ||
      length != kMatterSpake2pPointSize) {
    fail(MatterCommissioningError::kInvalidMessage);
    return;
  }

  Secp256r1Point peerPoint = {};
  if (!Secp256r1::decodeUncompressed(payload, &peerPoint)) {
    fail(MatterCommissioningError::kAuthenticationFailed);
    return;
  }
  memcpy(session_.X, payload, kMatterSpake2pPointSize);

  // Commissionee (verifier): compute Y and Z.
  if (!computeSpake2pY()) {
    fail(MatterCommissioningError::kCryptoFailed);
    return;
  }

  if (!computeSpake2pZ(true)) {  // responder is the verifier
    fail(MatterCommissioningError::kCryptoFailed);
    return;
  }

  // Z and V are already computed in computeSpake2pZ()

  if (!deriveSharedSecret()) {
    fail(MatterCommissioningError::kCryptoFailed);
    return;
  }

  if (!generateConfirmationB()) {
    fail(MatterCommissioningError::kCryptoFailed);
    return;
  }

  // Send spake2p2: Y || cB
  uint8_t spake2p2Payload[kMatterSpake2pPointSize +
                          kMatterSpake2pConfirmationSize] = {0};
  memcpy(spake2p2Payload, session_.Y, sizeof(session_.Y));
  memcpy(spake2p2Payload + sizeof(session_.Y), session_.cB,
         sizeof(session_.cB));

  MatterMessageHeader header = {};
  header.exchangeFlags = 0U;
  header.sessionType = 0U;
  header.messageId = nextMessageId();
  header.exchangeId = peerExchangeId_;
  header.protocolId = kProtocolSecureChannel;
  header.protocolOpcode =
      static_cast<uint8_t>(MatterMessageType::kPaseSpake2p2);

  if (!sendMessage(source, sourcePort, header, spake2p2Payload,
                   sizeof(spake2p2Payload))) {
    fail(MatterCommissioningError::kTransportFailed);
    return;
  }
  expectedMessage_ = ExpectedMessage::kSpake2p3;
}

void MatterPaseCommissioning::handleSpake2p2(
    const uint8_t* payload, uint16_t length,
    const otIp6Address& source, uint16_t sourcePort) {
  if (payload == nullptr || !initiator_) {
    fail(MatterCommissioningError::kInvalidMessage);
    return;
  }

  const size_t spake2p2Size =
      kMatterSpake2pPointSize + kMatterSpake2pConfirmationSize;
  if (length != spake2p2Size) {
    fail(MatterCommissioningError::kInvalidMessage);
    return;
  }

  Secp256r1Point peerPoint = {};
  if (!Secp256r1::decodeUncompressed(payload, &peerPoint)) {
    fail(MatterCommissioningError::kAuthenticationFailed);
    return;
  }

  // Parse Y and cB
  memcpy(session_.Y, payload, sizeof(session_.Y));
  memcpy(session_.cB, payload + sizeof(session_.Y), sizeof(session_.cB));

  // Commissioner (prover): compute Z.
  if (!computeSpake2pZ(false)) {  // initiator is the prover
    fail(MatterCommissioningError::kCryptoFailed);
    return;
  }

  if (!deriveSharedSecret()) {
    fail(MatterCommissioningError::kCryptoFailed);
    return;
  }

  // Verify cB
  if (!verifyConfirmationB()) {
    fail(MatterCommissioningError::kAuthenticationFailed);
    return;
  }

  if (!generateConfirmationA()) {
    fail(MatterCommissioningError::kCryptoFailed);
    return;
  }

  // Send spake2p3: cA
  MatterMessageHeader header = {};
  header.exchangeFlags = 0U;
  header.sessionType = 0U;
  header.messageId = nextMessageId();
  header.exchangeId = peerExchangeId_;
  header.protocolId = kProtocolSecureChannel;
  header.protocolOpcode =
      static_cast<uint8_t>(MatterMessageType::kPaseSpake2p3);

  if (!sendMessage(source, sourcePort, header, session_.cA,
                   sizeof(session_.cA))) {
    fail(MatterCommissioningError::kTransportFailed);
    return;
  }

  expectedMessage_ = ExpectedMessage::kNone;
  advanceState(MatterCommissioningState::kPaseComplete);
}

void MatterPaseCommissioning::handleSpake2p3(
    const uint8_t* payload, uint16_t length) {
  if (payload == nullptr || initiator_) {
    fail(MatterCommissioningError::kInvalidMessage);
    return;
  }

  if (length != sizeof(session_.cA)) {
    fail(MatterCommissioningError::kInvalidMessage);
    return;
  }

  // Parse cA
  memcpy(session_.cA, payload, sizeof(session_.cA));

  // Verify cA
  if (!verifyConfirmationA()) {
    fail(MatterCommissioningError::kAuthenticationFailed);
    return;
  }

  // PASE handshake complete!
  expectedMessage_ = ExpectedMessage::kNone;
  advanceState(MatterCommissioningState::kPaseComplete);
}

// ---------------------------------------------------------------------------
// Message framing
// ---------------------------------------------------------------------------

bool MatterPaseCommissioning::parseMessageHeader(
    const uint8_t* payload, uint16_t length,
    MatterMessageHeader* outHeader, size_t* outPayloadOffset) const {
  if (payload == nullptr || outHeader == nullptr || length < 20U) {
    return false;
  }

  *outHeader = MatterMessageHeader{};
  size_t offset = 0U;

  outHeader->exchangeFlags = payload[offset++];
  outHeader->sessionType = payload[offset++];
  outHeader->securityFlags = payload[offset++];

  readUint16Le(payload, offset, length, &outHeader->messageId);
  offset += 2U;
  readUint32Le(payload, offset, length, &outHeader->sourceNodeId);
  offset += 4U;
  readUint32Le(payload, offset, length, &outHeader->destNodeId);
  offset += 4U;
  readUint16Le(payload, offset, length, &outHeader->exchangeId);
  offset += 2U;

  // Skip vendor ID (2 bytes)
  offset += 2U;

  readUint16Le(payload, offset, length, &outHeader->protocolId);
  offset += 2U;

  if (offset < length) {
    outHeader->protocolOpcode = payload[offset++];
  }

  // Optional acked message ID
  if ((outHeader->exchangeFlags &
       static_cast<uint8_t>(MatterMessageExchangeFlags::kAck)) != 0U) {
    if (!readUint16Le(payload, offset, length,
                      &outHeader->ackedMessageId)) {
      return false;
    }
    offset += 2U;
  }

  if (outPayloadOffset != nullptr) {
    *outPayloadOffset = offset;
  }
  return true;
}

bool MatterPaseCommissioning::buildMessageHeader(
    const MatterMessageHeader& header,
    uint8_t* outBuffer, size_t outCapacity,
    size_t* outLength) const {
  const size_t requiredCapacity =
      (header.exchangeFlags &
       static_cast<uint8_t>(MatterMessageExchangeFlags::kAck)) != 0U
          ? 22U
          : 20U;
  if (outBuffer == nullptr || outCapacity < requiredCapacity) {
    if (outLength != nullptr) {
      *outLength = 0U;
    }
    return false;
  }

  size_t offset = 0U;
  outBuffer[offset++] = header.exchangeFlags;
  outBuffer[offset++] = header.sessionType;
  outBuffer[offset++] = header.securityFlags;

  writeUint16Le(header.messageId, outBuffer, offset);
  offset += 2U;
  writeUint32Le(header.sourceNodeId, outBuffer, offset);
  offset += 4U;
  writeUint32Le(header.destNodeId, outBuffer, offset);
  offset += 4U;
  writeUint16Le(header.exchangeId, outBuffer, offset);
  offset += 2U;

  // Protocol vendor ID (0 for standard)
  writeUint16Le(0U, outBuffer, offset);
  offset += 2U;
  writeUint16Le(header.protocolId, outBuffer, offset);
  offset += 2U;

  outBuffer[offset++] = header.protocolOpcode;

  if ((header.exchangeFlags &
       static_cast<uint8_t>(MatterMessageExchangeFlags::kAck)) != 0U) {
    writeUint16Le(header.ackedMessageId, outBuffer, offset);
    offset += 2U;
  }

  if (outLength != nullptr) {
    *outLength = offset;
  }
  return true;
}

bool MatterPaseCommissioning::sendMessage(
    const otIp6Address& peerAddr, uint16_t peerPort,
    const MatterMessageHeader& header,
    const uint8_t* appPayload, uint16_t appPayloadLength) {
  if (platform_ == nullptr || peerPort == 0U ||
      (appPayload == nullptr && appPayloadLength != 0U)) {
    return false;
  }

  uint8_t messageBuffer[256] = {0};
  size_t headerLength = 0U;
  if (!buildMessageHeader(header, messageBuffer, sizeof(messageBuffer),
                          &headerLength)) {
    return false;
  }

  if (appPayload != nullptr && appPayloadLength > 0U) {
    if ((headerLength + appPayloadLength) > sizeof(messageBuffer)) {
      return false;
    }
    memcpy(&messageBuffer[headerLength], appPayload, appPayloadLength);
  }

  return platform_->sendUdp(
      messageBuffer,
      static_cast<uint16_t>(headerLength + appPayloadLength),
      peerAddr, peerPort);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool MatterPaseCommissioning::messageExpected(
    const MatterMessageHeader& header, const otIp6Address& source,
    uint16_t sourcePort) const {
  if (sourcePort == 0U || header.sessionType != 0U ||
      header.securityFlags != 0U || header.messageId == 0U ||
      header.protocolId != kProtocolSecureChannel ||
      expectedMessage_ == ExpectedMessage::kNone) {
    return false;
  }

  uint8_t expectedOpcode = 0U;
  bool expectInitiatorFlag = false;
  bool roleMatches = false;
  switch (expectedMessage_) {
    case ExpectedMessage::kPbkdfParamRequest:
      expectedOpcode =
          static_cast<uint8_t>(MatterMessageType::kPBKDFParamRequest);
      expectInitiatorFlag = true;
      roleMatches = !initiator_;
      break;
    case ExpectedMessage::kPbkdfParamResponse:
      expectedOpcode =
          static_cast<uint8_t>(MatterMessageType::kPBKDFParamResponse);
      roleMatches = initiator_;
      break;
    case ExpectedMessage::kSpake2p1:
      expectedOpcode = static_cast<uint8_t>(MatterMessageType::kPaseSpake2p1);
      expectInitiatorFlag = true;
      roleMatches = !initiator_;
      break;
    case ExpectedMessage::kSpake2p2:
      expectedOpcode = static_cast<uint8_t>(MatterMessageType::kPaseSpake2p2);
      roleMatches = initiator_;
      break;
    case ExpectedMessage::kSpake2p3:
      expectedOpcode = static_cast<uint8_t>(MatterMessageType::kPaseSpake2p3);
      roleMatches = !initiator_;
      break;
    case ExpectedMessage::kNone:
    default:
      return false;
  }

  const uint8_t expectedFlags =
      expectInitiatorFlag
          ? static_cast<uint8_t>(MatterMessageExchangeFlags::kInitiator)
          : 0U;
  if (!roleMatches || header.protocolOpcode != expectedOpcode ||
      header.exchangeFlags != expectedFlags) {
    return false;
  }

  if (peerBound_) {
    return peerMatches(source, sourcePort) &&
           header.exchangeId == peerExchangeId_;
  }
  return !initiator_ && expectedMessage_ == ExpectedMessage::kPbkdfParamRequest &&
         header.exchangeId != 0U;
}

bool MatterPaseCommissioning::messagePayloadValid(
    const MatterMessageHeader& header, const uint8_t* payload,
    uint16_t length) const {
  switch (static_cast<MatterMessageType>(header.protocolOpcode)) {
    case MatterMessageType::kPBKDFParamRequest:
      return !initiator_ && validPbkdfRequestPayload(payload, length);

    case MatterMessageType::kPBKDFParamResponse: {
      if (!initiator_ || payload == nullptr || length != kPbkdfPayloadSize ||
          allZero(payload, 32U)) {
        return false;
      }
      size_t offset = 32U;
      uint16_t sessionId = 0U;
      uint16_t passcodeId = 0U;
      if (!readUint16Le(payload, offset, length, &sessionId)) {
        return false;
      }
      offset += 2U;
      if (!readUint16Le(payload, offset, length, &passcodeId)) {
        return false;
      }
      offset += 2U;
      if (sessionId == 0U || passcodeId != session_.passcodeId ||
          payload[offset++] != 1U ||
          !constantTimeEqual(payload + offset, session_.salt,
                             sizeof(session_.salt))) {
        return false;
      }
      offset += sizeof(session_.salt);
      uint32_t iterations = 0U;
      return readUint32Le(payload, offset, length, &iterations) &&
             iterations == session_.pbkdf2Iterations;
    }

    case MatterMessageType::kPaseSpake2p1: {
      Secp256r1Point point = {};
      return !initiator_ && payload != nullptr &&
             length == kMatterSpake2pPointSize &&
             Secp256r1::decodeUncompressed(payload, &point);
    }

    case MatterMessageType::kPaseSpake2p2: {
      Secp256r1Point point = {};
      return initiator_ && payload != nullptr &&
             length == (kMatterSpake2pPointSize +
                        kMatterSpake2pConfirmationSize) &&
             Secp256r1::decodeUncompressed(payload, &point);
    }

    case MatterMessageType::kPaseSpake2p3:
      return !initiator_ && payload != nullptr &&
             length == kMatterSpake2pConfirmationSize;

    default:
      return false;
  }
}

bool MatterPaseCommissioning::peerMatches(
    const otIp6Address& source, uint16_t sourcePort) const {
  return peerBound_ && sourcePort == peerPort_ &&
         memcmp(source.mFields.m8, peerAddr_.mFields.m8,
                sizeof(peerAddr_.mFields.m8)) == 0;
}

void MatterPaseCommissioning::bindPeer(
    const otIp6Address& source, uint16_t sourcePort, uint16_t exchangeId) {
  peerAddr_ = source;
  peerPort_ = sourcePort;
  peerExchangeId_ = exchangeId;
  peerBound_ = true;
}

void MatterPaseCommissioning::fail(MatterCommissioningError error) {
  if (session_.state == MatterCommissioningState::kFailed) {
    return;
  }
  secureZero(session_.w0, sizeof(session_.w0));
  secureZero(session_.w1, sizeof(session_.w1));
  secureZero(session_.ws, sizeof(session_.ws));
  secureZero(session_.L, sizeof(session_.L));
  secureZero(session_.X, sizeof(session_.X));
  secureZero(session_.Y, sizeof(session_.Y));
  secureZero(session_.Z, sizeof(session_.Z));
  secureZero(session_.V, sizeof(session_.V));
  secureZero(session_.ephemeralScalar, sizeof(session_.ephemeralScalar));
  secureZero(session_.sharedSecret, sizeof(session_.sharedSecret));
  secureZero(session_.ke, sizeof(session_.ke));
  secureZero(session_.kcA, sizeof(session_.kcA));
  secureZero(session_.kcB, sizeof(session_.kcB));
  secureZero(session_.cA, sizeof(session_.cA));
  secureZero(session_.cB, sizeof(session_.cB));
  secureZero(&session_.setupPinCode, sizeof(session_.setupPinCode));
  secureZero(&verifier_, sizeof(verifier_));
  expectedMessage_ = ExpectedMessage::kNone;
  advanceState(MatterCommissioningState::kFailed,
               static_cast<uint32_t>(error));
}

uint16_t MatterPaseCommissioning::nextExchangeId() {
  static uint16_t exchangeId = 0x5A3CU;
  exchangeId++;
  if (exchangeId == 0U) {
    exchangeId = 1U;
  }
  return exchangeId;
}

uint16_t MatterPaseCommissioning::nextMessageId() {
  localMessageId_++;
  if (localMessageId_ == 0U) {
    localMessageId_ = 1U;
  }
  return localMessageId_;
}

bool MatterPaseCommissioning::generateRandom(uint8_t* output,
                                             size_t length) {
  if (output == nullptr) {
    return false;
  }

  MatterRng rng;
  return rng.getRandomBytes(output, length, 400000UL);
}

void MatterPaseCommissioning::advanceState(
    MatterCommissioningState newState, uint32_t errorCode) {
  session_.state = newState;
  if (callback_ != nullptr) {
    callback_(callbackContext_, newState, errorCode);
  }
}

const char* MatterPaseCommissioning::stateName(
    MatterCommissioningState state) {
  switch (state) {
    case MatterCommissioningState::kIdle:
      return "idle";
    case MatterCommissioningState::kDiscovering:
      return "discovering";
    case MatterCommissioningState::kPasePbkdfParamsSent:
      return "pbkdf-params-sent";
    case MatterCommissioningState::kPaseSpake2pInProgress:
      return "spake2p-in-progress";
    case MatterCommissioningState::kPaseComplete:
      return "pase-complete";
    case MatterCommissioningState::kSigmaInProgress:
      return "sigma-in-progress";
    case MatterCommissioningState::kNocSent:
      return "noc-sent";
    case MatterCommissioningState::kCommissioned:
      return "commissioned";
    case MatterCommissioningState::kFailed:
      return "failed";
    default:
      return "unknown";
  }
}

}  // namespace xiao_nrf54l15
#endif // NRF54L15_CLEAN_MATTER_CORE_ENABLE
