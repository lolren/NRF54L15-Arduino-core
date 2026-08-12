# nRF54 Arduino Core 1.0.16

`1.0.16` fixes the system-rail hazard reported in
[issue #110](https://github.com/lolren/nrf54-arduino-core/issues/110) on the
XIAO nRF54LM20A.

## nPM1300 BUCK2 safety

- Treat BUCK2 as the board's fixed 3.3 V `VSYS_3V3` rail. It powers both the
  nRF54LM20A and the SAMD11 USB/debug bridge.
- Make `npm1300_buck2_enable(false)` fail without writing the PMIC, and block
  `npm1300_buck2_set_voltage()` by default. The legacy functions remain present
  for source compatibility and are deprecated.
- Retain a deliberately named build-wide unsafe opt-in for expert low-level
  work, while keeping normal builds fail-closed.
- Add `npm1300_system_buck_is_enabled()` and
  `npm1300_system_buck_set_mode()` for board-aware status and mode-only tuning.
- Correct the low-power examples to tune the populated BUCK2 channel instead
  of the unconnected BUCK1 channel, and remove unsupported current claims.

## Recovery for an already affected board

Remove both USB and battery power, then reconnect USB. If the battery cannot be
removed, connect the paired back pads `TP21` (`SHPHLD`) and `TP22` (GND) for
more than 10 seconds while powered to request an nPM1300 whole-system power
cycle. If the unsafe sketch runs immediately at startup, hold nRF54 RESET while
restoring power and replace it with **pyOCD Recovery** using connect-under-reset.

## Validation

- Core I/O regressions pass with explicit fail-closed BUCK2 contracts.
- All three corrected nPM1300 buck examples compile for XIAO nRF54LM20A.
- `LowPowerZephyrParityBlink` and `DelayAutoLowPowerMeasure` compile for XIAO
  nRF54LM20A with the board-aware system-buck API.
- The electrical and recovery behavior was verified against Nordic's nPM1300
  product specification and Seeed's official board schematic. No intentional
  rail shutdown was performed, and no XIAO nRF54LM20A was attached for this
  release validation.

## Install or upgrade

Use Arduino Boards Manager after refreshing the package index, or install from
the release archive attached to `v1.0.16`.
