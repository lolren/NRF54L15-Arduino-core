# nRF54 Arduino Core 1.0.4

`1.0.4` fixes Wire/I2C address scanning on XIAO nRF54L15 and XIAO
nRF54LM20A. It is a focused patch release for
[issue #100](https://github.com/lolren/nrf54-arduino-core/issues/100).

## Fixed

- Make zero-payload `Wire.endTransmission()` perform a real I2C address phase.
  The nRF54L TWIM `DMA.TX.MAXCNT` register only defines nonzero transfer
  lengths; starting a zero-byte EasyDMA write could complete locally without
  putting the address on the bus, causing scanners to report every address.
- Probe with a one-byte read rather than writing a dummy byte. This preserves
  the Arduino scanner API without sending data into an unknown target.
- Use the hardware `LASTRX -> STOP` shortcut so the one-byte probe terminates
  at the required byte boundary at 100 kHz, 400 kHz, and 1 MHz.
- Preserve DMA storage for the full transaction and keep `Wire` and `Wire1`
  probe storage independent.
- Return Arduino-compatible status codes: `0` for an acknowledged address, `2`
  for an address NACK, `3` for a data NACK, and `4` for timeout or other bus
  failures.
- Sample ordinary write errors after `STOPPED`, not only at `LASTTX`. On nRF54L,
  `LASTTX` occurs when transmission of the final byte starts, before its ACK or
  NACK is known. The final-byte data-NACK status is now retained.
- Use the hardware `LASTTX -> STOP` shortcut for stop-terminated writes while
  leaving repeated-start behavior unchanged.

## Example and regression coverage

- Add `File > Examples > Wire > WireScanner`, with separate device and bus-error
  totals so missing pull-ups or a stuck bus are visible.
- Add source/model contracts for both SoC core copies covering address NACK,
  data NACK, event-only errors, STOP timeout, non-writing address probes,
  hardware STOP shortcuts, DMA lifetime, and post-STOP error sampling.
- Add the scanner to the exact-release archive compile matrix for both XIAO
  nRF54L15 and XIAO nRF54LM20A.

The final scanner firmware was flashed through the two connected CMSIS-DAP
probes. Both boards repeatedly reported zero detected devices on the unwired
external buses instead of the former all-address false positive. All shipped
Wire examples compile warning-free for both targets.

## Install or upgrade

```bash
arduino-cli core update-index
arduino-cli core install "nrf54l15clean:nrf54l15clean@1.0.4"
```

[Full changes since v1.0.3](https://github.com/lolren/nrf54-arduino-core/compare/v1.0.3...v1.0.4)
