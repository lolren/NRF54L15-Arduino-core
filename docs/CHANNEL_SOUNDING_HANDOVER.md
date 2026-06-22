# Channel Sounding — Handover & Master Plan

## Purpose

This document is the single source of truth for everything Channel Sounding (CS) in the
nRF54L15 Clean Arduino Core. It covers architecture, what's been done, what remains,
how to build/flash/test, and the implementation order for reaching full Zephyr parity.

**Last updated:** 2026-06-21

---

## 1. Architecture Overview

### 1.1 Two-Processor Model

The nRF54L15 has two processors sharing SRAM:

| Processor | Role | Notes |
|-----------|------|-------|
| **CPUAPP** (Cortex-M33, ~1.5 MiB RAM) | Arduino host — runs sketches, builds HCI commands, owns workflow state machine | Full Arduino environment |
| **VPR** (RISC-V, dedicated coprocessor) | Controller firmware — runs CS transport stub, produces subevent results | 96 KiB reservation, ~13.5 KiB for CS controller image |

CPUAPP and VPR communicate through **shared memory** at `0x20018000` (VPR side) /
`0x20020000` (host side), framed by the `VprSharedTransportStream` protocol.

```
┌──────────────────────────────┐     ┌─────────────────────────────┐
│  CPUAPP (Arduino host)       │     │  VPR (controller stub)      │
│                              │     │                             │
│  BleCsControllerVprHost      │────▶│  consume_host_request()     │
│  ├─ sendDirectHciCommand()   │ HCI │  ├─ validate + arm timeout  │
│  ├─ drainDirectControllerEv..│ cmds│  ├─ schedule CS results     │
│  ├─ syncVprState()           │     │  ├─ build_demo_subevent_... │
│  ├─ handleDisconnect()       │◀────│  └─ publish to transport    │
│  └─ resetTransport()         │ HCI │                             │
│                              │ evts│  detect_and_handle_discon.. │
│  VprSharedTransportStream    │     │  check_peer_exchange_time.. │
│  └─ writeInternal()          │     │                             │
│     └─ checks vprFlags       │     │  Shared state:              │
│                              │     │  g_cs_peer_exchange_stage   │
│  Shared memory (0x20018000)  │◀═══▶│  g_cs_procedure_abort_re.. │
└──────────────────────────────┘     └─────────────────────────────┘
```

### 1.2 Key Shared-Memory Structure

From `nrf54l15_vpr_transport_shared.h`:

```
VPR shared (0x20018000): magic, version, status, heartbeat, vprSeq, vprFlags, vprLen, vprData[256]
Host shared (0x20020000): magic, version, hostSeq, hostFlags, hostLen, hostData[128], scripts[8]
```

- `vprFlags & PENDING` = VPR has data for host (blocks writes from host)
- `hostFlags & PENDING` = host has data for VPR
- `status == READY` (2) = transport is active
- Transport flow: host writes → VPR reads → VPR responds → host reads

### 1.3 File Map

| File | Size | Purpose |
|------|------|---------|
| `src/ble_channel_sounding.cpp` | 6601 L | Host-side CS controller, HCI command/event handling, workflow state machine, VPR host |
| `src/ble_channel_sounding.h` | 1622 L | Public API, command/event structs, `BleCsControllerVprHost` class |
| `src/nrf54l15_vpr.cpp` | 2665 L | VPR transport layer, shared-memory I/O, boot/monitoring |
| `src/nrf54l15_vpr.h` | 610 L | VPR classes, `VprSharedTransportStream`, boot API |
| `src/nrf54l15_vpr_transport_shared.h` | 98 L | Shared-memory layout structs and base addresses |
| `tools/vpr/vpr_cs_transport_stub.c` | 5106 L | VPR firmware blob source — CS controller + transport logic |
| `tools/vpr/vpr_cs_controller_stub.c` | ~51 KiB | VPR helper image controller stub (generated) |
| `tools/generate_vpr_cs_controller_stub.py` | — | Python generator that reads the stub .c and produces the blob header |
| `tools/generate_vpr_cs_transport_stub.py` | — | Same for transport blob |
| `docs/CHANNEL_SOUNDING_ZEPHYR_PARITY.md` | 513 L | Parity items, verified baseline, limitations |
| `docs/CHANNEL_SOUNDING_HANDOVER.md` | this file | Handover + master plan |

