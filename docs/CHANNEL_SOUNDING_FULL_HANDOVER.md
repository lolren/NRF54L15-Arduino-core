# Channel Sounding — Complete Handover Document

**Date:** 2026-06-21
**Repo:** `NRF54L15-Clean-Arduino-core` (git@github.com:lolren/nrf54-arduino-core.git)
**Branch:** `main`
**Latest commit:** `1bc48513`

---

## 1. What This Is

Bluetooth Channel Sounding (CS) on the Seeed Studio XIAO nRF54L15 board. A **host library** (C++, runs on the ARM Cortex-M33 CPUAPP) communicates with **VPR firmware** (C, runs on the RISC-V coprocessor) through shared memory at `0x20018000`. The VPR acts as a synthetic BLE controller — it accepts HCI commands and produces subevent results.

The objective is **Zephyr parity**: all 14 HCI opcodes, 9 LE Meta subevents, disconnect/timeout/abort handling, and eventually real RF ranging with a second board.

---

## 2. Architecture

```
┌─────────────────────────────────┐     ┌──────────────────────────────┐
│  CPUAPP (ARM Cortex-M33)        │     │  VPR (RISC-V coprocessor)    │
│  1.5 MB RAM, 1.5 MB flash       │     │  96 KiB private RAM          │
│                                 │     │                              │
│  BleCsControllerVprHost         │ HCI │  vpr_cs_transport_stub.c     │
│  ├─ beginFreshHost()            │───▶│  ├─ publish_builtin_response  │
│  ├─ sendDirectHciCommand()      │ cmds│  ├─ consume_host_request()   │
│  ├─ drainDirectControllerEvts() │     │  ├─ build_demo_subevent_...  │
│  ├─ syncVprState()              │◀───│  ├─ publish_pending_cs_...    │
│  ├─ handleDisconnect()          │ HCI │  ├─ detect_and_handle_disc.  │
│  └─ resetTransport()            │ evts│  └─ check_peer_exchange_...  │
│                                 │     │                              │
│  VprSharedTransportStream       │     │  Shared state (0x20018000):  │
│  ├─ writeInternal()             │◀═══▶│  magic, version, status,     │
│  └─ pullResponse()              │     │  heartbeat, vprFlags, vprLen,│
│                                 │     │  vprData[256]                │
│  Shared memory                  │     │                              │
└─────────────────────────────────┘     └──────────────────────────────┘
```

### Key Files

| File | Lines | Purpose |
|------|-------|---------|
| `src/ble_channel_sounding.cpp` | 6601 | Host: HCI command/event handling, workflow state machine, VPR host |
| `src/ble_channel_sounding.h` | 1622 | Host: public API, structs, classes |
| `src/nrf54l15_vpr.cpp` | 2665 | VPR transport layer, shared-memory I/O, boot |
| `src/nrf54l15_vpr_transport_shared.h` | 98 | Shared-memory layout structs, base addresses |
| `tools/vpr/vpr_cs_transport_stub.c` | 5100+ | **VPR firmware** — CS controller + BLE transport logic |
| `tools/vpr/vpr_cs_controller_stub.c` | 3 | Entry point: `#define VPR_CS_DEDICATED_IMAGE 1` + include |
| `tools/vpr/vpr_cs_transport_stub.ld` | 50 | VPR linker script (origin, length, stack) |
| `tools/vpr/vpr_cs_ll_control.h` | 150 | LL Control PDU definitions (opcodes 0x2C–0x35) |
| `tools/generate_vpr_cs_controller_stub.py` | 80 | RISC-V cross-compiler + header generator |
| `docs/CHANNEL_SOUNDING_MASTER_PLAN.md` | 550 | Progress tracker and detailed plans |
| `docs/CHANNEL_SOUNDING_ZEPHYR_PARITY.md` | 520 | Parity status per item |
| `docs/CHANNEL_SOUNDING_HANDOVER.md` | 500 | Architecture + technical notes |
| `docs/CHANNEL_SOUNDING_FULL_HANDOVER.md` | | **This file** |

