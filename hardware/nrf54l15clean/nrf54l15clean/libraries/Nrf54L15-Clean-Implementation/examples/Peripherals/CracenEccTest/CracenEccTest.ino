// CRACEN PKE CryptoRAM contract probe
//
// This is a non-secret RAM and boundary check, not an ECC implementation.
// High-level isolated-key signing/public-key operations intentionally fail
// closed until a validated PSA/NCS PKE lifecycle is integrated.

#include <Arduino.h>
#include <string.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

CracenIkg ikg;

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("=== CRACEN PKE CryptoRAM contract ===");

  const bool ready = ikg.begin(1000000UL);
  Serial.print("begin=");
  Serial.println(ready ? "OK" : "FAIL");
  if (!ready) {
    return;
  }

  static uint8_t pattern[32];
  static uint8_t result[sizeof(pattern)];
  static uint8_t oversized[CracenIkg::kPkOperandSlotSize + 1U];
  for (size_t i = 0; i < sizeof(pattern); ++i) {
    pattern[i] = static_cast<uint8_t>(0xA5U ^ i);
  }

  const bool writeOk = ikg.pkWriteOperand(0, pattern, sizeof(pattern));
  const bool readOk = ikg.pkReadOperand(0, result, sizeof(result));
  const bool roundTrip =
      writeOk && readOk && memcmp(pattern, result, sizeof(pattern)) == 0;
  const bool badSlotRejected = !ikg.pkWriteOperand(
      CracenIkg::kPkOperandSlotCount, pattern, sizeof(pattern));
  const bool oversizedRejected =
      !ikg.pkWriteOperand(0, oversized, sizeof(oversized));

  Serial.print("core_base=0x");
  Serial.println(nrf54l15::CRACENCORE_BASE, HEX);
  Serial.print("operand_ram_base=0x");
  Serial.println(nrf54l15::CRACENCORE_BASE +
                     CracenIkg::kPkOperandRamOffset,
                 HEX);
  Serial.print("slot_size=0x");
  Serial.println(CracenIkg::kPkOperandSlotSize, HEX);
  Serial.print("round_trip=");
  Serial.println(roundTrip ? "PASS" : "FAIL");
  Serial.print("bad_slot_rejected=");
  Serial.println(badSlotRejected ? "PASS" : "FAIL");
  Serial.print("oversized_rejected=");
  Serial.println(oversizedRejected ? "PASS" : "FAIL");

  ikg.end();
}

void loop() {}
