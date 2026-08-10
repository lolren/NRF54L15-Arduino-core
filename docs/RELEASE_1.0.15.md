# nRF54 Arduino Core 1.0.15

`1.0.15` fixes the sketch-dependent immediate reset reported in
[issue #108](https://github.com/lolren/nrf54-arduino-core/issues/108) when
`delaySystemOffNoRetention()` is called after initializing `Wire`.

## Idle I2C System OFF

- Treat an initialized but idle synchronous I2C controller as safely quiesced
  when its disabled register state is confirmed. On nRF54L15, issuing STOP to
  an already-idle TWIM does not produce a STOPPED event.
- Continue requiring an explicit STOPPED event for I2C target mode and a
  controller transaction with a pending repeated start.
- Apply the same guarded behavior to the nRF54L15 and nRF54LM20A core variants.
- Clarify that external peripherals may still require their own sleep command
  before measuring whole-board current.

## Validation

- Reproduced the pre-fix failure on an attached XIAO nRF54L15: an idle
  `Wire.begin()` caused shutdown abort stage 2 and software-reset reason `0x40`.
- The same exact-checkout hardware probe now enters timed no-retention System
  OFF and wakes from GRTC with reset reason `0x840` and abort stage 0.
- Baseline timed wake and an intentional asserted-GPIO wake remain unchanged.
- Core I/O regressions pass, and the mirrored Wire implementation compiles for
  XIAO nRF54LM20A.

## Install or upgrade

Use Arduino Boards Manager after refreshing the package index, or install from
the release archive attached to `v1.0.15`.
