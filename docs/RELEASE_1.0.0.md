# nRF54 Arduino Core 1.0.0

`1.0.0` is the first stable release of the nRF54 Arduino Core. It provides a
bare-metal Arduino workflow for Nordic nRF54L devices without a Zephyr runtime
or a separate nRF Connect SDK installation.

The stable scope covers the Arduino core, peripherals, power management, VPR
coprocessor access, and the documented single-link Bluetooth LE implementation.

## Bluetooth LE

This release substantially expands and hardens the BLE stack:

- Peripheral, central, and dual-role operation with legacy advertising,
  scanning, GATT server/client, notifications, indications, MTU up to 247, DLE
  up to 251, and 1M, 2M, and Coded PHY paths.
- LE Secure Connections with Just Works, passkey/fixed-PIN flows, asynchronous
  Numeric Comparison, and mutual or one-way OOB pairing.
- Stable local identity, IRK distribution, rotating resolvable private
  addresses, hardware AAR resolution, and privacy-aware bonded reconnects.
- CSRK exchange and authenticated ATT Signed Write Commands with AES-CMAC
  verification, durable monotonic counters, and replay rejection.
- Negotiated 7-16 byte encryption key sizes, the 30-second SMP transaction
  timeout, repeated-attempt throttling, and fail-closed CRACEN entropy handling.
- Persistent bond, CCCD, identity, and signing state in fixed, non-overlapping
  RRAM regions.

## GATT And Bluefruit Improvements

- Custom characteristic fixed and maximum lengths are enforced for local
  values, peer writes, and queued prepare/execute writes.
- Service and characteristic security permissions are propagated to reads,
  writes, notifications, and indications.
- Bluefruit central reads continue through ATT Read Blob responses, while
  discovery assigns complete characteristic handle ranges before descriptor
  discovery.
- Fresh-only ATT dispatch suppresses retransmitted responses and callbacks.
  Service, characteristic, descriptor, and Read Blob pagination now rejects
  malformed entry widths, out-of-range handles, and non-advancing peer data.
- `BLEUart::bufferTXD(true)` coalesces writes up to the current ATT payload and
  preserves unsent data when notification backpressure occurs.
- Battery Service database updates and explicit notifications have distinct
  behavior.
- HID discovery uses Report Reference descriptors for generic reports, with
  consistent encrypted-link requirements for protected subscriptions.
- ANCS sends complete Control Point commands, reassembles fragmented Data
  Source responses, and implements APIs for notification attributes, app
  display names, action labels, and positive or negative actions. Fragmented
  response parsing has host-side regression coverage; live iOS interoperability
  is outside the automated gate.
- Connection, LL control, L2CAP parameter, SMP, and ATT inputs receive stricter
  validation before live state changes.
- Central connection access addresses and CRC seeds are generated from
  fail-closed hardware entropy before radio ownership changes, with the
  additional bit-transition constraints required for LE Coded PHY.

## Supported Board Targets

- Seeed Studio XIAO nRF54L15 / Sense
- Seeed Studio XIAO nRF54LM20A / Sense
- HOLYIOT-25007
- HOLYIOT-25008
- Generic nRF54L15 36-pad module
- Nordic PCA10156 nRF54L15 DK

The stable two-board hardware gate uses a XIAO nRF54L15 and a XIAO nRF54LM20A.
The all-profile source matrix compiles the shipped examples for every compatible
advertised target, but the other boards still require validation on their
particular carrier hardware.

## Core And Power Features

The stable core includes GPIO, ADC, PWM, UART, I2C, SPI, high-speed SPI, I2S,
PDM, NFC-A, QDEC, watchdog, comparators, GRTC, DPPI, System OFF, reset-cause
APIs, nPM1300 support, LM20A QSPI flash handling, and direct VPR RISC-V
coprocessor access.

- XIAO nRF54LM20A Sense PDM now uses the LM20-specific EDGE and decimation-ratio
  encodings, a 1.28 MHz clock, and 16 kHz PCM. Its example keeps PDM clocking
  through a 512 ms DMA capture, discards a measured 32 ms startup interval, and
  analyzes the remaining 480 ms. The byte-addressed DMA path re-arms LM20
  clock/filter state after STOP, waits for the documented DMA ownership fence,
  and invalidates CPU cache lines before samples are read. A simultaneous
  70-second soak completed 115/115 captures on each Sense board with no timeout,
  underfill, guard, or DMA error (L15: 503-505 ms; LM20A: 504-505 ms).
