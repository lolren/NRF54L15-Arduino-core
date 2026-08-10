# nRF54 Arduino Core 1.0.14

`1.0.14` completes the timed-hibernate fix reported in
[issue #107](https://github.com/lolren/nrf54-arduino-core/issues/107).

## nPM1300 timed cold boot

- Keep true nPM1300 timed Hibernate when the XIAO nRF54LM20A is powered from
  VBAT with VBUS absent.
- When USB/VBUS is present, use the nRF54 GRTC timed System OFF wake-reset
  path. Nordic requires VBUS to be disconnected before the PMIC can enter
  Hibernate, so the SoC fallback provides the requested USB-powered behavior.
- Preserve one `1` through `268435440` ms API range on both supply paths by
  retaining the full millisecond delay in the GRTC calculation.
- Accept the LM20A RADIO EasyDMA cleared-pointer readback during the guarded
  System OFF shutdown sequence, while retaining the existing idle, disabled,
  and zero-length DMA checks.
- Both successful paths cold-boot and restart the sketch from `setup()`.

## Validation

- Core I/O regression suite passes with explicit battery/USB path contracts.
- `nPM1300_TimedHibernate` compiles for XIAO nRF54LM20A.
- An attached USB-powered XIAO nRF54LM20A completed three consecutive public
  API cycles: each entered timed System OFF and cold-booted from the GRTC
  compare event with no shutdown abort.

## Install or upgrade

Use Arduino Boards Manager after refreshing the package index, or install from
the release archive attached to `v1.0.14`.
