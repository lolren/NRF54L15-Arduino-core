/*
 * XiaoSenseLM20B_MicLevel
 *
 * Reads the onboard PDM microphone on XIAO nRF54LM20B.
 *
 * LM20B PDM pins (different from L15 Sense PDM21):
 *   CLK = P1.13
 *   DAT = P1.14
 *   PDM instance = PDM20
 *
 * Power: nPM1300 LDO1 (shared with IMU)
 *
 * Output: audio level in dBFS once per 100 ms
 */

#include <Arduino.h>
#include "npm1300.h"

// PDM20 base address
#define PDM20_BASE      0x50100000UL

// PDM registers
#define PDM_TASKS_START     0x000
#define PDM_TASKS_STOP      0x004
#define PDM_EVENTS_STARTED  0x100
#define PDM_EVENTS_END      0x104
#define PDM_EVENTS_STOPPED  0x108
#define PDM_ENABLE          0x500
#define PDM_PSEL_CLK        0x508
#define PDM_PSEL_DIN        0x50C
#define PDM_FREQ            0x510
#define PDM_MODE            0x514
#define PDM_GAINL           0x518
#define PDM_GAINR           0x51C
#define PDM_RATIO           0x520
#define PDM_SAMPLE_PTR      0x530
#define PDM_SAMPLE_MAXCNT   0x534
#define PDM_EVENTS_END_OFF  0x104

static volatile uint32_t* const pdm = (volatile uint32_t*)PDM20_BASE;
static int16_t samples[512];

void setup() {
    Serial.begin(115200);
    delay(250);

    // Power mic (shared LDO1 with IMU)
    npm1300_ldo1_enable(true);
    delay(10);

    // Configure PDM
    pdm[PDM_ENABLE/4] = 0;
    pdm[PDM_PSEL_CLK/4] = (1UL << 5) | 13;  // P1.13
    pdm[PDM_PSEL_DIN/4] = (1UL << 5) | 14;  // P1.14
    pdm[PDM_FREQ/4]   = 0x08000000;    // ~1.024 MHz PDM clock
    pdm[PDM_MODE/4]    = 0;             // Mono, rising edge
    pdm[PDM_GAINL/4]   = 0x28;          // Default gain
    pdm[PDM_RATIO/4]   = 0x40;          // 64x decimation → 16 kHz
    pdm[PDM_ENABLE/4]  = 1;

    Serial.println("XiaoSenseLM20B_MicLevel");
    Serial.println("pdm=PDM20 clk=P1.13 dat=P1.14");
}

void loop() {
    // Capture 512 samples
    pdm[PDM_SAMPLE_PTR/4]    = (uint32_t)(uintptr_t)samples;
    pdm[PDM_SAMPLE_MAXCNT/4] = 512;
    pdm[PDM_EVENTS_END/4]    = 0;
    pdm[PDM_TASKS_START/4]   = 1;

    // Wait for capture (~32 ms at 16 kHz)
    for (volatile int i = 0; i < 500000; i++) {
        if (pdm[PDM_EVENTS_END/4]) break;
    }
    pdm[PDM_TASKS_STOP/4] = 1;

    // Simple RMS level
    int64_t sum = 0;
    for (int i = 0; i < 512; i++) {
        int32_t s = samples[i];
        sum += (int64_t)s * s;
    }
    float rms = sqrtf((float)sum / 512);
    float dbFS = 20.0f * log10f(rms / 32768.0f);

    Serial.print("Mic level: "); Serial.print(rms, 0);
    Serial.print(" RMS, "); Serial.print(dbFS, 1);
    Serial.println(" dBFS");

    delay(100);
}
