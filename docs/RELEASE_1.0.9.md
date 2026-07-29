# nRF54 Arduino Core 1.0.9

`1.0.9` adds Arduino IDE 1.x upload compatibility and tightens the native
upload path following [issue #102](https://github.com/lolren/nrf54-arduino-core/issues/102)
and [discussion #103](https://github.com/lolren/nrf54-arduino-core/discussions/103).

## Arduino IDE 1.x compatibility

- Define the upload and programmer `params.verbose` and `params.quiet`
  properties required by Arduino IDE 1.8.x.
- Keep those properties empty, so Arduino IDE 2.x and Arduino CLI expand the
  same upload commands in quiet and verbose modes.
- Cover the default uploader and Upload Using Programmer recipes.

## LM20A upload safeguards

- Bundle [nRF OCD 0.3.8](https://github.com/lolren/open-nrf-ocd/releases/tag/v0.3.8).
- Reject a selected nRF54L15 target when the onboard LM20A CMSIS-DAP probe is
  connected, and reject the crossed case, before sending any CMSIS-DAP command.
- Leave external CMSIS-DAP probes available because their USB identity cannot
  determine which target is attached.
- On Windows, stop after the successful resetting upload instead of opening a
  second probe session to write DHCSR. This preserves the command-free
  post-reset boundary introduced in 1.0.8 on every native upload path.

## SAMD11 BOOTPROT boundary

The onboard SAMD11 exposes CMSIS-DAP access to the downstream nRF54 target, not
to its own NVM user row. The native uploader therefore cannot set SAMD11
BOOTPROT or repair a SAMD11 that no longer enumerates. Version 1.0.9 does not
guess a fuse value: the discussion does not provide Seeed's factory firmware
layout, a supported protection size, or a redistributable recovery image.

## Validation

- nRF OCD unit tests cover both matching onboard probe/target pairs, both
  crossed pairs, external probes, and command-free post-reset teardown.
- Release binaries build for Linux x86-64, ARM64, and ARMHF and Windows x86-64
  and x86. Their published checksum manifest and corresponding source verify.
- Arduino CLI expands the default upload recipe in quiet and verbose modes,
  and the focused upload and release-version suites pass.
- No new hardware upload was performed for this release.

## Install or upgrade

```bash
arduino-cli core update-index
arduino-cli core install "nrf54l15clean:nrf54l15clean@1.0.9"
```

[Full changes since v1.0.8](https://github.com/lolren/nrf54-arduino-core/compare/v1.0.8...v1.0.9)