### Memory Layout (After Window Expansion)

```
0x20018000 ┌─────────────────────┐
           │ VPR shared transport │  2 KiB
0x20018800 ├─────────────────────┤
           │    ~30 KiB FREE      │
0x20020000 ├─────────────────────┤
           │ Host shared trans.   │  2 KiB
0x20020800 ├─────────────────────┤
           │   ~108 KiB FREE      │
0x2003B000 ├─────────────────────┤  ◀── VPR image start (NEW: was 0x2003C900)
           │   VPR firmware       │
           │   19.6 KiB window    │  ◀── NEW: was 13.5 KiB
0x2003FE80 ├─────────────────────┤  ◀── Context save (384 B, NOT moved)
           │   Context save       │
0x20040000 └─────────────────────┘
```

**VPR image window:** 20096 bytes (`0x4E80`), firmware currently 12816 bytes, stack 512 bytes (`0x200`), **6.7 KiB headroom**.

---

## 3. What's Done (With Hardware Verification)

### ✅ HCI/VPR Command Parity — 14 opcodes, 9 subevents
Example: `BleChannelSoundingVprHciParity`
Output: `cs_vpr_hci_parity=PASS pumps=12 status=0/0/0/0/0/0/0 fae_valid=1 fae_handle=0x41 test_end=0`

### ✅ Cached Capability & FAE State
Example: `BleChannelSoundingVprCachedCapabilities`
Host caches remote capabilities v1/v2 and 72-value FAE table. Lifecycle via `reset()`.

### ✅ Disconnect / Timeout / Abort Framework (Parity #3a)
Example: `BleChannelSoundingVprDisconnectHandling`
Output: `cs_vpr_disconnect=PASS phase1=1 phase2=1 phase3=1 phase4=1 pumps=12/12/12 disconnected=1 idle=1 ok=1`

- **Phase 1** — Normal flow: beginFreshHost → ready
- **Phase 2** — Disconnect mid-procedure: resetTransport → verify cleanup
- **Phase 3** — Reconnect: fresh beginFreshHost → ready again
- **Phase 4** — Timeout resilience: 5-second delay >> ~800ms peer-exchange deadline, host still reaches ready

### ✅ LL Control PDU Definitions
File: `tools/vpr/vpr_cs_ll_control.h`
CS PDU opcodes 0x2C–0x35 with packed structs and serialization helpers.

### ✅ VPR Peer-Exchange State Machine
7-stage enum: `IDLE → AWAITING_CS_RSP → AWAITING_CS_CFG → AWAITING_PROC_RSP → AWAITING_SEC_RSP → AWAITING_START → PROCEDURE_ACTIVE`

### ✅ Payload Buffer Optimization
`publish_builtin_response_for_opcode` payload buffer: 192 → 80 bytes (FAE table at 75 bytes is the max).

### ✅ VPR Window Expansion
Window: 13568 → 20096 bytes. Stack: 288 → 512 bytes. 6.7 KiB headroom.

### ✅ Host Abort Reason Accessors
`lastProcedureAbortReason()` / `lastSubeventAbortReason()` plumbed through Session→Host→StreamHost→VprHost.

### ✅ Duplicate CS Test Rejection (VPR code)
`validate_cs_test_command()` returns `COMMAND_DISALLOWED (0x0C)` if `g_cs_test_active != 0`.

---

## 4. What's NOT Done

### 🔴 CS Test Result Stream (handle 0x0FFF)
**The main remaining gap.** The VPR sets `g_cs_test_active = 1` when it receives `LE CS Test`, but no code schedules standalone test results on handle `0x0FFF`. The connected-path demo results continue on the session handle.

**Status:** `start=0x0` (command accepted), `procedures=0` (no results consumed), `second_start=0x0` (unreachable without results), `end=0xFF` (unreachable).

**The Mystery:** Adding ANY code (even 3 variable assignments) to the `BLE_CS_HCI_OP_TEST` case inside `publish_builtin_response_for_opcode()` breaks `directStartTest` (`start=0xFF`). This is NOT a code-size issue (6.7 KiB free), NOT BSS, NOT stack. The theory is that the RISC-V compiler's handling of the large switch statement is fragile — adding code to one case corrupts the dispatch table.

