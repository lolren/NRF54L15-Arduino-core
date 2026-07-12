# Development Notes

## Implemented Arduino Core APIs

- GPIO: `pinMode`, `digitalRead`, `digitalWrite`, `attachInterrupt`, `detachInterrupt`
- ADC/PWM: `analogRead`, `analogReadResolution(bits)`, `analogWrite`, `analogWriteResolution`
- UART: `Serial`, `Serial1`, `Serial2`
- I2C: `Wire` + `Wire1`, repeated-start, target/slave callbacks
- SPI: transactions + runtime frequency/mode/order
- Timing/power: `millis`, `micros`, delays, optional low-power idle profile
- Stream parser helpers: `setTimeout`, `find*`, `parseInt/parseFloat`, `readBytes*`, `readString*`
- Print/Printable compatibility: `Printable`, `print(const Printable&)`, `println(const Printable&)`
- Bluefruit/Seeed compatibility helpers: `digitalToggle`, `suspendLoop/resumeLoop`, `Print::printf`, `printBuffer`, `printBufferReverse`, `LED_STATE_ON`, `LED_RED/GREEN/BLUE`
- Persistent storage: `Preferences` key/value API (flash-backed)
- EEPROM compatibility: `EEPROM` (`begin/read/write/update/get/put/commit/end`)
- Legacy compatibility hooks: `cli()/sei()`, `makeWord(...)`, AVR-like port access helpers
- Network base compatibility headers: `IPAddress`, `Client`, `Server`, `Udp/UDP`

## Bundled HAL Library

Library path:

- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation`

Implemented blocks:

- `ClockControl`, `Gpio`, `Spim`, `Spis`, `Twim`, `Uarte`
- `Saadc`, `Timer`, `Pwm`, `Gpiote`
- `PowerManager`, `Grtc`, `GrtcPwm` (experimental fixed-pin wrapper), `TempSensor`, `Watchdog`, `Pdm`
- `Dppic`, `Egu`
- `Kmu`, `CracenIkg`, `Tampc`
- `BleRadio` (custom peripheral LL + minimal central/initiate + ATT/GATT subset)
- `ZigbeeRadio` (IEEE 802.15.4 PHY/MAC-lite data frame TX/RX helpers)
- `BoardControl` (battery sense + antenna route control)
- `CtrlApMailbox`, `VprControl`, `VprSharedTransportStream`

HAL structure note:

- the former 22k-line `nrf54l15_hal.cpp` has been split into ordered fragments
  under `src/nrf54l15_hal_parts`
- the top-level `nrf54l15_hal.cpp` is now a small translation-unit wrapper that
  includes those fragments in the required order
- the BLE peripheral event fragments intentionally remain adjacent because they
  still share one connection-event state machine
- future cleanup should reduce cross-fragment helper sharing before turning any
  fragment into a separately compiled unit

## BLE Status

Validated and stable with host adapter + hardware:

- Advertising
- Passive scanning
- Active scanning (`SCAN_REQ` / `SCAN_RSP`)
- Connect/disconnect
- Central initiate + basic ATT client request flow
- GATT discovery/read
- Battery notify CCCD flow

Current gap:

- central support is still intentionally minimal (fixed-handle client flows and basic ATT request queueing, not a full generic host stack)
- Fresh Just Works, Numeric Comparison accept/reject, mutual and both one-way
  LE SC OOB modes, RPA rotation, identity-key exchange, and privacy-aware
  bonded reconnect have a reproducible two-board gate. Remaining work includes
  CSRK/signing, legacy bond-key generation, multi-bond/controller privacy
  policy, and broad negative/host interoperability.
- `Bluefruit52Lib` still targets the common peripheral/runtime subset first, but the active-scan wrapper now emits real separate `SCAN_RSP` callback reports with the correct `scan_response` bit instead of collapsing them into the ADV path
- the experimental Channel Sounding surface is exactly the two-board
  `BleChannelSoundingInitiator` / `BleChannelSoundingReflector` pair. It uses
  the bundled Nordic SDC/MPSL controller to run LE CS Test and reassembles the
  controller's HCI CS result events. A separate CRC-protected Arduino transport
  establishes a per-cycle session token before the test and returns a
  metadata-correlated reflector step buffer after SDC releases RADIO. This
  requires the 128 MHz boot profile. It is
  not a normal connected ACL CS workflow, Bluetooth qualification, universal
  calibration, or cross-vendor result protocol. Synthetic VPR and retired raw
  RADIO fixtures remain under `extras/tests/channel_sounding` for regression
  coverage only.
- the KMU/CRACEN example is a non-provisioning IKG seed-state diagnostic: it derives keys only when trusted provisioning has already made the hardware seed valid, and never validates unknown seed RAM. The VPR side now has a generic shared-transport proof, a reusable host-side controller-service wrapper, validated non-CS VPR offload proofs for `FNV1a`, `CRC32`, `CRC32C`, an autonomous ticker service, queued async ticker/vendor events, and real VPR hibernate saved-context probes, plus a live capability probe showing `svc=1.7` / `opmask=0x3FF`; there are now dedicated local probes for hibernate resume, hibernate wake, and loaded-image restart, repeated loaded-image restart is hardware-validated on both attached boards through `VprRestartLifecycleProbe`, and `VprHibernateResumeProbe` now passes on both attached boards through a deterministic reset-after-hibernate service restart that preserves retained host-side service state while disabling raw VPR hardware context restore for the restart path; richer VPR-side service/runtime work is still open, and true raw VPR CPU-context resume is still an investigation topic rather than a finished public feature; the public `Tampc` wrapper now covers active-shield / glitch / domain-debug / AP-debug configuration with a live config probe, and the extra serial-fabric `22` / `30` paths now have a runtime probe
- Zigbee has a staged HA stack: IEEE 802.15.4 radio helpers, commissioning/security helpers, NWK/APS/ZCL subsets, descriptors, binding, and sketch-configurable ZDO management table responses. Full OTA, broad ZCL coverage, and automatic production route maintenance are still open.
- Thread is experimental, not production-claimed. The repo now has staged
  OpenThread core bring-up with fixed dataset, leader/child/router paths,
  PSKc/passphrase dataset helpers, UDP examples, and saved-dataset restore
  diagnostics plus a reboot recovery probe. Joiner/commissioner,
  reference-network attach, validated reboot recovery, and sleepy-device depth
  remain open.
- Matter is foundation-only. The repo has staged `connectedhomeip` support,
  onboarding-code helpers, an on/off-light model, and a Thread dataset export
  seam. It can build structured staged DNS-SD/SRP records, queue commissionable
  `_matterc._udp` records through OpenThread SRP in Matter-stage builds, expose
  Thread restore diagnostics through the on-network node, run a reboot recovery
  probe, derive a unique default discriminator from factory FICR, and track
  publish/unpublish lifecycle diagnostics including SRP unregister-pending and
  registered-state reporting. Local two-board staged SRP readiness is passing;
  infrastructure mDNS/DNS-SD visibility through an external OTBR,
  commissioning, commissioner/Home Assistant control, and Matter session
  recovery remain open.

## Current Validation And Planning Docs

- [`NRF54L15_FEATURE_MATRIX.md`](NRF54L15_FEATURE_MATRIX.md)
- [`POWER_PROFILE_MEASUREMENTS.md`](../POWER_PROFILE_MEASUREMENTS.md)
- [`BLE_REGRESSION_RUNBOOK.md`](BLE_REGRESSION_RUNBOOK.md)
- [Historical BLE/CS Completion Checklist](archive/BLE_CS_COMPLETION_CHECKLIST.md)
- [`CHANNEL_SOUNDING_CURRENT_STATUS.md`](CHANNEL_SOUNDING_CURRENT_STATUS.md)
- [`TWO_BOARD_RELEASE_GATE.md`](TWO_BOARD_RELEASE_GATE.md)
- [`THREAD_MATTER_IMPLEMENTATION_PLAN.md`](THREAD_MATTER_IMPLEMENTATION_PLAN.md)
- [`THREAD_MATTER_NEXT_AI_HANDOVER_2026_06_07.md`](THREAD_MATTER_NEXT_AI_HANDOVER_2026_06_07.md)
- [`THREAD_RUNTIME_OWNERSHIP.md`](THREAD_RUNTIME_OWNERSHIP.md)
- [`MATTER_RUNTIME_OWNERSHIP.md`](MATTER_RUNTIME_OWNERSHIP.md)
- [`MATTER_FOUNDATION_MANIFEST.md`](MATTER_FOUNDATION_MANIFEST.md)
- `scripts/ble_cli_matrix.sh`
- `scripts/ble_pair_bond_regression.sh`
- `scripts/run_two_board_release_gate.py`

## Local Development Workflow

Use one of:

- `~/Arduino/hardware/...` sketchbook override
- `~/.arduino15/packages/...` package-layout override

Important:

- a sketchbook override at `~/Arduino/hardware/nrf54l15clean` takes precedence over Boards Manager and can make the packaged core disappear from installs/search results until you move or remove it

Example compile:

```bash
arduino-cli compile --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15 \
  hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/InterruptPwmApiProbe/InterruptPwmApiProbe.ino
```

Example BLE matrix run:

```bash
bash scripts/ble_cli_matrix.sh --port /dev/ttyACM0 --sudo
```

Example pair/bond regression run:

```bash
bash scripts/ble_pair_bond_regression.sh --port /dev/ttyACM0 --sudo --attempts 10 --mode pair-bond
```
