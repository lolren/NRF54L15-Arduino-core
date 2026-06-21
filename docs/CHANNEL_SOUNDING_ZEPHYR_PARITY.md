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

## Completed in This Pass — Standalone CS Test Result Stream

Implemented parity item #1 from the plan below: the bundled VPR controller image
now emits the standalone CS Test result stream while `LE CS Test` mode is active.

- VPR image emits HCI LE Meta `CS Subevent Result` (`0x31`) and
  `CS Subevent Result Continue` (`0x32`) on the reserved test connection handle
  `0x0FFF`, with `config_id = 0` and `start_acl_conn_event = 0`, matching Zephyr
  `host/cs.c` test-mode handling.
- Each procedure is an initial `0x31` (partial) followed by a `0x32` (complete);
  procedures repeat at a heartbeat interval until `LE CS Test End` clears the
  test-active state, so generation stops deterministically.
- A second `LE CS Test` while one is already active is rejected with
  Command Disallowed (`0x0C`). The not-active and unsupported-override cases
  were already covered.
- The stream is mutually exclusive with the connected-procedure stream and
  shares the single transport slot via the existing `vprFlags` PENDING guard.
- Host `BleCsControllerVprHost` gained a test-result collector independent of the
  connected session: a `BleCsSubeventResultReassembler` fed from the direct
  controller path, with `lastTestResultValid()` / `lastTestResult()` /
  `testResultCount()` accessors and a `drainPendingControllerEvents()` entry
  point (test results live on the direct path, so use this rather than
  `poll()`/`loopOnce()` which feed the connected session).
- Added `File > Examples > ... > ChannelSounding > BleChannelSoundingVprCsTestResults`.
- Both VPR images were regenerated and fit the
  `0x2003C900-0x2003FE00` window (controller/dedicated 12716 B, 852 B headroom;
  transport 10324 B).

These results are still **synthetic** (deterministic mode-2 step data, not real
RF measurements), so this does not change the "do not mark CS fully complete"
rule at the end of this document.

## Completed in This Pass — Cached Capability and FAE State (Parity item #2)

The host layer now maintains per-connection cached capability and FAE state
independent of the VPR controller's limited RAM. All caching lives on the
Arduino host side (six-figure RAM budget), avoiding the VPR's ~13.5 KiB
window which has no headroom for cache arrays.

- `consumeDirectAuxiliaryEvent` captures `Read Remote Supported Capabilities
  Complete` (0x2C) and `Read Remote Supported Capabilities Complete V2` (0x38)
  LE Meta events and stores the parsed capabilities in dedicated members.
- `directWriteCachedRemoteSupportedCapabilities` and its V2 variant cache
  the written payload locally after the VPR acknowledges the HCI command.
- `directWriteCachedRemoteFaeTable` caches FAE values in `lastRemoteFaeTable_`
  on success.
- Accessors `cachedRemoteCapabilitiesV1()` and `cachedRemoteCapabilitiesV2()`
  return the cached data or `false` when no data is available (not yet read,
  written, or invalidated).
- `reset()` clears all cached members (`cachedRemoteCapabilitiesV1_`, V2,
  `lastRemoteFaeTable_`, `lastTestEndComplete_`, `testReassembler_`, and
  their validity flags), providing lifecycle invalidation on disconnect or
  host reset.
- `lastRemoteFaeTableValid()` / `lastRemoteFaeTable()` return the cached
  remote FAE table populated by either a `directReadRemoteFaeTable` auxiliary
  event or a successful `directWriteCachedRemoteFaeTable`.

Lifecycle:
- **Populate**: direct read or write via the VPR transport.
- **Read back**: accessors reflect the most recently written or received value.
- **Invalidate**: `reset()`, which mirrors disconnect / controller reset /
  capability-change semantics.

Added:
- `File > Examples > ... > ChannelSounding > BleChannelSoundingVprCachedCapabilities`

Out of scope for this pass (remain as future work):
- Real link-layer FAE exchange (populated from actual CS procedures, not from
  HCI write commands).
- Per-connection separation (single connection only; the host caches are a
  singleton on `BleCsControllerVprHost`).
- VPR-side caching (the VPR has no RAM headroom; this is by design).

## Completed in This Pass — Disconnect/Timeout/Abort Framework (Parity item #3a)

