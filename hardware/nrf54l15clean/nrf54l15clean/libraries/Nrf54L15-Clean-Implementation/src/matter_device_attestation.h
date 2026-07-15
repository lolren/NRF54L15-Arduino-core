#pragma once

#include <stddef.h>
#include <stdint.h>

#include "matter_secp256r1.h"
#include "matter_pbkdf2.h"

namespace xiao_nrf54l15 {

// Test-only device-attestation model for local two-board validation.
//
// This is not a Matter certificate encoding or a production credential
// provider. The process-local keys are regenerated on every boot, and the
// simplified certificates are not suitable for Matter certification or for a
// standards-compliant commissioner. The local chain is PAA -> PAI -> DAC: the
// PAA is self-signed, the PAI is signed by the PAA, and the DAC is signed by
// the PAI.

constexpr size_t kAttestationHashSize = 32U;
constexpr size_t kAttestationPubKeySize = 65U;  // Uncompressed P-256
constexpr size_t kAttestationSignatureSize = 64U;  // r || s
constexpr size_t kAttestationCertMaxSize = 384U;

// Matter certificate types
enum class AttestationCertType : uint8_t {
  kPAA = 0U,   // Product Attestation Authority (root)
  kPAI = 1U,   // Product Attestation Intermediate
  kDAC = 2U,   // Device Attestation Certificate
};

// Minimal Matter certificate (simplified for Arduino)
struct AttestationCertificate {
  // Subject
  uint8_t subjectPubKey[kAttestationPubKeySize] = {0};
  uint16_t vendorId = 0U;
  uint16_t productId = 0U;
  uint8_t serialNumber[32] = {0};

  // Issuer
  uint8_t issuerPubKeyHash[kAttestationHashSize] = {0};

  // Validity
  uint32_t notBefore = 0U;
  uint32_t notAfter = 0U;

  // Signature
  uint8_t signature[kAttestationSignatureSize] = {0};

  // Metadata
  AttestationCertType type = AttestationCertType::kDAC;
  bool valid = false;
};

// Device attestation context
class MatterDeviceAttestation {
 public:
  MatterDeviceAttestation() = default;

  // Generate an ephemeral test chain (PAA -> PAI -> DAC). A failure clears
  // the complete chain so callers can never observe partially generated
  // credentials.
  bool generateTestChain(uint16_t vendorId, uint16_t productId,
                         const uint8_t serialNumber[32]);

  // Get the DAC certificate (presented during attestation)
  bool getDAC(AttestationCertificate* outCert) const;

  // Get the PAI certificate (intermediate, presented after DAC)
  bool getPAI(AttestationCertificate* outCert) const;

  // Get the PAA certificate (root, may be self-signed)
  bool getPAA(AttestationCertificate* outCert) const;

  // Verify a certificate chain (PAA -> PAI -> DAC)
  bool verifyChain(const AttestationCertificate& dac,
                   const AttestationCertificate& pai,
                   const AttestationCertificate& paa) const;

  // Verify a single certificate against its issuer
  bool verifyCertificate(const AttestationCertificate& cert,
                         const AttestationCertificate& issuer) const;

  // Get the DAC private key (for signing attestation responses)
  bool getDACPrivateKey(Secp256r1Scalar* outKey) const;

  // Get the PAI private key (for signing DAC)
  bool getPAIPrivateKey(Secp256r1Scalar* outKey) const;

  // Get the PAA private key (for signing PAI)
  bool getPAAPrivateKey(Secp256r1Scalar* outKey) const;

  // Check if attestation is available
  bool available() const { return paaValid_ && paiValid_ && dacValid_; }

  // Get certificate type name
  static const char* certTypeName(AttestationCertType type);

 private:
  bool signCertificate(const Secp256r1Scalar& issuerPrivateKey,
                       const Secp256r1Point& issuerPublicKey,
                       const Secp256r1Point& subjectPublicKey,
                       uint16_t vendorId, uint16_t productId,
                       const uint8_t serialNumber[32],
                       AttestationCertificate* outCert,
                       AttestationCertType certType);

  // PAA (root)
  Secp256r1Scalar paaPrivateKey_;
  Secp256r1Point paaPublicKey_;
  AttestationCertificate paaCert_;
  bool paaValid_ = false;

  // PAI (intermediate)
  Secp256r1Scalar paiPrivateKey_;
  Secp256r1Point paiPublicKey_;
  AttestationCertificate paiCert_;
  bool paiValid_ = false;

  // DAC (device)
  Secp256r1Scalar dacPrivateKey_;
  Secp256r1Point dacPublicKey_;
  AttestationCertificate dacCert_;
  bool dacValid_ = false;
};

}  // namespace xiao_nrf54l15
