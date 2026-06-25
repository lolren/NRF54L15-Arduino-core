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
- Two-board interoperability verification.

## Completed in This Pass — Test-Only Peer LL Control PDU Injection

The VPR CS controller image now has a deterministic peer-PDU injection path for
the CS control-procedure state machine. This is a debug/test hook, not a
production over-air transport.

- Added raw CS LL Control PDU helpers in `vpr_cs_ll_control.h`. The helpers now
  encode/decode the actual raw peer control payload (`opcode`, `len`, payload)
  instead of wrapping it in an L2CAP-style CS CID. That matches the data consumed
  by the VPR peer state machine.
- Added test-only VPR vendor commands:
  - `0xFCE8` — inject one raw peer CS LL Control PDU.
  - `0xFCE9` — read the current peer-exchange state.
- Added public test APIs on `BleCsControllerVprHost`:
  - `directInjectPeerPduForTest(...)`
  - `directReadPeerExchangeStateForTest(...)`
- Added `BleCsVprPeerExchangeState`, returning command status, previous/current
  stage, peer deadline heartbeat, procedure abort reason, and subevent abort
  reason.
- Fixed peer-exchange deadlines to use VPR heartbeat-loop scale rather than the
  older tiny 300/500 tick constants. Direct debug commands also bypass the
  normal direct-command drain so the act of reading/injecting does not consume
  the entire peer timeout window.
- Fixed the built-in peer demo gate: creating a CS config no longer implicitly
  enables the synthetic peer demo just because the channel map is non-empty.
  The host-side `builtInPeerDemo.enabled` flag now controls whether procedure
  enable auto-enters synthetic `PROCEDURE_ACTIVE` or waits for a real/test peer
  `CS_START`.
- Updated the state sequence to match the host validation path:
  `Create Config -> CS_RSP -> CS_CFG -> Security Enable -> CS_SEC_RSP -> Set Procedure Parameters -> CS_PROC_RSP -> Procedure Enable -> CS_START`.

Hardware-verified on XIAO nRF54L15 probe `E91217E8`:

```text
BleChannelSoundingVprPeerPduInjection
cs_vpr_peer_pdu_injection=PASS progress=0x3FFFF final_stage=0 prev_stage=6 state_status=0x0 hci_status=0x0 valid=1 abort=0x42 invalid_status=0x12
```

This confirms:
- Valid injected peer PDUs advance the VPR state machine through config,
  security, procedure-parameter, start, active, and abort states.
- The procedure and subevent abort reasons survive back to the host.
- Malformed raw PDU length is rejected with `0x12` (`Invalid HCI Command Parameters`).

Remaining before physical parity:
- Keep the injected-PDU example as the deterministic regression test for the
  state machine. The real over-air LL-control transport is now covered by
  `BleChannelSoundingLlControlWorkflowCentral` plus
  `BleChannelSoundingLlControlPeripheral`.
- Move from CPUAPP/sketch-polled LL-control scheduling to a controller/VPR-owned
  connection-event scheduler before claiming physical RF parity.

## Completed in This Pass — Two-Board CS LL-Control VPR Bridge

The raw CS LL-control transport has now been exercised over a real BLE
connection between two XIAO nRF54L15 boards. This moves the previous
single-board `0xFCE8` injection proof one step closer to production: peer PDUs
are received from the actual link-layer control path, then injected into the
VPR peer-exchange state machine.

Added diagnostics:

```text
File > Examples > Nrf54L15-Clean-Implementation > BLE > ChannelSounding >
  BleChannelSoundingLlControlPeripheral

File > Examples > Nrf54L15-Clean-Implementation > BLE > ChannelSounding >
  BleChannelSoundingLlControlCentral

File > Examples > Nrf54L15-Clean-Implementation > BLE > ChannelSounding >
  BleChannelSoundingLlControlWorkflowCentral
```

What the pair verifies:

- The central sends real-shaped raw CS LL-control PDUs:
  `CS_REQ`, `CS_SEC_REQ`, and `CS_PROC_REQ`.
- The peripheral answers with:
  `CS_RSP`, `CS_CFG`, `CS_SEC_RSP`, `CS_PROC_RSP`, `CS_START`, and `CS_ABORT`.