### 1.4 nRF54L15 Write-Back Cache Note

The CPUAPP system cache at `0xE0082000` is **write-back**. Key behaviors:

- **`TASKS_INVALIDATECACHE` does CLEAN+INVALIDATE** (hardware-verified): writes back dirty
  lines before invalidating. This means data written through the cache will eventually
  reach SRAM on invalidate.
- **No DCLEANALL task exists**: the Cache HAL at `0x4004B000` claims to offer DCLEANALL
  but it hangs the board — the register isn't backed by hardware on this revision.
- **Cache cannot be disabled from non-secure code**: `ENABLE = 0` at `0xE0082404` is
  silently ignored (SPU-locked).
- **Practical workaround**: In `resetTransport()`, after memset-zeroing shared memory
  and calling `syncVprState()`, force the disconnect cleanup unconditionally rather
  than relying on detecting a `linkSessionOpen` 1→0 transition from the VPR readback.

---

## 2. Completed Work

### 2.1 HCI/VPR Command Parity (Parity Item #1 — DONE)

All 14 HCI opcodes and 9 LE Meta subevents match Zephyr definitions.

- Opcodes: `0x208A` through `0x2096`, plus `0x20A6` (cached caps v2)
- Subevents: `0x2C` through `0x33`, plus `0x38` (caps v2)
- `BleChannelSoundingVprHciParity` example passes on hardware

### 2.2 Standalone CS Test Result Stream (Parity Item #1 — DONE, synthetic)

- VPR emits `0x31`/`0x32` subevent results on handle `0x0FFF` while test mode active
- `LE CS Test End` stops the stream; second `LE CS Test` is rejected with `0x0C`
- Hardware-verified with `BleChannelSoundingVprCsTestResults`:
  `cs_vpr_test_results=PASS procedures=29 handle=0xFFF start=0x0 second_start=0xC end=0x0`
- Still synthetic: the payload is deterministic mode-2 test data, not physical RF
  ranging data.

### 2.3 Per-Connection Cached State (Parity Item #2 — DONE)

- Host caches remote capabilities v1/v2 and FAE tables
- Lifecycle invalidation via `reset()`
- `BleChannelSoundingVprCachedCapabilities` example verified

### 2.4 Disconnect/Timeout/Abort Framework (Parity Item #3a — DONE)

**VPR side** (`vpr_cs_transport_stub.c`):

| Component | Line | What it does |
|-----------|------|--------------|
| `g_cs_procedure_abort_reason` | 473 | Global abort reason for procedure-level events |
| `g_cs_subevent_abort_reason` | 474 | Global abort reason for subevent-level events |
| `g_cs_peer_exchange_deadline` | 475 | Heartbeat deadline for peer-exchange timeout |
| `g_cs_peer_exchange_stage` | 476 | 0=idle, 1=awaiting_peer |
| `detect_and_handle_disconnect()` | 4867 | Fires when `g_cs_session_open` → 0; aborts with `0x0B` |
| `check_peer_exchange_timeout()` | 4885 | Fires when heartbeat exceeds deadline; aborts with `0x06` |
| Abort propagation in `build_demo_subevent_payload()` | 3024-3047 | Abort reasons replace hardcoded `0x00`; sets done_status to `0x0F` |
| Timeout arming in `consume_host_request()` | 3927,4017,4053,4152 | Arms deadline = heartbeat + (min_procedure_interval × 8) |
| Disconnect guards in scheduling | 2594,2620 | `schedule_next_cs_procedure()` and `schedule_next_cs_subevent()` guard on `g_cs_session_open` |
| Main-loop wiring | 5057-5058 | Called before host request processing each iteration |

**Host side** (`ble_channel_sounding.cpp`):

| Component | Line | What it does |
|-----------|------|--------------|
| `resetTransport()` | 4159 | Stops VPR, zeros shared memory, forces disconnect cleanup |
| `handleDisconnect()` | 4222 | Resets host_, testReassembler_, FAE table, cached caps, test results |
| `syncVprState()` | 5436 | Calls `handleDisconnect()` on `linkSessionOpen` 1→0 transition |
| `sendDirectHciCommand()` pre-drain | 4519 | Drains pending VPR events into scratch host before sending command |
| `reconcileReadyShadowState()` | 2957 | Drops workflow phase to `kIdle` when session closed |

