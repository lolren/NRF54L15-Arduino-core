#!/usr/bin/env python3
"""Run host utility sanitizers and validate nRF54 IRQ vector contracts."""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / "hardware/nrf54l15clean/nrf54l15clean"
TESTS = ROOT / "tests/core_io"

CHIPS = {
    "nrf54l15": {
        "last_irq": 269,
        "vectors": {
            70: "AAR00_CCM00_IRQHandler",
            71: "ECB00_IRQHandler",
            74: "SPIM00_IRQHandler",
            135: "EGU10_IRQHandler",
            138: "RADIO_0_IRQHandler",
            139: "RADIO_1_IRQHandler",
            198: "SPIM20_IRQHandler",
            199: "SPIM21_IRQHandler",
            200: "SPIM22_IRQHandler",
            201: "EGU20_IRQHandler",
            202: "TIMER20_IRQHandler",
            203: "TIMER21_IRQHandler",
            204: "TIMER22_IRQHandler",
            205: "TIMER23_IRQHandler",
            206: "TIMER24_IRQHandler",
            208: "PDM20_IRQHandler",
            209: "PDM21_IRQHandler",
            210: "PWM20_IRQHandler",
            211: "PWM21_IRQHandler",
            212: "PWM22_IRQHandler",
            213: "SAADC_IRQHandler",
            214: "NFCT_IRQHandler",
            215: "TEMP_IRQHandler",
            218: "GPIOTE20_0_IRQHandler",
            219: "GPIOTE20_1_IRQHandler",
            221: "I2S20_IRQHandler",
            224: "QDEC20_IRQHandler",
            225: "QDEC21_IRQHandler",
            226: "GRTC_0_IRQHandler",
            227: "GRTC_1_IRQHandler",
            228: "GRTC_2_IRQHandler",
            229: "GRTC_3_IRQHandler",
            260: "SPIM30_IRQHandler",
            261: "CLOCK_POWER_IRQHandler",
            262: "LPCOMP_IRQHandler",
            264: "WDT30_IRQHandler",
            265: "WDT31_IRQHandler",
            268: "GPIOTE30_0_IRQHandler",
            269: "GPIOTE30_1_IRQHandler",
        },
        "irqs": {
            "AAR00_CCM00": 70,
            "ECB00": 71,
            "SPIM00": 74,
            "SPIM20": 198,
            "SPIM21": 199,
            "SPIM22": 200,
            "SAADC": 213,
            "NFCT": 214,
            "GPIOTE20_0": 218,
            "GPIOTE20_1": 219,
            "I2S20": 221,
            "SPIM30": 260,
            "CLOCK_POWER": 261,
            "LPCOMP": 262,
            "GPIOTE30_0": 268,
            "GPIOTE30_1": 269,
        },
    },
    "nrf54lm20b": {
        "last_irq": 270,
        "vectors": {
            74: "AAR00_CCM00_IRQHandler",
            75: "ECB00_IRQHandler",
            77: "SPIM00_IRQHandler",
            135: "EGU10_IRQHandler",
            138: "RADIO_0_IRQHandler",
            139: "RADIO_1_IRQHandler",
            198: "SPIM20_IRQHandler",
            199: "SPIM21_IRQHandler",
            200: "SPIM22_IRQHandler",
            201: "EGU20_IRQHandler",
            202: "TIMER20_IRQHandler",
            203: "TIMER21_IRQHandler",
            204: "TIMER22_IRQHandler",
            205: "TIMER23_IRQHandler",
            206: "TIMER24_IRQHandler",
            208: "PDM20_IRQHandler",
            209: "PDM21_IRQHandler",
            210: "PWM20_IRQHandler",
            211: "PWM21_IRQHandler",
            212: "PWM22_IRQHandler",
            213: "SAADC_IRQHandler",
            214: "NFCT_IRQHandler",
            215: "TEMP_IRQHandler",
            218: "GPIOTE20_0_IRQHandler",
            219: "GPIOTE20_1_IRQHandler",
            224: "QDEC20_IRQHandler",
            225: "QDEC21_IRQHandler",
            226: "GRTC_0_IRQHandler",
            227: "GRTC_1_IRQHandler",
            228: "GRTC_2_IRQHandler",
            229: "GRTC_3_IRQHandler",
            237: "SPIM23_IRQHandler",
            260: "SPIM30_IRQHandler",
            262: "LPCOMP_IRQHandler",
            264: "WDT30_IRQHandler",
            265: "WDT31_IRQHandler",
            268: "GPIOTE30_0_IRQHandler",
            269: "GPIOTE30_1_IRQHandler",
            270: "CLOCK_POWER_IRQHandler",
        },
        "irqs": {
            "AAR00_CCM00": 74,
            "ECB00": 75,
            "SPIM00": 77,
            "SPIM20": 198,
            "SPIM21": 199,
            "SPIM22": 200,
            "SAADC": 213,
            "NFCT": 214,
            "GPIOTE20_0": 218,
            "GPIOTE20_1": 219,
            "SPIM23": 237,
            "SPIM30": 260,
            "LPCOMP": 262,
            "GPIOTE30_0": 268,
            "GPIOTE30_1": 269,
            "CLOCK_POWER": 270,
        },
    },
}