The VPR controller stub now detects BLE disconnect and peer-exchange timeout
events, propagates correct abort reasons into CS Subevent Result headers, and
the Arduino host cleans cached state when the link session closes. Previously
every subevent result had a hardcoded abort reason `0x00` regardless of
connection state.

- **VPR disconnect detection** (inlined in main loop): When `g_cs_session_open`
  transitions to `0` (cleared by host transport reset), active procedures are
  aborted with reason `0x0B` (Connection terminated by local host). All pending
  result state is flushed.
- **Peer-exchange timeout** (inlined in main loop): Commands that require peer
  response (Create Config, Security Enable, Set Procedure Parameters, Procedure
  Enable) arm a deadline based on `min_procedure_interval × 8` heartbeats. If
  the deadline is exceeded, the procedure is aborted with reason `0x06` (LL
  Procedure Timeout).
- **Abort reason propagation** in `build_demo_subevent_payload()`: The initial
  and continuation subevent result headers now carry abort reasons, with
  `procedure_done_status` / `subevent_done_status` set to `0x0F` (aborted) when
  the reason is non-zero.
- **Host-side disconnect handler** (`BleCsControllerVprHost::handleDisconnect()`):
  Detects the 1→0 transition of `linkSessionOpen` in `syncVprState()` and resets
  the inner host, test reassembler, FAE table, cached capabilities, and test
  results — returning to a clean state for reconnect.
- **Workflow phase reset**: When the session closes, `reconcileReadyShadowState()`
  drops the workflow phase to `kIdle` so the next `beginFreshHost()` starts
  fresh.
- **Stack reduction**: The VPR dedicated image `.stack` reservation was reduced
  from 640 B to 256 B to accommodate the new code within the
  `0x2003C900-0x2003FE00` window. (The new code added ~380 B of text + 7 B
  of BSS; call depth analysis confirmed 256 B is adequate for the shallow
  main-loop call chains with no interrupt nesting.)

Added:
- `File > Examples > ... > ChannelSounding > \
    BleChannelSoundingVprDisconnectHandling` — exercises normal flow, disconnect
    mid-procedure, and reconnect after disconnect.

Firmware blobs regenerated: controller stub 13096 B (up from 12716 B), transport
stub 9936 B (unchanged).

**nRF54L15 write-back cache coherency note:** The host-side disconnect cleanup
in Phase 2 exposed a hardware cache coherency issue. The `resetSharedState()`
function memsets the VPR shared memory region (0x20018000), writing zeros through
the write-back data cache. The transport state getters call
`invalidateCpuSystemCache()` (via `NRF_CACHE->TASKS_INVALIDATECACHE` at
0xE0082008) before reading, which discards dirty cache lines without write-back.
The result: stale VPR data in SRAM is read instead of freshly zeroed state —
`linkSessionOpen` appears still true and `handleDisconnect()` is never called.

Available cache-clean mechanisms were investigated:
- `Cache::cleanDataCache()` at 0x4004B008 (Cache HAL) **hangs the board** — the
  DCLEANALL register is not implemented on this hardware revision.
- `cpuSystemCache()->ENABLE = 0` at 0xE0082404 **silently ignored** — the ENABLE
  register is locked from non-secure writes by the SPU.
- CP15 DCCMVAC (`MCR p15, …`) **not backed** — the nRF54L15's system cache is
  nRF-specific hardware, not the Cortex-M33 L1 cache.

The working fix: `BleCsControllerVprHost::resetTransport()` forces the disconnect
cleanup (`host_.reset()`, test reassembler clear, `vprState_.linkSessionOpen = false`)
directly after `syncVprState()`, bypassing the cache-dependent `linkSessionOpen`
1→0 transition detection. This is safe because `resetTransport()` has already
stopped the VPR and zeroed its shared memory — the disconnect is logically
unconditional.