- Both sides use the raw LL-control payload shape:
  `opcode`, `payload_length`, payload bytes.
- The examples now use the public `bleCsBuildLlControl*()` helper API in
  `ble_channel_sounding.h`, so local CS LL-control PDU construction is no
  longer duplicated as ad-hoc byte arrays in sketches.
- The central now asks `BleCsControllerVprHost::queuePendingInitiatorLlControlPdu()`
  to build and queue the local initiator PDU implied by the current VPR
  peer-exchange stage. This keeps CS_REQ, CS_SEC_REQ, and CS_PROC_REQ source
  selection behind the CS host API instead of making the sketch own the
  VPR-stage-to-radio-queue seam.
- Peer CS LL-control events are consumed through
  `BleCsControllerVprHost::consumePeerLlControlPduFromEvent()`, keeping the
  VPR peer-stage injection behind the CS host API instead of exposing the raw
  direct-injection seam in the diagnostic loop.
- The central now services the initiator LL-control bridge through
  `BleCsControllerVprHost::serviceInitiatorLlControlBridge()`, which consumes
  peer CS LL-control events, performs the required direct-HCI transition, and
  queues the next initiator PDU through the host API.
- The central now uses `BleCsControllerVprHost::pollInitiatorLlControlBridge()`
  for the whole current CPUAPP bridge loop: duplicate-safe pending initiator PDU
  queueing, one BLE connection-event poll, received peer CS LL-control service,
  and direct-HCI follow-up transitions.
- Stream-workflow callers can use
  `BleCsControllerVprHost::pollWithInitiatorLlControlBridge()` or
  `loopOnceWithInitiatorLlControlBridge()` to pump the existing CS host/VPR
  stream path and then service one real BLE LL-control bridge event through the
  same helper.
- The workflow central diagnostic now uses
  `pumpInitiatorLlControlWorkflowBridge()` and
  `BleCsLlControlBridgeWorkflowTracker` to verify that the normal host workflow
  reaches ready state and publishes local/peer procedure results after over-air
  `CS_START`.
- The VPR/controller image now exposes vendor command `0xFCEA` to return the
  pending local initiator CS LL-control PDU selected from its peer-exchange
  state. `BleCsControllerVprHost::buildPendingInitiatorLlControlPdu()` prefers
  that VPR-provided PDU and only falls back to CPUAPP stage-based construction
  for older images or invalid responses. This removes the local CS_REQ,
  CS_SEC_REQ, and CS_PROC_REQ selection decision from sketch logic while
  retaining the existing CPUAPP BLE queue as the temporary transport sink.
- The two-board regression now requires `vpr_pdu=3`, confirming all three local
  initiator PDUs in the workflow came from the VPR readback path.
- The VPR now gates connected-procedure result publication so local/peer results
  are not emitted before peer exchange reaches `PROCEDURE_ACTIVE`.
- Received CS LL-control packets are tagged in `BleConnectionEvent` and counted
  by `BleChannelSoundingLlControlDebug`.
- The central injects the received peer PDUs into `BleCsControllerVprHost` and
  drives the VPR peer stage sequence back to idle after abort.

Hardware used:

```text
/dev/ttyACM1  XIAO nRF54L15 / Sense  central     probe 761FDE87
/dev/ttyACM2  XIAO nRF54L15 / Sense  peripheral  probe E91217E8
```

Observed output:

```text
Peripheral:
queued CS_PROC_RSP
queued CS_START
queued CS_ABORT
debug rx=3 txq=6 txsent=6 txdrop=0 rxdrop=0 last_rx=0x2F last_tx=0x35

Workflow central:
VPR inject op=0x30 prev=3 stage=5
VPR inject op=0x33 prev=5 stage=6
VPR inject op=0x35 prev=6 stage=0
cs_ll_workflow_bridge=PASS wf=0x7F tx=0x7 rx=0x3F injected=6 direct=3 local=1 peer=1 proc=1 est=1
```

Regression command:

```bash
scripts/test_cs_ll_workflow_bridge.sh
```

Status:

