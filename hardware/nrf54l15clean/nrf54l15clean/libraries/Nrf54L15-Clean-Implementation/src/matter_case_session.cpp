#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && NRF54L15_CLEAN_MATTER_CORE_ENABLE
#include "matter_case_session.h"

#include <Arduino.h>
#include <string.h>
#include <nrf54l15_hal.h>

namespace xiao_nrf54l15 {
namespace {

// HKDF-SHA256 key derivation
void hkdfSha256(const uint8_t* salt, size_t saltLen,
                const uint8_t* ikm, size_t ikmLen,
                const uint8_t* info, size_t infoLen,
                uint8_t* outKey, size_t outKeyLen) {
  // Extract: PRK = HMAC-SHA256(salt, IKM)
  uint8_t prk[32] = {0};
  MatterPbkdf2::hmacSha256(salt, saltLen, ikm, ikmLen, prk);

  // Expand: T(0) = empty, T(i) = HMAC(PRK, T(i-1) || info || i)
  uint8_t t[32] = {0};
  size_t generated = 0U;
  uint8_t counter = 1U;

  while (generated < outKeyLen) {
    uint8_t msg[64 + 32] = {0};
    size_t msgLen = 0U;
    if (counter > 1U) {
      memcpy(msg, t, 32);
      msgLen = 32U;
    }
    memcpy(msg + msgLen, info, infoLen);
    msgLen += infoLen;
    msg[msgLen++] = counter;

    MatterPbkdf2::hmacSha256(prk, 32, msg, msgLen, t);

    size_t copyLen = outKeyLen - generated;
    if (copyLen > 32U) copyLen = 32U;
    memcpy(outKey + generated, t, copyLen);
    generated += copyLen;
    counter++;
  }
}

void writeUint16Le(uint16_t v, uint8_t* b, size_t off) {
  b[off] = v & 0xFFU;
  b[off + 1] = (v >> 8U) & 0xFFU;
}

uint16_t readUint16Le(const uint8_t* b, size_t off) {
  return (uint16_t)b[off] | ((uint16_t)b[off + 1] << 8U);
}

void writeUint32Le(uint32_t v, uint8_t* b, size_t off) {
  b[off] = v & 0xFFU; b[off+1]=(v>>8)&0xFF; b[off+2]=(v>>16)&0xFF; b[off+3]=(v>>24)&0xFF;
}

uint32_t readUint32Le(const uint8_t* b, size_t off) {
  return static_cast<uint32_t>(b[off]) |
         (static_cast<uint32_t>(b[off + 1U]) << 8U) |
         (static_cast<uint32_t>(b[off + 2U]) << 16U) |
         (static_cast<uint32_t>(b[off + 3U]) << 24U);
}

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

bool scalarValid(const Secp256r1Scalar& scalar) {
  Secp256r1::BigNum256 value = {};
  Secp256r1::bnFromBytes(scalar.bytes, &value);
  return !Secp256r1::bnIsZero(value) &&
         Secp256r1::bnCompare(value, Secp256r1::orderN()) < 0;
}

size_t buildCertificateTbs(const CaseCertificate& cert, uint8_t* out,
                           size_t capacity) {
  constexpr size_t kTbsSize = 125U;
  if (out == nullptr || capacity < kTbsSize) {
    return 0U;
  }
  size_t offset = 0U;
  memcpy(out + offset, cert.subjectPubKey, sizeof(cert.subjectPubKey));
  offset += sizeof(cert.subjectPubKey);
  memcpy(out + offset, cert.issuerPubKeyHash,
         sizeof(cert.issuerPubKeyHash));
  offset += sizeof(cert.issuerPubKeyHash);
  writeUint32Le(cert.notBefore, out, offset);
  offset += 4U;
  writeUint32Le(cert.notAfter, out, offset);
  offset += 4U;
  writeUint16Le(cert.vendorId, out, offset);
  offset += 2U;
  writeUint16Le(cert.productId, out, offset);
  offset += 2U;
  memcpy(out + offset, cert.fabricId, sizeof(cert.fabricId));
  offset += sizeof(cert.fabricId);
  memcpy(out + offset, cert.nodeId, sizeof(cert.nodeId));
  offset += sizeof(cert.nodeId);
  return offset;
}

bool serializeCertificate(const CaseCertificate& cert, uint8_t* out,
                          size_t capacity, size_t* outLength) {
  if (outLength != nullptr) {
    *outLength = 0U;
  }
  if (!cert.valid || out == nullptr ||
      capacity < kCaseSerializedCertificateSize) {
    return false;
  }
  const size_t tbsLength = buildCertificateTbs(cert, out, capacity);
  if (tbsLength == 0U ||
      (tbsLength + sizeof(cert.signature)) !=
          kCaseSerializedCertificateSize) {
    return false;
  }
  memcpy(out + tbsLength, cert.signature, sizeof(cert.signature));
  if (outLength != nullptr) {
    *outLength = tbsLength + sizeof(cert.signature);
  }
  return true;
}

bool parseCertificate(const uint8_t* data, size_t length,
                      CaseCertificate* outCert,
                      Secp256r1Point* outSubjectPublicKey) {
  if (data == nullptr || outCert == nullptr ||
      outSubjectPublicKey == nullptr ||
      length != kCaseSerializedCertificateSize) {
    return false;
  }

  CaseCertificate candidate = {};
  size_t offset = 0U;
  memcpy(candidate.subjectPubKey, data + offset,
         sizeof(candidate.subjectPubKey));
  offset += sizeof(candidate.subjectPubKey);
  memcpy(candidate.issuerPubKeyHash, data + offset,
         sizeof(candidate.issuerPubKeyHash));
  offset += sizeof(candidate.issuerPubKeyHash);
  candidate.notBefore = readUint32Le(data, offset);
  offset += 4U;
  candidate.notAfter = readUint32Le(data, offset);
  offset += 4U;
  candidate.vendorId = readUint16Le(data, offset);
  offset += 2U;
  candidate.productId = readUint16Le(data, offset);
  offset += 2U;
  memcpy(candidate.fabricId, data + offset, sizeof(candidate.fabricId));
  offset += sizeof(candidate.fabricId);
  memcpy(candidate.nodeId, data + offset, sizeof(candidate.nodeId));
  offset += sizeof(candidate.nodeId);
  memcpy(candidate.signature, data + offset, sizeof(candidate.signature));
  offset += sizeof(candidate.signature);

  Secp256r1Point subjectPublicKey = {};
  if (offset != length || candidate.notAfter <= candidate.notBefore ||
      !Secp256r1::decodeUncompressed(candidate.subjectPubKey,
                                     &subjectPublicKey)) {
    return false;
  }
  candidate.valid = true;
  *outCert = candidate;
  *outSubjectPublicKey = subjectPublicKey;
  return true;
}

bool certificatesEqual(const CaseCertificate& lhs,
                       const CaseCertificate& rhs) {
  uint8_t lhsBytes[kCaseSerializedCertificateSize] = {0};
  uint8_t rhsBytes[kCaseSerializedCertificateSize] = {0};
  size_t lhsLength = 0U;
  size_t rhsLength = 0U;
  return serializeCertificate(lhs, lhsBytes, sizeof(lhsBytes), &lhsLength) &&
         serializeCertificate(rhs, rhsBytes, sizeof(rhsBytes), &rhsLength) &&
         lhsLength == rhsLength &&
         constantTimeEqual(lhsBytes, rhsBytes, lhsLength);
}

// Software AES-CTR using hardware ECB
bool aesCtrCrypt(const uint8_t key[16],
                 const uint8_t* nonce, size_t nonceLen,
                 const uint8_t* input, size_t inputLen,
                 uint8_t* output) {
  if (key == nullptr || nonce == nullptr ||
      (input == nullptr && inputLen != 0U) ||
      (output == nullptr && inputLen != 0U)) {
    return false;
  }
  Ecb ecb;
  // Build counter block from nonce
  uint8_t counter[16] = {0};
  size_t copyLen = nonceLen < 12U ? nonceLen : 12U;
  memcpy(counter, nonce, copyLen);

  size_t offset = 0U;
  uint32_t blockCounter = 0U;
  while (offset < inputLen) {
    // Set counter bytes in last 4 bytes
    counter[12] = (blockCounter >> 24U) & 0xFFU;
    counter[13] = (blockCounter >> 16U) & 0xFFU;
    counter[14] = (blockCounter >> 8U) & 0xFFU;
    counter[15] = blockCounter & 0xFFU;

    uint8_t keystream[16] = {0};
    if (!ecb.encryptBlock(key, counter, keystream)) {
      return false;
    }

    size_t chunk = inputLen - offset;
    if (chunk > 16U) chunk = 16U;
    for (size_t i = 0U; i < chunk; ++i) {
      output[offset + i] = input[offset + i] ^ keystream[i];
    }
    offset += chunk;
    blockCounter++;
  }
  return true;
}

bool computeAuthenticationTag(const uint8_t key[16],
                              const uint8_t* nonce, size_t nonceLen,
                              const uint8_t* aad, size_t aadLen,
                              const uint8_t* ciphertext, size_t ciphertextLen,
                              uint8_t outTag[8]) {
  if (key == nullptr || nonce == nullptr || nonceLen > 16U ||
      (aad == nullptr && aadLen != 0U) ||
      (ciphertext == nullptr && ciphertextLen != 0U) || outTag == nullptr ||
      aadLen > UINT32_MAX || ciphertextLen > UINT32_MAX) {
    return false;
  }

  uint8_t aadHash[32] = {0};
  uint8_t ciphertextHash[32] = {0};
  MatterPbkdf2::sha256(aad, aadLen, aadHash);
  MatterPbkdf2::sha256(ciphertext, ciphertextLen, ciphertextHash);

  constexpr uint8_t kDomain[] = {'C', 'A', 'S', 'E', '-', 'A', 'E', 'A', 'D'};
  uint8_t macInput[sizeof(kDomain) + 1U + 16U + 4U + 4U + 32U + 32U] = {0};
  size_t offset = 0U;
  memcpy(macInput + offset, kDomain, sizeof(kDomain));
  offset += sizeof(kDomain);
  macInput[offset++] = static_cast<uint8_t>(nonceLen);
  memcpy(macInput + offset, nonce, nonceLen);
  offset += 16U;
  writeUint32Le(static_cast<uint32_t>(aadLen), macInput, offset);
  offset += 4U;
  writeUint32Le(static_cast<uint32_t>(ciphertextLen), macInput, offset);
  offset += 4U;
  memcpy(macInput + offset, aadHash, sizeof(aadHash));
  offset += sizeof(aadHash);
  memcpy(macInput + offset, ciphertextHash, sizeof(ciphertextHash));
  offset += sizeof(ciphertextHash);

  uint8_t mac[32] = {0};
  MatterPbkdf2::hmacSha256(key, 16U, macInput, offset, mac);
  memcpy(outTag, mac, 8U);
  secureZero(mac, sizeof(mac));
  return true;
}

// Encrypt-then-MAC construction used only by this experimental CASE surface.
bool aeadEncrypt(const uint8_t key[16],
                 const uint8_t* nonce, size_t nonceLen,
                 const uint8_t* plaintext, size_t plaintextLen,
                 const uint8_t* aad, size_t aadLen,
                 uint8_t* outCiphertext, size_t outCapacity,
                 uint16_t* outLen) {
  if (outLen != nullptr) {
    *outLen = 0U;
  }
  if (key == nullptr || nonce == nullptr || outCiphertext == nullptr ||
      (plaintext == nullptr && plaintextLen != 0U) ||
      (aad == nullptr && aadLen != 0U) ||
      plaintextLen > (UINT16_MAX - 8U) ||
      outCapacity < (plaintextLen + 8U)) {
    return false;
  }
  // Encrypt with AES-CTR
  if (!aesCtrCrypt(key, nonce, nonceLen, plaintext, plaintextLen, outCiphertext)) {
    return false;
  }

  if (!computeAuthenticationTag(key, nonce, nonceLen, aad, aadLen,
                                outCiphertext, plaintextLen,
                                outCiphertext + plaintextLen)) {
    secureZero(outCiphertext, plaintextLen);
    return false;
  }

  if (outLen != nullptr) *outLen = (uint16_t)(plaintextLen + 8U);
  return true;
}

bool aeadDecrypt(const uint8_t key[16],
                 const uint8_t* nonce, size_t nonceLen,
                 const uint8_t* ciphertext, size_t ciphertextLen,
                 const uint8_t* aad, size_t aadLen,
                 uint8_t* outPlaintext, size_t outCapacity,
                 uint16_t* outLen) {
  if (outLen != nullptr) {
    *outLen = 0U;
  }
  if (key == nullptr || nonce == nullptr || ciphertext == nullptr ||
      outPlaintext == nullptr || (aad == nullptr && aadLen != 0U) ||
      ciphertextLen < 8U || ciphertextLen > UINT16_MAX ||
      outCapacity < (ciphertextLen - 8U)) {
    return false;
  }
  size_t ctLen = ciphertextLen - 8U;
  const uint8_t* tag = ciphertext + ctLen;

  uint8_t expectedTag[8] = {0};
  if (!computeAuthenticationTag(key, nonce, nonceLen, aad, aadLen,
                                ciphertext, ctLen, expectedTag) ||
      !constantTimeEqual(tag, expectedTag, sizeof(expectedTag))) {
    return false;
  }

  // Decrypt with AES-CTR
  if (!aesCtrCrypt(key, nonce, nonceLen, ciphertext, ctLen, outPlaintext)) {
    return false;
  }

  if (outLen != nullptr) *outLen = (uint16_t)ctLen;
  return true;
}

}  // namespace

// ─── Public API ──────────────────────────────────────────────────

bool MatterCaseSession::beginAsInitiator(StateCallback callback,
                                         void* context) {
  if (state_ != CaseState::kIdle || !localCertificateConfigured_ ||
      !peerCertificateConfigured_) {
    return false;
  }
  clearSessionSecrets();
  initiator_ = true;
  callback_ = callback;
  callbackContext_ = context;
  if (!generateRandom(reinterpret_cast<uint8_t*>(&localSessionId_),
                      sizeof(localSessionId_))) {
    fail();
    return false;
  }
  if (localSessionId_ == 0U) localSessionId_ = 1U;
  return true;
}

bool MatterCaseSession::beginAsResponder(StateCallback callback,
                                          void* context) {
  if (state_ != CaseState::kIdle || !localCertificateConfigured_ ||
      !peerCertificateConfigured_) {
    return false;
  }
  clearSessionSecrets();
  initiator_ = false;
  callback_ = callback;
  callbackContext_ = context;
  if (!generateRandom(reinterpret_cast<uint8_t*>(&localSessionId_),
                      sizeof(localSessionId_))) {
    fail();
    return false;
  }
  if (localSessionId_ == 0U) localSessionId_ = 1U;
  return true;
}

void MatterCaseSession::end() {
  clearSessionSecrets();
  callback_ = nullptr;
  callbackContext_ = nullptr;
  initiator_ = false;
  state_ = CaseState::kIdle;
}

bool MatterCaseSession::active() const {
  return state_ != CaseState::kIdle && state_ != CaseState::kFailed;
}

CaseState MatterCaseSession::state() const { return state_; }

const char* MatterCaseSession::stateName() const {
  return stateName(state_);
}

bool MatterCaseSession::setCertificate(const CaseCertificate& cert,
                                        const Secp256r1Scalar& privateKey) {
  if (state_ != CaseState::kIdle || !cert.valid ||
      cert.notAfter <= cert.notBefore || !scalarValid(privateKey)) {
    return false;
  }

  Secp256r1Point publicKey = {};
  uint8_t encodedPublicKey[kCaseEphemeralKeySize] = {0};
  if (!Secp256r1::scalarMultiplyBase(privateKey, &publicKey) ||
      Secp256r1::isInfinity(publicKey) ||
      !Secp256r1::isOnCurve(publicKey)) {
    return false;
  }
  Secp256r1::encodeUncompressed(publicKey, encodedPublicKey);
  if (!constantTimeEqual(encodedPublicKey, cert.subjectPubKey,
                         sizeof(encodedPublicKey)) ||
      !verifyCertificate(cert, publicKey)) {
    return false;
  }

  localCert_ = cert;
  localPrivateKey_ = privateKey;
  localPublicKey_ = publicKey;
  localCertificateConfigured_ = true;
  return true;
}

bool MatterCaseSession::setPeerCertificate(const CaseCertificate& cert) {
  if (state_ != CaseState::kIdle || !cert.valid ||
      cert.notAfter <= cert.notBefore) {
    return false;
  }
  Secp256r1Point publicKey = {};
  if (!Secp256r1::decodeUncompressed(cert.subjectPubKey, &publicKey) ||
      !verifyCertificate(cert, publicKey)) {
    return false;
  }
  peerCert_ = cert;
  peerPublicKey_ = publicKey;
  peerCertificateConfigured_ = true;
  return true;
}

bool MatterCaseSession::generateSelfSignedCert(
    const Secp256r1Scalar& privateKey,
    const Secp256r1Point& publicKey,
    uint16_t vendorId, uint16_t productId,
    CaseCertificate* outCert) {
  if (outCert == nullptr) return false;
  *outCert = CaseCertificate{};

  Secp256r1Point derivedPublicKey = {};
  uint8_t derivedEncoded[kCaseEphemeralKeySize] = {0};
  uint8_t suppliedEncoded[kCaseEphemeralKeySize] = {0};
  if (!scalarValid(privateKey) || !Secp256r1::isOnCurve(publicKey) ||
      Secp256r1::isInfinity(publicKey) ||
      !Secp256r1::scalarMultiplyBase(privateKey, &derivedPublicKey)) {
    return false;
  }
  Secp256r1::encodeUncompressed(derivedPublicKey, derivedEncoded);
  Secp256r1::encodeUncompressed(publicKey, suppliedEncoded);
  if (!constantTimeEqual(derivedEncoded, suppliedEncoded,
                         sizeof(derivedEncoded))) {
    return false;
  }

  CaseCertificate candidate = {};
  memcpy(candidate.subjectPubKey, suppliedEncoded, sizeof(suppliedEncoded));

  // Issuer = self, so hash the same public key
  MatterPbkdf2::sha256(candidate.subjectPubKey,
                      sizeof(candidate.subjectPubKey),
                      candidate.issuerPubKeyHash);

  candidate.vendorId = vendorId;
  candidate.productId = productId;
  candidate.notBefore = static_cast<uint32_t>(millis() / 1000U);
  candidate.notAfter = candidate.notBefore + 86400U * 365U;  // 1 year
  // Simple node ID = first 8 bytes of pub key hash
  memcpy(candidate.fabricId, candidate.issuerPubKeyHash,
         sizeof(candidate.fabricId));
  memcpy(candidate.nodeId,
         candidate.issuerPubKeyHash + sizeof(candidate.fabricId),
         sizeof(candidate.nodeId));

  // Sign all serialized identity and validity fields.
  uint8_t tbsData[128] = {0};
  const size_t tbsLength =
      buildCertificateTbs(candidate, tbsData, sizeof(tbsData));
  if (tbsLength == 0U) {
    return false;
  }

  uint8_t hash[32] = {0};
  MatterPbkdf2::sha256(tbsData, tbsLength, hash);

  if (!Secp256r1::ecdsaSign(privateKey, hash,
                            candidate.signature, candidate.signature + 32)) {
    return false;
  }

  candidate.valid = true;
  *outCert = candidate;
  return true;
}

// ─── Sigma1 ──────────────────────────────────────────────────────

bool MatterCaseSession::buildSigma1(CaseSigma1* outMsg) {
  if (outMsg == nullptr || !initiator_ || state_ != CaseState::kIdle ||
      localSessionId_ == 0U) {
    return false;
  }

  *outMsg = CaseSigma1{};
  if (!generateRandom(outMsg->initiatorRandom, kCaseRandomSize)) {
    fail();
    return false;
  }
  memcpy(initiatorRandom_, outMsg->initiatorRandom, kCaseRandomSize);

  outMsg->initiatorSessionId = localSessionId_;

  // Generate ephemeral key pair
  if (!Secp256r1::generateKeyPair(&ephPrivateKey_, &ephPublicKey_)) {
    fail();
    return false;
  }
  Secp256r1::encodeUncompressed(ephPublicKey_, outMsg->initiatorEphPubKey);

  // No resumption
  outMsg->resumptionIdLen = 0U;

  advanceState(CaseState::kSigma1Sent);
  return true;
}

bool MatterCaseSession::processSigma1(const CaseSigma1& msg) {
  if (initiator_ || state_ != CaseState::kIdle) return false;
  if (msg.initiatorSessionId == 0U || msg.resumptionIdLen != 0U ||
      allZero(msg.initiatorRandom, sizeof(msg.initiatorRandom))) {
    fail();
    return false;
  }

  Secp256r1Point peerEphemeralPublicKey = {};
  if (!Secp256r1::decodeUncompressed(msg.initiatorEphPubKey,
                                     &peerEphemeralPublicKey)) {
    fail();
    return false;
  }

  memcpy(initiatorRandom_, msg.initiatorRandom, kCaseRandomSize);
  peerSessionId_ = msg.initiatorSessionId;
  peerEphPublicKey_ = peerEphemeralPublicKey;
  advanceState(CaseState::kSigma1Received);
  return true;
}

// ─── Sigma2 ──────────────────────────────────────────────────────

bool MatterCaseSession::buildSigma2(CaseSigma2* outMsg) {
  if (outMsg == nullptr || initiator_ ||
      state_ != CaseState::kSigma1Received) {
    return false;
  }

  *outMsg = CaseSigma2{};
  if (!generateRandom(outMsg->responderRandom, kCaseRandomSize)) {
    fail();
    return false;
  }
  memcpy(responderRandom_, outMsg->responderRandom, kCaseRandomSize);

  outMsg->responderSessionId = localSessionId_;

  // Generate ephemeral key pair
  if (!Secp256r1::generateKeyPair(&ephPrivateKey_, &ephPublicKey_)) {
    fail();
    return false;
  }
  Secp256r1::encodeUncompressed(ephPublicKey_, outMsg->responderEphPubKey);

  // Derive shared secret via ECDH: shared = ephPrivate * peerEphPublic
  Secp256r1Point sharedPoint = {};
  if (!Secp256r1::scalarMultiply(ephPrivateKey_, peerEphPublicKey_,
                                 &sharedPoint) ||
      Secp256r1::isInfinity(sharedPoint) ||
      !Secp256r1::isOnCurve(sharedPoint)) {
    fail();
    return false;
  }
  memcpy(sessionKeys_.sharedSecret, sharedPoint.x, 32);
  if (!deriveSessionKeys()) {
    fail();
    return false;
  }

  uint8_t proofHash[kCaseHashSize] = {0};
  if (!buildTranscriptProofHash(2U, proofHash) ||
      !Secp256r1::ecdsaSign(localPrivateKey_, proofHash,
                            outMsg->transcriptSignature,
                            outMsg->transcriptSignature + 32U)) {
    fail();
    return false;
  }

  // Encrypt local certificate with the responder-to-initiator key.
  uint8_t certBuf[256] = {0};
  size_t certLen = 0U;
  if (!serializeCertificate(localCert_, certBuf, sizeof(certBuf), &certLen)) {
    fail();
    return false;
  }

  uint8_t nonce[13] = {0};
  memcpy(nonce, responderRandom_, 8);
  nonce[8] = 0x02;  // Sigma2 nonce marker

  if (!encryptWithKey(sessionKeys_.r2iKey, nonce, sizeof(nonce),
                      certBuf, certLen, nullptr, 0U,
                      outMsg->encryptedCert, sizeof(outMsg->encryptedCert),
                      &outMsg->encryptedCertLen)) {
    fail();
    return false;
  }

  advanceState(CaseState::kSigma2Sent);
  return true;
}

bool MatterCaseSession::processSigma2(const CaseSigma2& msg) {
  constexpr size_t kExpectedEncryptedCertificateSize =
      kCaseSerializedCertificateSize + 8U;
  if (!initiator_ || state_ != CaseState::kSigma1Sent) return false;
  if (msg.responderSessionId == 0U ||
      msg.encryptedCertLen != kExpectedEncryptedCertificateSize ||
      allZero(msg.responderRandom, sizeof(msg.responderRandom))) {
    fail();
    return false;
  }

  Secp256r1Point peerEphemeralPublicKey = {};
  if (!Secp256r1::decodeUncompressed(msg.responderEphPubKey,
                                     &peerEphemeralPublicKey)) {
    fail();
    return false;
  }
  memcpy(responderRandom_, msg.responderRandom, kCaseRandomSize);
  peerSessionId_ = msg.responderSessionId;
  peerEphPublicKey_ = peerEphemeralPublicKey;

  uint8_t proofHash[kCaseHashSize] = {0};
  if (!buildTranscriptProofHash(2U, proofHash) ||
      !Secp256r1::ecdsaVerify(peerPublicKey_, proofHash,
                              msg.transcriptSignature,
                              msg.transcriptSignature + 32U)) {
    fail();
    return false;
  }

  // Derive shared secret via ECDH
  Secp256r1Point sharedPoint = {};
  if (!Secp256r1::scalarMultiply(ephPrivateKey_, peerEphemeralPublicKey,
                                 &sharedPoint) ||
      Secp256r1::isInfinity(sharedPoint) ||
      !Secp256r1::isOnCurve(sharedPoint)) {
    fail();
    return false;
  }
  memcpy(sessionKeys_.sharedSecret, sharedPoint.x, 32);
  if (!deriveSessionKeys()) {
    fail();
    return false;
  }

  // Decrypt certificate
  uint8_t nonce[13] = {0};
  memcpy(nonce, msg.responderRandom, 8);
  nonce[8] = 0x02;

  uint8_t certBuf[256] = {0};
  uint16_t certLen = 0U;
  if (!decryptWithKey(sessionKeys_.r2iKey, nonce, sizeof(nonce),
                      msg.encryptedCert, msg.encryptedCertLen,
                      nullptr, 0U, certBuf, sizeof(certBuf), &certLen)) {
    fail();
    return false;
  }

  CaseCertificate receivedCertificate = {};
  Secp256r1Point receivedPublicKey = {};
  if (!parseCertificate(certBuf, certLen, &receivedCertificate,
                        &receivedPublicKey) ||
      !verifyCertificate(receivedCertificate, receivedPublicKey) ||
      !certificatesEqual(receivedCertificate, peerCert_)) {
    fail();
    return false;
  }

  advanceState(CaseState::kSigma2Received);
  return true;
}

// ─── Sigma3 ──────────────────────────────────────────────────────

bool MatterCaseSession::buildSigma3(CaseSigma3* outMsg) {
  if (outMsg == nullptr || !initiator_ ||
      state_ != CaseState::kSigma2Received) {
    return false;
  }

  *outMsg = CaseSigma3{};

  uint8_t proofHash[kCaseHashSize] = {0};
  if (!buildTranscriptProofHash(3U, proofHash) ||
      !Secp256r1::ecdsaSign(localPrivateKey_, proofHash,
                            outMsg->transcriptSignature,
                            outMsg->transcriptSignature + 32U)) {
    fail();
    return false;
  }

  // Serialize and encrypt local certificate
  uint8_t certBuf[256] = {0};
  size_t certLen = 0U;
  if (!serializeCertificate(localCert_, certBuf, sizeof(certBuf), &certLen)) {
    fail();
    return false;
  }

  uint8_t nonce[13] = {0};
  memcpy(nonce, initiatorRandom_, 8);
  nonce[8] = 0x03;

  if (!encryptWithKey(sessionKeys_.i2rKey, nonce, sizeof(nonce),
                      certBuf, certLen, nullptr, 0U,
                      outMsg->encryptedCert, sizeof(outMsg->encryptedCert),
                      &outMsg->encryptedCertLen)) {
    fail();
    return false;
  }

  advanceState(CaseState::kSigma3Sent);
  advanceState(CaseState::kEstablished);
  return true;
}

bool MatterCaseSession::processSigma3(const CaseSigma3& msg) {
  constexpr size_t kExpectedEncryptedCertificateSize =
      kCaseSerializedCertificateSize + 8U;
  if (initiator_ || state_ != CaseState::kSigma2Sent) return false;
  if (msg.encryptedCertLen != kExpectedEncryptedCertificateSize) {
    fail();
    return false;
  }

  uint8_t proofHash[kCaseHashSize] = {0};
  if (!buildTranscriptProofHash(3U, proofHash) ||
      !Secp256r1::ecdsaVerify(peerPublicKey_, proofHash,
                              msg.transcriptSignature,
                              msg.transcriptSignature + 32U)) {
    fail();
    return false;
  }

  uint8_t nonce[13] = {0};
  memcpy(nonce, initiatorRandom_, 8);
  nonce[8] = 0x03;

  uint8_t certBuf[256] = {0};
  uint16_t certLen = 0U;
  if (!decryptWithKey(sessionKeys_.i2rKey, nonce, sizeof(nonce),
                      msg.encryptedCert, msg.encryptedCertLen,
                      nullptr, 0U, certBuf, sizeof(certBuf), &certLen)) {
    fail();
    return false;
  }

  CaseCertificate receivedCertificate = {};
  Secp256r1Point receivedPublicKey = {};
  if (!parseCertificate(certBuf, certLen, &receivedCertificate,
                        &receivedPublicKey) ||
      !verifyCertificate(receivedCertificate, receivedPublicKey) ||
      !certificatesEqual(receivedCertificate, peerCert_)) {
    fail();
    return false;
  }

  advanceState(CaseState::kEstablished);
  return true;
}

// ─── Session Keys ────────────────────────────────────────────────

bool MatterCaseSession::getSessionKeys(CaseSessionKeys* outKeys) const {
  if (outKeys == nullptr || !sessionKeys_.valid ||
      state_ != CaseState::kEstablished) {
    return false;
  }
  *outKeys = sessionKeys_;
  return true;
}

bool MatterCaseSession::deriveSessionKeys() {
  if (allZero(sessionKeys_.sharedSecret,
              sizeof(sessionKeys_.sharedSecret))) {
    return false;
  }

  // Bind the keys to both randoms and both session identifiers.
  uint8_t transcript[2U * kCaseRandomSize + 4U] = {0};
  size_t transcriptLength = 0U;
  memcpy(transcript + transcriptLength, initiatorRandom_,
         sizeof(initiatorRandom_));
  transcriptLength += sizeof(initiatorRandom_);
  memcpy(transcript + transcriptLength, responderRandom_,
         sizeof(responderRandom_));
  transcriptLength += sizeof(responderRandom_);
  const uint16_t initiatorSessionId = initiator_ ? localSessionId_
                                                  : peerSessionId_;
  const uint16_t responderSessionId = initiator_ ? peerSessionId_
                                                  : localSessionId_;
  writeUint16Le(initiatorSessionId, transcript, transcriptLength);
  transcriptLength += 2U;
  writeUint16Le(responderSessionId, transcript, transcriptLength);
  transcriptLength += 2U;
  uint8_t salt[32] = {0};
  MatterPbkdf2::sha256(transcript, transcriptLength, salt);

  // "Session Keys" info
  const char* info = "Session Keys";
  const size_t infoLen = strlen(info);

  uint8_t keyMaterial[2U * kCaseAesKeySize] = {0};
  hkdfSha256(salt, 32, sessionKeys_.sharedSecret, 32,
             reinterpret_cast<const uint8_t*>(info), infoLen,
             keyMaterial, sizeof(keyMaterial));

  // Split: first 16 bytes = i2r key, next 16 = r2i key
  memcpy(sessionKeys_.i2rKey, keyMaterial, kCaseAesKeySize);
  memcpy(sessionKeys_.r2iKey, keyMaterial + kCaseAesKeySize,
         kCaseAesKeySize);

  encryptI2rCounter_ = 0U;
  encryptR2iCounter_ = 0U;
  decryptI2rCounter_ = 0U;
  decryptR2iCounter_ = 0U;
  sessionKeys_.valid = true;
  secureZero(keyMaterial, sizeof(keyMaterial));
  return true;
}

// ─── Experimental Session Encryption ─────────────────────────────

bool MatterCaseSession::encryptMessage(
    const uint8_t* plaintext, uint16_t plaintextLen,
    const uint8_t* aad, uint16_t aadLen,
    uint8_t* outCiphertext, size_t outCapacity, uint16_t* outLen,
    bool initiatorToResponder) {
  if (outLen != nullptr) *outLen = 0U;
  if (!sessionKeys_.valid || state_ != CaseState::kEstablished ||
      outCiphertext == nullptr ||
      (plaintext == nullptr && plaintextLen != 0U) ||
      (aad == nullptr && aadLen != 0U) ||
      (initiatorToResponder != initiator_)) {
    return false;
  }

  const uint8_t* key = initiatorToResponder
                           ? sessionKeys_.i2rKey
                           : sessionKeys_.r2iKey;

  uint32_t* counter = initiatorToResponder ? &encryptI2rCounter_
                                            : &encryptR2iCounter_;
  if (*counter == UINT32_MAX) {
    return false;
  }
  const uint32_t nextCounter = *counter + 1U;
  uint8_t nonce[13] = {0};
  memcpy(nonce,
         initiatorToResponder ? initiatorRandom_ : responderRandom_, 8U);
  writeUint32Le(nextCounter, nonce, 8);
  nonce[12] = initiatorToResponder ? 0x01 : 0x02;

  if (!aeadEncrypt(key, nonce, sizeof(nonce), plaintext, plaintextLen,
                   aad, aadLen, outCiphertext, outCapacity, outLen)) {
    return false;
  }
  *counter = nextCounter;
  return true;
}

bool MatterCaseSession::decryptMessage(
    const uint8_t* ciphertext, uint16_t ciphertextLen,
    const uint8_t* aad, uint16_t aadLen,
    uint8_t* outPlaintext, size_t outCapacity, uint16_t* outLen,
    bool initiatorToResponder) {
  if (outLen != nullptr) *outLen = 0U;
  if (!sessionKeys_.valid || state_ != CaseState::kEstablished ||
      ciphertext == nullptr || outPlaintext == nullptr ||
      ciphertextLen < 8U || (aad == nullptr && aadLen != 0U) ||
      (initiatorToResponder == initiator_)) {
    return false;
  }

  const uint8_t* key = initiatorToResponder
                           ? sessionKeys_.i2rKey
                           : sessionKeys_.r2iKey;

  uint32_t* counter = initiatorToResponder ? &decryptI2rCounter_
                                            : &decryptR2iCounter_;
  if (*counter == UINT32_MAX) {
    return false;
  }
  const uint32_t nextCounter = *counter + 1U;
  uint8_t nonce[13] = {0};
  memcpy(nonce,
         initiatorToResponder ? initiatorRandom_ : responderRandom_, 8U);
  writeUint32Le(nextCounter, nonce, 8);
  nonce[12] = initiatorToResponder ? 0x01 : 0x02;

  if (!aeadDecrypt(key, nonce, sizeof(nonce), ciphertext, ciphertextLen,
                   aad, aadLen, outPlaintext, outCapacity, outLen)) {
    return false;
  }
  *counter = nextCounter;
  return true;
}

// ─── Certificate Verification ────────────────────────────────────

bool MatterCaseSession::verifyCertificate(const CaseCertificate& cert,
                                           const Secp256r1Point& issuerPubKey) {
  if (!cert.valid || cert.notAfter <= cert.notBefore ||
      Secp256r1::isInfinity(issuerPubKey) ||
      !Secp256r1::isOnCurve(issuerPubKey)) {
    return false;
  }

  uint8_t encodedIssuer[kCaseEphemeralKeySize] = {0};
  uint8_t issuerHash[kCaseHashSize] = {0};
  Secp256r1::encodeUncompressed(issuerPubKey, encodedIssuer);
  MatterPbkdf2::sha256(encodedIssuer, sizeof(encodedIssuer), issuerHash);
  if (!constantTimeEqual(issuerHash, cert.issuerPubKeyHash,
                         sizeof(issuerHash))) {
    return false;
  }

  // Rebuild TBS data
  uint8_t tbsData[256] = {0};
  const size_t tbsLength =
      buildCertificateTbs(cert, tbsData, sizeof(tbsData));
  if (tbsLength == 0U) {
    return false;
  }

  uint8_t hash[32] = {0};
  MatterPbkdf2::sha256(tbsData, tbsLength, hash);

  return Secp256r1::ecdsaVerify(issuerPubKey, hash,
                                cert.signature, cert.signature + 32);
}

// ─── Internal Helpers ───────────────────────────────────────────

bool MatterCaseSession::buildTranscriptProofHash(
    uint8_t proofMarker, uint8_t outHash[kCaseHashSize]) const {
  if (outHash == nullptr || (proofMarker != 2U && proofMarker != 3U)) {
    return false;
  }

  const char* domain = proofMarker == 2U
                           ? "CASE-EXPERIMENTAL-SIGMA2-PROOF"
                           : "CASE-EXPERIMENTAL-SIGMA3-PROOF";
  const size_t domainLength = strlen(domain);
  uint8_t transcript[256] = {0};
  uint8_t initiatorEphemeral[kCaseEphemeralKeySize] = {0};
  uint8_t responderEphemeral[kCaseEphemeralKeySize] = {0};
  size_t offset = 0U;

  memcpy(transcript + offset, domain, domainLength);
  offset += domainLength;
  transcript[offset++] = proofMarker;
  memcpy(transcript + offset, initiatorRandom_, sizeof(initiatorRandom_));
  offset += sizeof(initiatorRandom_);
  memcpy(transcript + offset, responderRandom_, sizeof(responderRandom_));
  offset += sizeof(responderRandom_);

  const uint16_t initiatorSessionId =
      initiator_ ? localSessionId_ : peerSessionId_;
  const uint16_t responderSessionId =
      initiator_ ? peerSessionId_ : localSessionId_;
  writeUint16Le(initiatorSessionId, transcript, offset);
  offset += 2U;
  writeUint16Le(responderSessionId, transcript, offset);
  offset += 2U;

  const Secp256r1Point& initiatorPoint =
      initiator_ ? ephPublicKey_ : peerEphPublicKey_;
  const Secp256r1Point& responderPoint =
      initiator_ ? peerEphPublicKey_ : ephPublicKey_;
  Secp256r1::encodeUncompressed(initiatorPoint, initiatorEphemeral);
  Secp256r1::encodeUncompressed(responderPoint, responderEphemeral);
  memcpy(transcript + offset, initiatorEphemeral, sizeof(initiatorEphemeral));
  offset += sizeof(initiatorEphemeral);
  memcpy(transcript + offset, responderEphemeral, sizeof(responderEphemeral));
  offset += sizeof(responderEphemeral);

  MatterPbkdf2::sha256(transcript, offset, outHash);
  secureZero(transcript, sizeof(transcript));
  secureZero(initiatorEphemeral, sizeof(initiatorEphemeral));
  secureZero(responderEphemeral, sizeof(responderEphemeral));
  return true;
}

bool MatterCaseSession::encryptWithKey(
    const uint8_t key[16], const uint8_t* nonce, size_t nonceLen,
    const uint8_t* plaintext, size_t plaintextLen,
    const uint8_t* aad, size_t aadLen,
    uint8_t* outCiphertext, size_t outCapacity, uint16_t* outLen) {
  return aeadEncrypt(key, nonce, nonceLen, plaintext, plaintextLen,
                     aad, aadLen, outCiphertext, outCapacity, outLen);
}

bool MatterCaseSession::decryptWithKey(
    const uint8_t key[16], const uint8_t* nonce, size_t nonceLen,
    const uint8_t* ciphertext, size_t ciphertextLen,
    const uint8_t* aad, size_t aadLen,
    uint8_t* outPlaintext, size_t outCapacity, uint16_t* outLen) {
  return aeadDecrypt(key, nonce, nonceLen, ciphertext, ciphertextLen,
                     aad, aadLen, outPlaintext, outCapacity, outLen);
}

bool MatterCaseSession::generateRandom(uint8_t* out, size_t len) {
  if (out == nullptr && len != 0U) {
    return false;
  }
  MatterRng rng;
  return rng.getRandomBytes(out, len, 400000UL);
}

void MatterCaseSession::clearSessionSecrets() {
  secureZero(&ephPrivateKey_, sizeof(ephPrivateKey_));
  secureZero(&ephPublicKey_, sizeof(ephPublicKey_));
  secureZero(&peerEphPublicKey_, sizeof(peerEphPublicKey_));
  secureZero(&sessionKeys_, sizeof(sessionKeys_));
  secureZero(initiatorRandom_, sizeof(initiatorRandom_));
  secureZero(responderRandom_, sizeof(responderRandom_));
  localSessionId_ = 0U;
  peerSessionId_ = 0U;
  encryptI2rCounter_ = 0U;
  encryptR2iCounter_ = 0U;
  decryptI2rCounter_ = 0U;
  decryptR2iCounter_ = 0U;
}

void MatterCaseSession::fail() {
  if (state_ == CaseState::kFailed) {
    return;
  }
  clearSessionSecrets();
  advanceState(CaseState::kFailed);
}

void MatterCaseSession::advanceState(CaseState newState) {
  state_ = newState;
  if (callback_ != nullptr) {
    callback_(callbackContext_, newState);
  }
}

const char* MatterCaseSession::stateName(CaseState state) {
  switch (state) {
    case CaseState::kIdle: return "idle";
    case CaseState::kSigma1Sent: return "sigma1-sent";
    case CaseState::kSigma1Received: return "sigma1-received";
    case CaseState::kSigma2Sent: return "sigma2-sent";
    case CaseState::kSigma2Received: return "sigma2-received";
    case CaseState::kSigma3Sent: return "sigma3-sent";
    case CaseState::kEstablished: return "established";
    case CaseState::kFailed: return "failed";
    default: return "unknown";
  }
}

}  // namespace xiao_nrf54l15
#endif // NRF54L15_CLEAN_MATTER_CORE_ENABLE
