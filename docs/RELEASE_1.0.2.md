# nRF54 Arduino Core 1.0.2

`1.0.2` is a Bluetooth HID interoperability and link-layer correctness patch
release. It incorporates the fixes validated after `1.0.1` without weakening
the core's general GATT Robust Caching implementation.

## Bluetooth fixes

- Handle zero-length LLID1 data PDUs correctly while link encryption is active.
  These PDUs carry no MIC, remain unencrypted, and do not consume a CCM packet
  counter, but still require an acknowledgement inside the Bluetooth T_IFS.
- Keep the fast empty-PDU response on the normal 100 us radio trigger point.
- Preserve encrypted transmit history and sequence state when an empty PDU is
  acknowledged or retransmitted.
- Defer the direct Client Supported Features (`0x2B29`) lookup until encryption
  for HID peripherals. This restores compatibility with older Sony Android
  hosts while keeping Robust Caching available after security and unchanged for
  non-HID GATT applications.
- Keep HID compatibility configuration atomic with service registration.

## HID interoperability

- Retain the compact Zephyr-compatible mouse metadata profile used by the
  shipped `blehid_mouse` example.
- Restore reliable pairing, HID discovery, CCCD enablement, bonded reconnects,
  and mouse notifications across the tested Sony, Pixel, Fairphone, Huawei,
  iPhone, and Android tablet hosts.
- Remove the temporary high-volume controller diagnostics used to isolate the
  regression; release builds contain no additional serial logging or trace
  buffers.

## Other updates since 1.0.1

- Expand the README protocol matrix so stable, experimental, and incomplete
  BLE, Channel Sounding, Thread, Matter, and Zigbee surfaces are explicit.
- Harden the staged Thread/Matter platform and two-board validation gates.

## Validation

The shipped `blehid_mouse` example compiles with BLE tracing disabled and
enabled. The separate XIAO Sense Voice/IMU peripheral also compiles against this
release. Repository contracts cover connection timing, encryption, SMP,
multibond storage, privacy, Robust Caching, GATT authorization, client
discovery, indications, and directed advertising.

Hardware validation used the connected nRF54 board and all available phone and
tablet hosts. The final candidate paired, discovered the HID report, enabled
notifications, delivered cursor movement, and reconnected after bonding.

## Install or upgrade

```bash
arduino-cli core update-index
arduino-cli core install "nrf54l15clean:nrf54l15clean@1.0.2"
```

[Full changes since v1.0.1](https://github.com/lolren/nrf54-arduino-core/compare/v1.0.1...v1.0.2)
