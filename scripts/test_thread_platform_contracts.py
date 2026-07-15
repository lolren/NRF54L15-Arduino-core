#!/usr/bin/env python3
"""Source and native regressions for the staged OpenThread platform contract."""

from __future__ import annotations

import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
IMPLEMENTATION = (
    ROOT
    / "hardware/nrf54l15clean/nrf54l15clean/libraries"
    / "Nrf54L15-Clean-Implementation"
)
SRC = IMPLEMENTATION / "src"
TESTS = ROOT / "tests"
PLATFORM_SOURCE = SRC / "openthread_platform_nrf54l15.cpp"
PLATFORM_HEADER = SRC / "openthread_platform_nrf54l15.h"
THREAD_SOURCE = SRC / "nrf54_thread_experimental.cpp"
THREAD_HEADER = SRC / "nrf54_thread_experimental.h"
CONFIG_HEADER = SRC / "openthread-core-user-config.h"


def source(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def function_body(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        raise AssertionError(f"missing function: {signature}")
    brace = text.find("{", start + len(signature))
    if brace < 0:
        raise AssertionError(f"missing opening brace: {signature}")
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace + 1 : index]
    raise AssertionError(f"unterminated function: {signature}")


def function_definition(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        raise AssertionError(f"missing function: {signature}")
    brace = text.find("{", start + len(signature))
    if brace < 0:
        raise AssertionError(f"missing opening brace: {signature}")
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


def generate_settings_test_fragments(output: Path) -> None:
    platform = source(PLATFORM_SOURCE)
    helper_start = platform.find("bool ensureSettingsOpen()")
    helper_end = platform.find("void secureZero(", helper_start)
    if helper_start < 0 or helper_end < 0:
        raise AssertionError("missing production settings helper block")
    (output / "thread_settings_helpers.inc").write_text(
        platform[helper_start:helper_end], encoding="utf-8"
    )

    api_signatures = (
        "otError otPlatSettingsGet(",
        "otError otPlatSettingsSet(",
        "otError otPlatSettingsAdd(",
        "otError otPlatSettingsDelete(",
    )
    (output / "thread_settings_apis.inc").write_text(
        "\n\n".join(function_definition(platform, signature)
                     for signature in api_signatures)
        + "\n",
        encoding="utf-8",
    )


class ThreadPlatformSourceContracts(unittest.TestCase):
    def test_radio_advertises_only_implemented_acceleration(self) -> None:
        text = source(PLATFORM_SOURCE)
        match = re.search(
            r"constexpr\s+otRadioCaps\s+kThreadRadioCaps\s*=\s*"
            r"static_cast<otRadioCaps>\((.*?)\);",
            text,
            re.DOTALL,
        )
        self.assertIsNotNone(match, "missing kThreadRadioCaps declaration")
        advertised = set(re.findall(r"OT_RADIO_CAPS_[A-Z0-9_]+", match.group(1)))
        self.assertEqual(
            advertised,
            {
                "OT_RADIO_CAPS_ENERGY_SCAN",
                "OT_RADIO_CAPS_TRANSMIT_FRAME_POWER",
                "OT_RADIO_CAPS_ALT_SHORT_ADDR",
            },
        )
        for software_owned_capability in (
            "OT_RADIO_CAPS_CSMA_BACKOFF",
            "OT_RADIO_CAPS_RECEIVE_TIMING",
            "OT_RADIO_CAPS_RX_ON_WHEN_IDLE",
        ):
            self.assertNotIn(software_owned_capability, advertised)

    def test_software_rx_idle_keeps_mac_ack_capable_receive_path(self) -> None:
        text = source(PLATFORM_SOURCE)
        idle_state = function_body(text, "otRadioState threadRadioIdleState()")
        poll_receive = function_body(text, "bool pollThreadRadioReceive(")
        receive = function_body(
            text, "otError otPlatRadioReceive(otInstance*, uint8_t channel)"
        )
        transmit = function_body(
            text,
            "otError otPlatRadioTransmit(otInstance* instance, otRadioFrame* frame)",
        )

        self.assertIn("OT_RADIO_CAPS_RX_ON_WHEN_IDLE", idle_state)
        self.assertIn("state.radio.receiverArmed()", idle_state)
        self.assertIn(
            "const bool useBufferedReceive = !state.snapshot.radioReceiveAtActive;",
            poll_receive,
        )
        self.assertIn("pollBufferedReceive", poll_receive)
        self.assertIn("pollReceive", poll_receive)
        self.assertIn("state.radio.beginBufferedReceive", receive)
        self.assertNotIn("radioRxOnWhenIdle", receive)
        self.assertIn("if (state.snapshot.radioReceiveAtActive)", transmit)
        self.assertIn("state.radio.beginReceive", transmit)
        self.assertIn("state.radio.beginBufferedReceive", transmit)

    def test_entropy_seeds_mbedtls_from_independent_hardware_rng(self) -> None:
        text = source(PLATFORM_SOURCE)
        entropy = function_body(
            text, "otError otPlatEntropyGet(uint8_t* output, uint16_t outputLength)"
        )
        random_get = function_body(
            text, "otError otPlatCryptoRandomGet(uint8_t* buffer, uint16_t size)"
        )
        reset = function_body(text, "void resetCryptoState()")

        self.assertIn("CracenRng entropyRng;", entropy)
        self.assertIn("entropyRng.fill(output, outputLength)", entropy)
        self.assertNotIn("otPlatCryptoRandomGet", entropy)
        self.assertIn("memset(output, 0, outputLength);", entropy)
        self.assertIn("return OT_ERROR_FAILED;", entropy)
        self.assertIn("cryptoRng.fill(buffer, size)", random_get)
        self.assertIn("memset(buffer, 0, size);", random_get)
        self.assertIn(
            "++xiao_nrf54l15::gOpenThreadPlatformState.snapshot."
            "cryptoRandomFailures;",
            random_get,
        )
        self.assertIn("return OT_ERROR_FAILED;", random_get)
        failure_path = random_get.split("cryptoRng.fill", 1)[1].split("memset", 1)[1]
        self.assertNotIn("OT_ERROR_NONE", failure_path)
        self.assertNotIn("fillPseudoEntropy", text)
        self.assertNotIn("nextEntropyWord", text)
        self.assertIn("cryptoRandomFailures = 0", reset)
        self.assertIn("uint32_t cryptoRandomFailures = 0;", source(PLATFORM_HEADER))

    def test_public_start_and_restart_defaults_preserve_settings(self) -> None:
        header = source(THREAD_HEADER)
        for method in (
            "begin",
            "beginAsChild",
            "beginAsRouter",
            "beginChildFirst",
            "beginJoinerOnly",
            "beginAsSleepyChild",
            "restart",
        ):
            self.assertRegex(
                header,
                rf"\bbool\s+{method}\s*\(bool\s+wipeSettings\s*=\s*false\s*\);",
                f"{method} must preserve persistent settings by default",
            )
        self.assertIn("bool wipeSettings_ = false;", header)

    def test_settings_wipe_remains_explicit(self) -> None:
        body = function_body(
            source(THREAD_SOURCE),
            "bool Nrf54ThreadExperimental::begin(bool wipeSettings, AttachPolicy policy)",
        )
        wipe_branch = body.split("if (wipeSettings) {", 1)[1].split("\n  }", 1)[0]
        self.assertIn(
            "if (!OpenThreadPlatformSkeleton::wipeSettingsChecked())",
            wipe_branch,
        )
        self.assertIn("lastError_ = OT_ERROR_NO_BUFS;", wipe_branch)
        self.assertIn("return false;", wipe_branch)
        self.assertLess(
            wipe_branch.index("wipeSettingsChecked()"),
            wipe_branch.index("settingsWiped_ = true;"),
        )

    def test_settings_store_fails_closed_at_persistence_boundaries(self) -> None:
        text = source(PLATFORM_SOURCE)
        ensure_open = function_body(text, "bool ensureSettingsOpen()")
        read_item = function_body(text, "bool readSettingItem(")
        write_item = function_body(text, "bool writeSettingItem(")
        settings_get = function_body(text, "otError otPlatSettingsGet(")
        settings_add = function_body(text, "otError otPlatSettingsAdd(")

        self.assertIn(
            "gOpenThreadPlatformState.settingsOpen =\n"
            "        gOpenThreadPlatformState.settings.begin",
            ensure_open,
        )
        self.assertNotIn("settingsOpen = true", ensure_open)
        self.assertIn("constexpr uint16_t kSettingMaxValueCount = 32U;", text)
        self.assertIn("static_cast<unsigned int>(index) > UINT16_MAX", settings_get)
        self.assertIn("count >= xiao_nrf54l15::kSettingMaxValueCount", settings_add)

        self.assertIn("getBytesLength(dataKey) !=", read_item)
        self.assertIn("getBytesLength(chunkKey) !=", read_item)
        self.assertIn("before touching the caller's buffer", read_item)
        self.assertIn("countMissingSettingEntries", write_item)
        self.assertIn("settings.freeEntries()", write_item)

    def test_small_settings_use_backward_compatible_compact_storage(self) -> None:
        text = source(PLATFORM_SOURCE)
        missing = function_body(text, "size_t countMissingSettingEntries(")
        read_item = function_body(text, "bool readSettingItem(")
        write_item = function_body(text, "bool writeSettingItem(")

        compact_capacity = missing.split(
            "if (valueLength <= kSettingChunkLength)", 1
        )[1].split("\n  }", 1)[0]
        self.assertIn("makeDataKey", compact_capacity)
        self.assertNotIn("makeLengthKey", compact_capacity)
        self.assertIn("const bool hasLengthKey", read_item)
        self.assertIn("getBytesLength(dataKey)", read_item)
        compact_write = write_item.split(
            "if (valueLength <= kSettingChunkLength)", 1
        )[1].split("\n  }", 1)[0]
        self.assertIn("makeDataKey", compact_write)
        self.assertIn("settings.putBytes", compact_write)
        self.assertNotIn("putUShort", compact_write)

    def test_settings_directory_reads_legacy_counts_and_validates_mapping(self) -> None:
        text = source(PLATFORM_SOURCE)
        load = function_body(text, "bool loadSettingDirectory(")
        decode = function_body(text, "bool decodeSettingDirectory(")
        valid = function_body(text, "bool settingDirectoryValid(")

        self.assertIn("getBytesLength(countKey)", load)
        self.assertIn("getUShort(countKey, UINT16_MAX)", load)
        self.assertIn("Legacy records stored only a ushort count", load)
        self.assertIn(
            "directory->physicalIndices[i] = static_cast<uint8_t>(i);", load
        )
        self.assertIn("bytes[0] != 'O'", decode)
        self.assertIn("bytes[3] != '1'", decode)
        self.assertIn("used[physicalIndex]", valid)
        self.assertIn("!used[directory.pendingIndex]", valid)

    def test_failed_staged_writes_remain_owned_and_reclaimable(self) -> None:
        text = source(PLATFORM_SOURCE)
        stage = function_body(text, "bool stageSettingItem(")
        write_item = function_body(text, "bool writeSettingItem(")
        cleanup = function_body(text, "bool cleanupSettingDirectoryGarbage(")

        self.assertLess(
            stage.index("saveSettingDirectory(key, *directory)"),
            stage.index("writeSettingItem(key, physicalIndex"),
        )
        self.assertIn("directory->pendingIndex = physicalIndex;", stage)
        self.assertIn("directory->pendingLength = valueLength;", stage)
        self.assertIn("abandonPendingSetting(key, directory)", stage)

        chunk_failure = write_item.split(
            "settings.putBytes(\n            chunkKey", 1
        )[1].split("offset =", 1)[0]
        self.assertIn(
            "removeSettingItemKeys(key, index, valueLength)", chunk_failure
        )
        length_failure = write_item.split(
            "settings.putUShort(\n          lengthKey", 1
        )[1]
        self.assertIn(
            "removeSettingItemKeys(key, index, valueLength)", length_failure
        )
        self.assertIn("directory->pendingLength", cleanup)
        self.assertLess(
            cleanup.index("removeSettingItemKeys(key, directory->pendingIndex"),
            cleanup.index("directory->pendingIndex = kSettingInvalidPhysicalIndex"),
        )
        settings_get = function_body(text, "otError otPlatSettingsGet(")
        self.assertIn(
            "cleanupSettingDirectoryGarbage(key, &directory)", settings_get
        )

    def test_settings_set_publishes_only_after_complete_stage(self) -> None:
        settings_set = function_body(
            source(PLATFORM_SOURCE), "otError otPlatSettingsSet("
        )

        self.assertNotIn("otPlatSettingsDelete", settings_set)
        self.assertLess(
            settings_set.index("loadSettingDirectory(key, &stagedDirectory)"),
            settings_set.index("stageSettingItem("),
        )
        self.assertLess(
            settings_set.index("stageSettingItem("),
            settings_set.index(
                "saveSettingDirectory(key, committedDirectory)"
            ),
        )
        self.assertIn(
            "single Preferences API write is the visibility commit", settings_set
        )
        commit_failure = settings_set.split(
            "if (!xiao_nrf54l15::saveSettingDirectory", 1
        )[1].split("}\n", 1)[0]
        self.assertIn("abandonPendingSetting", commit_failure)

    def test_delete_and_wipe_commit_visibility_before_cleanup(self) -> None:
        text = source(PLATFORM_SOURCE)
        settings_delete = function_body(text, "otError otPlatSettingsDelete(")
        checked_wipe = function_body(
            text, "bool OpenThreadPlatformSkeleton::wipeSettingsChecked()"
        )
        thread_wipe = function_body(
            source(THREAD_SOURCE),
            "bool Nrf54ThreadExperimental::wipePersistentSettings()",
        )

        self.assertIn("if (index < -1)", settings_delete)
        delete_all = settings_delete.split("if (index == -1)", 1)[1]
        self.assertLess(
            delete_all.index("saveSettingDirectory(key, committedDirectory)"),
            delete_all.index("cleanupSettingDirectoryGarbage("),
        )
        indexed_delete = settings_delete.split(
            "const uint16_t itemIndex", 1
        )[1]
        self.assertNotIn("writeSettingItem", indexed_delete)
        self.assertNotIn("shiftSettingItem", indexed_delete)
        self.assertLess(
            indexed_delete.index(
                "saveSettingDirectory(key, committedDirectory)"
            ),
            indexed_delete.index("cleanupSettingDirectoryGarbage("),
        )
        self.assertIn(
            "No item data is overwritten or moved before this count/mapping commit",
            indexed_delete,
        )
        self.assertIn("const bool wiped = wipeSettingsStore();", checked_wipe)
        self.assertLess(
            checked_wipe.index("if (wiped)"),
            checked_wipe.index("settingsKeyCount = 0U"),
        )

        self.assertIn("if (!stop())", thread_wipe)
        self.assertIn("otInstanceErasePersistentInfo(instance_)", thread_wipe)
        self.assertIn("wipeSettingsChecked()", thread_wipe)
        self.assertLess(
            thread_wipe.index("wipeSettingsChecked()"),
            thread_wipe.index("settingsWiped_ = true;"),
        )

    def test_restart_clears_stale_restore_diagnostics(self) -> None:
        restart = function_body(
            source(THREAD_SOURCE),
            "bool Nrf54ThreadExperimental::restart(bool wipeSettings)",
        )
        self.assertIn("datasetRestoreAttempted_ = false;", restart)
        self.assertIn("datasetRestoredFromSettings_ = false;", restart)

    def test_child_first_fallback_uses_normal_mle_partition_election(self) -> None:
        text = source(THREAD_SOURCE)
        header = source(THREAD_HEADER)
        fallback = function_body(
            text,
            "bool Nrf54ThreadExperimental::maybePromoteChildFirstFallback(",
        )
        delay = function_body(
            text, "uint32_t Nrf54ThreadExperimental::computeChildFirstFallbackDelayMs()"
        )

        self.assertIn("otThreadSetRouterEligible(instance_, true)", fallback)
        self.assertIn("Let MLE continue its normal attach/election flow", fallback)
        self.assertNotIn("otThreadBecomeLeader", fallback)
        self.assertNotIn("maybeForceLeader", text)
        self.assertNotIn("maybeForceLeader", header)
        self.assertNotIn("micros()", delay)
        self.assertIn("otPlatRadioGetIeeeEui64(nullptr, eui64)", delay)

    def test_demo_dataset_is_byte_deterministic(self) -> None:
        dataset = function_body(
            source(THREAD_SOURCE),
            "void Nrf54ThreadExperimental::buildDemoDataset(",
        )
        self.assertIn("memset(outDataset, 0, sizeof(*outDataset));", dataset)
        for token in (
            "kDemoNetworkKey",
            "kDemoNetworkName",
            "kDemoExtPanId",
            "kDemoMeshLocalPrefix",
            "kDemoPskc",
            "kDemoPanId",
            "kDemoChannel",
            "kDemoChannelMask",
        ):
            self.assertIn(token, dataset)
        self.assertNotIn("micros()", dataset)
        self.assertNotIn("millis()", dataset)
        self.assertNotIn("hardwareUniqueId", dataset)

    def test_reset_pal_preserves_settings_and_reports_hardware_reason(self) -> None:
        text = source(PLATFORM_SOURCE)
        reset = function_body(text, "void otPlatReset(otInstance*)")
        reason = function_body(
            text, "otPlatResetReason otPlatGetResetReason(otInstance*)"
        )

        self.assertIn("closeSettings();", reset)
        self.assertIn("softReset();", reset)
        self.assertNotIn("wipeSettings", reset)
        for mask in (
            "RESET_RESETREAS_DOG0_Msk",
            "RESET_RESETREAS_LOCKUP_Msk",
            "RESET_RESETREAS_SREQ_Msk",
            "RESET_RESETREAS_RESETPIN_Msk",
            "RESET_RESETREAS_OFF_Msk",
        ):
            self.assertIn(mask, reason)
        self.assertIn("return OT_PLAT_RESET_REASON_WATCHDOG;", reason)
        self.assertIn("return OT_PLAT_RESET_REASON_SOFTWARE;", reason)
        self.assertIn("return OT_PLAT_RESET_REASON_POWER_ON;", reason)

    def test_udp_close_is_public_and_releases_the_slot_only_after_success(self) -> None:
        header = source(THREAD_HEADER)
        body = function_body(
            source(THREAD_SOURCE),
            "bool Nrf54ThreadExperimental::closeUdp(uint16_t port)",
        )
        self.assertIn("bool closeUdp(uint16_t port);", header)
        self.assertIn("UdpSocketSlot* slot = findUdpSlot(port);", body)
        self.assertIn("lastUdpError_ = otUdpClose(instance_, &slot->socket);", body)
        error_check = body.index("if (lastUdpError_ != OT_ERROR_NONE) return false;")
        release = body.index("*slot = UdpSocketSlot{};")
        self.assertLess(error_check, release)

    def test_explicit_udp_source_port_never_falls_back_to_another_socket(self) -> None:
        body = function_body(
            source(THREAD_SOURCE),
            "bool Nrf54ThreadExperimental::sendUdpFrom(uint16_t localPort,",
        )
        explicit_port = body.split("if (localPort != 0U) {", 1)[1].split(
            "} else {", 1
        )[0]

        self.assertIn("slot = findUdpSlot(localPort);", explicit_port)
        self.assertIn("lastUdpError_ = OT_ERROR_INVALID_STATE;", explicit_port)
        self.assertIn("return false;", explicit_port)
        self.assertNotIn("firstUdpSlot", explicit_port)
        self.assertIn("slot = firstUdpSlot(true);", body)

    def test_dataset_updater_wrappers_match_enabled_config(self) -> None:
        self.assertIn(
            "#define OPENTHREAD_CONFIG_DATASET_UPDATER_ENABLE 1",
            source(CONFIG_HEADER),
        )
        wrappers = {
            "api_dataset_updater_api.cpp": (
                "../../third_party/openthread-core/src/core/api/"
                "dataset_updater_api.cpp"
            ),
            "meshcop_dataset_updater.cpp": (
                "../../third_party/openthread-core/src/core/meshcop/"
                "dataset_updater.cpp"
            ),
        }
        for wrapper_name, included_path in wrappers.items():
            wrapper = SRC / "openthread_core_stage" / wrapper_name
            text = source(wrapper)
            self.assertIn("NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE", text)
            self.assertIn(f'#include "{included_path}"', text)
            self.assertTrue((wrapper.parent / included_path).resolve().is_file())


def run_native_tests() -> None:
    compiler = "g++"
    common = [
        compiler,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-ffunction-sections",
        "-fdata-sections",
        f"-I{TESTS / 'thread_platform_stubs'}",
        f"-I{SRC}",
    ]
    with tempfile.TemporaryDirectory(prefix="nrf54-thread-platform-") as directory:
        output = Path(directory)
        generate_settings_test_fragments(output)

        config_test = output / "thread_platform_config_test"
        subprocess.run(
            common
            + [str(TESTS / "thread_platform_config_test.cpp"), "-o", str(config_test)],
            cwd=ROOT,
            check=True,
        )
        subprocess.run([str(config_test)], cwd=ROOT, check=True)
        print("PASS Thread staged-core config static assertions")

        udp_test = output / "thread_udp_close_test"
        subprocess.run(
            common
            + [
                "-DNRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE=1",
                "-DOPENTHREAD_FTD=1",
                str(TESTS / "thread_udp_close_test.cpp"),
                "-Wl,--gc-sections",
                "-o",
                str(udp_test),
            ],
            cwd=ROOT,
            check=True,
        )
        subprocess.run([str(udp_test)], cwd=ROOT, check=True)

        settings_test = output / "thread_settings_directory_test"
        subprocess.run(
            common
            + [
                f"-I{output}",
                str(TESTS / "thread_settings_directory_test.cpp"),
                "-o",
                str(settings_test),
            ],
            cwd=ROOT,
            check=True,
        )
        subprocess.run([str(settings_test)], cwd=ROOT, check=True)


def main() -> int:
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(
        ThreadPlatformSourceContracts
    )
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if not result.wasSuccessful():
        return 1
    run_native_tests()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