**Verified on hardware** (`BleChannelSoundingVprDisconnectHandling`):
```
cs_vpr_disconnect=PASS phase1=1 phase2=1 phase3=1 pumps=12/12/12 disconnected=1 idle=1 ok=1
```

### 2.5 Examples

13 CS examples exist under `examples/BLE/ChannelSounding/`:

| Example | Purpose | Verified |
|---------|---------|----------|
| `BleChannelSoundingHciParity` | Host-side HCI command packing | Compiles |
| `BleChannelSoundingHostAbortCleanup` | Host abort cleanup/stale-result rejection | PASS |
| `BleChannelSoundingVprHciParity` | VPR round-trip: all 14 HCI commands | PASS |
| `BleChannelSoundingVprCsTestResults` | Standalone CS Test result stream | PASS |
| `BleChannelSoundingVprConfigRemoveActive` | Retained config removal/promotion | PASS |
| `BleChannelSoundingVprInvalidParams` | Direct-HCI invalid parameter paths | PASS |
| `BleChannelSoundingVprCachedCapabilities` | Capability/FAE caching lifecycle | Compiles |
| `BleChannelSoundingVprDisconnectHandling` | Disconnect/timeout framework | PASS |
| `BleChannelSoundingVprLinkedInitiator` | VPR connected initiator workflow | Compiles |
| `BleChannelSoundingVprServiceNominal` | VPR service nominal test | Compiles |
| `BleChannelSoundingVprServicePowerProbe` | VPR service power probe | Compiles |
| `BleChannelSoundingInitiator` | Initiator example sketch | Compiles |
| `BleChannelSoundingReflector` | Reflector example sketch | Compiles |

---

## 3. Remaining Work (Master Plan)

### 3.1 Priority Order

```
Item #3b — Real LL Control PDU exchange       [VPR firmware, ~500-800 lines]
Item #4  — Hardware event scheduler            [VPR firmware + RADIO config, ~600-1000 lines]
Item #5  — Physical result capture + security  [VPR firmware, ~400-600 lines]
Item #6  — Two-board interoperability          [integration + testing]
Item #7  — Power, soak, error coverage         [testing + hardening]
```

### 3.2 Item #3b — Real Link-Layer Control Exchange

**Current state:** The VPR immediately accepts every command locally. Peer-exchange
timeout is armed but never fires because the "peer response" is instant. The abort
framework exists but only fires on disconnect (real) or timeout (theoretical, since
no real peer exchange is pending).

**What's needed:**

1. **LL Control PDU construction** — Build actual LL_CHANNEL_SOUNDING_CONTROL PDUs
   per Bluetooth Core Spec v5.4+ Vol 6, Part F, Section 3:
   - CS_REQ / CS_RSP (capability exchange)
   - CS_CFG / CS_CFG_COMPLETE (configuration negotiation)
   - CS_PROCEDURE_REQ / CS_PROCEDURE_RSP (procedure parameter negotiation)
   - CS_SEC_REQ / CS_SEC_RSP (security establishment)
   - CS_START / CS_TERMINATE / CS_ABORT

2. **PDU serialization/deserialization** — Pack/unpack the CS Control PDUs:
   - Opaque data length, opcode, procedure index, configuration ID
   - Capability bitfields, FAE values, procedure parameters
   - Access address, CRC init, channel map, timing offsets

3. **Connection-event-relative timing** — Schedule CS exchanges in ~150 µs windows
   between BLE connection events (anchor points). Requires:
   - Reading the ACL connection interval and anchor point from the link layer
   - Positioning CS windows based on `min_procedure_interval`, `max_procedure_interval`,
     `max_procedure_count`, `min_subevent_interval`, etc.
   - Coordination with the softperipheral's BLE event schedule

4. **State machine for peer negotiation** — Replace instant-local-accept:
   - Capability exchange: send CS_REQ, wait for CS_RSP (or timeout→abort)
   - Config negotiation: send CS_CFG, wait for CS_CFG_COMPLETE
   - Security: send CS_SEC_REQ, wait for CS_SEC_RSP
   - Procedure: send CS_PROCEDURE_REQ, wait for CS_PROCEDURE_RSP
   - Each step arms the peer-exchange timeout already implemented

