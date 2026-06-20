# Channel Sounding Zephyr-Parity Status and Continuation Plan

## Purpose

This document records what the clean Arduino core currently implements for
Bluetooth Channel Sounding (CS), what was verified against Zephyr's public HCI
definitions, and what remains before the core can reasonably claim full
Zephyr-equivalent Channel Sounding.

The distinction below is important:

- **HCI/VPR command parity** means the host builds the same command layouts and
  accepts the same event layouts as Zephyr.
- **Controller workflow parity** means commands, state transitions, result
  fragmentation, and errors behave like a Bluetooth controller.
- **Physical RF parity** means two real boards execute standards-compliant CS
  procedures over the air with comparable timing and measurement output.

Only the first item is substantially complete. The current VPR connected-CS
result path is still a deterministic regression/demo implementation, not a
production physical-ranging controller.

## Verified Baseline

The primary local Zephyr reference used for this work is:

```text
/home/lolren/Desktop/test_pi_nrf54/ncs-workspace/zephyr
```

Relevant Zephyr sources include:

```text
include/zephyr/bluetooth/hci_types.h
include/zephyr/bluetooth/cs.h
subsys/bluetooth/host/cs.c
```

The following HCI opcodes now match Zephyr/Bluetooth definitions:

| Operation | Opcode |
|---|---:|
| LE Read Remote Supported Capabilities | `0x208A` |
| LE Write Cached Remote Supported Capabilities v1 | `0x208B` |
| LE CS Security Enable | `0x208C` |
| LE Set Default Settings | `0x208D` |
| LE Read Remote FAE Table | `0x208E` |
| LE Write Cached Remote FAE Table | `0x208F` |
| LE Create Config | `0x2090` |
| LE Remove Config | `0x2091` |
| LE Set Channel Classification | `0x2092` |
| LE Set Procedure Parameters | `0x2093` |
| LE Procedure Enable | `0x2094` |
| LE CS Test | `0x2095` |
| LE CS Test End | `0x2096` |
| LE Write Cached Remote Supported Capabilities v2 | `0x20A6` |

The following LE Meta subevents are also represented:

| Event | Subevent |
|---|---:|
| Read Remote Supported Capabilities Complete | `0x2C` |
| Read Remote FAE Table Complete | `0x2D` |
| CS Security Enable Complete | `0x2E` |
| CS Config Complete | `0x2F` |
| CS Procedure Enable Complete | `0x30` |
| CS Subevent Result | `0x31` |
| CS Subevent Result Continue | `0x32` |
| CS Test End Complete | `0x33` |
| Read Remote Supported Capabilities Complete v2 | `0x38` |

## Completed in This Pass

- Corrected cached-capability, FAE, channel-classification, CS Test, and CS Test
  End command opcodes.
- Implemented the exact 30-byte cached-capability v1 payload and 33-byte v2
  payload.
- Implemented the 72-value FAE table format and 75-byte FAE completion payload.
- Reworked CS Test command packing to match Zephyr's base command and ordered
  override data.
- Added validation for the supported CS Test override mask.
- Increased `BleCsHciCommand` payload capacity to 128 bytes for valid CS Test
  combinations.
- Added VPR responses for cached capabilities, FAE, channel classification,
  CS Test, and CS Test End.
- Added application-side direct APIs for every command above.
- Kept auxiliary FAE and CS Test End events separate from the connected
  procedure workflow parser.
- Added public access to the next raw H4 event from
  `VprControllerServiceHost`, allowing command-following asynchronous events to
  be drained without routing them through the wrong state machine.
- Expanded the dedicated VPR helper image window by 1 KiB:
  `0x2003C900-0x2003FE00`. The existing 96 KiB VPR/FLPR reservation and the
  saved-context boundary remain unchanged, so this does not reduce CPUAPP RAM.
- Added:
  `File > Examples > Nrf54L15 Clean Implementation > BLE > ChannelSounding >
  BleChannelSoundingHciParity`