def run(command: list[str], *, env: dict[str, str] | None = None) -> None:
    subprocess.run(command, cwd=ROOT, env=env, check=True)


def vector_entries(path: Path) -> list[str]:
    lines = path.read_text(encoding="utf-8").splitlines()
    label = next(i for i, line in enumerate(lines) if line.strip() == "g_pfnVectors:")
    size = next(
        i for i, line in enumerate(lines[label + 1 :], label + 1)
        if line.strip().startswith(".size g_pfnVectors")
    )
    entries: list[str] = []
    index = label + 1
    while index < size:
        code = lines[index].split("/*", 1)[0].strip()
        if code.startswith(".word"):
            entries.append(code.split()[1])
        elif code.startswith(".rept"):
            count = int(code.split()[1], 0)
            index += 1
            while index < size and not lines[index].strip().startswith(".word"):
                index += 1
            if index >= size:
                raise AssertionError(f"unterminated .rept in {path}")
            word = lines[index].split("/*", 1)[0].strip().split()[1]
            entries.extend([word] * count)
            while index < size and not lines[index].strip().startswith(".endr"):
                index += 1
            if index >= size:
                raise AssertionError(f"missing .endr in {path}")
        index += 1
    return entries


def validate_vectors() -> None:
    for chip, contract in CHIPS.items():
        core = PLATFORM / "cores" / chip
        startup = core / f"startup_{chip}.S"
        cmsis = core / "cmsis.h"
        entries = vector_entries(startup)
        expected_count = 16 + int(contract["last_irq"]) + 1
        assert len(entries) == expected_count, (
            f"{chip}: {len(entries)} vector entries, expected {expected_count}"
        )
        for irq, handler in contract["vectors"].items():
            actual = entries[16 + irq]
            assert actual == handler, f"{chip} IRQ {irq}: {actual}, expected {handler}"

        cmsis_text = cmsis.read_text(encoding="utf-8")
        for name, irq in contract["irqs"].items():
            match = re.search(rf"\b{name}_IRQn\s*=\s*(-?\d+)", cmsis_text)
            assert match is not None, f"{chip}: missing {name}_IRQn"
            assert int(match.group(1)) == irq, (
                f"{chip} {name}_IRQn={match.group(1)}, expected {irq}"
            )

        if chip == "nrf54lm20b":
            assert entries[16 + 221] == "Default_Handler"
            assert "I2S20_IRQn" not in cmsis_text
            assert all("I2S20" not in entry for entry in entries)
        print(f"PASS {chip} vectors: {len(entries)} entries")


