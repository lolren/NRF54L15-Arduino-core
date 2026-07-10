// Minimal current-measurement sketch for plain delay() in low-power mode.
//
// On XIAO nRF54L15, when Tools -> Power Profile is set to
// `Low Power (WFI Idle)`, delay() uses the tickless GRTC + WFI idle path.
// For sketches that need the explicit XIAO board-collapse/restore sequence,
// use delayLowPowerIdle(ms) instead.
//
// This sketch intentionally does nothing except sleep with delay(). It is the
// cleanest way to compare the core's ordinary-Arduino idle behavior against a
// Zephyr `k_sleep()` test.
//
// For a fair measurement:
// - do not call Serial.begin()
// - close any active Serial monitor
// - disconnect external LEDs and other GPIO loads
// - select Tools -> Power Profile -> Low Power (WFI Idle)

#if defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_XIAO_NRF54LM20A_CLEAN)
#include <npm1300.h>
#endif

void setup() {
#if defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_XIAO_NRF54LM20A_CLEAN)
  // nPM1300 state survives MCU reset. Clear loads left by a previous sensor or
  // battery-measurement sketch so each current run starts from the same state.
  (void)npm1300_imu_mic_power_enable(false);
  (void)npm1300_buck1_set_mode(NPM1300_BUCK_MODE_AUTO);
  (void)npm1300_prepare_for_sleep();
#endif
}

void loop() {
  delay(1000);
}