- Added:
  `File > Examples > Nrf54L15 Clean Implementation > BLE > ChannelSounding >
  BleChannelSoundingVprHciParity`

## Hardware Verification

`BleChannelSoundingVprHciParity` was compiled, uploaded, and run on a XIAO
nRF54L15 with probe UID `E91217E8`.

Observed output:

```text
BleChannelSoundingVprHciParity
cs_vpr_hci_parity=PASS pumps=12 status=0/0/0/0/0/0/0 fae_valid=1 fae_handle=0x41 test_end=0
```

The following examples also compile with the local source core:

```text
BleChannelSoundingHciParity
BleChannelSoundingVprHciParity
BleChannelSoundingVprLinkedInitiator
BleChannelSoundingInitiator
BleChannelSoundingReflector
```

This proves command construction, VPR transport, asynchronous event draining,
and response parsing. It does not prove over-the-air physical ranging.

## Current Limitations

### 1. CS Test Results Are Not Yet Generated

The VPR image accepts `LE CS Test`, tracks test-active state, accepts
`LE CS Test End`, and emits `LE CS Test End Complete`.

It does not yet emit the standalone CS Test `0x0FFF` connection-handle
subevent-result stream expected from a real controller. This is the next
contained implementation target.

Required work:

- Generate initial and continuation result events while test mode is active.
- Use connection handle `0x0FFF`.
- Follow the requested mode, role, channel selection, timing, antenna, payload,
  and override fields.
- Stop result generation deterministically on CS Test End.
- Add malformed-command, already-active, not-active, and unsupported-override
  tests.

### 2. Cached Capability and FAE Semantics Are Minimal

The VPR implementation validates the standardized commands and returns valid
HCI responses. The remote FAE read currently returns a synthetic zero table.

Required work:

- Store cached capability v1/v2 data per connection.
- Store cached remote FAE data per connection.
- Separate local calibration data from remote cached data.
- Invalidate cached data on disconnect, controller reset, or capability change
  using the same lifecycle rules as Zephyr.
- Populate remote FAE from the real link-layer exchange.

### 3. Connected CS Results Are Synthetic

The current VPR connected workflow produces deterministic controller-shaped
subevent data for parser, reassembly, state-machine, and regression testing.
It is not generated from real CS RF measurements.

Required work:

- Implement the link-layer CS control-procedure exchange.
- Negotiate capabilities, configuration, security, and procedure parameters
  with the peer rather than accepting only local host state.
- Schedule CS events relative to actual ACL connection events.
- Support initiator and reflector roles on real connected links.
- Emit correct abort reasons and partial-result states.

### 4. Real-Time Radio Execution Is Incomplete

Zephyr/Nordic controller behavior relies on precise radio scheduling. Polling
or CPU busy-waiting is not an acceptable final replacement.

Required work:

- Drive prewarm, TX, RX, switching, captures, and teardown from hardware events
  and timers.
- Use VPR/FLPR for the latency-sensitive event path.
- Configure RADIO CS/RTT/phase-measurement registers from the requested
  procedure.
- Capture hardware timestamps, frequency compensation, packet quality, RSSI,
  RTT, phase, antenna path, and tone quality.
- Keep CPUAPP asleep except for command submission and completed-result
  consumption.
- Verify that RF switch control is board-specific and active only for XIAO
  variants that physically contain the switch.

### 5. CS Security and DRBG Need Production Semantics

The host workflow includes CS Security Enable, but a complete controller must
derive and use the required CS security material for the real procedure.

Required work:

- Compare nonce, DRBG, and access-address generation with Zephyr controller
  behavior.
- Use the appropriate hardware entropy/CRACEN path where available.
- Implement reset, reconnect, replay, and procedure-counter lifecycle rules.
- Verify deterministic test vectors separately from live random operation.

### 6. Measurement and Calibration Need Real Data

Required work:

- Replace nominal synthetic distance with real RTT/PBR-derived estimates.
- Validate RTT-only, PBR-only, and combined modes.
- Implement per-board antenna-delay and RF-path calibration.
- Implement FAE correction and quality rejection.
- Report confidence/quality rather than presenting every estimate as valid.
- Characterize at multiple known distances; the earlier approximate
  `0.7-1.0 m` setup must not be treated as a precise calibration reference.

