/*
 * XiaoSenseLM20B_MicLevel
 *
 * Reads the onboard PDM microphone on XIAO nRF54LM20A using the HAL Pdm class.
 *
 * LM20A PDM pins (different from L15 Sense PDM21):
 *   CLK = P1.13
 *   DAT = P1.14
 *   PDM instance = PDM20 (default)
 *
 * Power: nPM1300 LDO1 (shared with IMU)
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

    // Power mic via PMIC LDO1 as a 3.3 V regulator
    if (!npm1300_imu_mic_power_enable(true)) {
        Serial.println("ERROR: nPM1300 sensor rail enable failed");
    }
    delay(25);

    if (!g_pdm.begin(kPdmClk, kPdmDin, true, 40, PDM_RATIO_RATIO_Ratio64,
                     PdmEdge::kLeftRising)) {
        Serial.println("ERROR: PDM20 begin failed");
        return;
    }

    Serial.println("XiaoSenseLM20B_MicLevel");
    Serial.println("pdm=PDM20 clk=P1.13 dat=P1.14");
}

void loop() {
    if (!g_pdm.capture(g_samples, kSampleCount, 4000000UL)) {
        Serial.println("capture timeout");
        delay(100);
        return;
    }

    // RMS level
    int64_t sum = 0;
    for (size_t i = 0; i < kSampleCount; i++) {
        int32_t s = g_samples[i];
        sum += (int64_t)s * s;
    }
    float rms = sqrtf((float)sum / kSampleCount);
    float dbFS = 20.0f * log10f(rms / 32768.0f);

    Serial.print("Mic: RMS="); Serial.print(rms, 0);
    Serial.print("  "); Serial.print(dbFS, 1);
    Serial.println(" dBFS");

    delay(100);
}
