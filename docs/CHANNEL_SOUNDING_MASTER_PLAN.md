# Channel Sounding — Master Plan & Progress Tracker

```
CHANNEL SOUNDING — FULL ZEPHYR PARITY
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
████████████████████████████████████████░░░░░░  84%
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
███████░░░░░░░░░ 43% Raw CS LL-control bridge + host-stage PDU selection hardware-verified
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

Two-board hardware result:

```text
Peripheral /dev/ttyACM1:
link ev=15 queued=6
debug rx=3 txq=6 txsent=6 txdrop=0 rxdrop=0 last_rx=0x2F last_tx=0x35

Central /dev/ttyACM2:
cs_ll_vpr_bridge=PASS progress=0x1FFF injected=6
debug phase=complete ev=17 txq=3 inj=6 ble_rx=6 ble_txsent=3 ble_txdrop=0 ble_rxdrop=0 vpr_stage=0 vpr_status=0x0 progress=0x1FFF last_rx=0x35 last_tx=0x2F
```

This proves the current CPUAPP BLE LL-control transport can move CS control
payloads over a real connection and that the VPR peer state machine can consume
the received peer PDUs.

### Still required

1. **VPR/RADIO ownership** — the VPR still does not own the actual RADIO
   subevent procedure. The current bridge receives PDUs on CPUAPP and injects
   them into VPR.
2. **Automatic controller-owned local PDU emission** — packet construction and
   stage-to-PDU selection are now shared, but production CS still needs the
   normal VPR/controller workflow to queue those PDUs automatically instead of
   having the diagnostic sketch call the helper.
3. **Connection-event-relative timing** — schedule CS exchanges in microsecond
   windows between BLE events, not by sketch polling.
4. **Physical CS subevents** — execute TX/RX tone exchange, capture timestamps
   and IQ, then feed real result data into the existing host reassembler.

### Implementation steps (after hardware access):

**Step 1** — Move local CS LL-control PDU source into the VPR/controller workflow:
- Keep the existing CPUAPP queue as the transport sink.
- Reuse the new `bleCsBuildLlControl*()` builders instead of reintroducing
  sketch-local byte arrays.
- Use `BleCsControllerVprHost::buildPendingInitiatorLlControlPdu()` to emit
  CS_REQ/SEC_REQ/PROC_REQ packets from the current peer-exchange stage.
- Move the helper call out of the diagnostic loop and into the normal
  connected-CS workflow once the production scheduling seam is ready.
- Preserve `BleChannelSoundingLlControlCentral/Peripheral` as the regression
  harness.

**Step 2** — VPR RADIO initialization:
- Configure `NRF_RADIO` for CS tone exchange
- Set up PPI channels for event chaining
- Configure `TIMER` instances for µs-precision scheduling

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