- Raw over-air CS LL-control PDU movement is hardware-verified.
- VPR peer-stage consumption of real received peer PDUs is hardware-verified.
- The workflow bridge reaches host ready state and publishes local/peer
  synthetic procedure results only after the over-air `CS_START`.
- This is still not physical CS ranging. The examples do not yet run the RF tone
  exchange, hardware scheduler, timestamp/IQ capture, or real result reporting.

## Completed in This Pass — Raw RADIO RF Smoke Harness

The standalone two-board raw RADIO path now has a repeatable regression command:

```bash
scripts/test_cs_raw_radio_pair.sh
```

This flashes:

```text
BleChannelSoundingReflector -> default /dev/ttyACM2, UID E91217E8
BleChannelSoundingInitiator -> default /dev/ttyACM1, UID 761FDE87
```

The harness captures serial after a synchronized reset and fails unless:

- The initiator prints `raw_cs_init=ok`.
- At least one initiator sweep reports nonzero `valid_channels`.
- The initiator captures nonzero DFE data (`dfe_zero=0`).
- At least one real over-air sweep round-trips through the standard Mode 2
  `BleCsSubeventResult` encoder/parser/estimator path (`std_est=1`).
- The reflector completes at least `CS_MIN_REFLECTOR_REPLIES` replies.

Hardware result from the local XIAO nRF54L15 pair:

```text
reflector replies in 20s: 843
initiator: raw_cs_ready=1, valid_channels=15 in the final PASS line,
           dfe_bytes=336, dfe_zero=0
standard result path: std_est=1, std_steps=15/15, std_m populated
controller host ingress path: host_est=1, host_steps=15/15, host_m populated
```

This is an important physical-RF smoke baseline, but it is still not
Zephyr-parity connected Channel Sounding. It runs the clean-core standalone
phase-sounding frame format and proves RADIO tone extension / CSTONES / DFE
capture can move real RF data between two boards. It also proves those real
measurements can be encoded as standard Mode 2 CS subevent result step data and
fed through the existing controller-style parser/estimator path. The raw
initiator now also feeds those real measurements into
`BleCsControllerHost::consumeMode2ResultsFromMeasurements()` and requires
`host_est=1` in `scripts/test_cs_raw_radio_pair.sh`, proving the same physical
data can enter the connected-controller host accumulation path without an HCI
event wrapper. The
remaining work is to move that physical execution under the connected CS
controller workflow and produce those real subevent results from the
controller/VPR scheduler rather than from the standalone sketch loop.

## Completed in This Pass — Direct Completed-Result Ingress Seam

The controller host stack now accepts already-built completed
`BleCsSubeventResult` objects through `consumeCompletedResult()` on:

- `BleCsControllerSession`
- `BleCsControllerHost`
- `BleCsControllerStreamHost`
- `BleCsControllerVprHost`

The higher-level host wrappers also accept raw Mode 2 channel measurements
through `consumeMode2ResultsFromMeasurements()` on:

- `BleCsControllerHost`
- `BleCsControllerStreamHost`
- `BleCsControllerVprHost`

This is the seam needed by the physical scheduler: once the RADIO/VPR layer has
captured local and peer tone data, it can either feed already-built Mode 2
result objects directly into the same accumulation, abort filtering,
completed-result snapshot, and distance-estimation logic used by the HCI event
stream, or hand raw `BleCsChannelMeasurement` arrays to the host and let the
host build the local/peer Mode 2 result objects before consuming them. It no
longer has to wrap real physical measurements as synthetic HCI event bytes just
to reach the estimator.

## Completed in This Pass — Connected Mode 2 Sweep Runner

The connected physical Mode 2 diagnostic now has a reusable core-level runner
instead of sketch-owned trigger/ACK/measurement sequencing.

- Added `BleCsConnectedMode2SweepRunner::runInitiator()`.
- Added `BleCsConnectedMode2SweepConfig`, `BleCsConnectedMode2ChannelResult`,
  and `BleCsConnectedMode2SweepResult` to make connected RF sweep execution
  visible without forcing examples to own the scheduler details.
