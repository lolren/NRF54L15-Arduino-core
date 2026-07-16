# nRF54 Arduino Core 1.0.3

`1.0.3` is a build-isolation hotfix for projects that switch between XIAO
nRF54L15 and XIAO nRF54LM20A targets. Bluetooth behavior is unchanged from
`1.0.2`.

## Fixed

- Prevent Arduino IDE build directories from linking objects compiled for two
  different nRF54 SoCs. Such a mixed build could place the LM20A CRACEN base
  (`0x50059000`) in an L15 Secure Connections path and hard-fault before BLE
  advertising began.
- Track the selected SoC core and platform installation in each build directory.
  A board change, core upgrade, or pre-1.0.3 cache now removes stale core,
  library, sketch-object, archive, and firmware outputs before compilation.
- Attach a link-once SoC identity to every C, C++, and assembly object. The L15
  and LM20A linker scripts provide only their matching identity, so a concurrent
  or otherwise mixed build fails at link time instead of producing flashable
  firmware.
- Extend exact-archive release verification to require the matching identity in
  every compiled ELF and reject the other SoC identity.

## Bluetooth scope

The HID compatibility and encrypted empty-PDU fixes from `1.0.2` are retained
without modification. A clean `1.0.2` build and the guarded `1.0.3` candidate
both advertise as `XIAO nRF54L15`; the guarded candidate was verified on the
connected L15 hardware and detected by a Linux BLE scan.

Normal target guards add 32 bytes to the tested HID firmware, no RAM, and no
runtime code.

## Install or upgrade

```bash
arduino-cli core update-index
arduino-cli core install "nrf54l15clean:nrf54l15clean@1.0.3"
```

[Full changes since v1.0.2](https://github.com/lolren/nrf54-arduino-core/compare/v1.0.2...v1.0.3)
