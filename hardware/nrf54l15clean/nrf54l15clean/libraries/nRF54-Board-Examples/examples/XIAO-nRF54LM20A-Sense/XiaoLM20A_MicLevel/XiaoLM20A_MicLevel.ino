/*
 * XiaoLM20A_MicLevel
 *
 * Reads the onboard PDM microphone on XIAO nRF54LM20A Sense.
 *
 * LM20A PDM pins (per schematic):
 *   CLK = P1.13
 *   DAT = P1.14
 *   PDM instance = PDM20 (1.28 MHz clock, 16 kHz PCM)
 *   Power = nPM1300 LDO1 (shared with IMU)
 *
 * If the MIC returns all zeros, check:
 *   1. Board is the "Sense" variant with MSM261DGT006 populated
 *   2. PDM_CLK is toggling (scope on P1.13)
 *   3. PDM_DAT shows PDM pulses (scope on P1.14)
 *
 * Output: audio level in dBFS via Serial (115200 baud)
 */

#include <Arduino.h>
#include "nrf54l15_hal.h"
#include "npm1300.h"

using namespace xiao_nrf54l15;

static constexpr size_t kSampleCount = 8192;
static constexpr size_t kAnalysisStart = 512U;
static constexpr size_t kAnalysisCount = kSampleCount - kAnalysisStart;
static constexpr size_t kGuardWordCount = 8;
static constexpr size_t kUnderfillProbeCount = 16;
static constexpr uint32_t kGuardPattern = 0x50444D20UL;
static constexpr int16_t kUnwrittenPattern = 0x5A5A;
static constexpr uint8_t kPdmPrescaler = 25U;
static constexpr uint8_t kPdmRatio = PDM_RATIO_RATIO_Ratio80;

static_assert(PDM_MODE_EDGE_LeftFalling == 0U,
              "nRF54LM20 PDM falling-edge encoding must match its MDK");
static_assert(PDM_MODE_EDGE_LeftRising == 1U,
              "nRF54LM20 PDM rising-edge encoding must match its MDK");
static_assert(PDM_RATIO_RATIO_Ratio80 == 3U,
              "nRF54LM20 PDM ratio encoding must match its MDK");

static Pdm g_pdm;
static bool g_ready = false;
struct alignas(4) GuardedCaptureBuffer {
    int16_t samples[kSampleCount];
    uint32_t trailing[kGuardWordCount];
};
static GuardedCaptureBuffer g_capture;

static void armCaptureGuard() {
    for (size_t i = 0; i < kSampleCount; i++) {
        g_capture.samples[i] = static_cast<int16_t>(
            static_cast<uint16_t>(kUnwrittenPattern) ^
            static_cast<uint16_t>(i));
    }
    for (size_t i = 0; i < kGuardWordCount; i++) {
        g_capture.trailing[i] = kGuardPattern ^ static_cast<uint32_t>(i);
    }
}

static size_t captureUntouchedTailCount() {
    size_t untouched = 0;
    for (size_t i = kSampleCount - kUnderfillProbeCount;
         i < kSampleCount; i++) {
        const int16_t expected = static_cast<int16_t>(
            static_cast<uint16_t>(kUnwrittenPattern) ^
            static_cast<uint16_t>(i));
        if (g_capture.samples[i] == expected) untouched++;
    }
    return untouched;
}

static bool captureGuardIntact() {
    for (size_t i = 0; i < kGuardWordCount; i++) {
        if (g_capture.trailing[i] !=
            (kGuardPattern ^ static_cast<uint32_t>(i))) {
            return false;
        }
    }
    return true;
}