- Timed System OFF verifies that reset causes are cleared before entry and
  records an abort-stage diagnostic. The hardware gate accepts only two
  consecutive GRTC wake-reset cycles after its deliberate reset boundary.
- Arduino IDE examples are organized into separate XIAO base/Sense, LM20A
  base/Sense, HOLYIOT-25008, and Nordic DK menus. Adafruit SPIFlash sketches are
  exposed under LM20A, and byte-identical duplicate platform examples were
  removed.

Community PPK2 measurements provide useful board-level reference points:

- XIAO nRF54L15 in the community side-by-side trace: about **4 uA** during
  returning System ON sleep and **2.5 uA** in no-retention System OFF. The post
  does not identify its exact core version.
- XIAO nRF54LM20A with `v0.9.222`: **8.7 uA** during returning System ON sleep
  and **3.1 uA** in no-retention System OFF.
- XIAO nRF54LM20A with the earlier `v0.9.59` hibernate path: about **0.5 uA**.

These are community snapshots from earlier core versions, not guaranteed
`1.0.0` limits. Power source, debug wiring, board revision, sketch, radio
settings, and attached peripherals materially affect the result. The
[README power section](https://github.com/lolren/nrf54-arduino-core#power-consumption)
records the complete measurement context and links to the source discussions.

## Release Validation

The stable release process verifies version metadata, Board Manager indexes,
archive checksums, regression contracts, and example compilation from the exact
packaged archive. The expanded source matrix also compiles every shipped sketch
for every compatible advertised board profile.

The full two-board hardware gate covers:

- Boot and programming on both XIAO board families
- PHY, MTU, DLE, discovery, CCCD, and long notifications
- Pairing, bond persistence, and encrypted reconnect
- Numeric Comparison acceptance and rejection
- Mutual and both one-way OOB directions
- RPA rotation, identity distribution, and AAR bond resolution
- Signed writes, replay rejection, and counters reloaded from RRAM
- Reset recovery and timed System OFF wake
- Experimental controller-backed Channel Sounding checks

This evidence does not replace Bluetooth PTS/BQB qualification or broad phone,
desktop, RF, power, and long-duration interoperability testing.

## Experimental And Unfinished Protocols

The following areas are included for evaluation but are not production-ready:

- **Zigbee:** partial stack and device demonstrations; Zigbee PRO coverage,
  routing, clusters, OTA, and ecosystem interoperability remain incomplete.
- **Thread:** staged OpenThread FTD, MeshCoP, SRP, and UDP paths; production
  commissioning, sleepy-device coverage, interoperability, and soak testing
  remain.
- **Matter:** protocol and crypto bring-up plus on-network examples; this is not
  a complete or certifiable Matter device implementation.
- **Channel Sounding:** experimental two-board controller-backed LE CS Test
  through `BleChannelSoundingInitiator` and `BleChannelSoundingReflector`. It
  requires the 128 MHz profile and is not connected-ACL Channel Sounding,
  calibrated ranging, or a qualification claim.

## BLE Scope Limits

The stable BLE claim is intentionally bounded. The core currently retains one
bond and does not implement directed advertising, automatic Service Changed
database-epoch management, Nordic secure DFU, a controller-enforced multi-peer
allow-list policy, or complete locally generated legacy LTK/EDIV/Rand
distribution.

`1.0.0` is not a Bluetooth SIG qualification or a claim of complete Bluetooth
Core or Bluefruit API parity.

## Licensing

Project-owned contributions are provided under the MIT License. Imported code
and binary components retain their own terms and notices. Both the platform
archive and the separately installed host-tools package carry self-contained
license metadata. The native Linux uploader also includes the corresponding
nRF OCD and libusb source plus offline relink instructions for its statically
linked libusb build. See `THIRD_PARTY_NOTICES.md` in the installed platform for
the component paths and exact terms.

## Install

Add this Boards Manager URL:

```text
https://raw.githubusercontent.com/lolren/nrf54-arduino-core/main/package_nrf54l15clean_index.json
```

Or install the exact version with Arduino CLI:

```bash
arduino-cli core install "nrf54l15clean:nrf54l15clean@1.0.0"
```

[Full changes since v0.9.223](https://github.com/lolren/nrf54-arduino-core/compare/v0.9.223...v1.0.0)
