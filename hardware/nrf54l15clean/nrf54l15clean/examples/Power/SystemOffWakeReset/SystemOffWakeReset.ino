/*
  SystemOffWakeReset

  Enters real SYSTEMOFF and wakes after 5 seconds through GRTC. Wake is a cold
  boot/reset, so code starts again from setup().

  LED code:
    1 blink = normal boot or external reset
    2 blinks = woke from SYSTEMOFF by the GRTC wake timer

  For "sleep then continue" behavior, use delayLowPowerIdle(). The
  delaySystemOff* APIs are also true SYSTEMOFF calls and never return.
*/

#include <Arduino.h>

static constexpr unsigned long kWakeDelayMs = 5000UL;

static void ledOff() {
  digitalWrite(LED_BUILTIN, HIGH);
}

static void ledOn() {
  digitalWrite(LED_BUILTIN, LOW);
}

static void blinkCode(uint8_t count) {
  for (uint8_t i = 0; i < count; ++i) {
    ledOn();
    delay(100);
    ledOff();
    delay(180);
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  ledOff();

  const bool wokeBySystemOff = wasSystemOffWakeReset();
  const bool wokeByGrtc = wasSystemOffWakeFromGrtc();
  clearSystemOffWakeResetReason();

  blinkCode((wokeBySystemOff && wokeByGrtc) ? 2U : 1U);
  delay(500);

  systemOffWakeReset(kWakeDelayMs);
}

void loop() {}