def validate_cmsis_priority_contracts() -> None:
    for chip in CHIPS:
        cmsis_text = (PLATFORM / "cores" / chip / "cmsis.h").read_text(
            encoding="utf-8"
        )
        assert "__NVIC_SystemPriorityByte" in cmsis_text
        set_body = function_body(cmsis_text, "static inline void __NVIC_SetPriority(")
        get_body = function_body(cmsis_text, "static inline uint32_t __NVIC_GetPriority(")
        assert "0xE000ED18UL" in cmsis_text
        assert "__NVIC_SystemPriorityByte(IRQn)" in set_body
        assert "__NVIC_SystemPriorityByte(IRQn)" in get_body
        print(f"PASS {chip} CMSIS system-exception priority contract")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


def validate_hardware_serial_contracts() -> None:
    for chip in CHIPS:
        source = (PLATFORM / "cores" / chip / "HardwareSerial.cpp").read_text(
            encoding="utf-8"
        )
        baud_body = function_body(source, "static uint32_t baud_to_reg(")
        assert "static_cast<uint64_t>(baud) << 32U" in baud_body
        assert "kPresets[bestIndex].baud) << 32U" not in baud_body

        end_body = function_body(source, "void HardwareSerial::end()")
        assert end_body.index("drainTxForShutdown()") < end_body.index(
            "U_TASKS_DMA_TX_STOP"
        )
        drain_body = function_body(source, "bool HardwareSerial::drainTxForShutdown()")
        assert "serial_byte_timeout_us" in drain_body
        assert "serviceTxDma()" in drain_body

        route_body = function_body(source, "static bool uart_route_valid(")
        assert "tx == 2U && rx == 0U" in route_body
        assert "tx == 8U && rx == 7U" in route_body
        print(f"PASS {chip} serial shutdown, high-baud, and route contracts")


def validate_spi_contracts() -> None:
    source = (PLATFORM / "cores/nrf54lm20b/SPI.cpp").read_text(encoding="utf-8")
    begin_transaction = function_body(
        source, "void SPIClass::beginTransaction(SPISettings settings)"
    )
    assert "if (!_initialized || _spim == nullptr)" in begin_transaction
    assert begin_transaction.index("if (!_initialized || _spim == nullptr)") < (
        begin_transaction.index("applySettings()")
    )
    assert "_inTransaction = false;" in begin_transaction
    print("PASS nrf54lm20b SPI beginTransaction route-failure guard")


def validate_system_off_wake_contracts() -> None:
    for chip in CHIPS:
        source = (PLATFORM / "cores" / chip / "wiring_time.c").read_text(
            encoding="utf-8"
        )
        arm_body = function_body(
            source, "static void armSystemOffWakeCompare("
        )
        wake_selector_body = function_body(
            source, "static uint8_t systemOffWakeChannel("
        )
        systemoff_entry_body = function_body(
            source, "static void enterSystemOffInternal(bool disableRamRetention"
        )
        program_body = function_body(
            source, "static system_off_wake_program_status_t programSystemOffWakeUs("
        )
        assert "uint64_t wakeTimestampUs" in source
        assert "CCADD" not in arm_body
        assert "GRTC_CC_CCEN_ACTIVE_Enable" in arm_body
        assert "kSystemOffWakePreferredCcChannel = 6U" in source
        assert "highestSetBit(available)" not in wake_selector_body
        assert "lowestSetBit(available)" in wake_selector_body
        assert wake_selector_body.index("kSystemOffWakePreferredCcChannel") < (
            wake_selector_body.index("lowestSetBit(available)")
        )
        assert "readGrtcCounterUs(grtc) + (uint64_t)wakeDelayUs" in program_body
        assert program_body.index("readGrtcCounterUs(grtc) + (uint64_t)wakeDelayUs") < (
            program_body.index("armSystemOffWakeCompare(")
        )
        assert "kScbScrSleepDeep_Msk" in systemoff_entry_body
        assert "__asm volatile(\"wfi\")" in systemoff_entry_body
        assert "timedWake && anyGrtcCompareEvent(NRF_GRTC)" in systemoff_entry_body
        assert "abortSystemOffWithReset();" in systemoff_entry_body
        assert "__asm volatile(\"wfe\")" not in systemoff_entry_body
        wfi_index = systemoff_entry_body.index("__asm volatile(\"wfi\")")
        fallback_index = systemoff_entry_body.index(
            "timedWake && anyGrtcCompareEvent(NRF_GRTC)", wfi_index
        )
        assert systemoff_entry_body.index("kScbScrSleepDeep_Msk") < (
            systemoff_entry_body.index("NRF_REGULATORS->SYSTEMOFF")
        )
        assert wfi_index < fallback_index
        assert fallback_index < (
            systemoff_entry_body.index("abortSystemOffWithReset();", fallback_index)
        )
        print(f"PASS {chip} timed SYSTEMOFF absolute GRTC compare/channel contract")

    hal_source = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_timebase.cpp"
    ).read_text(encoding="utf-8")
    hal_arm_body = function_body(hal_source, "void armSystemOffWakeCompare(")
    assert "GRTC_CC_CCEN_ACTIVE_Enable" in hal_arm_body
    print("PASS HAL timed SYSTEMOFF compare explicit enable contract")


