# Channel Sounding — Master Plan & Progress Tracker

```
CHANNEL SOUNDING — FULL ZEPHYR PARITY
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
██████████████████████████████████████████░░░░  87%
        done           |        remaining
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

| Done | Task | Status |
|------|------|--------|
| ██ | HCI/VPR command parity | ✅ Hardware-verified |
| ██ | Cached capability & FAE state | ✅ Hardware-verified |
| ██ | Disconnect framework (Phases 1–3) | ✅ Hardware-verified |
| ██ | Timeout resilience (Phase 4) | ✅ Hardware-verified |
| ██ | LL Control PDU definitions | ✅ Code complete |
| ██ | VPR peer-exchange state machine | ✅ Hardware-verified |
| ██ | Test-only peer LL PDU injection/readback | ✅ Hardware-verified |
| ██ | `directStartTest` transport fix | ✅ Hardware-verified |
| ██ | Duplicate CS Test rejection | ✅ Hardware-verified |
| ██ | CS Test result stream (handle 0x0FFF, synthetic) | ✅ Hardware-verified |
| ██ | CS Test End (`end=0xFF` fix) | ✅ Hardware-verified |
| ██ | Error-path testing (invalid direct-HCI example) | ✅ Hardware-verified |
| ██ | Host abort reason reaction | ✅ Hardware-verified |
| ██ | Config removal / retained promotion example | ✅ Hardware-verified |
| ██ | Multi-config slot testing | ✅ Hardware-verified |
| ██ | CS LL-control over-the-air bridge | ✅ Two-board hardware-verified |
| ██ | Host-owned initiator LL PDU queue seam | ✅ Two-board hardware-verified |
| ██ | Host-owned initiator LL bridge service | ✅ Two-board hardware-verified |
| ██ | Host-owned initiator LL bridge poll helper | ✅ Two-board hardware-verified |
| ██ | VPR-owned initiator LL PDU source | ✅ Two-board hardware-verified |
| ██ | Standalone raw RF tone/DFE + Mode 2 result encoder harness | ✅ Two-board hardware-verified |
| ██ | Direct completed-result ingress seam | ✅ Hardware-verified |
| ██ | Raw measurement -> host result ingress seam | ✅ Hardware-verified |
| ░░ | Hardware event scheduler (RADIO/PPI) | 🔒 Second board |
| ░░ | Physical RF ranging / measurements | 🔒 Second board |
| ░░ | Two-board physical interoperability | 🔒 Second board + RF scheduler |
| ██ | Power / soak / stress testing | ✅ Hardware-verified |

---

## Legend

- ✅ Hardware-verified — compiled, uploaded, tested on XIAO nRF54L15
- ✅ Code complete — implemented, compiles, pending hardware upload
- ⚠️ Code complete, unreachable — implemented but not yet exercised by any example
- 📋 Planned below — detailed implementation plan follows
- 🔒 Blocked — requires second board, RADIO access, or VPR memory work

---

# Item 1 — HCI/VPR Command Parity

```
████████████████ 100%
```

All 14 HCI opcodes (`0x208A`–`0x2096`, `0x20A6`) and 9 LE Meta subevents (`0x2C`–`0x33`, `0x38`) match Zephyr definitions. Verified via `BleChannelSoundingVprHciParity` on hardware.

**No remaining work.**

---

# Item 2 — Cached Capability & FAE State

```
████████████████ 100%
```

Host maintains per-connection cached remote capabilities (v1, 30 bytes; v2, 33 bytes) and FAE table (72 values). Lifecycle invalidation via `reset()` on disconnect. Verified via `BleChannelSoundingVprCachedCapabilities`.

**No remaining work.**

---

# Item 3 — Disconnect / Timeout / Abort Framework

```
███████████████░ 90%
```

### 3a — Disconnect Detection & Cleanup (Phase 1–3)

```
████████████████ 100% Hardware-verified
```

- **VPR `detect_and_handle_disconnect()`** — fires when `g_cs_session_open → 0`, aborts active procedures with reason `0x0B` (Connection Terminated by Local Host)
- **Host `handleDisconnect()`** — resets inner host, test reassembler, FAE table, cached caps
- **`syncVprState()`** detects `linkSessionOpen` 1→0 transition → calls `handleDisconnect()`
- **`reconcileReadyShadowState()`** drops workflow phase to `kIdle` on session close
- **`resetTransport()` force-disconnect** — bypasses cache-dependent transition detection
- Verified: `cs_vpr_disconnect=PASS phase1=1 phase2=1 phase3=1`

### 3b — Timeout Tracking & Abort Reasons

```
████████████████ 100% Hardware-verified
```

- **VPR `check_peer_exchange_timeout()`** — fires when heartbeat exceeds deadline, aborts with reason `0x06` (LL Procedure Timeout)
- **Abort reason propagation** in `build_demo_subevent_payload()` — initial and continuation headers carry abort reasons; `procedure_done_status` / `subevent_done_status` set to `0x0F` (aborted)
- **`handleDisconnect()`** host-side — detects 1→0 transition, resets cached state
- Phase 4 verified: `phase4=1` (5 s delay >> ~800 ms deadline, host reaches ready after timeout)

### 3c — Peer-Exchange State Machine (Part 3b)

```
████████████████ 100% Hardware-verified
```

- **7-stage enum** replaces 0/1 flag:
  `IDLE → AWAITING_CS_RSP → AWAITING_CS_CFG → AWAITING_SEC_RSP → AWAITING_PROC_RSP → AWAITING_START → PROCEDURE_ACTIVE`
- The effective host-driven flow is:
  `Create Config → CS_RSP → CS_CFG → Security Enable → CS_SEC_RSP → Set Procedure Parameters → CS_PROC_RSP → Procedure Enable → CS_START`.
- **`peer_deadline_for_stage()`** — per-stage timeout calculation in VPR heartbeat ticks (`50000` for caps/CFG, `30000` for procedure/security, `min_procedure_interval × 800` for START)
- **`abort_reason_for_peer_stage()`** — stage-specific abort mapping (0x06=procedure, 0x07=config, 0x09=security)
- **`handle_peer_cs_pdu()`** — callback framework wired, advances stage on expected PDU arrival
- All arm sites updated to use stage-specific enum values

### 3d — LL Control PDU Definitions & Test Injection

```
████████████████ 100% Hardware-verified
```

- `vpr_cs_ll_control.h` — CS LL Control PDU opcodes `0x2C`–`0x35` with packed structs
- Serialization helpers: `vpr_cs_ll_encode_pdu()`, `vpr_cs_ll_decode_header()`, using raw LL Control PDU payloads (`opcode,len,payload...`) for test injection
- Test-only VPR vendor commands:
  - `0xFCE8` injects one raw peer CS LL Control PDU and returns previous/current peer stage plus abort state.
  - `0xFCE9` reads the current peer-exchange stage without changing it.
- **Hardware-verified diagnostic:** `BleChannelSoundingVprPeerPduInjection`

```text
cs_vpr_peer_pdu_injection=PASS progress=0x3FFFF final_stage=0 prev_stage=6 state_status=0x0 hci_status=0x0 valid=1 abort=0x42 invalid_status=0x12
```

- This proves the VPR state machine, timeout windows, abort bookkeeping, host-side debug APIs, and malformed-PDU rejection on real hardware.
- This is still not over-air LL Control PDU exchange; the diagnostic injects peer PDUs through a test-only HCI path.

### 3e — Host Abort Reason Reaction

```
████████████████ 100% Code complete
```

The host now rejects aborted subevent results before they can be accumulated into
a distance estimate. `accumulateProcedureResult()` records the latest procedure
and subevent abort reasons, clears estimate validity, and refuses aborted data.
`updateEstimateIfComplete()` also clears accumulated buffers if either side has
an aborted done status or non-zero abort reason.

**Implemented / verified:** `BleChannelSoundingHostAbortCleanup`.

```text
cs_host_abort_cleanup=PASS stale_blocked=1 recovery=1 abort=0xB/0x0
```

The probe injects a valid local result, then an aborted peer result with abort
reason `0x0B`, then another valid peer result for the same procedure. The host
must not complete the procedure against stale pre-abort local data. It then runs
a fresh valid local/peer procedure to prove recovery still works.

---

# Item 4 — CS Test Result Stream

```
████████████████ 100% Synthetic HCI parity verified
```

### 4a — `directStartTest` Transport Fix

```
████████████████ 100% Hardware-verified
```

**The fix:** `BleChannelSoundingVprCsTestResults.ino` now sets `gConfig.session.workflow.procedureEnable.enable = 0U` before `beginFreshHost()`. This configures the session (boots VPR, creates config, sets parameters) without enabling the connected procedure, so the transport is clear when `directStartTest` sends the CS Test command.

**Why it matters:** CS Test mode is mutually exclusive with an active connected CS procedure. The demo workflow's `procedureEnable.enable = 1U` keeps the transport saturated with connected demo subevent results, so `directStartTest`'s command write is permanently rejected.

**Verified:** `start=0x0` consistently on hardware.

### 4b — Duplicate CS Test Rejection

```
████████████████ 100% Hardware-verified
```

`validate_cs_test_command()` in `vpr_cs_transport_stub.c` now checks `g_cs_test_active != 0U` and returns `BLE_CS_HCI_STATUS_COMMAND_DISALLOWED (0x0C)`. The CsTestResults example tests this at lines 77–79:
```cpp
uint8_t secondStartStatus = 0x00U;
ok = ok && gHost.directStartTest(testParams, &secondStartStatus) &&
     secondStartStatus == kBleCsHciStatusCommandDisallowed;