**Files to modify:**
- `tools/vpr/vpr_cs_transport_stub.c` — PDU builders, state machine, timeout wiring
- New: `tools/vpr/vpr_cs_ll_control.h` — PDU format constants and structs

**Prerequisites:** A second nRF54L15 board (or a Zephyr board acting as peer)

### 3.3 Item #4 — Hardware Event Scheduler

**Current state:** CS events are driven by the VPR main-loop heartbeat (~1 kHz
polling). No real-time radio scheduling exists.

**What's needed:**

1. **RADIO peripheral configuration** for CS tone exchange:
   - Configure `NRF_RADIO` CS/RTT registers: `CSEC`, `CSTA`, `CTE`, `DFE`, `RTT`
   - Set up PPI (Programmable Peripheral Interconnect) channels for event chains
   - Configure `DPPI` for FLPR↔RADIO task/event routing
   - Set up `TIMER` instances for µs-precision CS event scheduling

2. **VPR/FLPR event-path integration:**
   - FLPR (Fast Lightweight Processor) for latency-sensitive radio sequencing
   - VPR for CS logic and result assembly
   - Shared event-ring between VPR and FLPR

3. **CS procedure execution loop:**
   - Prewarm: radio ramp-up, antenna switching setup
   - TX phase: tone transmission with antenna path sequencing
   - RX phase: tone reception, IQ sample capture
   - Measurement: RTT calculation, phase extraction, frequency compensation
   - Teardown: radio idle, result assembly

4. **Power management:**
   - CPUAPP sleeps during active CS procedures
   - Only VPR/FLPR awake for radio events
   - Wake CPUAPP only for completed-result delivery

**Files to modify/create:**
- `tools/vpr/vpr_cs_transport_stub.c` — replace heartbeat-driven scheduling with event-driven
- New: `tools/vpr/vpr_cs_radio.c` — RADIO configuration and event chain setup
- New: `tools/vpr/vpr_cs_flpr.S` or `.c` — FLPR radio sequencing (if needed)
- `src/nrf54l15_vpr.cpp` — may need new transport commands for radio state

**Hardware reference:** The nRF54L15 RADIO peripheral documentation, Nordic
nrfxlib softdevice controller CS implementation (Zephyr).

### 3.4 Item #5 — Physical Result Capture + Security

**Current state:** Subevent results contain deterministic mode-2 step data, not
real RF measurements. CS security enable is accepted but no actual security
material is derived.

**What's needed:**

1. **Physical measurement capture:**
   - RTT (Round-Trip Time): capture TX→RX timestamps, compute distance
   - PBR (Phase-Based Ranging): extract phase from IQ samples across tones
   - Frequency compensation: correct for CFO (Carrier Frequency Offset)
   - Antenna path compensation: per-path delay calibration
   - RSSI, packet quality, tone quality metadata

2. **Subevent result population:**
   - Replace synthetic `mode-2 step` data in `build_demo_subevent_payload()`
   - Fill real `CS Subevent Result` fields: `rtt_samples[]`, `phase_samples[]`,
     `tone_quality[]`, `abort_reason`, `reference_power_level`
   - Support all CS modes: RTT only, PBR only, RTT+PBR combined

3. **CS Security material:**
   - CS1 nonce derivation from connection context
   - DRBG (Deterministic Random Bit Generator) for CS tone sequence
   - Access address generation for CS synchronization packets
   - Procedure counter lifecycle (increment on each procedure start)

4. **Calibration infrastructure:**
   - Per-board antenna delay measurement
   - RF path characterization
   - FAE correction curve
   - Quality/confidence scoring

**Files to modify:**
- `tools/vpr/vpr_cs_transport_stub.c` — result assembly, security derivation
- `src/ble_channel_sounding.cpp` — result parsing for real data fields
- New calibration example sketches

### 3.5 Item #6 — Two-Board Interoperability

**What's needed:**

