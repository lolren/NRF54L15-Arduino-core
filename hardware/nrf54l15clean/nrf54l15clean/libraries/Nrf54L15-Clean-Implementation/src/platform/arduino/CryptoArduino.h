#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <mbedtls/bignum.h>
#include <mbedtls/ccm.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include <system/SystemError.h>

#include "matter_rng.h"

namespace chip {
namespace Crypto {
namespace Internal {

constexpr size_t kSha256Length = 32U;
constexpr size_t kP256PrivateKeyLength = 32U;
constexpr size_t kP256PublicKeyLength = 65U;
constexpr size_t kP256SignatureLength = 64U;

inline bool validBuffer(const void* buffer, size_t length) {
  return buffer != nullptr || length == 0U;
}

inline const uint8_t* nonNullInput(const uint8_t* buffer) {
  static const uint8_t kEmptyInput = 0U;
  return buffer != nullptr ? buffer : &kEmptyInput;
}

inline int hardwareRandom(void*, unsigned char* output, size_t length) {
  xiao_nrf54l15::MatterRng rng;
  if (!rng.begin()) return -1;
  const bool ok = rng.getRandomBytes(output, length);
  rng.end();
  return ok ? 0 : -1;
}

inline CHIP_ERROR hmacSha256(const uint8_t* key, size_t keyLength,
                            const uint8_t* first, size_t firstLength,
                            const uint8_t* second, size_t secondLength,
                            const uint8_t* third, size_t thirdLength,
                            uint8_t output[kSha256Length]) {
  if (!validBuffer(key, keyLength) || !validBuffer(first, firstLength) ||
      !validBuffer(second, secondLength) ||
      !validBuffer(third, thirdLength) || output == nullptr) {
    return CHIP_ERROR_INVALID_ARGUMENT;
  }

  const mbedtls_md_info_t* md =
      mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (md == nullptr) return CHIP_ERROR_INTERNAL;

  mbedtls_md_context_t context;
  mbedtls_md_init(&context);
  int status = mbedtls_md_setup(&context, md, 1);
  if (status == 0) {
    status = mbedtls_md_hmac_starts(
        &context, nonNullInput(key), keyLength);
  }
  if (status == 0 && firstLength > 0U) {
    status = mbedtls_md_hmac_update(&context, first, firstLength);
  }
  if (status == 0 && secondLength > 0U) {
    status = mbedtls_md_hmac_update(&context, second, secondLength);
  }
  if (status == 0 && thirdLength > 0U) {
    status = mbedtls_md_hmac_update(&context, third, thirdLength);
  }
  if (status == 0) status = mbedtls_md_hmac_finish(&context, output);
  mbedtls_md_free(&context);

  if (status != 0) {
    memset(output, 0, kSha256Length);
    return CHIP_ERROR_INTERNAL;
  }
  return CHIP_NO_ERROR;
}

inline CHIP_ERROR loadP256PrivateKey(mbedtls_ecp_group* group,
                                     mbedtls_mpi* privateKey,
                                     const uint8_t* encoded,
                                     size_t encodedLength) {
  if (encoded == nullptr || encodedLength != kP256PrivateKeyLength) {
    return CHIP_ERROR_INVALID_ARGUMENT;
  }
  if (mbedtls_ecp_group_load(group, MBEDTLS_ECP_DP_SECP256R1) != 0 ||
      mbedtls_mpi_read_binary(privateKey, encoded, encodedLength) != 0 ||
      mbedtls_ecp_check_privkey(group, privateKey) != 0) {
    return CHIP_ERROR_INVALID_ARGUMENT;
  }
  return CHIP_NO_ERROR;
}

inline CHIP_ERROR loadP256PublicKey(mbedtls_ecp_group* group,
                                    mbedtls_ecp_point* publicKey,
                                    const uint8_t* encoded,
                                    size_t encodedLength) {
  if (encoded == nullptr || encodedLength != kP256PublicKeyLength ||
      encoded[0] != 0x04U) {
    return CHIP_ERROR_INVALID_ARGUMENT;
  }
  if (mbedtls_ecp_point_read_binary(group, publicKey, encoded,
                                    encodedLength) != 0 ||
      mbedtls_ecp_check_pubkey(group, publicKey) != 0) {
    return CHIP_ERROR_INVALID_ARGUMENT;
  }
  return CHIP_NO_ERROR;
}

}  // namespace Internal

inline CHIP_ERROR DRBG_get_bytes(uint8_t* buffer, size_t length) {
  if (!Internal::validBuffer(buffer, length)) {
    return CHIP_ERROR_INVALID_ARGUMENT;
  }
  if (length == 0U) return CHIP_NO_ERROR;

  xiao_nrf54l15::MatterRng rng;
  if (!rng.begin() || !rng.getRandomBytes(buffer, length)) {
    rng.end();
    memset(buffer, 0, length);
    return CHIP_ERROR_INTERNAL;
  }
  rng.end();
  return CHIP_NO_ERROR;
}

inline CHIP_ERROR Hash_SHA256(const uint8_t* data, size_t length,
                              uint8_t* hash) {
  if (!Internal::validBuffer(data, length) || hash == nullptr) {
    return CHIP_ERROR_INVALID_ARGUMENT;
  }
  if (mbedtls_sha256(Internal::nonNullInput(data), length, hash, 0) != 0) {
    memset(hash, 0, Internal::kSha256Length);
    return CHIP_ERROR_INTERNAL;
  }
  return CHIP_NO_ERROR;
}

inline CHIP_ERROR Hash_SHA1(const uint8_t*, size_t, uint8_t*) {
  // Matter uses SHA-256. Keep the legacy-only SHA-1 entry point fail-closed.
  return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
}

inline CHIP_ERROR HKDF(const uint8_t* salt, size_t saltLength,
                       const uint8_t* inputKey, size_t inputKeyLength,
                       const uint8_t* info, size_t infoLength,
                       uint8_t* outputKey, size_t outputKeyLength) {
  if (!Internal::validBuffer(salt, saltLength) ||
      !Internal::validBuffer(inputKey, inputKeyLength) ||
      !Internal::validBuffer(info, infoLength) ||
      !Internal::validBuffer(outputKey, outputKeyLength) ||
      outputKeyLength > (255U * Internal::kSha256Length)) {
    return CHIP_ERROR_INVALID_ARGUMENT;
  }
  if (outputKeyLength == 0U) return CHIP_NO_ERROR;

  uint8_t zeroSalt[Internal::kSha256Length] = {0};
  uint8_t pseudoRandomKey[Internal::kSha256Length] = {0};
  const uint8_t* extractSalt = saltLength > 0U ? salt : zeroSalt;
  const size_t extractSaltLength =
      saltLength > 0U ? saltLength : sizeof(zeroSalt);
  CHIP_ERROR error = Internal::hmacSha256(
      extractSalt, extractSaltLength, inputKey, inputKeyLength, nullptr, 0U,
      nullptr, 0U, pseudoRandomKey);
  if (error != CHIP_NO_ERROR) return error;

  uint8_t previous[Internal::kSha256Length] = {0};
  size_t previousLength = 0U;
  size_t offset = 0U;
  uint8_t counter = 1U;
  while (offset < outputKeyLength) {
    error = Internal::hmacSha256(
        pseudoRandomKey, sizeof(pseudoRandomKey), previous, previousLength,
        info, infoLength, &counter, sizeof(counter), previous);
    if (error != CHIP_NO_ERROR) {
      memset(outputKey, 0, outputKeyLength);
      memset(pseudoRandomKey, 0, sizeof(pseudoRandomKey));
      memset(previous, 0, sizeof(previous));
      return error;
    }
    previousLength = sizeof(previous);
    const size_t remaining = outputKeyLength - offset;
    const size_t copyLength =
        remaining < sizeof(previous) ? remaining : sizeof(previous);
    memcpy(outputKey + offset, previous, copyLength);
    offset += copyLength;
    ++counter;
  }

  memset(pseudoRandomKey, 0, sizeof(pseudoRandomKey));
  memset(previous, 0, sizeof(previous));
  return CHIP_NO_ERROR;
}

inline CHIP_ERROR AES_CCM_Encrypt(
    const uint8_t* key, size_t keyLength, const uint8_t* nonce,
    size_t nonceLength, const uint8_t* aad, size_t aadLength,
    const uint8_t* plaintext, size_t plaintextLength, uint8_t* ciphertext,
    uint8_t* tag, size_t tagLength) {
  if (key == nullptr || keyLength != 16U || nonce == nullptr ||
      nonceLength < 7U || nonceLength > 13U ||
      !Internal::validBuffer(aad, aadLength) ||
      !Internal::validBuffer(plaintext, plaintextLength) ||
      !Internal::validBuffer(ciphertext, plaintextLength) || tag == nullptr ||
      tagLength < 4U || tagLength > 16U || (tagLength & 1U) != 0U) {
    return CHIP_ERROR_INVALID_ARGUMENT;
  }

  mbedtls_ccm_context context;
  mbedtls_ccm_init(&context);
  int status = mbedtls_ccm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key, 128U);
  if (status == 0) {
    status = mbedtls_ccm_encrypt_and_tag(
        &context, plaintextLength, nonce, nonceLength,
        Internal::nonNullInput(aad), aadLength,
        Internal::nonNullInput(plaintext), ciphertext, tag, tagLength);
  }
  mbedtls_ccm_free(&context);
  if (status != 0) {
    if (plaintextLength > 0U) memset(ciphertext, 0, plaintextLength);
    memset(tag, 0, tagLength);
    return CHIP_ERROR_INTERNAL;
  }
  return CHIP_NO_ERROR;
}

inline CHIP_ERROR AES_CCM_Decrypt(
    const uint8_t* key, size_t keyLength, const uint8_t* nonce,
    size_t nonceLength, const uint8_t* aad, size_t aadLength,
    const uint8_t* ciphertext, size_t ciphertextLength, uint8_t* plaintext,
    const uint8_t* tag, size_t tagLength) {
  if (key == nullptr || keyLength != 16U || nonce == nullptr ||
      nonceLength < 7U || nonceLength > 13U ||
      !Internal::validBuffer(aad, aadLength) ||
      !Internal::validBuffer(ciphertext, ciphertextLength) ||
      !Internal::validBuffer(plaintext, ciphertextLength) || tag == nullptr ||
      tagLength < 4U || tagLength > 16U || (tagLength & 1U) != 0U) {
    return CHIP_ERROR_INVALID_ARGUMENT;
  }

  mbedtls_ccm_context context;
  mbedtls_ccm_init(&context);
  int status = mbedtls_ccm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key, 128U);
  if (status == 0) {
    status = mbedtls_ccm_auth_decrypt(
        &context, ciphertextLength, nonce, nonceLength,
        Internal::nonNullInput(aad), aadLength,
        Internal::nonNullInput(ciphertext), plaintext, tag, tagLength);
  }
  mbedtls_ccm_free(&context);
  if (status != 0) {
    if (ciphertextLength > 0U) memset(plaintext, 0, ciphertextLength);
    return status == MBEDTLS_ERR_CCM_AUTH_FAILED
               ? CHIP_ERROR_INTEGRITY_CHECK_FAILED
               : CHIP_ERROR_INTERNAL;
  }
  return CHIP_NO_ERROR;
}

