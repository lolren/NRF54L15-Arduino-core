# nRF54 Arduino Core 1.0.7

`1.0.7` is a focused upload reliability release for
[issue #100](https://github.com/lolren/nrf54-arduino-core/issues/100).

## Windows upload fix

- Stop forwarding an empty Arduino `upload.uid` property as `-Uid ""` to
  `powershell.exe`. PowerShell treated the empty value as a missing parameter
  argument and rejected every normal upload before the wrapper could run.
- Keep explicit UID support inside `upload_windows.ps1`; the normal Arduino
  recipe now lets the wrapper select a unique target-matching probe or use the
  selected COM port when several matching boards are attached.
- Add a recipe contract that prevents the empty UID argument from returning.

## Native CMSIS-DAP teardown

- Bundle [nRF OCD 0.3.6](https://github.com/lolren/open-nrf-ocd/releases/tag/v0.3.6).
- After a successful target reset, make one short, best-effort CMSIS-DAP
  `DAP_Disconnect` (`0x03`) attempt. A live probe receives the port-off command;
  a stale or re-enumerated handle is closed without retries or a misleading
  error after programming has already completed.
- Bound SWD `NO_ACK` recovery to one line reset and one retry, and decode bulk
  and HID retry responses from their correct transport-specific offsets.
- Treat known nRF OCD transport-error output as a failure even when an older
  tool exits with status zero, allowing the upload wrapper to recover with
  pyOCD safe mode instead of falsely reporting success.
- Ship the exact 0.3.6 source tag and libusb source beside the native binary.

## Focused validation

- The nRF OCD unit suite covers graceful and post-reset close modes, malformed
  or stale teardown responses, HID/bulk response layouts, and bounded
  `WAIT`/`NO_ACK` recovery.
- Native cross-builds pass for Linux x86-64, Linux ARM64/ARMHF, and Windows
  x86-64/x86.
- A 133,980-byte image uploaded to XIAO nRF54LM20A and a 129,904-byte image
  uploaded to XIAO nRF54L15 with chip erase and reset enabled. Both CMSIS-DAP
  probes and serial ports remained enumerated with no command `0x03` error.
- The shipped WireScanner found the Sense IMU at `0x6A` with `errors=0` on both
  boards after upload.

## Install or upgrade

```bash
arduino-cli core update-index
arduino-cli core install "nrf54l15clean:nrf54l15clean@1.0.7"
```

[Full changes since v1.0.6](https://github.com/lolren/nrf54-arduino-core/compare/v1.0.6...v1.0.7)
