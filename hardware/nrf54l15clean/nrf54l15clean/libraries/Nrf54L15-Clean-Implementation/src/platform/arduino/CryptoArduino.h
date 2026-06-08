#pragma once
#include <Arduino.h>
#include <system/SystemError.h>
namespace chip { namespace Crypto {
    CHIP_ERROR DRBG_get_bytes(uint8_t *buf, size_t len) {
        for (size_t i = 0; i < len; i++) buf[i] = random(256);
        return CHIP_NO_ERROR;
    }
    CHIP_ERROR Hash_SHA256(const uint8_t *data, size_t len, uint8_t *hash) {
        return CHIP_ERROR_NOT_IMPLEMENTED;
    }
    CHIP_ERROR Hash_SHA1(const uint8_t *data, size_t len, uint8_t *hash) {
        return CHIP_ERROR_NOT_IMPLEMENTED;
    }
    CHIP_ERROR HKDF(const uint8_t *salt, size_t saltLen, const uint8_t *ikm, size_t ikmLen, const uint8_t *info, size_t infoLen, uint8_t *okm, size_t okmLen) {
        return CHIP_ERROR_NOT_IMPLEMENTED;
    }
    CHIP_ERROR AES_CCM_Encrypt(const uint8_t *key, size_t keyLen, const uint8_t *nonce, size_t nonceLen, const uint8_t *aad, size_t aadLen, const uint8_t *ptext, size_t ptextLen, uint8_t *ctext, uint8_t *tag, size_t tagLen) {
        return CHIP_ERROR_NOT_IMPLEMENTED;
    }
    CHIP_ERROR AES_CCM_Decrypt(const uint8_t *key, size_t keyLen, const uint8_t *nonce, size_t nonceLen, const uint8_t *aad, size_t aadLen, const uint8_t *ctext, size_t ctextLen, uint8_t *ptext, const uint8_t *tag, size_t tagLen) {
        return CHIP_ERROR_NOT_IMPLEMENTED;
    }
    CHIP_ERROR ECDH_Agree(const uint8_t *privKey, size_t privKeyLen, const uint8_t *pubKey, size_t pubKeyLen, uint8_t *sharedSecret, size_t sharedSecretLen) {
        return CHIP_ERROR_NOT_IMPLEMENTED;
    }
    CHIP_ERROR ECDSA_Sign(const uint8_t *privKey, size_t privKeyLen, const uint8_t *hash, size_t hashLen, uint8_t *sig, size_t *sigLen) {
        return CHIP_ERROR_NOT_IMPLEMENTED;
    }
    CHIP_ERROR ECDSA_Verify(const uint8_t *pubKey, size_t pubKeyLen, const uint8_t *hash, size_t hashLen, const uint8_t *sig, size_t sigLen) {
        return CHIP_ERROR_NOT_IMPLEMENTED;
    }
}}
