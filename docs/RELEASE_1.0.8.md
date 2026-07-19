# nRF54 Arduino Core 1.0.8

`1.0.8` is a preventive CMSIS-DAP teardown release following the SAMD11 USB
enumeration report in
[issue #100](https://github.com/lolren/nrf54-arduino-core/issues/100).

## SAMD11-safe upload teardown

- Bundle [nRF OCD 0.3.7](https://github.com/lolren/open-nrf-ocd/releases/tag/v0.3.7).
- After a successful nRF54 target reset, close the saved USB transport without
  sending `DAP_Disconnect` or any other command to the onboard SAMD11 probe.
  The probe can re-enumerate at that boundary, so another command may target a
  stale or no-longer-responsive USB session.
- Keep normal `DAP_Disconnect` behavior for live sessions that did not reset
  the target, including failed resets, register access, and error paths.
- Preserve the bounded `WAIT`/`NO_ACK` recovery, Windows empty-UID fix,
  deterministic bundled-tool selection, and zero-exit transport-error handling
  from 1.0.7.

## Scope of the report

Windows `USB\\DEVICE_DESCRIPTOR_FAILURE` occurs while enumerating the
ATSAMD11D14A USB bridge, before the host can run the Arduino uploader. The
nRF54 application continuing to run through J-Link confirms that this is a
probe-side failure rather than an nRF54 application-flash failure. Version
1.0.8 removes the only new 1.0.7 post-reset probe command as a precaution; it
cannot repair SAMD11 firmware that no longer enumerates.

## Validation

- The nRF OCD unit suite proves both bulk and HID post-reset teardown perform
  zero USB reads, writes, or flushes while still releasing local state and the
  saved host handle.
- Ordinary bulk/HID disconnect and bounded transfer-recovery tests pass.
- Release binaries build for Linux x86-64, ARM64, and ARMHF and Windows
  x86-64 and x86. No boards were connected for a new hardware upload test.
- The exact nRF OCD 0.3.7 source and libusb 1.0.27 source accompany the core.

## Install or upgrade

```bash
arduino-cli core update-index
arduino-cli core install "nrf54l15clean:nrf54l15clean@1.0.8"
```

[Full changes since v1.0.7](https://github.com/lolren/nrf54-arduino-core/compare/v1.0.7...v1.0.8)
