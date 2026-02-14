# Changelog

All notable changes to this project are documented in this file.

## 1.1.0 - 2026-02-14

- Added `examples/03.Board/HardwareValidationMatrix`:
  a single serial-menu bring-up sketch for ADC, I2C, SPI, UART1 loopback,
  watchdog, sleep, CPU frequency control, power profiles, BLE tests, and
  IEEE 802.15.4 tests.
- Added deterministic release archive mtimes in
  `tools/release_boards_manager.py` via `--source-date-epoch`.
- Added `tools/check_release_reproducible.py` to generate release artifacts
  twice and verify matching archive SHA-256 and package index JSON.
- Extended `tools/ci_fresh_machine_smoke.py` to validate
  `HardwareValidationMatrix` presence and compile coverage on default, BLE,
  and 802.15.4 profiles.
- Updated GitHub Actions workflow to run reproducibility checks before
  fresh-machine smoke.