- The runner owns:
  - connected LL-control trigger PDU queueing,
  - peer ACK detection,
  - connection-event-offset wait,
  - RF path enable,
  - `BleChannelSoundingRadio::measureConnectedWindowChannel()` execution,
  - DFE capture metadata collection,
  - raw phase-slope estimate,
  - and optional `BleCsControllerVprHost` Mode 2 host ingestion.
- `BleChannelSoundingLlControlWorkflowCentral` now calls this runner and only
  prints the per-channel diagnostics used by the regression harness.

Hardware check:

```text
scripts/test_cs_ll_workflow_bridge.sh
cs_ll_workflow_bridge=PASS wf=0x7F tx=0x7 rx=0x3F vpr_pdu=3 injected=6 direct=3 local=1 peer=1 proc=1 est=1
cs_connected_sweep=PASS attempts=9 valid_channels=8 min_valid=3 requested_channels=9 raw_est=1 host_est=1 host_steps=8/8
cs_ll_physical_followup=PASS sweeps=1 valid_channels=21 raw_est=1 host_est=1 host_steps=21/21 proc=3
```

This is still not full Zephyr parity. The sweep runner is a CPUAPP/core-owned
orchestration seam. The next parity step is to move the connected timing owner
from the sketch/core runner into a controller/VPR scheduler that emits native CS
subevent results from real physical measurements.

## Completed in This Pass — CS Subevent Result Serialization Seam

The host-side Channel Sounding code now has public serializers for the same
subevent result layouts that the parser already accepted:

- `BleChannelSoundingRadio::buildHciSubeventResultEvent()`
- `BleChannelSoundingRadio::buildHciSubeventResultContinueEvent()`
- `BleChannelSoundingRadio::buildH4LeMetaSubeventResultPacket()`

This removes another temporary gap between physical measurement capture and
controller-style event publication. A real `BleCsSubeventResult` produced from
connected Mode 2 measurements can now be emitted as an LE Meta `CS Subevent
Result` (`0x31`) or `CS Subevent Result Continue` (`0x32`) packet without each
diagnostic sketch rebuilding the HCI byte layout by hand.

Hardware check after adding the serializers:

```text
scripts/test_cs_ll_workflow_bridge.sh
cs_ll_workflow_bridge=PASS wf=0x7F tx=0x7 rx=0x3F vpr_pdu=3 injected=6 direct=3 local=1 peer=1 proc=1 est=1
cs_connected_sweep=PASS attempts=9 valid_channels=7 min_valid=3 requested_channels=9 host_est=1 host_steps=7/7
cs_ll_physical_followup=PASS sweeps=1 valid_channels=17 raw_est=1 host_est=1 host_steps=17/17 proc=3
```

## Completed in This Pass — CS Result Fragmentation Helper

The serializer seam now also supports controller-style fragmentation:

- `BleChannelSoundingRadio::buildH4LeMetaSubeventResultFragmentPacket()` emits
  either the initial `CS Subevent Result` (`0x31`) or the continuation
  `CS Subevent Result Continue` (`0x32`) based on the current step-data offset.
- Fragmenting is done only on whole CS step records. Malformed step buffers,
  zero-length steps, incomplete steps, and unaligned offsets are rejected.
- Intermediate fragments report `procedure_done_status = partial` and
  `subevent_done_status = partial`; the final fragment preserves the source
  result's final status and abort reasons.
- `BleChannelSoundingHciParity` now includes a 40-step Mode 2 fragmentation
  self-test which forces one initial result plus one continuation result and
  round-trips both through the existing HCI parser.

Hardware/regression check:

```text
arduino-cli compile --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15 \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding/BleChannelSoundingHciParity

scripts/test_cs_ll_workflow_bridge.sh
cs_ll_workflow_bridge=PASS wf=0x7F tx=0x7 rx=0x3F vpr_pdu=3 injected=6 direct=3 local=1 peer=1 proc=1 est=1
cs_connected_sweep=PASS attempts=9 valid_channels=8 min_valid=3 requested_channels=9 raw_est=1 host_est=1 host_steps=8/8
cs_ll_physical_followup=PASS sweeps=1 valid_channels=21 raw_est=1 host_est=1 host_steps=21/21 proc=3
```

