#!/usr/bin/env python3
"""Regression checks for the XIAO nRF54LM20A Sense PDM path."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / "hardware/nrf54l15clean/nrf54l15clean"
LM20_TYPES = PLATFORM / "cores/nrf54lm20b/nrf54lm20b_types.h"
L15_TYPES = PLATFORM / "cores/nrf54l15/nrf54l15_types.h"
LM20_PINS = PLATFORM / "variants/xiao_nrf54lm20b/pins_arduino.h"
HAL_HEADER = (
    PLATFORM
    / "libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.h"
)
HAL_ANALOG = (
    PLATFORM
    / "libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts"
    / "nrf54l15_hal_crypto_analog.inc"
)
PIN_HEADER = (
    PLATFORM
    / "libraries/Nrf54L15-Clean-Implementation/src/xiao_nrf54l15_pins.h"
)
MIC_EXAMPLE = (
    PLATFORM
    / "libraries/nRF54-Board-Examples/examples/XIAO-nRF54LM20A-Sense/XiaoLM20A_MicLevel/XiaoLM20A_MicLevel.ino"
)
L15_MIC_EXAMPLE = (
    PLATFORM
    / "libraries/nRF54-Board-Examples/examples/XIAO-nRF54L15-Sense/XiaoSenseMicLevel/XiaoSenseMicLevel.ino"
)
PDM21_EXAMPLE = (
    PLATFORM
    / "libraries/Nrf54L15-Clean-Implementation/examples/Peripherals/Pdm21Microphone/Pdm21Microphone.ino"
)


def macro_value(source: str, name: str) -> int:
    match = re.search(
        rf"^\s*#define\s+{re.escape(name)}\s+\(?(0x[0-9A-Fa-f]+|[0-9]+)",
        source,
        re.MULTILINE,
    )
    if not match:
        raise AssertionError(f"missing numeric macro {name}")
    return int(match.group(1), 0)


class Lm20aPdmContractTests(unittest.TestCase):
    def test_lm20_edge_and_ratio_encoding_matches_product_mdk(self) -> None:
        source = LM20_TYPES.read_text(encoding="utf-8")
        expected = {
            "PDM_MODE_EDGE_LeftFalling": 0,
            "PDM_MODE_EDGE_LeftRising": 1,
            "PDM_RATIO_RATIO_Ratio48": 0,
            "PDM_RATIO_RATIO_Ratio50": 1,
            "PDM_RATIO_RATIO_Ratio64": 2,
            "PDM_RATIO_RATIO_Ratio80": 3,
            "PDM_RATIO_RATIO_Ratio96": 4,
            "PDM_RATIO_RATIO_Ratio150": 5,
            "PDM_RATIO_RATIO_Ratio192": 6,
            "PDM_RATIO_RATIO_Custom": 7,
        }
        for name, value in expected.items():
            self.assertEqual(macro_value(source, name), value, name)

        for stale_name in (
            "PDM_RATIO_RATIO_Ratio32",
            "PDM_RATIO_RATIO_Ratio100",
            "PDM_RATIO_RATIO_Ratio128",
        ):
            self.assertNotIn(stale_name, source)

    def test_l15_encoding_remains_product_specific(self) -> None:
        source = L15_TYPES.read_text(encoding="utf-8")
        self.assertEqual(macro_value(source, "PDM_MODE_EDGE_LeftFalling"), 1)
        self.assertEqual(macro_value(source, "PDM_MODE_EDGE_LeftRising"), 0)
        self.assertEqual(macro_value(source, "PDM_RATIO_RATIO_Ratio64"), 3)

    def test_board_mic_routes_match_schematic(self) -> None:
        source = LM20_PINS.read_text(encoding="utf-8")
        self.assertRegex(
            source,
            r"case\s+PIN_PDM_CLK:\s+\*port\s*=\s*1;\s*\*pinInPort\s*=\s*13;",
        )
        self.assertRegex(
            source,
            r"case\s+PIN_PDM_DATA:\s+\*port\s*=\s*1;\s*\*pinInPort\s*=\s*14;",
        )

    def test_pdm_dma_count_and_timeout_cleanup_match_hardware_contract(self) -> None:
        header = HAL_HEADER.read_text(encoding="utf-8")
        source = HAL_ANALOG.read_text(encoding="utf-8")
        cleanup_start = source.index("bool Pdm::stopAndReleaseDma(")
        capture_start = source.index("bool Pdm::capture(")
        cleanup = source[cleanup_start:capture_start]
        capture = source[capture_start:]

        self.assertIn("bool stopAndReleaseDma(uint32_t timeoutUs);", header)
        self.assertIn(
            "sampleCount is the number of int16_t samples", header
        )
        self.assertIn(
            "PDM_SAMPLE_MAXCNT_BUFFSIZE_Max / sizeof(int16_t)", capture
        )
        self.assertIn(
            "sampleCount * sizeof(int16_t)", capture
        )
        self.assertIn("pdm_->SAMPLE.MAXCNT = dmaByteCount;", capture)
        self.assertIn("Gpio::write(clk, false)", source)
        self.assertIn("clearPdmEvent(&pdm_->EVENTS_END)", source)
        self.assertIn("(void)*event;", source)
        self.assertIn("+ 0x54CUL", source)
        self.assertIn("if (pdm_->ENABLE != PDM_ENABLE_ENABLE_Disabled)", cleanup)
        self.assertIn("pdm_->ENABLE = PDM_ENABLE_ENABLE_Disabled;", cleanup)
        self.assertIn("pdm_->ENABLE = PDM_ENABLE_ENABLE_Enabled;", capture)
        self.assertIn("void applyConfiguration();", header)
        self.assertIn("void Pdm::applyConfiguration()", source)
        self.assertGreaterEqual(capture.count("applyConfiguration();"), 1)
        self.assertIn("kDrainSampleCount = 64U", header)
        self.assertIn("sampleCount < kDrainSampleCount", capture)
        self.assertIn("sizeof(drainSamples_)", capture)
        self.assertIn("reinterpret_cast<uintptr_t>(drainSamples_)", capture)
        self.assertIn("uint32_t timeoutUs = 0UL", header)
        self.assertIn("pdmCaptureTimeoutUs(sampleCount, sampleRateHz_)", capture)
        self.assertIn("static_cast<uint32_t>(micros() - startedUs)", source)
        self.assertIn("expectedUs * 2ULL + 250000ULL", source)

        started_wait = capture.index(
            "waitForPdmEvent(&pdm_->EVENTS_STARTED"
        )
        first_return = capture.index("return false;", started_wait)
        started_invalidate = capture.index(
            "Cache::invalidateForDma(samples, dmaByteCount)", started_wait
        )
        self.assertLess(
            capture.index("stopAndReleaseDma(100000UL)", started_wait),
            started_invalidate,
        )
        self.assertLess(started_invalidate, first_return)
        final_return = capture.index(
            "return endSeen && stoppedBeforeDeadline && !busError;"
        )
        stop_before_invalidate = capture.rindex(
            "stopAndReleaseDma(100000UL)", 0, final_return
        )
        invalidate = capture.index(
            "Cache::invalidateForDma(samples, dmaByteCount)",
            stop_before_invalidate,
        )
        self.assertLess(
            stop_before_invalidate,
            invalidate,
        )
        self.assertLess(
            invalidate,
            final_return,
        )

        self.assertIn("PDM_ENABLE_ENABLE_Disabled", cleanup)
        self.assertIn("while (pdm_->EVENTS_STOPPED == 0U)", cleanup)
        self.assertIn("return stoppedBeforeDeadline;", cleanup)
        self.assertNotIn("pdm_->PSEL.CLK = PSEL_DISCONNECTED", cleanup)
        self.assertNotIn("pdm_->SAMPLE.PTR = 0U", cleanup)
        stop_fence = cleanup.index("while (pdm_->EVENTS_STOPPED == 0U)")
        disable = cleanup.index(
            "pdm_->ENABLE = PDM_ENABLE_ENABLE_Disabled;", stop_fence
        )
        self.assertLess(stop_fence, disable)

    def test_pdm_pin_routes_follow_each_product_specification(self) -> None:
        pins = PIN_HEADER.read_text(encoding="utf-8")
        source = HAL_ANALOG.read_text(encoding="utf-8")
        example = PDM21_EXAMPLE.read_text(encoding="utf-8")

        self.assertIn("(p.port == 1U) && (p.pin <= 31U)", pins)
        self.assertIn("(p.port == 3U) && (p.pin <= 12U)", pins)
        self.assertIn("(p.port == 1U) && (p.pin <= 16U)", pins)
        self.assertIn("bool isPdmPortSupported(const Pin& pin)", source)
        self.assertIn("return pin.port == 1U || pin.port == 3U;", source)
        self.assertIn("!isPdmPortSupported(clk)", source)
        self.assertIn("!isPdmPortSupported(din)", source)
        self.assertIn("clk.port == din.port && clk.pin == din.pin", source)
        self.assertIn("case PDM_RATIO_RATIO_Custom:", source)
        self.assertIn("uint8_t prescalerDiv = 25", HAL_HEADER.read_text(encoding="utf-8"))
        self.assertIn("uint8_t ratio = PDM_RATIO_RATIO_Ratio80", HAL_HEADER.read_text(encoding="utf-8"))
        self.assertIn("PdmEdge edge = PdmEdge::kLeftFalling", HAL_HEADER.read_text(encoding="utf-8"))
        self.assertIn("ratioConfig_(PDM_RATIO_RATIO_Ratio80)", source)
        self.assertIn("prescalerConfig_(25U)", source)
        self.assertIn("static constexpr Pin kPdm21Clk{1, 6};", example)
        self.assertIn("static constexpr Pin kPdm21Din{1, 7};", example)
        self.assertIn("true, 25, PDM_RATIO_RATIO_Ratio80", example)
        self.assertIn("PdmEdge::kLeftFalling", example)

    def test_continuous_stream_uses_ping_pong_dma_and_releases_cache(self) -> None:
        header = HAL_HEADER.read_text(encoding="utf-8")
        source = HAL_ANALOG.read_text(encoding="utf-8")
        self.assertIn("struct StreamEvent", header)
        self.assertIn("bool startStream(int16_t* firstBuffer", header)
        self.assertIn("bool queueStreamBuffer(int16_t* buffer)", header)
        self.assertIn("bool pollStream(StreamEvent* event)", header)
        self.assertIn("bool stopStream(uint32_t timeoutUs = 100000UL)", header)
        self.assertIn("streamActiveBuffer_ ^ 1U", source)
        self.assertIn(
            "buffer == streamBuffers_[streamActiveBuffer_]", source
        )
        self.assertIn("event->overflow = true", source)
        self.assertIn("event->bufferRequested = true", source)
        self.assertIn("Cache::invalidateForDma(\n      event->releasedBuffer", source)
        self.assertIn("if (!configured_ || streaming_ || samples == nullptr", source)

    def test_example_selects_exact_16_khz_left_channel(self) -> None:
        source = MIC_EXAMPLE.read_text(encoding="utf-8")
        self.assertIn("kPdmPrescaler = 25U", source)
        self.assertIn("kPdmRatio = PDM_RATIO_RATIO_Ratio80", source)
        self.assertIn("PdmEdge::kLeftFalling", source)
        self.assertIn("kSampleCount = 8192", source)
        self.assertIn("kAnalysisStart = 512U", source)
        self.assertIn("g_pdm.capture(g_capture.samples, kSampleCount", source)
        self.assertNotIn("50000000UL", source)
        self.assertIn("captureGuardIntact()", source)
        self.assertIn("captureUntouchedTailCount()", source)
        self.assertIn("captureMs < 400U || captureMs > 800U", source)
        self.assertIn("PDM DMA exceeded the sample buffer", source)
        self.assertIn("pdm_hz=1280000 pcm_hz=16000", source)
        self.assertIn("npm1300_ldo1_is_enabled()", source)
        self.assertIn("static bool g_ready = false", source)
        self.assertIn("if (!g_ready)", source)

        l15_source = L15_MIC_EXAMPLE.read_text(encoding="utf-8")
        self.assertIn("true, 25U", l15_source)
        self.assertIn("PDM_RATIO_RATIO_Ratio80", l15_source)
        self.assertIn("PdmEdge::kLeftFalling", l15_source)
        self.assertIn("kSampleCount = 8192U", l15_source)
        self.assertIn("kAnalysisStart = 512U", l15_source)
        self.assertIn("captureGuardIntact()", l15_source)
        self.assertIn("captureUntouchedTailCount()", l15_source)
        self.assertIn("captureMs < 400U || captureMs > 800U", l15_source)
        self.assertIn("if (!BoardControl::setImuMicEnabled(true))", l15_source)
        self.assertIn("static bool g_ready = false", l15_source)
        self.assertIn("if (!g_ready)", l15_source)


if __name__ == "__main__":
    unittest.main(verbosity=2)