inline CHIP_ERROR ECDH_Agree(
    const uint8_t* privateKey, size_t privateKeyLength,
    const uint8_t* publicKey, size_t publicKeyLength, uint8_t* sharedSecret,
    size_t sharedSecretLength) {
  if (sharedSecret == nullptr ||
      sharedSecretLength < Internal::kP256PrivateKeyLength) {
    return CHIP_ERROR_BUFFER_TOO_SMALL;
  }
  memset(sharedSecret, 0, sharedSecretLength);

  mbedtls_ecp_group group;
  mbedtls_ecp_point peer;
  mbedtls_mpi secret;
  mbedtls_mpi privateValue;
  mbedtls_ecp_group_init(&group);
  mbedtls_ecp_point_init(&peer);
  mbedtls_mpi_init(&secret);
  mbedtls_mpi_init(&privateValue);

  CHIP_ERROR error = Internal::loadP256PrivateKey(
      &group, &privateValue, privateKey, privateKeyLength);
  if (error == CHIP_NO_ERROR) {
    error = Internal::loadP256PublicKey(&group, &peer, publicKey,
                                        publicKeyLength);
  }
  if (error == CHIP_NO_ERROR &&
      mbedtls_ecdh_compute_shared(&group, &secret, &peer, &privateValue,
                                  Internal::hardwareRandom, nullptr) != 0) {
    error = CHIP_ERROR_INTERNAL;
  }
  if (error == CHIP_NO_ERROR &&
      mbedtls_mpi_write_binary(&secret, sharedSecret,
                               Internal::kP256PrivateKeyLength) != 0) {
    error = CHIP_ERROR_INTERNAL;
  }

  mbedtls_mpi_free(&privateValue);
  mbedtls_mpi_free(&secret);
  mbedtls_ecp_point_free(&peer);
  mbedtls_ecp_group_free(&group);
  if (error != CHIP_NO_ERROR) memset(sharedSecret, 0, sharedSecretLength);
  return error;
}