1. Set up two nRF54L15 boards (or one Arduino + one Zephyr)
2. Establish BLE connection between them
3. Run CS capability exchange, config negotiation, procedure enable
4. Verify subevent results match on both sides
5. Test initiator→reflector and reflector→initiator roles
6. Arduino↔Zephyr cross-implementation compatibility
7. Capture and compare HCI traces between implementations

### 3.6 Item #7 — Testing & Hardening

**What's needed:**

- Disconnect during every CS phase (cap exchange, config, security, procedure)
- Config removal while selected/active/retained
- Multiple stored configs and eviction
- HCI queue saturation and fragmented event streams
- Controller reset while VPR is active
- Invalid parameter coverage for all commands
- Long result streams (max payload, max continuation fragments)
- 24-hour soak test with periodic connect/disconnect/procedure cycles
- Power characterization: connected idle, active procedure, disable

---

## 4. Build, Flash, Test Workflow

### 4.1 Directory Structure

```
NRF54L15-Clean-Arduino-core/
├── hardware/nrf54l15clean/nrf54l15clean/
│   ├── libraries/Nrf54L15-Clean-Implementation/
│   │   ├── src/                    ← host-side C++ source
│   │   ├── tools/vpr/              ← VPR firmware source + generator scripts
│   │   ├── examples/BLE/ChannelSounding/
│   │   └── ...
│   ├── cores/nrf54l15/             ← core Arduino runtime
│   └── variants/xiao_nrf54l15/     ← board pin definitions
├── docs/                           ← documentation
└── ...
```

### 4.2 Regenerating VPR Firmware

After editing `tools/vpr/vpr_cs_transport_stub.c`:

```bash
cd hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation
python3 tools/generate_vpr_cs_controller_stub.py
python3 tools/generate_vpr_cs_transport_stub.py
```

Both must fit within `0x2003C900`—`0x2003FE00` (13568 B window).
Current sizes: controller ~13096 B, transport ~9936 B.

### 4.3 Compiling an Example

```bash
rm -rf /tmp/nrf54-cs-sketchbook
mkdir -p /tmp/nrf54-cs-sketchbook/hardware/nrf54l15clean
ln -s "$PWD/hardware/nrf54l15clean/nrf54l15clean" \
  /tmp/nrf54-cs-sketchbook/hardware/nrf54l15clean/nrf54l15clean

ARDUINO_DIRECTORIES_USER=/tmp/nrf54-cs-sketchbook \
arduino-cli compile \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15 \
  hardware/nrf54l15clean/nrf54l15clean/libraries/\
Nrf54L15-Clean-Implementation/examples/BLE/ChannelSouring/\
BleChannelSoundingVprHciParity
```

### 4.4 Flashing

```bash
# With nrf_ocd + OpenOCD (probe UID from lsusb or dmesg)
openocd -f interface/cmsis-dap.cfg -f target/nrf54l15.cfg \
  -c "program sketch.hex verify reset exit"

# Or via Arduino CLI with upload port
arduino-cli upload --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15 \
  -p /dev/ttyACM0 sketch/
```

### 4.5 Installed Library Sync

Arduino builds use the **installed** library under `~/.arduino15/packages/`, not the
source repo. After editing source files in the repo, sync to the installed location:

```bash
SRC=hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation
DST=~/.arduino15/packages/nrf54l15clean/hardware/nrf54l15clean/0.9.194/libraries/Nrf54L15-Clean-Implementation
cp $SRC/src/ble_channel_sounding.cpp $DST/src/
cp $SRC/src/ble_channel_sounding.h $DST/src/
cp $SRC/src/nrf54l15_vpr.cpp $DST/src/
cp $SRC/src/nrf54l15_vpr.h $DST/src/
# ... etc for any modified files
```

### 4.6 Regression Test Checklist

Before declaring any CS change done:

1. Regenerate VPR blobs; verify size within window
2. Compile all CS examples — zero regressions
3. Upload `BleChannelSoundingVprHciParity` → `PASS`
4. Upload `BleChannelSoundingVprDisconnectHandling` → `PASS`
5. Upload `BleChannelSoundingVprCsTestResults` → `PASS`
6. Upload `BleChannelSoundingVprInvalidParams` → `PASS`
7. Upload `BleChannelSoundingVprConfigRemoveActive` → `PASS`
8. Upload `BleChannelSoundingHostAbortCleanup` → `PASS`
9. Spot-check at least one connected example compiles and produces output

