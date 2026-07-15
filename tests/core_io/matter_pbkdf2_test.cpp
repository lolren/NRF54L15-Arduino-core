#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "matter_pbkdf2.h"

using xiao_nrf54l15::MatterPbkdf2;

namespace {

uint8_t hexNibble(char value) {
  if (value >= '0' && value <= '9') {
    return static_cast<uint8_t>(value - '0');
  }
  if (value >= 'a' && value <= 'f') {
    return static_cast<uint8_t>(value - 'a' + 10);
  }
  assert(false);
  return 0U;
}

void expectHex(const uint8_t* actual, size_t length, const char* expected) {
  assert(strlen(expected) == length * 2U);
  for (size_t i = 0; i < length; ++i) {
    const uint8_t value = static_cast<uint8_t>(
        (hexNibble(expected[i * 2U]) << 4U) |
        hexNibble(expected[(i * 2U) + 1U]));
    assert(actual[i] == value);
  }
}

void expectZero(const uint8_t* actual, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    assert(actual[i] == 0U);
  }
}

void testSha256() {
  uint8_t hash[MatterPbkdf2::kHashSize] = {0};
  MatterPbkdf2::sha256(nullptr, 0U, hash);
  expectHex(hash, sizeof(hash),
            "e3b0c44298fc1c149afbf4c8996fb924"
            "27ae41e4649b934ca495991b7852b855");

  const uint8_t abc[] = {'a', 'b', 'c'};
  MatterPbkdf2::sha256(abc, sizeof(abc), hash);
  expectHex(hash, sizeof(hash),
            "ba7816bf8f01cfea414140de5dae2223"
            "b00361a396177a9cb410ff61f20015ad");

  memset(hash, 0xA5, sizeof(hash));
  MatterPbkdf2::sha256(nullptr, 1U, hash);
  expectZero(hash, sizeof(hash));

  MatterPbkdf2::sha256(nullptr, 0U, nullptr);
}

void testHmacSha256KnownVector() {
  uint8_t key[20];
  memset(key, 0x0B, sizeof(key));
  const uint8_t data[] = {'H', 'i', ' ', 'T', 'h', 'e', 'r', 'e'};
  uint8_t mac[MatterPbkdf2::kHashSize] = {0};
  MatterPbkdf2::hmacSha256(key, sizeof(key), data, sizeof(data), mac);
  expectHex(mac, sizeof(mac),
            "b0344c61d8db38535ca8afceaf0bf12b"
            "881dc200c9833da726e9376c2e32cff7");
}

void testHmacSha256ArbitraryLength() {
  uint8_t key[100];
  uint8_t data[1024];
  for (size_t i = 0; i < sizeof(key); ++i) {
    key[i] = static_cast<uint8_t>((i * 7U + 3U) & 0xFFU);
  }
  for (size_t i = 0; i < sizeof(data); ++i) {
    data[i] = static_cast<uint8_t>((i * 13U + 11U) & 0xFFU);
  }

  uint8_t mac[MatterPbkdf2::kHashSize] = {0};
  MatterPbkdf2::hmacSha256(key, sizeof(key), data, sizeof(data), mac);
  expectHex(mac, sizeof(mac),
            "99cf06aea15b2395ac0a77b8f9cc3bde"
            "b4a9321a7a6902a5b7f3f6783068f70e");

  uint8_t boundaryKey[65];
  uint8_t boundaryData[257];
  for (size_t i = 0; i < sizeof(boundaryKey); ++i) {
    boundaryKey[i] = static_cast<uint8_t>(i);
  }
  for (size_t i = 0; i < sizeof(boundaryData); ++i) {
    boundaryData[i] = static_cast<uint8_t>(i & 0xFFU);
  }

  MatterPbkdf2::hmacSha256(boundaryKey, sizeof(boundaryKey), boundaryData,
                           256U, mac);
  expectHex(mac, sizeof(mac),
            "ac99376fdeb977aa4b029590d63f33c6"
            "f5437617a333efb55495fa232d9b4924");
  MatterPbkdf2::hmacSha256(boundaryKey, sizeof(boundaryKey), boundaryData,
                           sizeof(boundaryData), mac);
  expectHex(mac, sizeof(mac),
            "b72b4850bad1923d5558ae1a5afb87a6"
            "b3903ec226498c2224b40dbb44a284d0");
}

void testHmacSha256NullInputs() {
  uint8_t mac[MatterPbkdf2::kHashSize] = {0};
  MatterPbkdf2::hmacSha256(nullptr, 0U, nullptr, 0U, mac);
  expectHex(mac, sizeof(mac),
            "b613679a0814d9ec772f95d778c35fc5"
            "ff1697c493715653c6c712144292c5ad");

  memset(mac, 0xA5, sizeof(mac));
  MatterPbkdf2::hmacSha256(nullptr, 1U, nullptr, 0U, mac);
  expectZero(mac, sizeof(mac));
  memset(mac, 0xA5, sizeof(mac));
  MatterPbkdf2::hmacSha256(nullptr, 0U, nullptr, 1U, mac);
  expectZero(mac, sizeof(mac));

  MatterPbkdf2::hmacSha256(nullptr, 0U, nullptr, 0U, nullptr);
}

void testPbkdf2RegressionVector() {
  const uint8_t password[] = {'p', 'a', 's', 's', 'w', 'o', 'r', 'd'};
  const uint8_t salt[] = {'s', 'a', 'l', 't'};
  uint8_t key[32] = {0};
  assert(MatterPbkdf2::deriveKey(password, sizeof(password), salt,
                                 sizeof(salt), 2U, sizeof(key), key));
  expectHex(key, sizeof(key),
            "ae4d0c95af6b46d32d0adff928f06dd"
            "02a303f8ef3c251dfd6e2d85a95474c43");
}

}  // namespace

int main() {
  testSha256();
  testHmacSha256KnownVector();
  testHmacSha256ArbitraryLength();
  testHmacSha256NullInputs();
  testPbkdf2RegressionVector();
  return 0;
}
