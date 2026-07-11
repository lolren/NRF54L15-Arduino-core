/*
 * GrtcPwm Continuous — GRTC Built-in PWM Waveform
 *
 * The nRF54L15 GRTC peripheral has a built-in 8-bit PWM that runs
 * from the 32.768 kHz LFCLK. The PWM period is 256 LFCLK ticks,
 * giving a frequency of 128 Hz (32768/256).
 *
 * This example demonstrates:
 *   - Starting continuous PWM on the GRTC PWM pin
 *   - Sweeping duty cycle (0-100%)
 *   - Reading duty and frequency info
 *   - Event-driven period counting
 *
 * Hardware: Board variant exposing PIN_GRTC_PWM
 * Pin:      P0.03 (GRTC PWM output)
 * Serial:   115200 baud
 */

#include <Arduino.h>
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

#if !defined(PIN_GRTC_PWM)
#error "This board variant must expose PIN_GRTC_PWM for the fixed P0.03 GRTC PWM output."
#endif

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println(F("======================================"));
  Serial.println(F("  GRTC PWM — Continuous Waveform"));
  Serial.println(F("======================================"));
  Serial.println();

  GrtcPwm pwm(nrf54l15::GRTC_BASE);

  Serial.print(F("  GRTC base: 0x"));
  Serial.println(nrf54l15::GRTC_BASE, HEX);
  Serial.print(F("  PWM frequency: "));
  Serial.print(pwm.frequencyHz());
  Serial.println(F(" Hz (128 Hz from 32.768 kHz / 256)"));
  Serial.println();

  // Check if GRTC PWM is supported on this board
  Serial.println(F("--- Pin Support Check ---"));
  const bool grtcPwmSupported = pwm.supportsArduinoPin(PIN_GRTC_PWM);
  Serial.print(F("  PIN_GRTC_PWM / P0.03 supported: "));
  Serial.println(grtcPwmSupported ? F("Yes") : F("No"));
  Serial.println();

  if (!grtcPwmSupported) {
    Serial.println(F("  GRTC PWM pin not available on this board."));
  } else {
    // Start PWM at 50% duty
    Serial.println(F("--- Starting PWM at 50% Duty ---"));
    bool ok = pwm.beginArduinoPin(PIN_GRTC_PWM, 128);  // 128/256 = 50%
    Serial.print(F("  begin() result: "));
    Serial.println(ok ? F("OK") : F("FAIL"));

    if (ok) {
      Serial.println(F("  PWM running on PIN_GRTC_PWM / P0.03 at 50% duty"));
      Serial.println();

      // Sweep duty cycle from 0% to 100% in 10 steps
      Serial.println(F("--- Duty Cycle Sweep ---"));
      for (int step = 0; step <= 10; step++) {
        uint8_t duty = (step * 255) / 10;  // 0..255
        pwm.setDuty8(duty);
        int pct = (duty * 100 + 127) / 255;

        Serial.print(F("  "));
        Serial.print(pct, DEC);
        Serial.print(F("% (duty="));
        Serial.print(duty);
        Serial.print(F(") — "));
        Serial.print(F("periods counted: "));

        // Count some periods
        int count = 0;
        for (int i = 0; i < 5; i++) {
          if (pwm.pollPeriodEnd()) count++;
          delay(25);
        }
        Serial.println(count);
        delay(300);
      }
      Serial.println();

      // Show final state
      Serial.println(F("--- Final State ---"));
      Serial.print(F("  Current duty: "));
      Serial.print(pwm.duty8());
      Serial.print(F(" ("));
      int finalPct = (pwm.duty8() * 100 + 127) / 255;
      Serial.print(finalPct);
      Serial.println(F("%)"));
      Serial.print(F("  Ready: "));
      Serial.println(pwm.ready() ? F("Yes") : F("No"));

      // Stop PWM
      pwm.end();
      Serial.println(F("  PWM stopped"));
    }
  }
  Serial.println();

  Serial.println(F("======================================"));
  Serial.println(F("  GRTC PWM demo complete."));
  Serial.println(F("======================================"));

  while (true) delay(1000);
}

void loop() {
  // Not reached.
}