def validate_xiao_low_power_board_contracts() -> None:
    source = (PLATFORM / "variants/xiao_nrf54l15/variant.cpp").read_text(
        encoding="utf-8"
    )
    board_state_body = function_body(
        source, 'extern "C" void xiaoNrf54l15EnterLowestPowerBoardState('
    )
    assert "gpioSetInputHighZ(kSamd11TxPort" not in board_state_body
    assert "gpioSetInputHighZ(kSamd11RxPort" not in board_state_body
    print("PASS XIAO low-power board state preserves SAMD11 serial bridge pins")


def compile_and_run_host_tests(temp: Path) -> None:
    cxx = os.environ.get("CXX")
    if not cxx:
        cxx = "/usr/bin/g++" if Path("/usr/bin/g++").is_file() else "g++"
    if shutil.which(cxx) is None:
        raise SystemExit(f"C++ compiler not found: {cxx}")
    core = PLATFORM / "cores/nrf54l15"
    common = [cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror"]
    sanitizer_env = os.environ.copy()
    sanitizer_env["ASAN_OPTIONS"] = "abort_on_error=1:detect_leaks=1"
    sanitizer_env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"

    wstring = temp / "wstring_alias_test"
    run(common + [
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        f"-I{core}",
        str(TESTS / "wstring_alias_test.cpp"),
        "-o", str(wstring),
    ])
    run([str(wstring)], env=sanitizer_env)
    print("PASS WString alias/self-concat ASan+UBSan")

    timer = temp / "software_timer_test"
    run(common + [
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-include", str(TESTS / "software_timer_stubs.h"),
        f"-I{core}",
        str(core / "SoftwareTimer.cpp"),
        str(TESTS / "software_timer_test.cpp"),
        "-o", str(timer),
    ])
    run([str(timer)], env=sanitizer_env)
    print("PASS SoftwareTimer deletion/lifetime ASan+UBSan")

    math_test = temp / "wiring_math_test"
    run(common + [
        "-fsanitize=undefined",
        "-fno-sanitize-recover=undefined",
        f"-I{core}",
        str(TESTS / "wiring_math_test.cpp"),
        "-o", str(math_test),
    ])
    run([str(math_test)], env=sanitizer_env)
    print("PASS map extreme-range UBSan")

    for chip, contract in CHIPS.items():
        nvic_test = temp / f"nvic_layout_{chip}"
        run(common + [
            "-Wno-int-to-pointer-cast",
            f"-DEXPECTED_LAST_IRQ={contract['last_irq']}",
            f"-I{PLATFORM / 'cores' / chip}",
            str(TESTS / "nvic_layout_test.cpp"),
            "-o", str(nvic_test),
        ])
        run([str(nvic_test)])
        print(f"PASS {chip} NVIC register layout and IRQ priority coverage")


def main() -> int:
    validate_vectors()
    validate_cmsis_priority_contracts()
    validate_hardware_serial_contracts()
    validate_spi_contracts()
    validate_system_off_wake_contracts()
    validate_xiao_low_power_board_contracts()
    with tempfile.TemporaryDirectory(prefix="nrf54-core-io-") as directory:
        compile_and_run_host_tests(Path(directory))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