void setup() {
    Serial.begin(115200);
    delay(250);

    // LDO1 supplies the shared IMU&MIC_3V3 net on the Sense board.
    npm1300_begin();
    if (!npm1300_imu_mic_power_enable(true)) {
        Serial.println("ERROR: nPM1300 sensor rail enable failed");
        return;
    }
    delay(25);
    if (!npm1300_ldo1_is_enabled()) {
        Serial.println("ERROR: nPM1300 LDO1 did not report enabled");
        return;
    }

    // The board's grounded L/R microphone is the left channel in Seeed's
    // working Zephyr mapping. nRF54LM20 encodes that path as LeftFalling.
    const Pin pdmClk{1, 13};
    const Pin pdmDin{1, 14};
    if (!g_pdm.begin(pdmClk, pdmDin, true, kPdmPrescaler, kPdmRatio,
                     PdmEdge::kLeftFalling)) {
        Serial.println("ERROR: PDM20 begin failed");
        return;
    }

    Serial.println("XiaoLM20A_MicLevel");
    Serial.println("pdm=PDM20 clk=P1.13 dat=P1.14 pdm_hz=1280000 pcm_hz=16000");
    Serial.print("registers mode=0x");
    Serial.print(NRF_PDM20->MODE, HEX);
    Serial.print(" ratio=0x");
    Serial.print(NRF_PDM20->RATIO, HEX);
    Serial.print(" prescaler=");
    Serial.println(NRF_PDM20->PRESCALER);
    g_ready = true;
}

void loop() {
    if (!g_ready) {
        delay(1000);
        return;
    }
    // Keep PDM_CLK continuous for the whole 512 ms DMA transaction. Nordic's
    // product specification says startup can invalidate the first samples. The
    // board microphone shows a longer repeatable impulse, so exclude 32 ms
    // (512 samples) and analyze the remaining 480 ms.
    armCaptureGuard();
    const uint32_t captureStartedMs = millis();
    if (!g_pdm.capture(g_capture.samples, kSampleCount)) {
        Serial.println("capture timeout");
        delay(100);
        return;
    }
    const uint32_t captureMs = millis() - captureStartedMs;
    if (!captureGuardIntact()) {
        Serial.println("ERROR: PDM DMA exceeded the sample buffer");
        delay(1000);
        return;
    }
    const size_t untouchedTail = captureUntouchedTailCount();
    if (untouchedTail == kUnderfillProbeCount ||
        captureMs < 400U || captureMs > 800U) {
        Serial.print("ERROR: incomplete PDM DMA capture_ms=");
        Serial.print(captureMs);
        Serial.print(" untouched_tail=");
        Serial.print(untouchedTail);
        Serial.print(" mode=0x");
        Serial.print(NRF_PDM20->MODE, HEX);
        Serial.print(" ratio=0x");
        Serial.print(NRF_PDM20->RATIO, HEX);
        Serial.print(" prescaler=");
        Serial.println(NRF_PDM20->PRESCALER);
        delay(1000);
        return;
    }

    int64_t sum = 0;
    int16_t minimum = INT16_MAX;
    int16_t maximum = INT16_MIN;
    for (size_t i = kAnalysisStart; i < kSampleCount; i++) {
        const int16_t sample = g_capture.samples[i];
        sum += sample;
        if (sample < minimum) {
            minimum = sample;
        }
        if (sample > maximum) {
            maximum = sample;
        }
    }

    const int32_t dc = static_cast<int32_t>(sum / kAnalysisCount);
    uint64_t centeredSquares = 0U;
    for (size_t i = kAnalysisStart; i < kSampleCount; i++) {
        const int32_t centered =
            static_cast<int32_t>(g_capture.samples[i]) - dc;
        centeredSquares += static_cast<uint64_t>(
            static_cast<int64_t>(centered) * centered);
    }

    const float rms = sqrtf(static_cast<float>(centeredSquares) / kAnalysisCount);
    const float dbFS = (rms > 0.0f) ? 20.0f * log10f(rms / 32768.0f) : -INFINITY;

    Serial.print("Mic: dc=");
    Serial.print(dc);
    Serial.print(" min=");
    Serial.print(minimum);
    Serial.print(" max=");
    Serial.print(maximum);
    Serial.print(" p2p=");
    Serial.print(static_cast<int32_t>(maximum) - minimum);
    Serial.print(" RMS=");
    Serial.print(rms, 0);
    Serial.print("  ");
    Serial.print(dbFS, 1);
    Serial.print(" dBFS capture_ms=");
    Serial.println(captureMs);

    delay(100);
}