**Workaround path:** Move test staging init out of `publish_builtin_response_for_opcode`. Instead, set the staging globals from `consume_host_request()` AFTER `publish_response_for_opcode()` returns. This avoids modifying the switch statement.

### 🟡 Host Abort Reason Reaction
The host parses abort reasons from subevent results but never checks them. `accumulateProcedureResult()` should reject results with non-zero `procedureAbortReason`.

### 🟡 Error-Path Testing
No examples exercise invalid parameters, config removal while active, multi-config eviction, controller reset mid-procedure, or soak testing.

### 🟡 Multi-Config Slots
VPR supports 8 slots. No example tests multi-config operations.

---

## 5. Build, Flash, Test Procedure

### Prerequisites
```bash
# The Arduino CLI for nRF54L15
arduino-cli version  # any recent version

# RISC-V toolchain (for VPR firmware regeneration)
riscv64-unknown-elf-gcc --version  # 13.2.0

# nrf_ocd for flashing and target reset
# Located at: ~/.arduino15/packages/nrf54l15clean/hardware/nrf54l15clean/0.9.196/tools/nrf_ocd
```

### Regenerating VPR Firmware

After editing `tools/vpr/vpr_cs_transport_stub.c`:

```bash
cd hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation
python3 tools/generate_vpr_cs_controller_stub.py
```

Output: `generated .../vpr_cs_controller_stub_firmware.h (12816 bytes)`

If it fails with "region RAM overflowed", the firmware is too large. Current window: 20096 bytes, current firmware: 12816 bytes.

### Syncing to Installed Library

**CRITICAL:** Arduino builds use the installed library, NOT the source repo. After ANY file change:

```bash
SRC=hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation
DST=~/.arduino15/packages/nrf54l15clean/hardware/nrf54l15clean/0.9.196/libraries/Nrf54L15-Clean-Implementation

# After VPR changes:
cp $SRC/src/vpr_cs_controller_stub_firmware.h $DST/src/
cp $SRC/tools/vpr/vpr_cs_transport_stub.c $DST/tools/vpr/
cp $SRC/tools/vpr/vpr_cs_transport_stub.ld $DST/tools/vpr/

# After host library changes:
cp $SRC/src/ble_channel_sounding.cpp $DST/src/
cp $SRC/src/ble_channel_sounding.h $DST/src/

# After transport header changes:
cp $SRC/src/nrf54l15_vpr_transport_shared.h $DST/src/

# After example changes:
cp $SRC/examples/BLE/ChannelSounding/<ExampleName>/<ExampleName>.ino \
   $DST/examples/BLE/ChannelSounding/<ExampleName>/
```

