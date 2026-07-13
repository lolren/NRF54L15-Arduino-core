# nRF54 Arduino Core 1.0.1

`1.0.1` is a Bluetooth interoperability and controller-stability patch release.
It restores reliable HID pairing and bonded reconnect behavior across a broader
set of Android and iOS hosts while retaining the security, privacy, robust
caching, and low-power functionality introduced in `1.0.0`.

## Bluetooth fixes

- Restored the Bluetooth-specified encryption start sequence: a peripheral
  initiates `LL_START_ENC_REQ`, while a central waits for that request and sends
  the encrypted response.
- Restored bidirectional Secure Connections identity and signing-key
  distribution for durable phone bonds.
- Added graceful local `LL_TERMINATE_IND` handling for both central and
  peripheral roles.
- Hardened first-anchor timing, supervision-timeout accounting, connection
  update instant catch-up, LE Coded acknowledgements, and encrypted retries.
- Preserved high-priority SMP traffic when the controller transmit queue is
  under pressure.
- Improved stale-bond recovery and background central/peripheral handoff.

## HID interoperability

- The mouse example now exposes the full HOGP Report and Boot Mouse profile,
  advertises the HID Mouse appearance, and uses Numeric Comparison pairing.
- Boot Mouse input reports and characteristics now use the required three-byte
  report shape.
- HID Information reports HOGP version 1.11, and protected HID descriptors and
  notifications consistently require encryption.
- The diagnostic HID pairing probe now defaults to the production full profile
  and emits visible, timestamped mouse movement for phone testing.

## Validation

The release archive was reproduced from the tagged source and verified against
all three Board Manager indexes. Extracted-archive builds cover Numeric
Comparison, OOB pairing, privacy/RPA, scanning, Channel Sounding initiator and
reflector targets, staged Thread and Matter targets, both XIAO microphone
examples, and the supported board families.

Hardware validation used two connected boards for central/peripheral pairing
and bonding. The shipped `blehid_mouse` sketch also completed encrypted HOGP
discovery, CCCD enablement, and mouse notification delivery with multiple phone
sessions.

## Install or upgrade

```bash
arduino-cli core update-index
arduino-cli core install "nrf54l15clean:nrf54l15clean@1.0.1"
```

[Full changes since v1.0.0](https://github.com/lolren/nrf54-arduino-core/compare/v1.0.0...v1.0.1)
