#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "matter_device_attestation.h"

namespace {

unsigned long gMillis = 1234000UL;
uint8_t gNextKeyId = 1U;
int gKeyPairCalls = 0;
int gSignCalls = 0;
int gFailKeyPairCall = 0;
int gFailSignCall = 0;

void resetCryptoDoubles() {
  gNextKeyId = 1U;
  gKeyPairCalls = 0;
  gSignCalls = 0;
  gFailKeyPairCall = 0;
  gFailSignCall = 0;
}

void assertUnavailable(
    const xiao_nrf54l15::MatterDeviceAttestation& attestation) {
  using namespace xiao_nrf54l15;
  AttestationCertificate certificate = {};
  Secp256r1Scalar privateKey = {};
  assert(!attestation.available());
  assert(!attestation.getPAA(&certificate));
  assert(!attestation.getPAI(&certificate));
  assert(!attestation.getDAC(&certificate));
  assert(!attestation.getPAAPrivateKey(&privateKey));
  assert(!attestation.getPAIPrivateKey(&privateKey));
  assert(!attestation.getDACPrivateKey(&privateKey));
}

}  // namespace

unsigned long millis() { return gMillis; }
unsigned long micros() { return gMillis * 1000UL; }

namespace xiao_nrf54l15 {

void MatterPbkdf2::sha256(const uint8_t* data, size_t length,
                          uint8_t outHash[kHashSize]) {
  uint32_t state = 2166136261UL;
  for (size_t i = 0U; i < length; ++i) {
    state ^= data[i];
    state *= 16777619UL;
    state ^= state >> 13U;
  }
  for (size_t i = 0U; i < kHashSize; ++i) {
    state ^= static_cast<uint32_t>(length + i * 0x9DU);
    state *= 2246822519UL;
    state ^= state >> 15U;
    outHash[i] = static_cast<uint8_t>(state >> ((i & 3U) * 8U));
  }
}

bool Secp256r1::generateKeyPair(Secp256r1Scalar* outPriv,
                                Secp256r1Point* outPub) {
  ++gKeyPairCalls;
  if (outPriv == nullptr || outPub == nullptr ||
      gKeyPairCalls == gFailKeyPairCall) {
    return false;
  }

  const uint8_t keyId = gNextKeyId++;
  memset(outPriv, 0, sizeof(*outPriv));
  memset(outPub, 0, sizeof(*outPub));
  outPriv->bytes[0] = keyId;
  for (size_t i = 0U; i < sizeof(outPub->x); ++i) {
    outPub->x[i] = static_cast<uint8_t>(keyId + i);
    outPub->y[i] = static_cast<uint8_t>(keyId ^ (0xA5U + i));
  }
  return true;
}

void Secp256r1::encodeUncompressed(const Secp256r1Point& point,
                                   uint8_t outBytes[65]) {
  outBytes[0] = 0x04U;
  memcpy(outBytes + 1U, point.x, sizeof(point.x));
  memcpy(outBytes + 33U, point.y, sizeof(point.y));
}

bool Secp256r1::decodeUncompressed(const uint8_t bytes[65],
                                   Secp256r1Point* outPoint) {
  if (bytes == nullptr || outPoint == nullptr || bytes[0] != 0x04U) {
    return false;
  }
  memcpy(outPoint->x, bytes + 1U, sizeof(outPoint->x));
  memcpy(outPoint->y, bytes + 33U, sizeof(outPoint->y));
  return true;
}

bool Secp256r1::ecdsaSign(const Secp256r1Scalar& priv,
                          const uint8_t hash[32], uint8_t r[32],
                          uint8_t s[32]) {
  ++gSignCalls;
  if (gSignCalls == gFailSignCall) {
    return false;
  }
  for (size_t i = 0U; i < 32U; ++i) {
    r[i] = static_cast<uint8_t>(hash[i] ^ priv.bytes[0] ^ 0x5AU);
    s[i] = static_cast<uint8_t>(hash[31U - i] ^ priv.bytes[0] ^ 0xC3U);
  }
  return true;
}

bool Secp256r1::ecdsaVerify(const Secp256r1Point& pub,
                            const uint8_t hash[32], const uint8_t r[32],
                            const uint8_t s[32]) {
  const uint8_t keyId = pub.x[0];
  for (size_t i = 0U; i < 32U; ++i) {
    if (r[i] != static_cast<uint8_t>(hash[i] ^ keyId ^ 0x5AU) ||
        s[i] != static_cast<uint8_t>(hash[31U - i] ^ keyId ^ 0xC3U)) {
      return false;
    }
  }
  return true;
}

}  // namespace xiao_nrf54l15

int main() {
  using namespace xiao_nrf54l15;

  uint8_t serial[32] = {};
  memcpy(serial, "NRF54-ATTESTATION-TEST", 22U);

  resetCryptoDoubles();
  MatterDeviceAttestation attestation;
  assert(attestation.generateTestChain(0x1234U, 0x5678U, serial));
  assert(attestation.available());

  AttestationCertificate paa = {};
  AttestationCertificate pai = {};
  AttestationCertificate dac = {};
  assert(attestation.getPAA(&paa));
  assert(attestation.getPAI(&pai));
  assert(attestation.getDAC(&dac));
  assert(attestation.verifyChain(dac, pai, paa));

  AttestationCertificate changed = dac;
  changed.subjectPubKey[7] ^= 0x01U;
  assert(!attestation.verifyCertificate(changed, pai));
  changed = dac;
  ++changed.vendorId;
  assert(!attestation.verifyCertificate(changed, pai));
  changed = dac;
  ++changed.productId;
  assert(!attestation.verifyCertificate(changed, pai));
  changed = dac;
  changed.serialNumber[4] ^= 0x80U;
  assert(!attestation.verifyCertificate(changed, pai));
  changed = dac;
  changed.issuerPubKeyHash[9] ^= 0x20U;
  assert(!attestation.verifyCertificate(changed, pai));
  changed = dac;
  ++changed.notBefore;
  assert(!attestation.verifyCertificate(changed, pai));
  changed = dac;
  ++changed.notAfter;
  assert(!attestation.verifyCertificate(changed, pai));
  changed = dac;
  changed.type = AttestationCertType::kPAI;
  assert(!attestation.verifyCertificate(changed, pai));
  changed = dac;
  changed.signature[13] ^= 0x04U;
  assert(!attestation.verifyCertificate(changed, pai));
  changed = dac;
  changed.notAfter = changed.notBefore;
  assert(!attestation.verifyCertificate(changed, pai));

  AttestationCertificate wrongPai = pai;
  wrongPai.productId ^= 1U;
  assert(!attestation.verifyChain(dac, wrongPai, paa));
  AttestationCertificate wrongType = paa;
  wrongType.type = AttestationCertType::kDAC;
  assert(!attestation.verifyChain(dac, pai, wrongType));

  resetCryptoDoubles();
  gFailKeyPairCall = 3;
  assert(!attestation.generateTestChain(0x1234U, 0x5678U, serial));
  assertUnavailable(attestation);

  resetCryptoDoubles();
  gFailSignCall = 3;
  assert(!attestation.generateTestChain(0x1234U, 0x5678U, serial));
  assertUnavailable(attestation);

  resetCryptoDoubles();
  assert(!attestation.generateTestChain(0x1234U, 0x5678U, nullptr));
  assertUnavailable(attestation);

  return 0;
}