```
This executes in `BleChannelSoundingVprCsTestResults` and returns
`second_start=0xC` on hardware.

### 4c — CS Test Result Stream (handle 0x0FFF)

```
████████████████ 100% Hardware-verified, synthetic
```

The VPR image now stages standalone test procedures after `LE CS Test`, emits
`0x31` / `0x32` result events on reserved handle `0x0FFF`, and stops
deterministically on `LE CS Test End`. Verified by:

```text
cs_vpr_test_results=PASS procedures=29 handle=0xFFF start=0x0 second_start=0xC end=0x0
```

This remains a synthetic controller-parity stream. The result payload is still
deterministic mode-2 test data, not real RF ranging data.

### 4d — CS Test End (`end=0xFF`)

```
████████████████ 100% Hardware-verified
```

The false `end=0xFF` path was caused by direct-HCI drain treating valid
command-complete / test result packets as a transport failure. The direct drain
now ignores direct-only packets that are not meant for the public connected host,
while preserving real `readNextH4Event()` failures. Verified by
`BleChannelSoundingVprCsTestResults` and `BleChannelSoundingVprHciParity`.

---

# Item 5 — Error-Path Testing

```
████████████████ 100%
```

Write example sketches that exercise error paths. **Pure software, testable on existing board.**

### 5a — Invalid Parameter Coverage

**Example:** `BleChannelSoundingVprInvalidParams`

Send malformed HCI commands through `directCreateConfig()`, `directProcedureEnable()`, `directSetProcedureParameters()`, `directSecurityEnable()`, `directStartTest()`, `directStopTest()`, etc. and verify the VPR returns `BLE_CS_HCI_STATUS_INVALID_PARAMS (0x12)`.

**Implemented / verified:** `BleChannelSoundingVprInvalidParams`

```text
cs_vpr_invalid_params=PASS pumps=12 statuses=12/12/12/12/C/0/0
```

Coverage currently includes:
- Create config with invalid `configId=0`
- Set procedure parameters for missing config `99`
- Procedure enable with invalid enable value `2`
- Remove missing config `99`
- Stop test while no CS Test is active
- Valid CS Test start and stop after the negative probes

Remaining negative coverage:
- Bad mainModeType / subModeType values
- Invalid channel map contents
- Invalid CS Test override mask bits
- Security enable malformed inputs

### 5b — Config Removal / Promotion

**Example:** `BleChannelSoundingVprConfigRemoveActive` — implemented and
hardware-verified.

- Create and arm an alternate config
- Re-select the original config
- Remove the selected/original config
- Verify the alternate config is promoted and runnable
- Verify enabling the removed config returns Invalid Parameters (`0x12`)

```text
cs_vpr_config_remove=PASS pumps=12 statuses=0/0/0/0/0/0/12
```

### 5c — Contradictory Commands

**Example:** `BleChannelSoundingVprEdgeCases` — implemented and
hardware-verified.

- Procedure enable without prior config creation
- Security enable before capabilities/config are established
- Set procedure parameters after the target config was removed
- Remove non-existent config
- Invalid CS Test override mask
- Valid command after negative probes to verify the host/transport remains usable

```text
cs_vpr_edge_cases=PASS e1=1 e2=1 e3=1 e4=1 e5=1
```

### 5d — Public Direct-HCI Burst Resilience

**Example:** `BleChannelSoundingVprHciBurst` — implemented and
hardware-verified.

The public direct-HCI helpers intentionally serialize by draining pending
controller output before each command. This test therefore verifies the supported
public API path under repeated create/security/set/remove/error cycles rather
than a raw shared-memory queue overflow. Raw async queue saturation remains a
lower-level VPR transport test item if a non-serializing debug hook is added.

```text
cs_vpr_hci_burst=PASS sent=10 success=8 rejected=2 polls=16 failed=0
```

---

# Item 6 — Multi-Config Slot Operations

```
████████████████ 100%
```

The VPR retained config table now supports 8 primary slots via `g_cs_slots[]`.
`BleChannelSoundingVprConfigRemoveActive` covers selected/active removal and
promotion, and `BleChannelSoundingVprMultiConfig` covers five retained configs,
slot removal, slot reuse, selecting an older config after later configs were
created, and rejection of a removed config. `BleChannelSoundingVprResetClearsConfigs`
verifies retained config state is cleared by `resetTransport()` and that a fresh
session starts from a single boot config again.

### 6a — Multiple Config Create/Select/Evict

**Example:** `BleChannelSoundingVprMultiConfig`

1. Create configs 2–5 in addition to the boot config
2. Verify retained config count reaches 5
3. Re-select and run the base config after later configs were created
4. Remove config 3 and verify count drops to 4
5. Create config 6 and verify the freed slot is reused
6. Verify the removed config rejects procedure parameters with `0x12`

**Files:** `examples/BLE/ChannelSounding/BleChannelSoundingVprMultiConfig`

**Status:** Hardware-verified on XIAO nRF54L15:

```text
cs_vpr_multi_config=PASS count=5>4>5 ... removed_select=12
```

### 6b — Retained Config State After Reset

1. Create configs in multiple slots
2. `resetTransport()` — verify VPR clears all slots
3. Re-create configs — verify IDs and slot state

**Example:** `BleChannelSoundingVprResetClearsConfigs`

**Status:** Hardware-verified on XIAO nRF54L15:

```text
cs_vpr_reset_clears_configs=PASS before=4 reset=0 fresh=1
```

---

# Item 7 — Soak / Stress / Power Testing

```
████████████████ 100% Software/VPR diagnostics hardware-verified
```

### 7a — Long-Running Stability

**Example:** `BleChannelSoundingVprSoakTest` — implemented and
hardware-verified.

- `beginFreshHost` → pump until 100 completed procedures
- `resetTransport()` / reconnect cycle × 10 — verify no state corruption
- Create/remove config cycle × 10 — verify retained config table recovery
- Final local/peer result snapshot sanity check

```text
cs_vpr_soak=PASS procedures=100 disconnects=10 configs=10 final=1
```

### 7b — Controller Reset While VPR Active

**Example:** `BleChannelSoundingVprResetMidProcedure` — implemented and
hardware-verified.

- `beginFreshHost` → verify procedure output before reset
- `resetTransport()` while VPR/session state is active
- Verify `handleDisconnect()` cleans state correctly
- Verify `beginFreshHost` works after reset and produces fresh output

```text
cs_vpr_reset_mid=PASS phase1=1 phase2=1 phase3=1 procedures=1
```

### 7c — Maximum Payload Sizes

**Example:** `BleChannelSoundingVprMaxPayload` — implemented and
hardware-verified.

- Standalone `LE CS Test` path
- Reassembles initial + continuation events on reserved handle `0x0FFF`
- Verifies 8 reported steps and 64 bytes of step payload

```text
cs_vpr_max_payload=PASS procedures=111 steps=8 bytes=64 start=0x0 end=0x0 failed=0
```

Remaining stress work is below the public API layer: a raw non-serializing VPR
transport queue saturation hook would be useful, but the public direct-HCI
surface now intentionally avoids that failure mode by draining responses.

---

# Item 8 — Real LL Control PDU Exchange

```
█████████████░░░ 80% Raw CS LL-control workflow bridge hardware-verified
```

### Completed in this slice

- Added `BleRadio::queueChannelSoundingLlControlPdu()` validation for the real
  raw CS LL-control shape: `opcode`, `payload length`, payload bytes.
- Added BLE connection-event tagging for received CS LL-control packets so
  examples can distinguish them from normal LL control and L2CAP/GATT data.
- Added a two-board diagnostic pair:
  - `BleChannelSoundingLlControlPeripheral`
  - `BleChannelSoundingLlControlCentral`
- The peripheral now answers real-shaped CS LL-control PDUs:
  `CS_RSP`, `CS_CFG`, `CS_SEC_RSP`, `CS_PROC_RSP`, `CS_START`, and `CS_ABORT`.
- The central now injects those real over-air peer PDUs into the VPR
  peer-exchange state machine through the existing direct peer-PDU test seam.
- Added public `bleCsBuildLlControl*()` helpers in `ble_channel_sounding.h` for
  `CS_REQ`, `CS_RSP`, `CS_CFG`, `CS_SEC_REQ/RSP`, `CS_PROC_REQ/RSP`,
  `CS_START`, `CS_TERMINATE`, and `CS_ABORT`. The two-board diagnostics now use
  those builders instead of local byte-array copies.
- Added `BleCsControllerVprHost::buildPendingInitiatorLlControlPdu()`, which
  reads the VPR peer-exchange stage and builds the corresponding local
  initiator-side PDU from the active workflow configuration.
- Added `BleCsControllerVprHost::queuePendingInitiatorLlControlPdu()`, which
  wraps that stage-to-PDU selection and queues the resulting payload into the
  current CPUAPP BLE transport seam. The diagnostic central no longer calls the
  low-level radio queue directly for initiator CS_REQ/SEC_REQ/PROC_REQ.
- Added `BleCsControllerVprHost::consumePeerLlControlPduFromEvent()`, which
  validates a received CS LL-control connection event and advances the VPR
  peer-exchange state behind the CS host API.
- Added `BleCsControllerVprHost::serviceInitiatorLlControlBridge()`, which
  combines peer CS LL-control consumption, the required direct-HCI transition
  (`Security Enable`, `Set Procedure Parameters`, `Procedure Enable`), and
  queueing of the next initiator PDU. The central diagnostic now calls this
  host service instead of owning the CS LL-control phase sequence itself.
- Added `BleCsControllerVprHost::pollInitiatorLlControlBridge()`, which owns
  the current CPUAPP bridge loop: duplicate-safe pending initiator PDU queueing,
  one BLE connection-event poll, peer CS LL-control event consumption, and the
  direct-HCI transition service. The central diagnostic now calls this single
  helper instead of deciding when to poll BLE events and when to run the bridge.
- Added `BleCsControllerVprHost::pollWithInitiatorLlControlBridge()` and
  `loopOnceWithInitiatorLlControlBridge()` as production-facing wrappers for
  callers that already use the normal CS host stream workflow. These pump/poll
  the existing host/VPR stream path first, then service one real BLE LL-control
  bridge event through the same helper.
- Added `BleChannelSoundingLlControlWorkflowCentral`, a two-board workflow
  diagnostic that uses `pumpInitiatorLlControlWorkflowBridge()` and
  `BleCsLlControlBridgeWorkflowTracker` instead of manually owning all
  workflow/tx/rx/result pass criteria in the sketch.
- Added `BleCsLlControlBridgeWorkflowTracker` so the shared CS library now owns
  the verified workflow mask (`0x7F`), initiator TX mask (`0x7`), peer RX mask
  (`0x3F`), and local/peer/procedure/estimate completion criteria used by the
  two-board bridge regression.
- Added VPR vendor command `0xFCEA`, which reads the pending local initiator
  CS LL-control PDU directly from the VPR/controller peer-exchange state.
  `BleCsControllerVprHost::buildPendingInitiatorLlControlPdu()` now prefers
  this VPR-provided PDU and falls back to the older CPUAPP stage-based builder
  if the command is unavailable. This moves CS_REQ, CS_SEC_REQ, and CS_PROC_REQ
  source ownership out of sketch/CPUAPP inference and into the controller-side
  workflow while preserving the existing BLE queue transport seam.
- Tightened the VPR peer-exchange timing so connected-procedure result packets
  are published only after over-air `CS_START` moves the peer exchange to
  `PROCEDURE_ACTIVE`.
- Expanded peer-response timeouts to match the slower software-polled
  over-air bridge path, avoiding false resets while waiting for `CS_PROC_RSP`
  and `CS_START`.
- Added `scripts/test_cs_ll_workflow_bridge.sh` to compile/upload both boards,
  capture serial, and require the full workflow PASS line.

Two-board hardware result:

```text
Peripheral /dev/ttyACM2:
queued CS_PROC_RSP
queued CS_START
queued CS_ABORT
debug rx=3 txq=6 txsent=6 txdrop=0 rxdrop=0 last_rx=0x2F last_tx=0x35