Remaining limitation: fragmentation can now be built correctly by the host, but
the connected controller/VPR scheduler still does not own native publication of
those fragments from real RF measurements.

## Completed in This Pass — Connected Physical Results Through HCI Event Path

The connected Mode 2 sweep no longer bypasses the controller/session HCI result
path for physical measurements.

- `BleCsControllerHost::consumeResultEventStream()` takes a completed
  `BleCsSubeventResult`, fragments it into HCI LE Meta `0x31/0x32` packets, and
  feeds those packets back through the existing session reassembler.
- `BleCsControllerHost::consumeMode2ResultEventsFromMeasurements()` builds
  local and peer Mode 2 results from real `BleCsChannelMeasurement` arrays and
  consumes them via that HCI event stream.
- `BleCsControllerStreamHost` and `BleCsControllerVprHost` now expose matching
  forwarding APIs.
- `BleCsConnectedMode2SweepRunner::runInitiator()` now uses
  `consumeConnectedMode2ResultEventsFromMeasurements()` instead of the older
  direct completed-result shortcut.
- Source-specific local/peer event ingestion now increments subevent counters
  on initial `0x31` events, so diagnostics still reflect actual completed local
  and peer procedure result streams.

Hardware/regression check:

```text
arduino-cli compile --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15 \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding/BleChannelSoundingHciParity

scripts/test_cs_ll_workflow_bridge.sh
cs_ll_workflow_bridge=PASS wf=0x7F tx=0x7 rx=0x3F vpr_pdu=3 injected=6 direct=3 local=1 peer=1 proc=1 est=1
cs_connected_sweep=PASS attempts=9 valid_channels=7 min_valid=3 requested_channels=9 host_est=1 host_steps=7/7
cs_ll_physical_followup=PASS sweeps=1 valid_channels=21 raw_est=1 host_est=1 host_steps=21/21 proc=3
```

Remaining limitation: result publication now uses controller-style HCI packets,
but CPUAPP still owns the connected RF sweep timing. Zephyr parity still
requires moving the scheduler/timing owner into the controller/VPR side.

## Completed in This Pass — VPR Scheduler State Readback

The dedicated CS VPR image now exposes a controller-owned scheduler snapshot
through test/vendor HCI opcode `0xFCEB`.

- The snapshot reports the VPR procedure counter, pending result stage, active
  subevent index, total subevents/steps, local/peer chunk offsets, heartbeat,
  next procedure/subevent/peer/chunk deadlines, computed delay ticks, config
  id, interval selector, and peer-gap ticks.
- `BleCsControllerVprHost::directReadSchedulerStateForTest()` parses the
  response into `BleCsVprSchedulerState` and mirrors the latest snapshot in
  `BleCsControllerVprHostState::scheduler`.
- `BleChannelSoundingLlControlWorkflowCentral` now prints this scheduler
  snapshot in the PASS line, and `scripts/test_cs_ll_workflow_bridge.sh`
  requires `sched=1`, `sched_proc=1`, and a non-zero subevent plan.

This does not make CS Zephyr-parity by itself. It closes a diagnostics gap:
the two-board workflow can now prove that the VPR/controller image owns and
advances the connected procedure/subevent/chunk plan. The remaining parity
work is to move the physical RADIO measurement window into that scheduler and
emit native result events from VPR/RADIO-owned measurements.

## Completed in This Pass — Scheduler-Aware Connected Result Headers

Connected Mode 2 physical measurement ingress now uses the cached VPR scheduler
snapshot when stamping host-consumed result headers.

- `BleCsControllerVprHost::consumeConnectedMode2ResultEventsFromMeasurements()`
  and the completed-result variant now derive `connHandle`, `configId`, and
  `procedureCounter` from a valid scheduler snapshot when one matches the
  active connection.
- If the scheduler snapshot is absent or invalid, the old fallback remains:
  use the active connection handle and `completedProcedureCounter + 1`.
- `BleChannelSoundingLlControlWorkflowCentral` reads the scheduler before the
  connected physical sweep and prints `host_cfg` / `host_proc` in the
  `cs_connected_sweep=PASS` line.
