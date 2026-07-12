#!/usr/bin/env python3
"""Regression checks for fail-closed CRACEN IKG and operand RAM access."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / "hardware/nrf54l15clean/nrf54l15clean"
HAL_HEADER = (
    PLATFORM
    / "libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.h"
)
HAL_SECURITY = (
    PLATFORM
    / "libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_security.cpp"
)
SEED_EXAMPLE = (
    PLATFORM
    / "examples/KMU/KmuCracenIkgSeedProof/KmuCracenIkgSeedProof.ino"
)
RAM_EXAMPLE = (
    PLATFORM
    / "libraries/Nrf54L15-Clean-Implementation/examples/Peripherals"
    / "CracenEccTest/CracenEccTest.ino"
)


class CracenIkgContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = HAL_HEADER.read_text(encoding="utf-8")
        cls.source = HAL_SECURITY.read_text(encoding="utf-8")

    def method(self, name: str, next_name: str) -> str:
        start = self.source.index(f"CracenIkg::{name}")
        end = self.source.index(f"CracenIkg::{next_name}", start)
        return self.source[start:end]

    def test_operand_ram_is_chip_relative_with_0x200_byte_slots(self) -> None:
        self.assertIn("kPkOperandRamOffset = 0x8000U", self.header)
        self.assertIn("kPkOperandSlotSize = 0x200U", self.header)
        self.assertIn("kPkOperandSlotCount = 15U", self.header)
        self.assertIn("kPkOperandSlotCount = 16U", self.header)
        constructor = self.method("CracenIkg", "begin")
        self.assertIn("static_cast<uintptr_t>(coreBase)", constructor)
        self.assertIn("kPkOperandRamOffset", constructor)
        self.assertNotIn("0x51808000", self.source)
        self.assertNotIn("slot * 256", self.source)

    def test_operand_access_has_strict_bounds_and_secure_mode_pages(self) -> None:
        access = self.method("operandAccessAllowed", "pkWriteOperand")
        self.assertIn("len > kPkOperandSlotSize", access)
        self.assertIn("slot < 0", access)
        self.assertIn("slot >= static_cast<int>(kPkOperandSlotCount)", access)
        self.assertIn("!privateKeysStored() || (slot >= 8 && slot <= 12)", access)

        write = self.method("pkWriteOperand", "pkReadOperand")
        read = self.method("pkReadOperand", "pkWaitComplete")
        self.assertIn("operandAccessAllowed(slot, data, len)", write)
        self.assertIn("operandAccessAllowed(slot, data, len)", read)
        self.assertIn("slot) * kPkOperandSlotSize", write)
        self.assertIn("slot) * kPkOperandSlotSize", read)
        self.assertNotIn("PROTECTEDRAMLOCK =", read)

    def test_seed_setters_are_fail_closed_and_readback_verified(self) -> None:
        valid = self.method("markSeedValid", "lockSeed")
        lock = self.method("lockSeed", "lockProtectedRam")
        self.assertIn("kSeedStateManagedByKmu || valid || seedLocked()", valid)
        self.assertIn("return !seedValid();", valid)
        self.assertNotIn("valid ?", valid)
        self.assertIn("kSeedStateManagedByKmu || !seedValid()", lock)
        self.assertIn("return seedLocked();", lock)

        self.assertIn("defined(NRF54LM20A_XXAA)", self.source)
        self.assertIn("defined(NRF54LM20B_XXAA)", self.source)
        self.assertIn("bool seedStateManagedByKmu() const;", self.header)

    def test_success_state_queries_and_write_once_lock_require_live_core(self) -> None:
        okay = self.method("okay", "seedError")
        symmetric = self.method("symmetricKeysStored", "privateKeysStored")
        private = self.method("privateKeysStored", "seedValid")
        lock = self.method("lockProtectedRam", "softResetKeys")
        self.assertIn("core_ != nullptr", okay)
        self.assertIn("core_ != nullptr", symmetric)
        self.assertIn("core_ != nullptr", private)
        self.assertIn("cracen_ == nullptr || !active_", lock)

    def test_key_derivation_never_validates_or_synthesizes_seed_material(self) -> None:
        start = self.method("start", "waitReady")
        ready = self.method("waitReady", "waitGenerationComplete")
        keygen = self.method("ikgGenerateKey", "ikgEcdsaSign")
        self.assertIn("!seedValid()", start)
        self.assertIn("CRACENCORE_PK_STATUS_PKBUSY_Msk", ready)
        self.assertGreaterEqual(keygen.count("seedValid()"), 2)
        self.assertNotIn("markSeedValid", keygen)
        self.assertNotIn("0x4E524635", keygen)
        self.assertNotIn("0x9E3779B9", keygen)
        self.assertNotIn("0x12345678", keygen)

    def test_unfinished_high_level_pke_wrappers_fail_closed(self) -> None:
        section = self.source[
            self.source.index("CracenIkg::ikgEcdsaSign") :
            self.source.index("CracenIkg::pkStatus")
        ]
        self.assertNotIn("PKECOMMAND =", section)
        self.assertNotIn("PKECONTROL =", section)
        self.assertGreaterEqual(section.count("return false;"), 5)
        self.assertIn("memset(pubKey, 0, 65U)", section)
        self.assertIn("memset(r, 0, 32U)", section)
        self.assertIn("memset(s, 0, 32U)", section)

    def test_examples_are_non_destructive_and_exercise_bounds(self) -> None:
        seed_example = SEED_EXAMPLE.read_text(encoding="utf-8")
        self.assertIn("non-provisioning", seed_example)
        self.assertIn("never provisions KMU slots or validates a seed", seed_example)
        self.assertIn("g_ikg.seedValid()", seed_example)
        for unsafe_text in ("Kmu g_", "CracenRng", ".provision(", "markSeedValid"):
            self.assertNotIn(unsafe_text, seed_example)

        ram_example = RAM_EXAMPLE.read_text(encoding="utf-8")
        self.assertIn("kPkOperandSlotSize + 1U", ram_example)
        self.assertIn("kPkOperandSlotCount", ram_example)
        self.assertIn("oversized_rejected", ram_example)
        self.assertIn("bad_slot_rejected", ram_example)


if __name__ == "__main__":
    unittest.main(verbosity=2)
