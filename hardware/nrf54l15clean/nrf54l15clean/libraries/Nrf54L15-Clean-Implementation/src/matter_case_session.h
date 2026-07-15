#pragma once

#include <stddef.h>
#include <stdint.h>

#include "matter_secp256r1.h"
#include "matter_pbkdf2.h"
#include "matter_rng.h"

namespace xiao_nrf54l15 {

// Experimental CASE-like session surface for two-core demonstrations. Its
// certificate and message encodings are not Matter wire-compatible Sigma/CASE.

constexpr size_t kCaseHashSize = 32U;
constexpr size_t kCaseAesKeySize = 16U;  // AES-128 key
constexpr size_t kCaseIvSize = 13U;
constexpr size_t kCaseMicSize = 8U;
constexpr size_t kCaseRandomSize = 32U;
constexpr size_t kCaseEphemeralKeySize = 65U;  // Uncompressed P-256
constexpr size_t kCaseCertificateMaxSize = 256U;
constexpr size_t kCaseSignatureSize = 64U;  // r || s
constexpr size_t kCaseSerializedCertificateSize = 189U;

enum class CaseState : uint8_t {
  kIdle = 0U,
  kSigma1Sent = 1U,
  kSigma1Received = 2U,
  kSigma2Sent = 3U,
  kSigma2Received = 4U,
  kSigma3Sent = 5U,
  kEstablished = 6U,
  kFailed = 7U,
};

struct CaseSessionKeys {
  uint8_t i2rKey[kCaseAesKeySize] = {0};  // Initiator → Responder encryption key
  uint8_t r2iKey[kCaseAesKeySize] = {0};  // Responder → Initiator encryption key
  uint8_t sharedSecret[kCaseHashSize] = {0};
  bool valid = false;
};

struct CaseMessageHeader {
  uint8_t exchangeFlags = 0U;
  uint8_t sessionType = 0U;  // 2 = CASE
  uint16_t sessionId = 0U;
  uint8_t messageId = 0U;
};

struct CaseSigma1 {
  uint8_t initiatorRandom[kCaseRandomSize] = {0};
  uint16_t initiatorSessionId = 0U;
  uint8_t initiatorEphPubKey[kCaseEphemeralKeySize] = {0};
  uint8_t resumptionId[16] = {0};
  uint16_t resumptionIdLen = 0U;
};

struct CaseSigma2 {
  uint8_t responderRandom[kCaseRandomSize] = {0};
  uint16_t responderSessionId = 0U;
  uint8_t responderEphPubKey[kCaseEphemeralKeySize] = {0};
  uint8_t transcriptSignature[kCaseSignatureSize] = {0};
  uint8_t encryptedCert[kCaseCertificateMaxSize] = {0};
  uint16_t encryptedCertLen = 0U;
};

struct CaseSigma3 {
  uint8_t transcriptSignature[kCaseSignatureSize] = {0};
  uint8_t encryptedCert[kCaseCertificateMaxSize] = {0};
  uint16_t encryptedCertLen = 0U;
};

// Minimal self-signed certificate
struct CaseCertificate {
  uint8_t subjectPubKey[65] = {0};    // Uncompressed P-256 public key
  uint8_t issuerPubKeyHash[32] = {0}; // SHA256 of issuer's public key (self: same)
  uint8_t signature[64] = {0};        // ECDSA signature (r || s)
  uint32_t notBefore = 0U;            // Unix-ish timestamp
  uint32_t notAfter = 0U;
  uint16_t vendorId = 0U;
  uint16_t productId = 0U;
  uint8_t fabricId[8] = {0};
  uint8_t nodeId[8] = {0};
  bool valid = false;
};

class MatterCaseSession {
 public:
  using StateCallback = void (*)(void* context, CaseState state);

  MatterCaseSession() = default;

  bool beginAsInitiator(StateCallback callback = nullptr,
                        void* context = nullptr);
  bool beginAsResponder(StateCallback callback = nullptr,
                        void* context = nullptr);
  void end();
  bool active() const;
  CaseState state() const;
  const char* stateName() const;