### 7. Error and Concurrency Coverage Is Incomplete

Required work:

- Disconnect during capability exchange, configuration, security, and active
  procedure.
- Procedure disable and re-enable.
- Config removal while selected, active, or retained.
- Multiple stored configurations and eviction.
- Multiple connections, or explicit rejection if the implementation remains
  single-link.
- HCI queue saturation and fragmented/concatenated event streams.
- Controller reset while VPR is active.
- Invalid channel maps, timing combinations, roles, PHYs, antenna selections,
  and override lengths.

## Recommended Implementation Order

1. **Standalone CS Test result stream**
   - Emit `0x31/0x32` result events using handle `0x0FFF`.
   - Add a host-side test-result collector independent of the connected
     workflow state.
   - Validate against Zephyr command/event byte captures.

2. **Per-connection cached state**
   - Store capabilities and FAE tables.
   - Add lifecycle and invalidation tests.

3. **Real link-layer control exchange**
   - Replace local acceptance with peer negotiation.
   - Implement disconnect and timeout behavior first.

4. **Hardware event scheduler**
   - Port the timing model from Zephyr/Nordic open code where licensing permits.
   - Use hardware timers/PPI-style routing and VPR execution.

5. **Physical result capture**
   - Populate controller subevent results from hardware measurements.
   - Preserve the existing reassembler and host API.

6. **Two-board interoperability**
   - Arduino initiator to Arduino reflector.
   - Arduino initiator to Zephyr reflector.
   - Zephyr initiator to Arduino reflector.

7. **Power and soak validation**
   - Connected idle, active procedures, disable, disconnect, and reconnect.
   - Long result streams and all payload/continuation sizes.

## Required Test Matrix

### Command/API Tests

- Every opcode returns the expected Command Status or Command Complete event.
- Every asynchronous completion has the exact subevent code and length.
- v1 and v2 capabilities remain distinct.
- All 72 signed FAE values round-trip.
- All supported CS Test override combinations pack correctly.
- Unsupported override bits fail with Invalid HCI Command Parameters.

### Two-Board Tests

- Initiator and reflector establish a normal BLE connection first.
- Capability exchange succeeds in both directions.
- Security enable succeeds.
- Config create/remove and procedure enable/disable succeed repeatedly.
- Initial and continuation result events reassemble for minimum and maximum
  payload sizes.
- Disconnect during every phase returns to a clean advertising/scanning state.

### Zephyr Comparison

- Capture HCI command and event bytes on both implementations.
- Compare event ordering and status values, not only final success.
- Compare connection-event-relative timing with a logic analyzer or PPK2.
- Compare procedure current, idle current, and teardown current.
- Compare RTT/PBR output using the same boards, channels, PHY, antenna path, and
  known physical distance.

## Regenerating the VPR Images

From the repository root:

```bash
python3 hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/tools/generate_vpr_cs_transport_stub.py
python3 hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/tools/generate_vpr_cs_controller_stub.py
```

Both generated headers must fit the configured
`0x2003C900-0x2003FE00` image window.

## Local Compile Pattern

```bash
rm -rf /tmp/nrf54-cs-sketchbook
mkdir -p /tmp/nrf54-cs-sketchbook/hardware/nrf54l15clean
ln -s "$PWD/hardware/nrf54l15clean/nrf54l15clean" \
  /tmp/nrf54-cs-sketchbook/hardware/nrf54l15clean/nrf54l15clean

ARDUINO_DIRECTORIES_USER=/tmp/nrf54-cs-sketchbook \
arduino-cli compile \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15 \
  hardware/nrf54l15clean/nrf54l15clean/libraries/\
Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding/\
BleChannelSoundingVprHciParity
```

Do not mark Channel Sounding fully complete in the feature matrix until the
synthetic connected-result source has been replaced and Arduino/Zephyr
two-board interoperability passes.
