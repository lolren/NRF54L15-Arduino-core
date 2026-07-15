#include <nrf54_all.h>

#include <CHIPError.h>
#include <CHIPProjectConfig.h>
#include <CryptoArduino.h>

#include <string.h>

namespace {

uint32_t gPassCount = 0U;
uint32_t gFailCount = 0U;

void check(bool passed, const char* name) {
  Serial.print(passed ? "[PASS] " : "[FAIL] ");
  Serial.println(name);
  if (passed) {
    ++gPassCount;
  } else {
    ++gFailCount;
  }
}

bool equalBytes(const uint8_t* left, const uint8_t* right, size_t length) {
  return left != nullptr && right != nullptr &&
         memcmp(left, right, length) == 0;
}

void testRandom() {
  uint8_t randomBytes[32] = {0};
  const CHIP_ERROR error =
      chip::Crypto::DRBG_get_bytes(randomBytes, sizeof(randomBytes));
  uint8_t combined = 0U;
  for (uint8_t value : randomBytes) combined |= value;
  check(error == CHIP_NO_ERROR && combined != 0U,
        "CRACEN-backed random bytes");
}

void testSha256() {
  static const uint8_t kExpected[32] = {
      0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA,
      0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE, 0x22, 0x23,
      0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C,
      0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD,
  };
  uint8_t actual[32] = {0};
  const uint8_t input[] = {'a', 'b', 'c'};
  const CHIP_ERROR error =
      chip::Crypto::Hash_SHA256(input, sizeof(input), actual);
  check(error == CHIP_NO_ERROR && equalBytes(actual, kExpected, sizeof(actual)),
        "SHA-256 known-answer vector");
}

void testHkdf() {
  static const uint8_t kInputKey[22] = {
      0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B,
      0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B,
      0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B,
  };
  static const uint8_t kSalt[13] = {
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
      0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
  };
  static const uint8_t kInfo[10] = {
      0xF0, 0xF1, 0xF2, 0xF3, 0xF4,
      0xF5, 0xF6, 0xF7, 0xF8, 0xF9,
  };
  static const uint8_t kExpected[42] = {
      0x3C, 0xB2, 0x5F, 0x25, 0xFA, 0xAC, 0xD5, 0x7A,
      0x90, 0x43, 0x4F, 0x64, 0xD0, 0x36, 0x2F, 0x2A,
      0x2D, 0x2D, 0x0A, 0x90, 0xCF, 0x1A, 0x5A, 0x4C,
      0x5D, 0xB0, 0x2D, 0x56, 0xEC, 0xC4, 0xC5, 0xBF,
      0x34, 0x00, 0x72, 0x08, 0xD5, 0xB8, 0x87, 0x18,
      0x58, 0x65,
  };
  uint8_t actual[sizeof(kExpected)] = {0};
  const CHIP_ERROR error = chip::Crypto::HKDF(
      kSalt, sizeof(kSalt), kInputKey, sizeof(kInputKey), kInfo,
      sizeof(kInfo), actual, sizeof(actual));
  check(error == CHIP_NO_ERROR && equalBytes(actual, kExpected, sizeof(actual)),
        "HKDF-SHA256 RFC 5869 vector");
}

void testAesCcm() {
  static const uint8_t kKey[16] = {
      0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
      0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
  };
  static const uint8_t kNonce[13] = {
      0x00, 0x00, 0x00, 0x03, 0x02, 0x01, 0x00,
      0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5,
  };
  static const uint8_t kAad[8] = {
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  };
  static const uint8_t kPlaintext[16] = {
      0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
      0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
  };
  static const uint8_t kExpectedCiphertext[16] = {
      0x58, 0x8C, 0x97, 0x9A, 0x61, 0xC6, 0x63, 0xD2,
      0xF0, 0x66, 0xD0, 0xC2, 0xC0, 0xF9, 0x89, 0x80,
  };
  static const uint8_t kExpectedTag[8] = {
      0x6D, 0x5F, 0x6B, 0x61, 0xDA, 0xC3, 0x84, 0x17,
  };

  uint8_t ciphertext[sizeof(kPlaintext)] = {0};
  uint8_t tag[sizeof(kExpectedTag)] = {0};
  CHIP_ERROR error = chip::Crypto::AES_CCM_Encrypt(
      kKey, sizeof(kKey), kNonce, sizeof(kNonce), kAad, sizeof(kAad),
      kPlaintext, sizeof(kPlaintext), ciphertext, tag, sizeof(tag));
  check(error == CHIP_NO_ERROR &&
            equalBytes(ciphertext, kExpectedCiphertext, sizeof(ciphertext)) &&
            equalBytes(tag, kExpectedTag, sizeof(tag)),
        "AES-128-CCM RFC 3610 vector");

  uint8_t decrypted[sizeof(kPlaintext)] = {0};
  error = chip::Crypto::AES_CCM_Decrypt(
      kKey, sizeof(kKey), kNonce, sizeof(kNonce), kAad, sizeof(kAad),
      ciphertext, sizeof(ciphertext), decrypted, tag, sizeof(tag));
  check(error == CHIP_NO_ERROR &&
            equalBytes(decrypted, kPlaintext, sizeof(decrypted)),
        "AES-128-CCM authenticated decrypt");

  tag[0] ^= 0x01U;
  error = chip::Crypto::AES_CCM_Decrypt(
      kKey, sizeof(kKey), kNonce, sizeof(kNonce), kAad, sizeof(kAad),
      ciphertext, sizeof(ciphertext), decrypted, tag, sizeof(tag));
  check(error == CHIP_ERROR_INTEGRITY_CHECK_FAILED,
        "AES-128-CCM rejects a modified tag");
}

void testP256() {
  static const uint8_t kPrivateKey[32] = {
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
  };
  static const uint8_t kPublicKey[65] = {
      0x04,
      0x6B, 0x17, 0xD1, 0xF2, 0xE1, 0x2C, 0x42, 0x47,
      0xF8, 0xBC, 0xE6, 0xE5, 0x63, 0xA4, 0x40, 0xF2,
      0x77, 0x03, 0x7D, 0x81, 0x2D, 0xEB, 0x33, 0xA0,
      0xF4, 0xA1, 0x39, 0x45, 0xD8, 0x98, 0xC2, 0x96,
      0x4F, 0xE3, 0x42, 0xE2, 0xFE, 0x1A, 0x7F, 0x9B,
      0x8E, 0xE7, 0xEB, 0x4A, 0x7C, 0x0F, 0x9E, 0x16,
      0x2B, 0xCE, 0x33, 0x57, 0x6B, 0x31, 0x5E, 0xCE,
      0xCB, 0xB6, 0x40, 0x68, 0x37, 0xBF, 0x51, 0xF5,
  };
  uint8_t sharedSecret[32] = {0};
  CHIP_ERROR error = chip::Crypto::ECDH_Agree(
      kPrivateKey, sizeof(kPrivateKey), kPublicKey, sizeof(kPublicKey),
      sharedSecret, sizeof(sharedSecret));
  check(error == CHIP_NO_ERROR &&
            equalBytes(sharedSecret, kPublicKey + 1U, sizeof(sharedSecret)),
        "P-256 ECDH known-answer vector");

  uint8_t hash[32] = {0};
  const uint8_t message[] = {'M', 'a', 't', 't', 'e', 'r'};
  error = chip::Crypto::Hash_SHA256(message, sizeof(message), hash);
  uint8_t signature[64] = {0};
  size_t signatureLength = sizeof(signature);
  if (error == CHIP_NO_ERROR) {
    error = chip::Crypto::ECDSA_Sign(
        kPrivateKey, sizeof(kPrivateKey), hash, sizeof(hash), signature,
        &signatureLength);
  }
  check(error == CHIP_NO_ERROR && signatureLength == sizeof(signature),
        "P-256 ECDSA sign with hardware blinding entropy");

  error = chip::Crypto::ECDSA_Verify(
      kPublicKey, sizeof(kPublicKey), hash, sizeof(hash), signature,
      signatureLength);
  check(error == CHIP_NO_ERROR, "P-256 ECDSA verify");
  signature[0] ^= 0x01U;
  error = chip::Crypto::ECDSA_Verify(
      kPublicKey, sizeof(kPublicKey), hash, sizeof(hash), signature,
      signatureLength);
  check(error == CHIP_ERROR_INVALID_SIGNATURE,
        "P-256 ECDSA rejects a modified signature");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) delay(10);

  Serial.println("=== CHIP Crypto Known-Answer Test ===");
  testRandom();
  testSha256();
  testHkdf();
  testAesCcm();
  testP256();
  Serial.print("crypto_result pass=");
  Serial.print(gPassCount);
  Serial.print(" fail=");
  Serial.println(gFailCount);
}

void loop() {}
