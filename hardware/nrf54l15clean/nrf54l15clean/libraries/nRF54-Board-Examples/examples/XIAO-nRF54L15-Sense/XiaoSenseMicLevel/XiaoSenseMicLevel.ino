/*
  XiaoSenseMicLevel

  Captures the XIAO nRF54L15 Sense onboard MSM261DGT006 at 16 kHz and prints
  DC-centered RMS/dBFS plus peak-to-peak level. Talk, tap the desk, or blow near
  the microphone and watch the values change in Serial Monitor at 115200 baud.
*/

#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static constexpr size_t kSampleCount = 8192U;
static constexpr size_t kAnalysisStart = 512U;
static constexpr size_t kAnalysisCount = kSampleCount - kAnalysisStart;
static constexpr size_t kGuardWordCount = 8U;
static constexpr size_t kUnderfillProbeCount = 16U;
static constexpr uint32_t kGuardPattern = 0x50444D15UL;
static constexpr int16_t kUnwrittenPattern = 0x5A5A;

static_assert(PDM_MODE_EDGE_LeftFalling == 1U,
              "nRF54L15 PDM falling-edge encoding must match its MDK");
static_assert(PDM_MODE_EDGE_LeftRising == 0U,
              "nRF54L15 PDM rising-edge encoding must match its MDK");
static_assert(PDM_RATIO_RATIO_Ratio80 == 4U,
              "nRF54L15 PDM ratio encoding must match its MDK");

static Pdm g_pdm;
static bool g_ready = false;

struct alignas(4) GuardedCaptureBuffer {
  int16_t samples[kSampleCount];
  uint32_t trailing[kGuardWordCount];
};
static GuardedCaptureBuffer g_capture;

static void armCaptureGuard() {
  for (size_t i = 0; i < kSampleCount; ++i) {
    g_capture.samples[i] = static_cast<int16_t>(
        static_cast<uint16_t>(kUnwrittenPattern) ^ static_cast<uint16_t>(i));
  }
  for (size_t i = 0; i < kGuardWordCount; ++i) {
    g_capture.trailing[i] = kGuardPattern ^ static_cast<uint32_t>(i);
  }
}

static bool captureGuardIntact() {
  for (size_t i = 0; i < kGuardWordCount; ++i) {
    if (g_capture.trailing[i] !=
        (kGuardPattern ^ static_cast<uint32_t>(i))) {
      return false;
    }
  }
  return true;
}

static size_t captureUntouchedTailCount() {
  size_t untouched = 0U;
  for (size_t i = kSampleCount - kUnderfillProbeCount;
       i < kSampleCount; ++i) {
    const int16_t expected = static_cast<int16_t>(
        static_cast<uint16_t>(kUnwrittenPattern) ^ static_cast<uint16_t>(i));
    if (g_capture.samples[i] == expected) {
      ++untouched;
    }
  }
  return untouched;
}

void setup() {
  Serial.begin(115200);
  delay(250);

  if (!BoardControl::setImuMicEnabled(true)) {
    Serial.println("ERROR: IMU/microphone sensor rail enable failed");
    return;
  }
  delay(25);

  if (!g_pdm.begin(kPinMicClk, kPinMicData, true, 25U,
                   PDM_RATIO_RATIO_Ratio80, PdmEdge::kLeftFalling)) {
    Serial.println("ERROR: PDM20 begin failed");
    return;
  }

  Serial.println("XiaoSenseMicLevel");
  Serial.println("pdm=PDM20 clk=P1.12 dat=P1.13 pdm_hz=1280000 pcm_hz=16000");
  g_ready = true;
}

void loop() {
  if (!g_ready) {
    delay(1000);
    return;
  }

  armCaptureGuard();
  const uint32_t captureStartedMs = millis();
  if (!g_pdm.capture(g_capture.samples, kSampleCount)) {
    Serial.println("ERROR: PDM capture failed");
    delay(1000);
    return;
  }
  const uint32_t captureMs = millis() - captureStartedMs;
  const size_t untouchedTail = captureUntouchedTailCount();
  if (!captureGuardIntact()) {
    Serial.println("ERROR: PDM DMA exceeded the sample buffer");
    delay(1000);
    return;
  }
  if (untouchedTail == kUnderfillProbeCount ||
      captureMs < 400U || captureMs > 800U) {
    Serial.print("ERROR: incomplete PDM DMA capture_ms=");
    Serial.print(captureMs);
    Serial.print(" untouched_tail=");
    Serial.println(untouchedTail);
    delay(1000);
    return;
  }

  int64_t sum = 0;
  int16_t minimum = INT16_MAX;
  int16_t maximum = INT16_MIN;
  for (size_t i = kAnalysisStart; i < kSampleCount; ++i) {
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
  for (size_t i = kAnalysisStart; i < kSampleCount; ++i) {
    const int32_t centered = static_cast<int32_t>(g_capture.samples[i]) - dc;
    centeredSquares += static_cast<uint64_t>(
        static_cast<int64_t>(centered) * centered);
  }

  const float rms = sqrtf(static_cast<float>(centeredSquares) / kAnalysisCount);
  const float dbFS =
      (rms > 0.0f) ? 20.0f * log10f(rms / 32768.0f) : -INFINITY;

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
