# nRF54 Arduino Core 1.0.5

`1.0.5` restores the XIAO nRF54LM20A header SPI route used by the last
known-good SD-card releases and makes the shipped Wire scanner exercise the
actual onboard sensor buses. It addresses the remaining reports in
[issue #100](https://github.com/lolren/nrf54-arduino-core/issues/100) and
[issue #101](https://github.com/lolren/nrf54-arduino-core/issues/101).

## XIAO nRF54LM20A SPI and SD

- Restore the public `SPI` object on header pins D8/D9/D10 to SPIM21, matching
  core `0.9.215` and earlier. The route was inadvertently moved to SPIM23 in
  `0.9.216`.
- Keep SPIM23 available to the onboard nPM1300 PMIC driver, which uses the same
  serial peripheral window in TWIM mode. Public SD-card traffic no longer
  aliases that PMIC controller.
- Add a release contract that requires the LM20A header SPI object to remain on
  SPIM21 and rejects a future SPIM23 regression.

The Arduino SD `1.3.0` timestamp-and-flush sketch compiles against this release.
No SD card was attached to the local test boards, so physical-card throughput
still requires confirmation by the issue reporter on the affected hardware.

## Wire and onboard sensors

- Scan both `Wire` and `Wire1` in the shipped `WireScanner` example.
- Enable the XIAO Sense IMU/microphone rail before scanning the onboard bus.
- On XIAO nRF54LM20A, enable PMIC LDO1 and drive the IMU chip-select high so the
  LSM6DS3TR-C enters I2C mode before `Wire1` is scanned.
- Check that SDA and SCL are idle high before scanning. An unpowered bus or a
  bus without pull-ups is now reported immediately instead of waiting through
  126 hardware timeouts.
- Add source contracts for dual-bus scanning, board rail enablement, LM20A I2C
  selection, and the missing-pull-up diagnostic.

The final scanner firmware was flashed through both connected CMSIS-DAP probes.
XIAO nRF54L15 Sense and XIAO nRF54LM20A each repeatedly found the onboard IMU at
`0x6A` on `Wire1` with zero transaction errors. Their unwired external `Wire`
buses were correctly identified as not idle-high.

## Validation

- Warning-enabled scanner builds for both XIAO targets.
- Warning-enabled LM20A SPI and Arduino SD builds.
- Two-board Wire1 hardware validation against the onboard IMUs.
- Wire address-probe, core I/O, versioning, and exact-release archive gates.
- Full source and example compile matrix across all supported board profiles.

## Install or upgrade

```bash
arduino-cli core update-index
arduino-cli core install "nrf54l15clean:nrf54l15clean@1.0.5"
```

[Full changes since v1.0.4](https://github.com/lolren/nrf54-arduino-core/compare/v1.0.4...v1.0.5)