inline CHIP_ERROR ECDSA_Sign(
    const uint8_t* privateKey, size_t privateKeyLength, const uint8_t* hash,
    size_t hashLength, uint8_t* signature, size_t* signatureLength) {
  if (signatureLength == nullptr ||
      !Internal::validBuffer(hash, hashLength)) {
    return CHIP_ERROR_INVALID_ARGUMENT;
  }
  if (signature == nullptr ||
      *signatureLength < Internal::kP256SignatureLength) {
    *signatureLength = Internal::kP256SignatureLength;
    return CHIP_ERROR_BUFFER_TOO_SMALL;
  }

  mbedtls_ecp_group group;
  mbedtls_mpi privateValue;
  mbedtls_mpi r;
  mbedtls_mpi s;
  mbedtls_ecp_group_init(&group);
  mbedtls_mpi_init(&privateValue);
  mbedtls_mpi_init(&r);
  mbedtls_mpi_init(&s);

  CHIP_ERROR error = Internal::loadP256PrivateKey(
      &group, &privateValue, privateKey, privateKeyLength);
  if (error == CHIP_NO_ERROR &&
      mbedtls_ecdsa_sign(&group, &r, &s, &privateValue, hash, hashLength,
                         Internal::hardwareRandom, nullptr) != 0) {
    error = CHIP_ERROR_INTERNAL;
  }
  if (error == CHIP_NO_ERROR &&
      (mbedtls_mpi_write_binary(&r, signature, 32U) != 0 ||
       mbedtls_mpi_write_binary(&s, signature + 32U, 32U) != 0)) {
    error = CHIP_ERROR_INTERNAL;
  }

  mbedtls_mpi_free(&s);
  mbedtls_mpi_free(&r);
  mbedtls_mpi_free(&privateValue);
  mbedtls_ecp_group_free(&group);
  if (error != CHIP_NO_ERROR) {
    memset(signature, 0, Internal::kP256SignatureLength);
    *signatureLength = 0U;
    return error;
  }
  *signatureLength = Internal::kP256SignatureLength;
  return CHIP_NO_ERROR;
}