Workflow central /dev/ttyACM1:
VPR inject op=0x30 prev=3 stage=5
VPR inject op=0x33 prev=5 stage=6
VPR inject op=0x35 prev=6 stage=0
cs_ll_workflow_bridge=PASS wf=0x7F tx=0x7 rx=0x3F vpr_pdu=3 injected=6 direct=3 local=1 peer=1 proc=1 est=1
cs_connected_window snapshot=1 role=2 next_ce=105 interval_us=30000 until_next_us=28949 single_fit=1 single_avail_us=22649 single_reason=0 sweep_fit=0 sweep_avail_us=22649 sweep_reason=4
cs_ll_physical_followup=PASS sweeps=1 valid_channels=10 raw_est=1 raw_m=0.9242 host_est=1 host_steps=10/10 host_m=1.7047 proc=2
```

This proves the current CPUAPP BLE LL-control transport can move CS control
payloads over a real connection, that the VPR peer state machine can consume
the received peer PDUs, and that the normal host workflow can reach ready state
and publish local/peer procedure results after the over-air `CS_START`.
The follow-up line proves the same two-board run can then switch to physical
raw RADIO CS and feed real measurement data through the controller-host Mode 2
result ingestion path. That is still a staged handoff after disconnect, not a
true in-connection CS subevent scheduler.

The `cs_connected_window` line is the current timing seam for the next hard
slice. `BleRadio::getConnectionTimingSnapshot()` exposes a read-only snapshot
of the active BLE event cadence, and
`BleChannelSoundingRadio::planConnectedWindow()` reports whether a requested
single-channel or full-sweep CS work window fits before the next connection
event after configurable guard time. This is intentionally planning-only: it
does not yet take RADIO ownership inside the connected event schedule.

Regression command:

```bash
scripts/test_cs_ll_workflow_bridge.sh
scripts/test_cs_raw_radio_pair.sh
```

Defaults match the current local test bench:
central `/dev/ttyACM1` / UID `761FDE87`, peripheral `/dev/ttyACM2` / UID
`E91217E8`. Override with `CS_CENTRAL_PORT`, `CS_PERIPHERAL_PORT`,
`CS_CENTRAL_UID`, `CS_PERIPHERAL_UID`, and `CS_INSTALLED_VERSION`.
Both scripts sync the full implementation `src/` tree into the installed
Arduino package before compiling so local hardware tests do not accidentally
mix current repo CS/HAL code with older Board Manager sources.

### Still required

1. **VPR/RADIO ownership** — the VPR still does not own the actual RADIO
   subevent procedure. Local initiator LL-control PDU selection is now
   VPR-owned, but the current bridge still queues packets through CPUAPP BLE
   transport and receives peer PDUs on CPUAPP before injecting them into VPR.
2. **Automatic controller-owned scheduling** — packet construction, host-owned
   initiator queueing, peer-event consumption, direct-HCI bridge service, and
   one-event polling are now shared and workflow-tested. The VPR scheduler
   snapshot command (`0xFCEB`) now proves the controller image owns the
   procedure/subevent/chunk plan, and connected physical result ingestion now
   stamps result headers from that scheduler snapshot. Production CS still
   needs VPR/RADIO to own the timed physical measurement window rather than the
   Arduino sketch loop calling the helper.
3. **Connection-event-relative timing** — schedule CS exchanges in microsecond
   windows between BLE events, not by sketch polling. The read-only timing
   snapshot and connected-window planner now exist; the remaining work is to
   make VPR/RADIO consume that plan and own the timed subevent rather than only
   printing the plan from the diagnostic sketch.
4. **Physical CS subevents inside the connected procedure** — the raw RADIO
   tone exchange and real result ingestion are now hardware-tested as a
   post-LL-control follow-up, but production parity still requires moving that
   physical sweep into the negotiated connected procedure instead of
   disconnecting first.

### Implementation steps (after hardware access):

**Step 1 — Complete** — Move local CS LL-control PDU source into the
VPR/controller workflow:
- `0xFCEA` returns the VPR/controller-selected pending local PDU.
- `queuePendingInitiatorLlControlPdu()` keeps the existing CPUAPP queue as the
  transport sink, but the PDU source is now VPR-owned when the bundled image
  supports the command.
- `scripts/test_cs_ll_workflow_bridge.sh` now requires `vpr_pdu=3`, proving
  the workflow used the VPR readback path for CS_REQ, CS_SEC_REQ, and
  CS_PROC_REQ rather than silently passing through the fallback builder.
- `consumePeerLlControlPduFromEvent()`, `pollInitiatorLlControlBridge()`, and
  `loopOnceWithInitiatorLlControlBridge()` remain the current CPUAPP bridge
  service seams.
- `BleChannelSoundingLlControlCentral/Peripheral` and
  `BleChannelSoundingLlControlWorkflowCentral` remain the regression harness.

**Step 2** — VPR RADIO initialization:
- Configure `NRF_RADIO` for CS tone exchange
- Set up PPI channels for event chaining
- Configure `TIMER` instances for µs-precision scheduling
- Use `scripts/test_cs_raw_radio_pair.sh` as the current hardware baseline for
  standalone RADIO tone/DFE behavior and real-measurement Mode 2 subevent
  result encoding while moving execution under the connected controller
  workflow. The script requires both `std_est=1` and `host_est=1`, so the same
  over-air measurements must pass through the standalone estimator and the
  controller-host measurement ingress path.
- Use `scripts/test_cs_ll_workflow_bridge.sh` as the connected-workflow handoff
  baseline. It now requires `cs_ll_workflow_bridge=PASS` and
  `cs_ll_physical_followup=PASS`, and the PASS line must include `sched=1`,
  `work=1`, `work_proc=1`, and a non-zero `work_steps` plan, while the
  connected sweep must include `host_cfg=1` and `host_proc=1`, proving
  LL-control negotiation, VPR scheduler-state readback, VPR measurement-work
  readback, VPR-selected channel-plan readback, VPR work-item consumption by
  the connected sweep runner, scheduler-owned result identity, and real
  physical measurement host-ingress in one two-board run.
- `BleChannelSoundingRadio::measureMode2Sweep()` is the shared raw RADIO
  centre-out sweep primitive used by the workflow follow-up; future connected
  scheduler work should call through this primitive or move its internals into
  the controller/VPR timing path rather than open-coding another sweep loop.
- Feed completed local/peer result objects into the host through
  `consumeCompletedResult()` instead of wrapping physical result data as
  synthetic HCI event bytes.
- For real Mode 2 tone captures, use `consumeMode2ResultsFromMeasurements()`
  on `BleCsControllerHost`, `BleCsControllerStreamHost`, or
  `BleCsControllerVprHost`. It builds local and peer `BleCsSubeventResult`
  objects from raw `BleCsChannelMeasurement` arrays and then uses the same
  completed-result ingress path. This is hardware-verified by
  `BleChannelSoundingHostAbortCleanup` with `measurement_ingress=1`.

**Step 3** — LL Control PDU construction:
- Use the public `bleCsBuildLlControl*()` helpers for CPUAPP-side packet
  construction and keep `vpr_cs_ll_control.h` aligned for VPR-side C code
- Inject into the BLE data/control path at the right connection-event phase
- Reuse the test-only `0xFCE8` path as the deterministic validation harness while replacing its source with real received peer PDUs

**Step 4** — Peer negotiation state machine:
- Promote the temporary direct-injection bridge into the normal connected-CS
  host/controller path.
- Verify state transitions through the 7-stage peer-exchange enum; the
  two-board LL-control diagnostic already proves the local state machine and
  raw over-air PDU transport.

**Step 5** — Procedure execution:
- Schedule CS subevents at connection-event-anchor-relative times
- Execute TX/RX tone exchange
- Capture RTT timestamps and IQ samples

**Estimated effort:** raw LL-control transport is now unblocked and tested;
physical RF parity remains a multi-slice VPR/RADIO scheduler task.

---

# Item 9 — Hardware Event Scheduler

```
░░░░░░░░░░░░░░░░ 0% 🔒 Second board + RADIO docs required
```

Replace the VPR heartbeat-driven (polling) scheduler with a hardware-event-driven scheduler. Requires:
- `NRF_RADIO` CS/RTT register configuration
- PPI (Programmable Peripheral Interconnect) for task/event routing
- FLPR (Fast Lightweight Processor) for latency-sensitive radio sequencing
- CPUAPP sleep management during active CS procedures

**Blocked on:** Hardware documentation and second board.

---

# Item 10 — Physical Ranging & Security

```
░░░░░░░░░░░░░░░░ 0% 🔒 Second board + RADIO hardware required
```

Replace synthetic mode-2 step data with real RF measurements. Requires:
- RTT calculation from TX/RX timestamps
- Phase extraction from IQ samples across tones
- Frequency compensation (CFO correction)
- Antenna path delay calibration
- FAE correction curves
- Quality/confidence scoring
- CS1 nonce derivation, DRBG

**Blocked on:** Items 8 and 9.

---

# Item 11 — Two-Board Interoperability

```
░░░░░░░░░░░░░░░░ 0% 🔒 Second board required
```

- Arduino initiator ↔ Arduino reflector
- Arduino initiator ↔ Zephyr reflector
- Capture and compare HCI traces between implementations

**Blocked on:** Items 8, 9, 10.

---

# Implementation Order (Recommended)

```
Phase A — Quick wins (software only, 1 day)
─────────────────────────────────────────
Item 5c   More direct-HCI edge cases        ~80 lines
Abort-injection host-only unit example     DONE