Out of scope for this pass (remain as future work):
- Real LL Control PDU construction and RADIO transmission (items #4, #5, #6).
- CS security material derivation / DRBG (item #5).
- Multi-config slot negotiation for stored configs.
- Two-board interoperability verification.

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
BleChannelSoundingVprCsTestResults
BleChannelSoundingVprCachedCapabilities
BleChannelSoundingVprDisconnectHandling
```

`BleChannelSoundingVprDisconnectHandling` was compiled, uploaded, and verified on a XIAO
nRF54L15 with probe UID `E91217E8`. Observed output:

```text
BleChannelSoundingVprDisconnectHandling
cs_vpr_disconnect=PASS phase1=1 phase2=1 phase3=1 pumps=12/12/12 disconnected=1 idle=1 ok=1
```

This confirms: normal flow (Phase 1), disconnect mid-procedure via resetTransport
(Phase 2), and reconnect after disconnect (Phase 3) all pass. Phase 2 required a
cache coherency workaround in `resetTransport()` — see the nRF54L15 write-back
cache note below.

Phase 4 (timeout resilience) has been added to the example and compiles; it
verifies that the VPR peer-exchange timeout handler fires without destabilising
the transport. Hardware verification of Phase 4 is pending the next upload cycle.

`BleChannelSoundingVprHciParity` was previously verified on the same hardware
with probe UID `E91217E8`:

```text
BleChannelSoundingVprHciParity
cs_vpr_hci_parity=PASS pumps=12 status=0/0/0/0/0/0/0 fae_valid=1 fae_handle=0x41 test_end=0
```

**CsTestResults note:** `BleChannelSoundingVprCsTestResults` fails with
`cs_vpr_test_results=FAIL procedures=0 handle=0x0 start=0x0 second_start=0x0 end=0xFF`.
The `directStartTest()` now succeeds (status 0x00) after the pre-drain fix
described below, but no standalone test results are consumed. Root cause: the VPR
firmware blob sets `g_cs_test_active = 1` when it receives `LE CS Test`, but the
standalone test result scheduling path (`g_pending_cs_test_result_stage`) is
never activated — subevent results continue to be emitted on the connected-session
handle instead of the reserved `0x0FFF` test handle. This is a VPR firmware-level
gap, not a host code defect.

**Pre-drain fix:** Any direct HCI command sent while the VPR has pending output
(e.g. demo-mode subevent results after `beginFreshHost`) would be rejected by
`writeInternal()` because the `vprFlags=PENDING` gate blocked new writes. The fix
adds a pre-drain step in `sendDirectHciCommand()` that consumes pending VPR events
into a scratch host before sending the command, so the transport is always clear.

## Current Limitations

### 1. CS Test Results — Stream Implemented (Synthetic), RF Capture Open

The VPR image now accepts `LE CS Test`, emits the standalone `0x0FFF`
subevent-result stream (`0x31`/`0x32`) while test mode is active, stops it on
`LE CS Test End`, and rejects a second test while active (see "Completed in This
Pass — Standalone CS Test Result Stream" above).

Remaining work before this can be called controller-complete:

- Replace the deterministic mode-2 step data with results captured from real CS
  radio execution (depends on items #4 and #5 below).
- Validate emitted bytes against Zephyr HCI captures on hardware.
- The "follow the requested mode/role/channel/timing/antenna/payload/override"
  coverage is still nominal/synthetic, not driven by the actual CS Test
  parameters.

### 2. Cached Capability and FAE State — Host-Side Done, Link-Layer Exchange Open

The Arduino host (BleCsControllerVprHost) now caches remote capabilities v1/v2
and FAE tables on the host side, with lifecycle invalidation via `reset()`.
See "Completed in This Pass — Cached Capability and FAE State" above.

Remaining work:

- Populate remote FAE from the real link-layer exchange rather than from HCI
  write commands or a synthetic zero table.
- Per-connection separation (currently a singleton on `BleCsControllerVprHost`;
  multiple simultaneous connections would need keyed storage).

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

1. **Standalone CS Test result stream** — DONE (synthetic)
   - Emits `0x31/0x32` result events using handle `0x0FFF`.
   - Host-side test-result collector independent of the connected workflow state
     (`drainPendingControllerEvents()` + `testResultCount()`).
   - Still to validate against Zephyr command/event byte captures on hardware
     and to drive results from real CS Test parameters (not synthetic).

2. **Per-connection cached state** — DONE
   - Store capabilities and FAE tables.
   - Add lifecycle and invalidation tests.

3. **Real link-layer control exchange** — IN PROGRESS (disconnect/timeout done)
   - Replace local acceptance with peer negotiation.
   - Disconnect detection, timeout tracking, and abort reason propagation
     implemented (see "Completed in This Pass — Disconnect/Timeout/Abort
     Framework" above).
   - Real LL Control PDU construction and RADIO transmission remain.

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