- `scripts/test_cs_ll_workflow_bridge.sh` now requires `host_cfg=1` and
  `host_proc=1`, proving the measured connected sweep was consumed under the
  controller scheduler's procedure identity, not a sketch-local guess.

This still does not make CS fully Zephyr-parity. The RADIO window is still
executed by the CPUAPP helper, but the result ingestion now follows the
controller-owned scheduler identity.

Hardware check:

```text
BleChannelSoundingHostAbortCleanup
cs_host_abort_cleanup=PASS stale_blocked=1 recovery=1 direct_ingress=1 measurement_ingress=1 abort=0xB/0x0
```

Next required slice:

- Move CS LL-control PDU emission/consumption from the CPUAPP sketch loop into
  a VPR/controller-owned connected-CS scheduler. The packet builders,
  host-owned bridge service, peer-event consumer, one-event poll helper,
  connected Mode 2 sweep runner, and stream-workflow wrappers now exist and
  are hardware-tested.
- Keep the current CPUAPP BLE queue/dequeue as the transport seam until the
  RADIO/VPR scheduler owns the timing.
- Then replace synthetic result data with real CS subevent capture.

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
BleChannelSoundingVprMultiConfig
BleChannelSoundingVprResetClearsConfigs
BleChannelSoundingVprEdgeCases
BleChannelSoundingVprHciBurst
BleChannelSoundingVprMaxPayload
BleChannelSoundingVprResetMidProcedure
BleChannelSoundingVprSoakTest
```

`BleChannelSoundingVprMultiConfig` was compiled, uploaded, and verified on a
XIAO nRF54L15 after expanding the dedicated VPR retained config table to 8
primary slots. It creates five retained configs, selects/runs the base config
after later configs were created, removes a middle config, reuses the freed slot,
and verifies the removed config rejects procedure parameters with `0x12`.

`BleChannelSoundingVprResetClearsConfigs` was compiled, uploaded, and verified
on a XIAO nRF54L15. It creates four retained configs, calls `resetTransport()`,
verifies the cached VPR state reports zero retained configs, then starts a fresh
session and verifies only the boot config exists.

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

Phase 4 (timeout resilience) has been added to the example and verified on
hardware; it confirms that the VPR peer-exchange timeout handler fires without
destabilising the transport.

`BleChannelSoundingVprHciParity` was previously verified on the same hardware
with probe UID `E91217E8`:

```text
BleChannelSoundingVprHciParity
cs_vpr_hci_parity=PASS pumps=12 status=0/0/0/0/0/0/0 fae_valid=1 fae_handle=0x41 test_end=0
```

**CsTestResults note:** The VPR firmware previously defined `g_pending_cs_test_result_stage`
but never activated it — `g_cs_test_active` was set by `LE CS Test` but standalone test
results on handle `0x0FFF` were never scheduled. This was fixed by adding
`publish_pending_cs_test_result_packet()` (emits `0x31`/`0x32` on `0x0FFF` with
`config_id=0` per Zephyr spec), wiring it into the main loop, and initializing test
staging in the `LE CS Test` handler. The fix is hardware-verified:

```text
cs_vpr_test_results=PASS procedures=29 handle=0xFFF start=0x0 second_start=0xC end=0x0
```

**Direct-drain fix:** Any direct HCI command sent while the VPR has pending output
(e.g. demo-mode subevent results after `beginFreshHost`) would be rejected by
`writeInternal()` because the `vprFlags=PENDING` gate blocked new writes. The
direct path now drains valid command-complete / CS Test packets without treating
direct-only events as a public-host failure, so `LE CS Test End` no longer returns
the old synthetic `0xFF` failure. Verified by `BleChannelSoundingVprHciParity` and
`BleChannelSoundingVprCsTestResults`.

**Host abort cleanup fix:** Aborted CS subevent results now clear both
accumulated local and peer procedure buffers immediately. Before this, a valid
result received after an abort could complete a procedure against stale
pre-abort data from the other side. Verified on hardware:

```text
cs_host_abort_cleanup=PASS stale_blocked=1 recovery=1 abort=0xB/0x0
```

**Software/VPR diagnostics pass:** The remaining public-API stress and edge-case
diagnostics were added under
`File > Examples > Nrf54L15 Clean Implementation > BLE > ChannelSounding` and
hardware-verified on XIAO nRF54L15 probe `E91217E8`.

```text
BleChannelSoundingVprEdgeCases
cs_vpr_edge_cases=PASS e1=1 e2=1 e3=1 e4=1 e5=1

