# nRF54 Arduino Core 1.0.6

`1.0.6` fixes native Windows uploads when a board's USB serial port is absent,
stale, or temporarily unavailable. It is a focused follow-up to
[issue #100](https://github.com/lolren/nrf54-arduino-core/issues/100).

## Windows upload recovery

- Discover connected CMSIS-DAP probes with the bundled `nrf_ocd list` command.
- When exactly one probe matches the selected board target, upload with its
  stable CMSIS-DAP UID instead of depending on the `COMx` CDC port.
- Honor an explicitly configured upload UID before automatic discovery.
- Use the selected Arduino port as a compatibility fallback when several
  probes of the same target type are connected.
- Refuse to guess when several matching probes are present and neither a UID
  nor a usable port can identify the intended board.
- Reuse the same probe selector for programming and the final best-effort
  debugger detach.

This addresses the reported `Cannot determine probe serial from port COM4`
failure, which occurred during command-line option handling before `nrf_ocd`
could access, erase, or program the target.

## Wire1 retest boundary

The `1.0.6` Wire implementation and board-aware `WireScanner` are unchanged
from `1.0.5`. The reported text `Unknown error at address` comes from Arduino's
generic scanner rather than this core's shipped example. On XIAO Sense boards,
the generic scanner does not enable the IMU/microphone rail before scanning
`Wire1`.

For a comparable retest, use the unchanged **File > Examples > Wire >
WireScanner** example. It enables the Sense rail, selects I2C mode on LM20A,
scans both buses, and reports the raw device/error totals.

## Build reliability

- Avoid a GCC 13 internal compiler error when resetting the experimental
  Zigbee Home Automation configuration aggregate.
- Remove a warning-as-error from the 15-second sleepy climate sensor example
  when its boot-report delay is configured as zero.

## Focused validation

- Native Windows uploader tests cover stale COM recovery, unique target UID
  selection, multiple-probe port fallback, explicit UID selection, retry,
  detach, and fail-closed ambiguity.
- The upload helper and Wire address-probe source contracts pass.
- The Zigbee feature gate links with GCC 13.
- Warning-enabled `WireScanner` builds pass for XIAO nRF54L15 and XIAO
  nRF54LM20A; the affected Zigbee example also builds warning-free.

## Install or upgrade

```bash
arduino-cli core update-index
arduino-cli core install "nrf54l15clean:nrf54l15clean@1.0.6"
```

[Full changes since v1.0.5](https://github.com/lolren/nrf54-arduino-core/compare/v1.0.5...v1.0.6)