Phase B — Multi-config (software only, 1 day)
─────────────────────────────────────────
Item 6a   Multi-config example             DONE
Item 6b   Retained config test             DONE

Phase C — Stress & soak (software only, 1 day)
─────────────────────────────────────────
Item 7a   Soak test example                DONE
Item 7b   Reset mid-procedure test         DONE
Item 7c   Max payload test                 DONE
Item 5d   Public direct-HCI burst test      DONE

Phase D — RF-backed CS Test parameters (VPR + RADIO work)
─────────────────────────────────────────
Use real test parameters and hardware measurements instead of synthetic mode-2 data

Phase E — Hardware (2nd board, 1–2 weeks)
─────────────────────────────────────────
Item 8    VPR-owned LL PDU generation
Item 9    Hardware event scheduler
Item 10   Physical ranging
Item 11   Zephyr/Arduino physical interoperability
```

---

# Current Commit History

```
20bf8b64 test: add CS direct HCI invalid parameter probe
83db24aa fix: drain direct CS HCI events without false failure
b5d8cc13 fix: disable connected procedure in CsTestResults example (enable=0)
6bf86d7d perf: shrink publish_builtin_response_for_opcode payload buffer 192->80 B
477c3da5 fix: add duplicate LE CS Test rejection in validate_cs_test_command
b5dbcd64 docs: update CsTestResults status — standalone test stream implemented
93f34a89 fix: implement standalone CS Test result stream (handle 0x0FFF)
f50a1907 feat: host abort handling + LL Control PDU framework (Parity item #3b)
3b4fa671 docs: note Phase 4 timeout test addition to disconnect handling example
d1443995 feat: add Phase 4 timeout resilience test to disconnect handling example
c7925a13 docs: comprehensive CS handover and master plan document
```

---

# File Reference

| File | Purpose |
|------|---------|
| `src/ble_channel_sounding.cpp` (6601 L) | Host HCI handling, workflow state machine, VPR host |
| `src/ble_channel_sounding.h` (1622 L) | Public API, command/event structs |
| `src/nrf54l15_vpr.cpp` (2665 L) | VPR transport layer, shared-memory I/O |
| `tools/vpr/vpr_cs_transport_stub.c` | VPR firmware source (dedicated + transport images) |
| `tools/vpr/vpr_cs_ll_control.h` | **New** — CS LL Control PDU definitions |
| `tools/vpr/vpr_cs_transport_stub.ld` | VPR linker script (stack at 0x120) |
| `tools/generate_vpr_cs_controller_stub.py` | RISC-V cross-compiler + header generator |
| `docs/CHANNEL_SOUNDING_ZEPHYR_PARITY.md` | Detailed parity status and completion log |
| `docs/CHANNEL_SOUNDING_HANDOVER.md` | Handover + architecture + technical notes |
| `docs/CHANNEL_SOUNDING_MASTER_PLAN.md` | This file |

---

# Build & Test Quick Reference

```bash
# Regenerate VPR firmware
cd hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation
python3 tools/generate_vpr_cs_controller_stub.py

# Compile an example
ARDUINO_DIRECTORIES_USER=/tmp/nrf54-cs-sketchbook
arduino-cli compile --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15 \
  hardware/.../examples/BLE/ChannelSounding/<ExampleName>

# Upload (probe UID E91217E8)
arduino-cli upload --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15 \
  -p /dev/ttyACM3 hardware/.../examples/BLE/ChannelSounding/<ExampleName>

# Read serial output
nrf_ocd -u E91217E8 reset   # reset via SWD
cat /dev/ttyACM3             # read virtual COM port

# Sync to installed Arduino library
DST=~/.arduino15/packages/nrf54l15clean/hardware/nrf54l15clean/0.9.196/...
cp <source_file> $DST/<dest_file>
```