BleChannelSoundingVprHciBurst
cs_vpr_hci_burst=PASS sent=10 success=8 rejected=2 polls=16 failed=0

BleChannelSoundingVprMaxPayload
cs_vpr_max_payload=PASS procedures=111 steps=8 bytes=64 start=0x0 end=0x0 failed=0

BleChannelSoundingVprResetMidProcedure
cs_vpr_reset_mid=PASS phase1=1 phase2=1 phase3=1 procedures=1

BleChannelSoundingVprSoakTest
cs_vpr_soak=PASS procedures=100 disconnects=10 configs=10 final=1
```

`BleChannelSoundingVprHciBurst` is deliberately a public direct-HCI burst test:
the public helpers drain the VPR output slot before each command, so this checks
that repeated command/status/error cycles do not deadlock or corrupt state. A
true raw queue-saturation test would require a lower-level non-draining debug
hook and is separate from the supported Arduino API.

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

### 7. Error and Concurrency Coverage Is Partly Synthetic

Current invalid-parameter coverage exists in
`BleChannelSoundingVprInvalidParams` and is hardware-verified:

```text
cs_vpr_invalid_params=PASS pumps=12 statuses=12/12/12/12/C/0/0
```

Retained config removal/promotion coverage exists in
`BleChannelSoundingVprConfigRemoveActive` and is hardware-verified:

```text
cs_vpr_config_remove=PASS pumps=12 statuses=0/0/0/0/0/0/12
```

Additional public API diagnostics are now hardware-verified:

```text
cs_vpr_edge_cases=PASS e1=1 e2=1 e3=1 e4=1 e5=1
cs_vpr_hci_burst=PASS sent=10 success=8 rejected=2 polls=16 failed=0
cs_vpr_reset_mid=PASS phase1=1 phase2=1 phase3=1 procedures=1
cs_vpr_max_payload=PASS procedures=111 steps=8 bytes=64 start=0x0 end=0x0 failed=0
cs_vpr_soak=PASS procedures=100 disconnects=10 configs=10 final=1
```

Required work:

- Disconnect during capability exchange, configuration, security, and active
  procedure.
- Procedure disable and re-enable.
- Multiple connections, or explicit rejection if the implementation remains
  single-link.
- Raw non-draining HCI queue saturation and malformed fragmented/concatenated
  event streams.
- More invalid channel maps, timing combinations, roles, PHYs, antenna
  selections, security-enable inputs, and override lengths.

## Recommended Implementation Order

1. **Standalone CS Test result stream** — DONE (synthetic)
   - Emits `0x31/0x32` result events using handle `0x0FFF`.
   - Host-side test-result collector independent of the connected workflow state
     (`drainPendingControllerEvents()` + `testResultCount()`).
   - Verified on hardware for start, duplicate rejection, result streaming, and
     Test End.
   - Still to drive results from real CS Test parameters and RF measurements
     instead of synthetic payloads.

2. **Per-connection cached state** — DONE
   - Store capabilities and FAE tables.
   - Add lifecycle and invalidation tests.

3. **Real link-layer control exchange** — WORKFLOW BRIDGE HARDWARE-VERIFIED
   - Real over-air peer negotiation is working in the diagnostic bridge.
   - Disconnect detection, timeout tracking, and abort reason propagation
     implemented (see "Completed in This Pass — Disconnect/Timeout/Abort
     Framework" above).
   - Host-side abort cleanup/stale-result rejection is hardware-verified with
     `BleChannelSoundingHostAbortCleanup`.
   - Test-only peer LL PDU injection/readback is hardware-verified with
     `BleChannelSoundingVprPeerPduInjection`.
   - Real LL Control PDU reception/transmission over the BLE data path is
     hardware-verified with `BleChannelSoundingLlControlWorkflowCentral`.

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
   - Public software/VPR diagnostics are hardware-verified; real RF power and
     two-board soak remain open.

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