---

## 5. Technical Notes & Gotchas

### 5.1 VPR Transport Flow Control

`writeInternal()` (nrf54l15_vpr.cpp:1021) rejects writes if ANY of:
- `vpr->status != READY`
- `host->hostFlags & PENDING` (previous host write still unread)
- `vpr->vprFlags & PENDING` (VPR has output pending)
- `rxIndex_ < rxLen_` (local rxBuffer has unread data)

The pre-drain fix in `sendDirectHciCommand()` handles the `vprFlags=PENDING` case.

### 5.2 Two Event Handling Paths

| Path | Handle | Purpose |
|------|--------|---------|
| Connected | `connHandle == 0x0041` (session) | Consumed by `host_.consumeControllerPacket()` |
| Direct/Test | `connHandle == 0x0FFF` | Consumed by `consumeTestResultEvent()` |

Events on the wrong handle are silently ignored by the respective path.

### 5.3 VPR Firmware Limitations

- **Standalone CS Test result scheduling is synthetic:** test mode emits
  deterministic `0x31` / `0x32` packets on handle `0x0FFF`, but those packets are
  not yet backed by real RADIO CS measurements.
- **Synthetic results only:** All subevent data is from `build_demo_subevent_payload()`
  using deterministic mode-2 step values.
- **Single connection only:** The `g_cs_session_*` globals assume one ACL link.

### 5.4 Memory Budget

| Region | Address | Size | Usage |
|--------|---------|------|-------|
| VPR shared | `0x20018000` | 2048 B | Transport ring buffer |
| Host shared | `0x20020000` | 2048 B | Transport ring buffer |
| Controller image | `0x2003C900` | 13568 B | VPR firmware blob |
| Context save | `0x2003FE80` | 384 B | VPR state save/restore |

Controller blob headroom: ~472 B (13096/13568 used). Stack reservation: 256 B.

### 5.5 Cache Coherency Fix Pattern

Any function that:
1. Writes to shared memory via CPU (memset, struct assignment), then
2. Reads from shared memory with cache invalidate, then
3. Makes decisions based on the read values

...needs the force-disconnect pattern from `resetTransport()`. The cache guarantees
eventual write-back on invalidate, but the timing is non-deterministic within a
single function call.

---

## 6. Quick-Start for Continuing Work

### If you're picking this up fresh:

1. Read this document first
2. Read `docs/CHANNEL_SOUNDING_ZEPHYR_PARITY.md` for the detailed parity status
3. Read `src/ble_channel_sounding.h` (1622 lines) for the host API
4. Skim `tools/vpr/vpr_cs_transport_stub.c` — focus on:
   - Lines 252–266 (CS globals)
   - Lines 460–476 (dedicated globals + abort state)
   - Lines 2916–3050 (`build_demo_subevent_payload`)
   - Lines 3800–4165 (`consume_host_request` CS opcode dispatch)
   - Lines 4867–4900 (disconnect + timeout handlers)
   - Lines 5040–5060 (main loop)

### Next implementation task (Item #3b):

Start with LL Control PDU construction. The simplest first step:
1. Define PDU format constants for CS_REQ/CS_RSP in a new header
2. Add a `build_cs_ll_control_pdu()` function in the VPR stub
3. Wire it into `consume_host_request()` for the capability exchange path
4. Arm the existing peer-exchange timeout → verify it fires on hardware

---

## 7. Related Resources

- `docs/CHANNEL_SOUNDING_ZEPHYR_PARITY.md` — detailed parity status and completion log
- `~/.claude/projects/-home-lolren-Desktop-test-pi-nrf54/memory/nrf54l15-cache-coherency-fix.md` — cache workaround details
- `~/.claude/projects/-home-lolren-Desktop-test-pi-nrf54/memory/MEMORY.md` — memory index
- Zephyr reference: `ncs-workspace/zephyr/include/zephyr/bluetooth/hci_types.h`
- Zephyr reference: `ncs-workspace/zephyr/subsys/bluetooth/host/cs.c`
- Bluetooth Core Spec v5.4+ Vol 6, Part F — Link Layer Channel Sounding