  // Set the local certificate and private key
  bool setCertificate(const CaseCertificate& cert,
                      const Secp256r1Scalar& privateKey);
  // Pins the expected peer identity. CASE will reject an encrypted Sigma
  // certificate that does not exactly match this validated certificate.
  bool setPeerCertificate(const CaseCertificate& cert);

  // Generate a self-signed certificate for testing
  static bool generateSelfSignedCert(
      const Secp256r1Scalar& privateKey,
      const Secp256r1Point& publicKey,
      uint16_t vendorId, uint16_t productId,
      CaseCertificate* outCert);

  // Build and process protocol messages
  bool buildSigma1(CaseSigma1* outMsg);
  bool processSigma1(const CaseSigma1& msg);
  bool buildSigma2(CaseSigma2* outMsg);
  bool processSigma2(const CaseSigma2& msg);
  bool buildSigma3(CaseSigma3* outMsg);
  bool processSigma3(const CaseSigma3& msg);

  // Get derived session keys (available after Establishment)
  bool getSessionKeys(CaseSessionKeys* outKeys) const;

  // Authenticated encryption for this experimental session format.
  bool encryptMessage(const uint8_t* plaintext, uint16_t plaintextLen,
                      const uint8_t* aad, uint16_t aadLen,
                      uint8_t* outCiphertext, size_t outCapacity,
                      uint16_t* outLen,
                      bool initiatorToResponder);
  bool decryptMessage(const uint8_t* ciphertext, uint16_t ciphertextLen,
                      const uint8_t* aad, uint16_t aadLen,
                      uint8_t* outPlaintext, size_t outCapacity,
                      uint16_t* outLen,
                      bool initiatorToResponder);

  static const char* stateName(CaseState state);

 private:
  bool deriveSessionKeys();
  bool encryptWithKey(const uint8_t key[16], const uint8_t* nonce, size_t nonceLen,
                      const uint8_t* plaintext, size_t plaintextLen,
                      const uint8_t* aad, size_t aadLen,
                      uint8_t* outCiphertext, size_t outCapacity,
                      uint16_t* outLen);
  bool decryptWithKey(const uint8_t key[16], const uint8_t* nonce, size_t nonceLen,
                      const uint8_t* ciphertext, size_t ciphertextLen,
                      const uint8_t* aad, size_t aadLen,
                      uint8_t* outPlaintext, size_t outCapacity,
                      uint16_t* outLen);
  bool verifyCertificate(const CaseCertificate& cert,
                         const Secp256r1Point& issuerPubKey);
  bool buildTranscriptProofHash(uint8_t proofMarker,
                                uint8_t outHash[kCaseHashSize]) const;
  bool generateRandom(uint8_t* out, size_t len);
  void clearSessionSecrets();
  void fail();
  void advanceState(CaseState newState);

  StateCallback callback_ = nullptr;
  void* callbackContext_ = nullptr;
  bool initiator_ = false;
  CaseState state_ = CaseState::kIdle;

  // Local keying material
  Secp256r1Scalar localPrivateKey_;
  Secp256r1Point localPublicKey_;
  Secp256r1Scalar ephPrivateKey_;
  Secp256r1Point ephPublicKey_;
  CaseCertificate localCert_;
  bool localCertificateConfigured_ = false;

  // Peer's information
  Secp256r1Point peerPublicKey_;
  Secp256r1Point peerEphPublicKey_;
  CaseCertificate peerCert_;
  bool peerCertificateConfigured_ = false;

  // Session data
  uint16_t localSessionId_ = 0U;
  uint16_t peerSessionId_ = 0U;
  uint8_t initiatorRandom_[kCaseRandomSize] = {0};
  uint8_t responderRandom_[kCaseRandomSize] = {0};
  CaseSessionKeys sessionKeys_;
  uint32_t encryptI2rCounter_ = 0U;
  uint32_t encryptR2iCounter_ = 0U;
  uint32_t decryptI2rCounter_ = 0U;
  uint32_t decryptR2iCounter_ = 0U;
};

}  // namespace xiao_nrf54l15
