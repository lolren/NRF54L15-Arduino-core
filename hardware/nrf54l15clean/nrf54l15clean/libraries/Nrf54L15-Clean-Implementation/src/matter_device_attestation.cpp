#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && NRF54L15_CLEAN_MATTER_CORE_ENABLE
#include "matter_device_attestation.h"

#include <Arduino.h>
#include <string.h>

namespace xiao_nrf54l15 {
namespace {

void writeUint16Le(uint16_t v, uint8_t* b, size_t off) {
  b[off] = v & 0xFFU;
  b[off + 1] = (v >> 8U) & 0xFFU;
}

void writeUint32Le(uint32_t value, uint8_t* buffer, size_t offset) {
  for (size_t i = 0U; i < 4U; ++i) {
    buffer[offset + i] = static_cast<uint8_t>(value >> (i * 8U));
  }
}

// Build the To-Be-Signed data for a certificate
size_t buildTbsData(const uint8_t subjectPubKey[65],
                    uint16_t vendorId, uint16_t productId,
                    const uint8_t serialNumber[32],
                    const uint8_t issuerPubKeyHash[32],
                    uint32_t notBefore, uint32_t notAfter,
                    AttestationCertType certType,
                    uint8_t* tbsData, size_t tbsMax) {
  size_t off = 0U;
  if (subjectPubKey == nullptr || serialNumber == nullptr ||
      issuerPubKeyHash == nullptr || tbsData == nullptr || tbsMax < 142U) {
    return 0U;
  }

  // Subject public key
  memcpy(tbsData + off, subjectPubKey, 65); off += 65;

  // Vendor ID
  writeUint16Le(vendorId, tbsData, off); off += 2;

  // Product ID
  writeUint16Le(productId, tbsData, off); off += 2;

  // Serial number (32 bytes)
  memcpy(tbsData + off, serialNumber, 32); off += 32;

  memcpy(tbsData + off, issuerPubKeyHash, 32); off += 32;
  writeUint32Le(notBefore, tbsData, off); off += 4;
  writeUint32Le(notAfter, tbsData, off); off += 4;
  tbsData[off++] = static_cast<uint8_t>(certType);

  return off;
}

// Compute issuer public key hash
void computeIssuerHash(const uint8_t issuerPubKey[65],
                       uint8_t hash[32]) {
  MatterPbkdf2::sha256(issuerPubKey, 65, hash);
}

}  // namespace

// ─── Certificate Chain Generation ────────────────────────────────

bool MatterDeviceAttestation::generateTestChain(
    uint16_t vendorId, uint16_t productId,
    const uint8_t serialNumber[32]) {

  *this = MatterDeviceAttestation{};
  if (serialNumber == nullptr) {
    return false;
  }

  // Generate PAA key pair (root CA)
  if (!Secp256r1::generateKeyPair(&paaPrivateKey_, &paaPublicKey_)) {
    *this = MatterDeviceAttestation{};
    return false;
  }

  // Generate PAI key pair (intermediate)
  if (!Secp256r1::generateKeyPair(&paiPrivateKey_, &paiPublicKey_)) {
    *this = MatterDeviceAttestation{};
    return false;
  }

  // Generate DAC key pair (device)
  if (!Secp256r1::generateKeyPair(&dacPrivateKey_, &dacPublicKey_)) {
    *this = MatterDeviceAttestation{};
    return false;
  }

  // Create PAA certificate (self-signed root)
  {
    uint8_t serial[32] = {0};
    if (!signCertificate(paaPrivateKey_, paaPublicKey_, paaPublicKey_,
                         vendorId, productId,
                         serial, &paaCert_,
                         AttestationCertType::kPAA)) {
      *this = MatterDeviceAttestation{};
      return false;
    }
  }

  // Create PAI certificate (signed by PAA)
  {
    uint8_t serial[32] = {0};
    if (!signCertificate(paaPrivateKey_, paaPublicKey_, paiPublicKey_,
                         vendorId, productId,
                         serial, &paiCert_,
                         AttestationCertType::kPAI)) {
      *this = MatterDeviceAttestation{};
      return false;
    }
  }

  // Create DAC certificate (signed by PAI)
  if (!signCertificate(paiPrivateKey_, paiPublicKey_, dacPublicKey_,
                       vendorId, productId,
                       serialNumber, &dacCert_,
                       AttestationCertType::kDAC)) {
    *this = MatterDeviceAttestation{};
    return false;
  }

  // Publish the chain only after every key and certificate is complete.
  paaValid_ = true;
  paiValid_ = true;
  dacValid_ = true;

  return true;
}

bool MatterDeviceAttestation::signCertificate(
    const Secp256r1Scalar& issuerPrivateKey,
    const Secp256r1Point& issuerPublicKey,
    const Secp256r1Point& subjectPublicKey,
    uint16_t vendorId, uint16_t productId,
    const uint8_t serialNumber[32],
    AttestationCertificate* outCert,
    AttestationCertType certType) {

  if (serialNumber == nullptr || outCert == nullptr) return false;

  *outCert = AttestationCertificate{};

  // Subject public key
  Secp256r1::encodeUncompressed(subjectPublicKey, outCert->subjectPubKey);

  // Issuer public key hash
  uint8_t issuerPubKey[65] = {0};
  Secp256r1::encodeUncompressed(issuerPublicKey, issuerPubKey);
  computeIssuerHash(issuerPubKey, outCert->issuerPubKeyHash);

  // Vendor and product IDs
  outCert->vendorId = vendorId;
  outCert->productId = productId;

  // Serial number
  memcpy(outCert->serialNumber, serialNumber, 32);

  // Validity period
  outCert->notBefore = (uint32_t)(millis() / 1000U);
  outCert->notAfter = outCert->notBefore + 86400U * 365U * 10U;  // 10 years

  // Certificate type
  outCert->type = certType;

  // Build TBS data and sign
  uint8_t tbsData[256] = {0};
  size_t tbsLen = buildTbsData(outCert->subjectPubKey,
                               vendorId, productId,
                               serialNumber,
                               outCert->issuerPubKeyHash,
                               outCert->notBefore, outCert->notAfter,
                               outCert->type,
                               tbsData, sizeof(tbsData));

  if (tbsLen == 0U) return false;

  // Hash TBS data
  uint8_t hash[32] = {0};
  MatterPbkdf2::sha256(tbsData, tbsLen, hash);

  // Sign with issuer's private key
  if (!Secp256r1::ecdsaSign(issuerPrivateKey, hash,
                            outCert->signature, outCert->signature + 32)) {
    return false;
  }

  outCert->valid = true;
  return true;
}

// ─── Certificate Accessors ───────────────────────────────────────

bool MatterDeviceAttestation::getDAC(AttestationCertificate* outCert) const {
  if (outCert == nullptr || !dacValid_) return false;
  *outCert = dacCert_;
  return true;
}

bool MatterDeviceAttestation::getPAI(AttestationCertificate* outCert) const {
  if (outCert == nullptr || !paiValid_) return false;
  *outCert = paiCert_;
  return true;
}

bool MatterDeviceAttestation::getPAA(AttestationCertificate* outCert) const {
  if (outCert == nullptr || !paaValid_) return false;
  *outCert = paaCert_;
  return true;
}

bool MatterDeviceAttestation::getDACPrivateKey(Secp256r1Scalar* outKey) const {
  if (outKey == nullptr || !dacValid_) return false;
  *outKey = dacPrivateKey_;
  return true;
}

bool MatterDeviceAttestation::getPAIPrivateKey(Secp256r1Scalar* outKey) const {
  if (outKey == nullptr || !paiValid_) return false;
  *outKey = paiPrivateKey_;
  return true;
}

bool MatterDeviceAttestation::getPAAPrivateKey(Secp256r1Scalar* outKey) const {
  if (outKey == nullptr || !paaValid_) return false;
  *outKey = paaPrivateKey_;
  return true;
}

// ─── Certificate Verification ────────────────────────────────────

bool MatterDeviceAttestation::verifyCertificate(
    const AttestationCertificate& cert,
    const AttestationCertificate& issuer) const {

  if (!cert.valid || !issuer.valid) return false;
  if (cert.notAfter <= cert.notBefore) return false;

  // Verify issuer public key hash
  uint8_t expectedHash[32] = {0};
  computeIssuerHash(issuer.subjectPubKey, expectedHash);
  if (memcmp(cert.issuerPubKeyHash, expectedHash, 32) != 0) {
    return false;
  }

  // Build TBS data
  uint8_t tbsData[256] = {0};
  size_t tbsLen = buildTbsData(cert.subjectPubKey,
                               cert.vendorId, cert.productId,
                               cert.serialNumber,
                               cert.issuerPubKeyHash,
                               cert.notBefore, cert.notAfter, cert.type,
                               tbsData, sizeof(tbsData));

  if (tbsLen == 0U) return false;

  // Hash TBS data
  uint8_t hash[32] = {0};
  MatterPbkdf2::sha256(tbsData, tbsLen, hash);

  // Verify signature with issuer's public key
  Secp256r1Point issuerPubKey;
  if (!Secp256r1::decodeUncompressed(issuer.subjectPubKey, &issuerPubKey)) {
    return false;
  }

  return Secp256r1::ecdsaVerify(issuerPubKey, hash,
                                 cert.signature, cert.signature + 32);
}

bool MatterDeviceAttestation::verifyChain(
    const AttestationCertificate& dac,
    const AttestationCertificate& pai,
    const AttestationCertificate& paa) const {

  if (dac.type != AttestationCertType::kDAC ||
      pai.type != AttestationCertType::kPAI ||
      paa.type != AttestationCertType::kPAA ||
      dac.vendorId != pai.vendorId || pai.vendorId != paa.vendorId ||
      dac.productId != pai.productId || pai.productId != paa.productId) {
    return false;
  }

  // Verify DAC signed by PAI
  if (!verifyCertificate(dac, pai)) return false;

  // Verify PAI signed by PAA
  if (!verifyCertificate(pai, paa)) return false;

  // Verify PAA is self-signed (optional for root)
  if (!verifyCertificate(paa, paa)) return false;

  return true;
}

// ─── Utilities ───────────────────────────────────────────────────

const char* MatterDeviceAttestation::certTypeName(AttestationCertType type) {
  switch (type) {
    case AttestationCertType::kPAA: return "PAA";
    case AttestationCertType::kPAI: return "PAI";
    case AttestationCertType::kDAC: return "DAC";
    default: return "Unknown";
  }
}

}  // namespace xiao_nrf54l15
#endif // NRF54L15_CLEAN_MATTER_CORE_ENABLE