inline CHIP_ERROR ECDSA_Verify(
    const uint8_t* publicKey, size_t publicKeyLength, const uint8_t* hash,
    size_t hashLength, const uint8_t* signature, size_t signatureLength) {
  if (!Internal::validBuffer(hash, hashLength) || signature == nullptr ||
      signatureLength != Internal::kP256SignatureLength) {
    return CHIP_ERROR_INVALID_ARGUMENT;
  }

  mbedtls_ecp_group group;
  mbedtls_ecp_point point;
  mbedtls_mpi r;
  mbedtls_mpi s;
  mbedtls_ecp_group_init(&group);
  mbedtls_ecp_point_init(&point);
  mbedtls_mpi_init(&r);
  mbedtls_mpi_init(&s);

  CHIP_ERROR error = CHIP_NO_ERROR;
  if (mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) != 0) {
    error = CHIP_ERROR_INTERNAL;
  }
  if (error == CHIP_NO_ERROR) {
    error = Internal::loadP256PublicKey(&group, &point, publicKey,
                                        publicKeyLength);
  }
  if (error == CHIP_NO_ERROR &&
      (mbedtls_mpi_read_binary(&r, signature, 32U) != 0 ||
       mbedtls_mpi_read_binary(&s, signature + 32U, 32U) != 0)) {
    error = CHIP_ERROR_INVALID_ARGUMENT;
  }
  if (error == CHIP_NO_ERROR &&
      mbedtls_ecdsa_verify(&group, hash, hashLength, &point, &r, &s) != 0) {
    error = CHIP_ERROR_INVALID_SIGNATURE;
  }

  mbedtls_mpi_free(&s);
  mbedtls_mpi_free(&r);
  mbedtls_ecp_point_free(&point);
  mbedtls_ecp_group_free(&group);
  return error;
}

}  // namespace Crypto
}  // namespace chip