### Compiling an Example

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
<ExampleName>
```

### Flashing

```bash
ARDUINO_DIRECTORIES_USER=/tmp/nrf54-cs-sketchbook \
arduino-cli upload \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15 \
  -p /dev/ttyACM3 \
  hardware/nrf54l15clean/nrf54l15clean/libraries/\
Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding/\
<ExampleName>
```

The probe UID for the primary test board is **`E91217E8`**. It appears as `/dev/ttyACM3`.

### Reading Serial Output

The XIAO nRF54L15 CMSIS-DAP has a CDC ACM virtual COM port on `/dev/ttyACM3`. The board must be reset via SWD while the serial port is open to capture the boot output:

```bash
# In one terminal: open the serial port and read
python3 -c "
import serial, subprocess, time, threading
ser = serial.Serial('/dev/ttyACM3', 115200, timeout=0.05)
output = []
running = [True]
t = threading.Thread(target=lambda: [output.append(d) if (d:=ser.read(4096)) else None for _ in iter(bool,True) if not running[0]], daemon=True)
t.start()
time.sleep(0.3)
# Reset target via SWD
subprocess.run(['/home/lolren/.arduino15/packages/nrf54l15clean/hardware/nrf54l15clean/0.9.196/tools/nrf_ocd', '-u', 'E91217E8', 'reset'], capture_output=True, timeout=10)
time.sleep(15)  # wait for test to complete
running[0] = False; t.join(timeout=1); ser.close()
print(b''.join(output).decode('utf-8', errors='replace'))
"
```

**Important:** `nrf_ocd reset` (v0.3.1) properly resets the target and releases it to run. Without this reset, the board doesn't execute the sketch after programming. DTR toggling does NOT work on this board (CMSIS-DAP doesn't wire DTR to reset).

### Board Listing

```bash
# List all connected boards
ls /dev/serial/by-id/
# usb-Seeed_Studio_Seeed_Studio_XIAO_nrf54_CMSIS-DAP_E91217E8-if02  ← debug interface
# The virtual COM port for Serial output is /dev/ttyACM3 (same USB device, CDC ACM interface)
```

---

## 6. Test Examples — Reference Outputs

### BleChannelSoundingVprDisconnectHandling (PASS)

```
BleChannelSoundingVprDisconnectHandling
cs_vpr_disconnect=PASS phase1=1 phase2=1 phase3=1 phase4=1 pumps=12/12/12 disconnected=1 idle=1 ok=1
```

Interpretation:
- `phase1=1`: Normal flow (beginFreshHost → ready)
- `phase2=1`: Disconnect mid-procedure (resetTransport → cleanup → idle)
- `phase3=1`: Reconnect after disconnect (beginFreshHost → ready again)
- `phase4=1`: Timeout resilience (5s delay → host still reaches ready)
- `pumps=12/12/12`: Pump count for each phase
- `disconnected=1`: After resetTransport, `ready()` returned false
- `idle=1`: Workflow phase was `kIdle` after disconnect

### BleChannelSoundingVprHciParity (PASS)

```
BleChannelSoundingVprHciParity
cs_vpr_hci_parity=PASS pumps=12 status=0/0/0/0/0/0/0 fae_valid=1 fae_handle=0x41 test_end=0
```

Interpretation:
- `pumps=12`: Number of pump iterations to reach ready
- `status=0/0/0/0/0/0/0`: All 7 setup HCI commands returned status 0x00 (SUCCESS)
- `fae_valid=1`: FAE table was successfully read back
- `fae_handle=0x41`: FAE response was on connection handle 0x0041
- `test_end=0`: CS Test End status was 0x00 (SUCCESS)

### BleChannelSoundingVprCsTestResults (EXPECTED FAIL)

```
BleChannelSoundingVprCsTestResults
cs_vpr_test_results=FAIL procedures=0 handle=0x0 start=0x0 second_start=0x0 end=0xFF
```

Current expected output:
- `start=0x0`: directStartTest succeeded (command accepted by VPR) ✅
- `procedures=0`: No test results consumed — stream not yet implemented
- `handle=0x0`: No valid test result received
- `second_start=0x0`: Unreachable code (ok=false before this line)
- `end=0xFF`: Unreachable code (ok=false before this line)

**Target output (after test stream fix):**

```
cs_vpr_test_results=PASS procedures=3 handle=0xFFF start=0x0 second_start=0xC end=0x0
```

- `procedures=3`: Three complete test procedures consumed
- `handle=0xFFF`: Results arrived on the reserved test handle
- `second_start=0xC`: Second CS Test rejected with Command Disallowed
- `end=0x0`: CS Test End succeeded

---

## 7. Known Issues

### Issue 1 — CS Test Stream Cannot Be Implemented (The Mystery)
**Severity:** Blocker for CS test stream
**Symptom:** Adding any code to the `BLE_CS_HCI_OP_TEST` case in `publish_builtin_response_for_opcode()` causes `directStartTest` to fail (`start=0xFF`). Even 3 variable assignments placed AFTER the response building.
**Root cause:** Unknown. Suspected RISC-V compiler quirk with the large switch statement. Not code-size, not BSS, not stack.
**Workaround path:** Put test staging init outside `publish_builtin_response_for_opcode`. Call it from `consume_host_request()` after `publish_response_for_opcode()` returns.

### Issue 2 — CS Test End Fails (end=0xFF)
**Severity:** Blocks CS Test End
**Symptom:** `directStopTest` returns false when called.
**Root cause:** Same as Issue 1? The host's `sendDirectHciCommand` for the `CS Test End` opcode is reached only after the drain loop. Since `ok` is false when the drain loop fails (0 procedures), `directStopTest` is never called (`ok &&` short-circuits).
**Expected fix:** Automatically resolved when Issue 1 is fixed and procedures start flowing.

### Issue 3 — nRF54L15 Write-Back Cache
**Severity:** Workaround in place
**Symptom:** After `resetSharedState()` memsets VPR shared memory, `syncVprState()` reads stale cache data instead of zeros.
**Fix:** `TASKS_INVALIDATECACHE` does clean+invalidate (verified by hardware test). `resetTransport()` forces disconnect cleanup unconditionally, bypassing cache-dependent detection.

### Issue 4 — `Serial.operator bool()` Never Returns True
**Severity:** Minor
**Symptom:** On XIAO nRF54L15, `while (!Serial)` hangs forever.
**Fix:** Use `delay(2000)` in setup() instead. All examples include this.

---

## 8. Commits (Most Recent First)

```
1bc48513 feat: expand VPR image window from 13.5 KiB to 19.6 KiB
767e4a20 docs: master plan and progress tracker for full CS parity
b5d8cc13 fix: disable connected procedure in CsTestResults example (enable=0)
6bf86d7d perf: shrink publish_builtin_response_for_opcode payload buffer 192->80 B
477c3da5 fix: add duplicate LE CS Test rejection in validate_cs_test_command
b5dbcd64 docs: update CsTestResults status
93f34a89 fix: implement standalone CS Test result stream (attempt, reverted)
f50a1907 feat: host abort handling + LL Control PDU framework (Parity item #3b)
3b4fa671 docs: note Phase 4 timeout test addition
d1443995 feat: add Phase 4 timeout resilience test to disconnect handling
c7925a13 docs: comprehensive CS handover and master plan document
```

---

## 9. Recommended Next Steps (Ordered)

### A. Fix the CS Test Stream (Issue 1) — ~2 hours

Move test staging initialization out of `publish_builtin_response_for_opcode()` and into `consume_host_request()`. This is the workaround for the switch-statement mystery.

**Code change:**

In `consume_host_request()` (line ~5050), after `publish_response_for_opcode(opcode)`:

```c
// After publish_response_for_opcode, if this was a CS Test command,
// initialize test staging OUTSIDE the switch statement.
#if VPR_CS_DEDICATED_IMAGE
if (opcode == BLE_CS_HCI_OP_TEST) {
  const uint8_t status = g_vpr_transport->lastError; // or check response
  if (status == 0U) {  // SUCCESS
    g_cs_test_procedure_counter = 1U;
    g_pending_cs_test_result_stage = 1U;
    g_cs_test_next_stage_heartbeat = g_vpr_transport->heartbeat + 50U;
  }
}
#endif
```

Then add the test publisher function and wire it into the main loop. The test publisher should be a standalone function that:
- Guards on `g_cs_test_active == 1` and `g_pending_cs_test_result_stage != 0`
- Emits `0x31`/`0x32` subevent results on handle `0x0FFF`
- Uses `config_id=0`, `acl_event_counter=0`, `frequency_compensation=0`, `reference_power_level=0`
- Emits initial (partial) + continuation (complete) pairs
- Uses `BLE_CS_HCI_TEST_PROCEDURE_INTERVAL_TICKS (200)` for procedure re-scheduling

**Test:** Upload `BleChannelSoundingVprCsTestResults` and verify:
1. `start=0x0`
2. `procedures >= 3`  
3. `handle=0xFFF`
4. `second_start=0xC`
5. `end=0x0`

### B. Host Abort Reason Reaction — ~30 minutes

In `ble_channel_sounding.cpp`, `accumulateProcedureResult()` (line ~3437):
- Check `result.header.procedureAbortReason != 0` and reject

In `updateEstimateIfComplete()` (line ~3585):
- Clean up accumulated results when abort detected

### C. Error Path Examples — ~4 hours

Write 3-4 example sketches covering:
- Invalid parameters on each HCI command
- Config removal while procedure is active
- Multiple config create/select/evict
- Controller reset mid-procedure

### D. Real LL PDU Exchange — Needs Second Board

Requires a second nRF54L15 or Zephyr board. The PDU definitions exist in `vpr_cs_ll_control.h`.

---

## 10. Quick Reference — Key Constants & Addresses

| Symbol | Value | Location |
|--------|-------|----------|
| `NRF54L15_VPR_IMAGE_BASE` | `0x2003B000` | `nrf54l15_vpr_transport_shared.h:10` |
| `NRF54L15_VPR_IMAGE_SIZE` | `0x4E80` (20096) | `nrf54l15_vpr_transport_shared.h:11` |
| `NRF54L15_VPR_CONTEXT_SAVE_BASE` | `0x2003FE80` | `nrf54l15_vpr_transport_shared.h:20` |
| `NRF54L15_VPR_CONTEXT_SAVE_SIZE` | `0x180` (384) | `nrf54l15_vpr_transport_shared.h:21` |
| VPR linker RAM origin | `0x2003B000` | `vpr_cs_transport_stub.ld:5` |
| VPR linker RAM length | `0x4E80` (20096) | `vpr_cs_transport_stub.ld:5` |
| VPR stack | `0x200` (512) | `vpr_cs_transport_stub.ld:39` |
| Controller firmware size | 12816 bytes | Generated header |
| `BLE_CS_HCI_TEST_CONN_HANDLE` | `0x0FFF` | `vpr_cs_transport_stub.c:116` |
| `BLE_CS_HCI_TEST_PROCEDURE_INTERVAL_TICKS` | `200` | `vpr_cs_transport_stub.c:118` |
| `BLE_CS_HCI_TEST_CHUNK_DELAY_TICKS` | `8` | `vpr_cs_transport_stub.c:119` |
| Test board probe UID | `E91217E8` | -- |
| Test board serial port | `/dev/ttyACM3` | -- |
| Installed Arduino library | `~/.arduino15/.../0.9.196/` | -- |

---

## 11. Git Remotes

```
origin  git@github.com:lolren/nrf54-arduino-core.git (fetch)
origin  git@github.com:lolren/nrf54-arduino-core.git (push)
```

Branch: `main`

There are NO uncommitted changes. Working tree is clean at `1bc48513`.

---

## 12. Session Memory

Claude project memory is at: `~/.claude/projects/-home-lolren-Desktop-test-pi-nrf54/memory/`

Key memory files:
- `nrf54l15-cache-coherency-fix.md` — Cache behavior verification and workaround
- `MEMORY.md` — Memory index

---

## 13. If You Get Stuck

1. **VPR firmware won't regenerate:** Check RISC-V toolchain is installed. Run `riscv64-unknown-elf-gcc --version`. Check the linker script hasn't been corrupted.

2. **Upload fails:** Check probe UID with `nrf_ocd list`. Make sure /dev/ttyACM3 is the correct port. Try power-cycling the board.

3. **No serial output:** Serial capture is timing-sensitive. The sketch runs ONCE in setup() and never again. You MUST open the serial port BEFORE resetting the board via nrf_ocd. See the Python capture script in Section 5.

4. **`start=0xFF` in CsTestResults:** This means `directStartTest` failed. Check that `gConfig.session.workflow.procedureEnable.enable = 0U` is present in the example (disables the connected procedure, preventing transport saturation).

5. **Everything was working, now it's not:** Check that the installed Arduino library (`~/.arduino15/.../0.9.196/`) has the latest files. The Arduino compiler uses the INSTALLED library, not the source repo. Always sync with `cp` after changes.

6. **Firmware size changed unexpectedly:** The `--gc-sections` linker removes unused functions. Adding code that IS called (even indirectly) can cause cascade inclusion. Check with `riscv64-unknown-elf-size` on the ELF file.
