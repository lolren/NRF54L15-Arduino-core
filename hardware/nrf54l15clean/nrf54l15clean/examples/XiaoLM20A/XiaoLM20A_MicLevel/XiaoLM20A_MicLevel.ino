/*
 * XiaoLM20A_MicLevel
 *
 * Reads the onboard PDM microphone on XIAO nRF54LM20A Sense.
 *
 * LM20A PDM pins (per schematic):
 *   CLK = P1.13
 *   DAT = P1.14
 *   PDM instance = PDM20
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

static constexpr Pin kPdmClk{1, 13};
static constexpr Pin kPdmDin{1, 14};
static constexpr size_t kSampleCount = 512;

static Pdm g_pdm;
alignas(4) static int16_t g_samples[kSampleCount];

void setup() {
    Serial.begin(115200);
    delay(250);

    // Power IMU+MIC rail
    npm1300_begin();
    if (!npm1300_imu_mic_power_enable(true)) {
        Serial.println("ERROR: nPM1300 sensor rail failed");
    }
    delay(25);

    // PDM: mono, prescaler=10 (~1.6 MHz PDM_CLK with PCLK32M), ratio=64
    if (!g_pdm.begin(kPdmClk, kPdmDin, true, 10, PDM_RATIO_RATIO_Ratio64,
                     PdmEdge::kLeftRising)) {
        Serial.println("ERROR: PDM20 begin failed");
        return;
    }

    Serial.println("XiaoLM20A_MicLevel");
    Serial.println("pdm=PDM20 clk=P1.13 dat=P1.14");
}

void loop() {
    if (!g_pdm.capture(g_samples, kSampleCount, 4000000UL)) {
        Serial.println("capture timeout");
        delay(100);
        return;
    }

    int64_t sum = 0;
    bool hasAudio = false;
    for (size_t i = 0; i < kSampleCount; i++) {
        sum += static_cast<int64_t>(g_samples[i]) * g_samples[i];
        if (g_samples[i] != 0) hasAudio = true;
    }

    if (!hasAudio) {
        Serial.println("Mic: silence (all zeros)");
        delay(100);
        return;
    }

    float rms = sqrtf(static_cast<float>(sum) / kSampleCount);
    float dbFS = 20.0f * log10f(rms / 32768.0f);

    Serial.print("Mic: RMS=");
    Serial.print(rms, 0);
    Serial.print("  ");
    Serial.print(dbFS, 1);
    Serial.println(" dBFS");

    delay(100);
}
