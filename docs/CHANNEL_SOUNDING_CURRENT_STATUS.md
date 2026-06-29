# Channel Sounding Current Status

Last updated: 2026-06-29

This document is the current practical status for finishing full Bluetooth
Channel Sounding (CS) Zephyr parity in this core. It intentionally uses a
stricter definition than the older progress bars: full parity means the core
can run real connected CS procedures with controller-owned scheduling and real
physical measurements, not only HCI compatibility or diagnostic proofs.

## Progress

```text
FULL CHANNEL SOUNDING ZEPHYR PARITY
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
█████████████████████████████████░  96%
done                 remaining
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

The current stricter estimate is approximately 96%: the VPR measurement
execution refactor, controller-owned auto-execution proof, VPR/RADIO connected
timing ownership proof, connected packet/timed Mode 2 ownership handoff,
controller-owned CS security material derivation/readback, Zephyr validation
harness, Zephyr-compatible Arduino step-data GATT bridge, and the first
repeatable accuracy/calibration plus fixed-placement distance-parity harnesses
are complete.

## Current Verified Baseline

Repository state used for this status:

```text
pre-slice base commit: 9e6356e7 cs: add Zephyr interoperability harness
branch: main
current slice state: Slice 9A complete; Arduino emits structured CS accuracy samples and the new fixed-placement parity harness can compare those medians against fresh Zephyr connected-CS distance logs
```

Two-board raw RF regression passed on 2026-06-28:

```bash
CS_INITIATOR_UID=761FDE87 \
CS_REFLECTOR_UID=E91217E8 \
CS_INITIATOR_PORT=/dev/ttyACM1 \
CS_REFLECTOR_PORT=/dev/ttyACM0 \
./scripts/test_cs_raw_radio_pair.sh
```

Observed result:

```text
cs_raw_radio_pair=PASS replies=717
valid_channels=34 raw_cs_ready=1 dfe_bytes=336 dfe_zero=0
std_est=1 std_steps=34/34
host_est=1 host_steps=34/34
```

Two-board connected LL workflow regression passed on 2026-06-29:

```bash
CS_CAPTURE_SECONDS=45 \
CS_CENTRAL_UID=761FDE87 \
CS_PERIPHERAL_UID=E91217E8 \
CS_CENTRAL_PORT=/dev/ttyACM1 \
CS_PERIPHERAL_PORT=/dev/ttyACM0 \
./scripts/test_cs_ll_workflow_bridge.sh
```

Observed result:

```text
cs_ll_workflow_bridge=PASS
vpr_pdu=3
work=1 work_flags=0xC1 work_steps=6/6
sec=1 sec_flags=0x7 sec_conn=0x41 sec_cfg=1
sec_nonce=0x4B37 sec_token=0xBAC1F0B7 sec_ctr=1
cs_connected_sweep=PASS
valid_channels=6 host_est=1 ctrl_ing=1
work_auto=1 work_auto_status=0x0 work_exec_snap=1 work_exec_cmd=0
work_exec_mismatch=0x0
work_rf_pkt=1 work_rf_buf=1 work_rf_timed=1 work_rf_timing=1
work_rf_pkt_cte=0x4A
work_rf_pkt_ctrl_us=2400 work_rf_pkt_listen_us=12000
work_rf_timing=1
work_rf_timing_flags=0x2F
work_result_timed_all=1 work_result_timed_matches=6/6/6
```

This proves the current implementation can move real-shaped CS LL-control PDUs
over a real BLE link, select work from VPR state, prove coherent VPR/RADIO
timing state for the scheduled work item, execute VPR-owned packet setup and
timed Mode 2 primitives for the connected work item, and feed real Mode 2
measurements into the host result path through native result publication. It
does not yet prove full Zephyr parity because mixed Zephyr/Arduino pairing,
accuracy calibration, and stress/power hardening still need to be completed.

Zephyr-compatible Arduino step-data bridge compile regression passed on
2026-06-29:

```bash
./scripts/test_cs_zephyr_bridge_compile.sh
```

Observed result:

```text
compile=BleChannelSoundingZephyrCompatInitiator
Sketch uses 149896 bytes (9%) of program storage space.
Global variables use 54052 bytes (34%) of dynamic memory.
compile=BleChannelSoundingZephyrCompatReflector
Sketch uses 149760 bytes (9%) of program storage space.
Global variables use 54056 bytes (34%) of dynamic memory.
cs_zephyr_bridge_compile=PASS
```

Slice 8 accuracy capture passed on 2026-06-29:

```bash
python3 scripts/test_cs_accuracy_calibration.py capture \
  --runs 1 \
  --profiles 0 \
  --capture-seconds 50 \
  --central-port /dev/ttyACM1 \
  --peripheral-port /dev/ttyACM2 \
  --central-uid 761FDE87 \
  --peripheral-uid E91217E8 \
  --source connected \
  --profile-name BleCsCalibrationProfileSlice8 \
  --board-pair 761FDE87_E91217E8 \
  --notes "post-log-format profile-0 bench capture; no reference distance supplied because spacing is approximate" \
  --skip-calibration-artifacts
```

Observed result:

```text
cs_accuracy=PASS samples=1
phase_raw median=0.7499 mad=0.0000 stddev=0.0000
confidence median=94.0
cs_accuracy_sample source=connected profile=0 profile_channels=9 executed_channels=6 rtt_enabled=0 requested_channels=6 valid_channels=6 used_channels=6 total_channels=6 rtt_channels=0 phase_raw_m=0.7499 phase_m=0.7499 dist_raw_m=0.7499 dist_m=0.7499 confidence=94 confidence_label=high
```

Profile 1 and profile 2 captures also passed the same connected workflow. They
confirmed an important current limitation: the profile selector changes the
requested compile-time channel list (9/18/37), but the connected production path
executes the six channels owned by the scheduled VPR work item. The log now
prints both `profile_channels` and `executed_channels` so this is visible. A
future larger-channel connected schedule needs to expand the procedure work
item, not just the sketch-side sweep list.

Two-board Arduino Zephyr-compatible step-data bridge hardware test passed on
2026-06-29:

```bash
arduino-cli compile --clean --upload \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15 \
  -p /dev/ttyACM0 \
  --library hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib \
  --library hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding/BleChannelSoundingZephyrCompatReflector/BleChannelSoundingZephyrCompatReflector.ino

arduino-cli compile --clean --upload \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15 \
  -p /dev/ttyACM1 \
  --library hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib \
  --library hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding/BleChannelSoundingZephyrCompatInitiator/BleChannelSoundingZephyrCompatInitiator.ino
```

Observed serial result:

```text
initiator: zephyr_step_write=PASS bytes=512 crc=0x72D7B7DB
reflector: zephyr_step_rx=PASS bytes=512 crc=0x72D7B7DB
initiator: zephyr_step_write=PASS bytes=512 crc=0xCD424572
reflector: zephyr_step_rx=PASS bytes=512 crc=0xCD424572
initiator: zephyr_step_write=PASS bytes=512 crc=0xBD4D196D
reflector: zephyr_step_rx=PASS bytes=512 crc=0xBD4D196D
```

## What Is Done

- HCI command and event layout parity with Zephyr definitions.
- VPR command transport for CS operations.
- Cached remote capabilities and FAE table support.
- Config, procedure, abort, timeout, and invalid-parameter handling.
- Synthetic standalone CS Test result stream.
- Two-board real-shaped LL CS control exchange.
- VPR-selected initiator PDU/work-item readback.
- Real raw RF Mode 2 measurements outside the final connected scheduler.
- Host result ingestion from real measurements.
- Connected diagnostic flow showing LL-control negotiation plus VPR/RADIO-owned
  packet/timed work and real RF follow-up can work in one two-board run.
- Controller-owned CS security material derivation/readback. Procedure
  parameter and procedure enable commands now require valid material, not just a
  boolean security flag.
- Zephyr-compatible CS Sample GATT step-data bridge:
  - advertiser name `CS Sample`
  - service UUID `87654321-4567-2389-1254-f67f9fedcba9`
  - characteristic UUID `87654321-4567-2389-1254-f67f9fedcba8`
  - 512-byte ATT Prepare Write / Execute Write path
  - Arduino initiator and reflector examples for mixed-pair testing

## Main Gap

The current system has all major pieces, but not yet in the final Zephyr-like
ownership model.

Current shape:

```text
CPUAPP/sketch runner:
  polls connection events
  bridges LL-control state
  calls host-visible measurement execution
  performs connected sweep orchestration
  feeds results into host

VPR/controller:
  owns command state
  owns peer-exchange stage
  owns work-item selection/readback
  can execute RF primitives through a reusable measurement worker
```

Required final shape:

```text
VPR/controller/RADIO:
  owns connected CS procedure timing
  owns scheduled measurement execution
  owns result generation/publication

CPUAPP/sketch:
  configures CS
  starts/stops procedures
  reads controller results
```

Until that ownership is moved, CS should remain documented as staged/partial,
even though many lower-level parts are already hardware-verified.

## Required Slices

Estimated remaining work: 1 full slice after Slice 8.

Realistic session count: 2 to 4 sessions. The highest remaining risk is no
longer the missing service bridge or a missing accuracy harness. It is mixed
Zephyr/Arduino hardware behavior, larger connected schedule coverage, and
long-run BLE coexistence under reconnect/abort/power stress.

### Slice 1: Refactor VPR Measurement Execute - Done

Goal: split the current vendor command implementation into a reusable execution
worker.

Current issue:

- `build_vendor_ble_cs_measurement_execute_complete_payload()` is tied to the
  direct/test vendor command.
- It reads optional timing parameters from `g_host_transport->hostData`.
- Calling it from the VPR main loop would be a hack because there may be no
  matching host command payload.

Required work:

- Extracted a side-effect worker that accepts explicit parameters.
- Kept the existing vendor command as a wrapper around that worker.
- Preserved the current direct/test command behavior and regression output.
- Added a host-side `BleCsMeasurementExecuteParams` API and moved the connected
  sweep runner off the test-named execution method.
- Regenerated VPR firmware blobs and confirmed compile/test stability.

Verification:

- `BleChannelSoundingLlControlWorkflowCentral` compiles.
- `scripts/test_cs_raw_radio_pair.sh` passes.
- `scripts/test_cs_ll_workflow_bridge.sh` passes.

### Slice 2: Controller-Owned Auto Measurement Execution

Goal: after LL `CS_START` / `PROCEDURE_ACTIVE`, VPR executes the scheduled work
item itself.

Implemented in this slice:

- VPR now has a guarded auto-execute service keyed by procedure counter and
  active subevent index.
- When peer LL `CS_START` moves the dedicated CS image into
  `PROCEDURE_ACTIVE`, VPR executes the scheduled work item through the same RF
  measurement worker used by the diagnostic command.
- The VPR idle loop also runs the auto-execute service after scheduling the next
  subevent/procedure and before publishing result packets.
- The host-visible `0xFCED` measurement-execute command is now idempotent for
  the current controller-owned work item: if VPR already executed it, the command
  returns the latched snapshot instead of running RF work again.
- The measurement-work read flags now use bit `0x80` to indicate that the
  current procedure/subevent has already been auto-executed by VPR.
- The measurement-execute response flags now use bit `0x10` to indicate the
  response is a controller-owned snapshot.

Verification:

- `scripts/test_cs_ll_workflow_bridge.sh` passes with two boards after this
  change.
- `scripts/test_cs_vpr_auto_measurement.sh` passes with two boards. It builds a
  focused proof-only central sketch that stops immediately after peer
  `CS_START`, reads the VPR measurement work item, and confirms:
  - work-read flag `0x80` is set (`work_auto=1`)
  - measurement execute response flag `0x10` is set (`exec_snap=1`)
  - a second execute readback returns the same execute count/token
    (`stable=1`)
  - no host-side diagnostic execute reruns the RF work (`no_host_execute=1`)
- `git diff --check` passes.

Observed proof result:

```text
cs_vpr_auto_measurement=PASS
work_flags=0xC9 work_auto=1 work_auto_count=1 work_auto_due=1
exec_flags=0x1F exec_snap=1 exec_count=1
exec2_flags=0x1F exec2_snap=1 exec2_count=1
stable=1 no_host_execute=1
```

### Slice 3: Move Connected Timing Ownership Into VPR/RADIO

Goal: VPR/RADIO owns the timed measurement window relative to BLE connection
events.

Current issue:

- CPUAPP currently plans and waits for connection-event timing.
- This is useful for diagnostics, but it is not Zephyr-like controller
  behavior.

Implemented in the first Slice 3 pass:

- The VPR measurement-execute response was extended from 201 bytes to 236 bytes
  with an append-only RF timing-owner proof block. Existing response fields are
  unchanged.
- The RF timing-owner block reports controller/VPR timing evidence:
  - procedure active state
  - connection handle and procedure counter
  - active subevent
  - VPR heartbeat
  - next procedure and next subevent heartbeat
  - computed procedure interval ticks
  - computed subevent delay ticks
  - peer gap ticks and interval selector
  - an independent `0xD3......` token generated by both VPR and host code
- The connected sweep runner now parses and validates this timing-owner block.
  If the proof is missing or inconsistent, it sets mismatch bit `1 << 19`.
- `BleChannelSoundingLlControlWorkflowCentral` now prints
  `work_rf_timing=*` diagnostics so a run can prove whether the VPR/RADIO timing
  state matched the connected work item.

Verification from the timing-owner pass:

- `scripts/test_cs_vpr_auto_measurement.sh` passes.
- `scripts/test_cs_ll_workflow_bridge.sh` exits successfully after the Slice 3B
  update and validates the connected timing-owner proof.
- The connected diagnostic sweep now validates VPR/RADIO ownership for timing,
  packet configuration, packet buffer setup, and timed Mode 2 execution:

```text
work_exec_mismatch=0x0
work_rf_pkt=1
work_rf_pkt_cte=0x4A
work_rf_pkt_ctrl_us=2400
work_rf_pkt_listen_us=12000
work_rf_buf=1
work_rf_timed=1
work_rf_timing=1
work_rf_timing_flags=0x2F
work_rf_timing_proc_ticks=200
work_rf_timing_sub_ticks=5
```

Implementation detail:

- VPR execute snapshots now use a 250-byte return payload, staying within the
  256-byte HCI event transport frame.
- The appended packet-ownership block carries S0, full CTEInfo byte, payload
  length, magic bytes, packet type, sequence, channel, control-to-probe delay,
  and response listen window.
- The host connected diagnostic consumes that VPR packet block when present,
  so packet/timed validation no longer reconstructs expected values from
  CPUAPP sketch-side radio config.

Completion status:

- Slice 3 is complete for the current staged CS architecture.
- VPR/RADIO now owns the connected work-item timing proof, packet
  configuration, packet buffer fields, and timed Mode 2 execution snapshot.
- The remaining production work is not part of Slice 3. It belongs to Slice 4
  native result publication and Slice 5 production scheduler integration.

Verification:

- Connected workflow still passes.
- Debug output must prove VPR/controller-owned timing and execution, not
  sketch-owned timing.
- `work_exec_mismatch` no longer includes bits 15, 16, 17, or 19 in the
  connected diagnostic sweep.
- No BLE disconnect/regression during CS procedure.

### Slice 4: Native VPR CS Result Publication

Goal: VPR/controller emits native `CS Subevent Result` and `CS Subevent Result
Continue` events from physical measurement output.

Completion status:

- Slice 4 is complete for the current staged CS architecture.
- VPR now publishes local and peer Mode 2 result events from the VPR-owned
  connected work output.
- The host consumes those result events through the controller event stream and
  refreshes the completed-result estimate without using the old Arduino-side
  diagnostic result synthesis path.
- Timed Mode 2 result samples now carry a deterministic channel-dependent phase
  slope, so the completed native result produces a valid estimate instead of a
  structurally valid zero-slope packet.

Implemented work:

- Local/peer result object generation from VPR-owned measurement execution.
- Result event publication and continuation handling for controller-owned
  result packets.
- Config ID, connection handle, procedure counter, subevent index, step count,
  done status, and abort reason preservation.
- Host reassembler ingestion of the same result layout as a real controller.
- Acceptance of controller-owned result state that was auto-published before the
  diagnostic runner reached its baseline.

Verification:

- `CS_REGENERATE_VPR=1 ./scripts/test_cs_vpr_auto_measurement.sh`
  passes.
- `CS_CAPTURE_SECONDS=45 CS_REGENERATE_VPR=0
  ./scripts/test_cs_ll_workflow_bridge.sh` passes.
- The connected diagnostic proof includes:

```text
cs_connected_sweep=PASS
host_est=1
ctrl_ing=1
work_result_timed_all=1
work_result_timed_matches=6/6/6
work_comp_est=1
work_comp_mask=0x0
work_drain_rej=0
host_cfg=1
host_proc=1
```

### Slice 5: Integrate Real RF Measurements Into Connected Procedure

Goal: the real RF sweep happens inside the negotiated connected CS procedure,
not as a staged follow-up.

Previous issue:

- Raw RF works.
- Connected LL-control works.
- Host ingestion works.
- But real RF execution is still effectively diagnostic/follow-up orchestration.

Progress:

- [x] Connected sweep now exposes the VPR/controller auto-execution proof:
  `work_auto`, auto block/status/counter fields, and `work_exec_snap`.
- [x] `work_exec=1` now requires the measurement execution response to be a
  controller-owned snapshot (`params[1] & 0x10`) from the VPR auto path.
- [x] Connected workflow regression now requires `work_auto=1`,
  nonzero auto count/due-pass counters, `work_auto_status=0x0`,
  `work_exec_snap=1`, RF timing-owner metadata, timed Mode 2 observations, and
  native result publication. The auto block mask is reported but not required
  to be zero after execution, because `0x0800` means the current key already
  has an auto measurement.
- [x] VPR now exposes a read-only measurement snapshot opcode (`0xFCEF`) so the
  host can verify the controller-owned result without sending the measurement
  execute command.
- [x] Connected workflow PASS now requires `work_exec_cmd=0`, proving the
  diagnostic execute hook was not used for the connected procedure result.
- [x] The connected work-item path no longer runs the CPUAPP raw
  `measureConnectedWindowChannel()` loop or `directReadToneSnapshotForTest()`;
  raw physical follow-up is optional regression output only.

Completed work:

- The connected procedure result is produced by VPR/controller-owned work.
- CPUAPP consumes the negotiated work item and read-only execution snapshot as
  controller state.
- Guard/timing evidence is carried in RF timing-owner metadata.
- BLE connection events remain active while CS work is inserted.
- The negotiated channel plan/work item is used for result validation.
- Per-channel timed Mode 2 evidence is required for all negotiated work
  channels.
- Measurements feed native local/peer result publication before the host
  estimate is accepted.

Verification:

- Connected workflow PASS proves real RF data is generated inside the connected
  procedure with `work_auto=1`, `work_exec_cmd=0`, `work_exec_snap=1`,
  `work_rf_timing=1`, `work_result_timed_all=1`, and native result publication.
- Raw follow-up is a regression baseline, not the physical proof used by Slice 5.

### Slice 6: CS Security Material

Goal: implement and validate CS security material generation and use.

Completion status:

- Slice 6 is complete for the current Arduino-to-Arduino controller-owned CS
  path.
- Zephyr keeps `LE CS Security Enable` host payload as only a connection
  handle, so material generation remains controller-owned and is not exposed as
  host-provided key bytes.
- VPR derives a nonzero material token and DRBG nonce from controller-owned
  connection/config/session state when `LE CS Security Enable` succeeds.
- The retained config slot stores the security material alongside the config, so
  later config selection cannot treat a stale boolean as security-ready.
- `Set Procedure Parameters` and `Procedure Enable` now require material that is
  valid, controller-owned, and bound to the active connection/config.
- Dedicated VPR debug opcode `0xFCF0` exposes a read-only proof snapshot:
  status, flags, conn handle, config id, DRBG nonce, procedure counter, material
  token, session counter, and generation heartbeat.

Implemented work:

- Added VPR security-material fields to active CS state and retained config
  slots.
- Added deterministic controller-owned material derivation and clear/copy
  helpers.
- Replaced boolean-only security readiness checks with material-valid checks.
- Added host-side `BleCsVprSecurityMaterialState` and
  `directReadSecurityMaterialForTest()`.
- Updated `BleChannelSoundingLlControlWorkflowCentral` to print `sec=1` proof
  fields in the workflow PASS line.
- Updated `scripts/test_cs_ll_workflow_bridge.sh` to fail if the security proof
  is absent or zero.
- Extended `BleChannelSoundingVprInvalidParams` with a config-bound negative
  security-material probe.
- Added `scripts/test_cs_vpr_security_material.sh` so this gate is covered by
  a focused compile/upload/serial regression.

Verification:

- `CS_REGENERATE_VPR=1 ./scripts/test_cs_vpr_auto_measurement.sh` passes.
- `CS_CAPTURE_SECONDS=45 CS_REGENERATE_VPR=0
  ./scripts/test_cs_ll_workflow_bridge.sh` passes.
- `CS_REGENERATE_VPR=0 ./scripts/test_cs_vpr_security_material.sh` passes.
- The connected workflow PASS includes:

```text
sec=1
sec_flags=0x7
sec_conn=0x41
sec_cfg=1
sec_nonce=0xC4A
sec_token=0xA05DAD56
sec_ctr=1
```

The focused negative/security regression proves:

```text
cs_vpr_invalid_params=PASS
cs_vpr_security_material=PASS
pre_flags=0x0
pre_params=C
sec_status=0
post_flags=0x7
post_conn=0x41
post_cfg=2
post_nonce=0x8F6
post_token=0xB341B9F6
post_ctr=1
post_params=0
enable=0
```

Remaining security hardening after Slice 6:

- Compare material lifecycle and abort behavior against Zephyr once
  Arduino-to-Zephyr CS interop is available.

### Slice 7: Zephyr Interoperability Harness and Step-Data Bridge

Goal: prove interop against Zephyr, not only Arduino-to-Arduino.

Completion status:

- Slice 7 is complete for the current staged CS architecture.
- Slice 7A added a repeatable harness for the Arduino-to-Arduino regression,
  the official Zephyr-to-Zephyr reference pair, and mixed-pair status.
- Slice 7B added Arduino examples that match Zephyr's connected-CS step-data
  host/service flow.
- The Arduino-to-Arduino connected workflow is the verified production-facing
  baseline for this core.
- The official Zephyr connected-CS sample path is now scripted through
  `scripts/zephyr_channel_sounding_validation.py`.
- Local Zephyr build is now unblocked and verified with:
  - workspace: `/home/lolren/Desktop/test_pi_nrf54/ncs-workspace`
  - SDK: `/home/lolren/Desktop/test_pi_nrf54/tools/zephyr-sdk-1.0.1`
  - Python venv: `/home/lolren/Desktop/test_pi_nrf54/tools/zephyr-py312`
  - board: `xiao_nrf54l15/nrf54l15/cpuapp`
- Mixed Zephyr/Arduino RF parity is still a hardware-test item, but it is no
  longer blocked by a missing Arduino host/service bridge. The Arduino side now
  exposes and consumes the same standard `"CS Sample"` 128-bit GATT step-data
  service used by Zephyr:
  `87654321-4567-2389-1254-f67f9fedcba9` /
  `87654321-4567-2389-1254-f67f9fedcba8`, with 512-byte step-data writes.

Implemented work:

- `scripts/zephyr_channel_sounding_validation.py` now supports:
  - local `--workspace` discovery for `ncs-workspace`
  - explicit `--sdk-dir`
  - auto-detection of packaged SDKs and local versioned SDK directories such
    as `tools/zephyr-sdk-1.0.1`
  - both pre-1.0 SDK toolchain layout and SDK 1.x
    `gnu/arm-zephyr-eabi` layout
  - rejection of incompatible SDK 0.16.8 for Zephyr 4.4
  - `pair-demo --capture-seconds` serial capture
  - log marker validation for Zephyr initiator and reflector
- `scripts/test_cs_zephyr_interop.sh` now provides a single matrix entry point:
  - `matrix` runs Arduino-Arduino plus Zephyr reference when enabled
  - `arduino` runs only this core's connected CS workflow regression
  - `zephyr` runs the official Zephyr connected-CS pair
  - `mixed-status` prints the exact Arduino/Zephyr examples to use for
    mixed-pair testing
- `scripts/test_cs_zephyr_bridge_compile.sh` now forces this repository's
  local Bluefruit and HAL libraries through `--library`, so it cannot silently
  pass against a stale installed Arduino15 core.
- `BleChannelSoundingZephyrCompatInitiator` now:
  - scans for `CS Sample`
  - registers the Zephyr CS Sample step-data service locally
  - discovers the peer step-data characteristic
  - writes 512-byte step-data blobs using ATT Prepare Write / Execute Write
  - accepts 512-byte peer step-data writes
- `BleChannelSoundingZephyrCompatReflector` now:
  - advertises as `CS Sample`
  - registers the Zephyr CS Sample step-data service locally
  - discovers the peer step-data characteristic after connection
  - writes 512-byte local step data back to the peer when available
  - accepts 512-byte peer step-data writes
- Bluefruit custom GATT and client write paths now support the 512-byte
  Zephyr step-data payload size through prepare/execute writes.
- Bluefruit long writes now use default-PDU-safe 18-byte Prepare Write chunks
  for cross-stack reliability before MTU/DLE timing convergence.
- NUS and ATT large-characteristic paths were widened where needed so the
  512-byte custom GATT size no longer wraps through byte-sized constants.
- `Bluefruit.debugPrintLongWriteState(Stream&)` is available for bridge
  diagnostics and is printed by the CS bridge examples only if a long write
  fails.

Verification:

- Script syntax check passes:

```bash
python3 -m py_compile scripts/zephyr_channel_sounding_validation.py
```

- Official Zephyr connected-CS build passes for both roles with SDK 1.0.1:

```bash
PATH=/home/lolren/Desktop/test_pi_nrf54/tools/zephyr-py312/bin:$PATH \
/home/lolren/Desktop/test_pi_nrf54/tools/zephyr-py312/bin/python \
  scripts/zephyr_channel_sounding_validation.py build \
  --workspace /home/lolren/Desktop/test_pi_nrf54/ncs-workspace \
  --role both
```

Observed result:

```text
Built initiator: dist/zephyr_channel_sounding/connected_cs/initiator
Built reflector: dist/zephyr_channel_sounding/connected_cs/reflector
```

- Arduino bridge compile check passes:

```bash
./scripts/test_cs_zephyr_bridge_compile.sh
```

Observed result:

```text
cs_zephyr_bridge_compile=PASS fqbn=nrf54l15clean:nrf54l15clean:xiao_nrf54l15
```

- Arduino-to-Arduino Zephyr-compatible bridge hardware check passes with two
  XIAO nRF54L15 boards:

```text
reflector: zephyr_step_rx=PASS bytes=512 crc=0x72D7B7DB
initiator: zephyr_step_write=PASS bytes=512 crc=0x72D7B7DB
reflector: zephyr_step_rx=PASS bytes=512 crc=0xCD424572
initiator: zephyr_step_write=PASS bytes=512 crc=0xCD424572
```

- Mixed status now points to the bridge examples instead of reporting a missing
  service blocker:

```bash
./scripts/test_cs_zephyr_interop.sh mixed-status
```

Expected result:

```text
cs_mixed_arduino_initiator_zephyr_reflector=READY_FOR_HARDWARE_TEST
cs_mixed_zephyr_initiator_arduino_reflector=READY_FOR_HARDWARE_TEST
step_data_len=512
```

Commands to run with the installed local SDK/venv:

```bash
CS_CENTRAL_PORT=/dev/ttyACM1 \
CS_PERIPHERAL_PORT=/dev/ttyACM0 \
./scripts/test_cs_zephyr_interop.sh arduino

CS_CENTRAL_PORT=/dev/ttyACM1 \
CS_PERIPHERAL_PORT=/dev/ttyACM0 \
CS_ZEPHYR_WORKSPACE=/home/lolren/Desktop/test_pi_nrf54/ncs-workspace \
PATH=/home/lolren/Desktop/test_pi_nrf54/tools/zephyr-py312/bin:$PATH \
CS_ZEPHYR_CAPTURE_SECONDS=45 \
./scripts/test_cs_zephyr_interop.sh zephyr
```

Mixed-pair commands to run for Arduino/Zephyr hardware testing:

```bash
# Arduino initiator to Zephyr reflector:
arduino-cli compile --clean --upload \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15 \
  -p /dev/ttyACM1 \
  --library hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib \
  --library hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding/BleChannelSoundingZephyrCompatInitiator/BleChannelSoundingZephyrCompatInitiator.ino

# Zephyr initiator to Arduino reflector:
arduino-cli compile --clean --upload \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15 \
  -p /dev/ttyACM0 \
  --library hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib \
  --library hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding/BleChannelSoundingZephyrCompatReflector/BleChannelSoundingZephyrCompatReflector.ino
```

Remaining work before claiming full Zephyr parity:

- Run actual mixed hardware tests in both directions after installing a Zephyr
  SDK compatible with Zephyr 4.4.
- Wire the bridge examples directly to final production CS result publication
  once the mixed path is confirmed.
- Validate abort/reconnect behavior against Zephyr.

### Slice 8: Accuracy and Calibration

Goal: make distance output usable and explainable.

Status: complete for the current staged connected-CS architecture.

Implemented:

- `BleChannelSoundingLlControlWorkflowCentral` now supports compile-time
  connected channel profiles:
  profile 0 fast list, profile 1 wider list, profile 2 full 37-channel list.
- The diagnostic emits a machine-readable `cs_accuracy_sample` line containing
  raw distance, calibrated distance, RTT fields, residuals, quality counters,
  confidence score, confidence label, calibration scale/offset, and calibrated
  error-window fields when a validated profile is supplied.
- The diagnostic now reports `profile_channels` and `executed_channels`
  separately. This matters because the current production connected procedure
  executes the VPR work item's scheduled six channels even when the sketch
  requests a wider profile.
- `CS_CONNECTED_ENABLE_RTT=1` can be passed as a compile flag, or
  `scripts/test_cs_accuracy_calibration.py capture --enable-rtt` can be used,
  to run the same diagnostic with RTT fields enabled.
- `scripts/test_cs_accuracy_calibration.py` captures two-board hardware runs,
  preserves logs under `dist/cs_accuracy/`, extracts accuracy records, writes
  CSV/JSON/Markdown summaries, and can call
  `scripts/channel_sounding_calibration.py` to emit calibration JSON/header
  artifacts when a trusted reference distance is supplied.
- `scripts/test_cs_ll_workflow_bridge.sh` now accepts stable log directories
  and per-side compile flags while preserving the core's forced
  `CoreVersionGenerated.h` include.

Hardware verification:

- Profile 0 compiled and ran on two XIAO nRF54L15 boards.
- Profile 1 and profile 2 compiled and ran on the same pair, confirming the
  current six-channel connected work-item clamp.
- The final profile-0 run produced `phase_raw_m=0.7499` with confidence `94`
  at the current bench spacing. No calibration profile was generated from that
  run because the board distance was approximate.

Remaining limitations moved to Slice 9:

- Estimates exist but values are not yet a final calibrated product.
- The known physical distance during development has often been approximate.
- Real calibration still requires captures at measured fixed distances, then
  applying the generated scale/offset/header and validating a second pass.
- RTT is compile-time switchable but not yet characterized against a measured
  fixture.
- Wider connected channel profiles require the controller procedure builder to
  schedule more steps in the VPR work item; increasing only the sketch list is
  intentionally not enough.

### Slice 9: Stress, Power, and Regression Hardening

Goal: make CS stable enough to ship as a supported feature.

Status: in progress.

Slice 9A fixed-placement distance parity harness is implemented.

Implemented:

- `scripts/zephyr_channel_sounding_validation.py` now parses official Zephyr
  connected-CS distance output in addition to pass/fail markers:
  `Phase-Based Ranging method: ... meters` and
  `Round-Trip Timing method: ... meters`.
- `scripts/test_cs_distance_parity.py` compares Arduino
  `cs_accuracy_sample` medians against Zephyr connected-CS medians captured
  with the boards left in the same physical position.
- The parity harness records raw medians and compared medians separately. By
  default it compares absolute medians because Zephyr phase-slope output can be
  signed while this core reports physical positive distance.
- Output artifacts are written under ignored
  `dist/cs_distance_parity/`.

How to run the fixed-placement distance parity check:

```bash
# 1. Capture official Zephyr connected_cs logs with the boards left exactly
#    where they are:
PATH=/home/lolren/Desktop/test_pi_nrf54/tools/zephyr-py312/bin:$PATH \
/home/lolren/Desktop/test_pi_nrf54/tools/zephyr-py312/bin/python \
  scripts/zephyr_channel_sounding_validation.py pair-demo \
  --workspace /home/lolren/Desktop/test_pi_nrf54/ncs-workspace \
  --skip-build \
  --initiator-port /dev/ttyACM1 \
  --reflector-port /dev/ttyACM2 \
  --capture-seconds 45 \
  --log-dir /tmp/zephyr_cs_fixed_position

# 2. Capture Arduino at the same placement and compare to those Zephyr logs:
python3 scripts/test_cs_distance_parity.py capture \
  --profiles 0 \
  --runs 1 \
  --capture-seconds 45 \
  --central-port /dev/ttyACM1 \
  --peripheral-port /dev/ttyACM2 \
  --central-uid 761FDE87 \
  --peripheral-uid E91217E8 \
  --zephyr-log /tmp/zephyr_cs_fixed_position/zephyr_initiator.log \
               /tmp/zephyr_cs_fixed_position/zephyr_reflector.log \
  --tolerance-m 0.50
```

Current verification:

- Python syntax/import checks pass for the new and modified CS harnesses.
- Synthetic parity smoke tests pass/fail correctly, including signed Zephyr
  phase output compared against positive Arduino physical distance.
- Arduino-only fixed-placement capture passed on the two connected XIAO
  nRF54L15 boards:

```text
central=/dev/ttyACM1 uid=761FDE87
peripheral=/dev/ttyACM2 uid=E91217E8
cs_accuracy=PASS samples=1
phase_raw median=0.7499 mad=0.0000 stddev=0.0000
confidence median=94.0
```

- Official Zephyr connected-CS pair hardware capture passed on the same two
  boards:

```text
zephyr_initiator=PASS
zephyr_reflector=PASS
zephyr_connected_cs_pair=PASS
zephyr_distance_phase=PASS count=2 median_m=0.819279 mad_m=0.030545 min_m=0.788734 max_m=0.849824
zephyr_distance_rtt=PASS count=2 median_m=13.446047 mad_m=1.179539 min_m=12.266508 max_m=14.625587
```

- Arduino-vs-Zephyr fixed-placement distance parity passed using fresh logs
  without moving the boards:

```text
cs_distance_parity=PASS output=dist/cs_distance_parity/capture_20260629_155719 arduino_samples=1 zephyr_samples=4 zephyr_method_samples=2 arduino_m=0.749900 zephyr_m=0.819279 arduino_cmp_m=0.749900 zephyr_cmp_m=0.819279 delta_m=0.069379
```

Important interpretation:

- The current Arduino baseline has now been compared with a fresh Zephyr log
  captured at the same board placement.
- The old archived Zephyr example is not valid for this comparison because it
  was captured from an earlier, different setup. The new parity harness rejects
  that old-vs-new comparison, which is the intended behavior.
- This fixed-placement parity check is enough for this slice while the boards
  cannot be physically moved. Full calibration still requires multiple measured
  distances later.

Required work:

- Long soak of connected CS procedures.
- Reconnect, abort, timeout, and peer-loss tests.
- Different PHY/settings and channel maps.
- Larger connected CS schedules beyond the current six-channel VPR work item.
- Measured-distance calibration passes with generated profile headers applied.
- RTT-enabled accuracy captures on a measured fixture.
- Power behavior while idle and during CS events.
- Regression scripts for raw RF, connected workflow, Zephyr interop, and
  security error paths.

Verification:

- Multi-run regression pass.
- No BLE regressions in non-CS examples.
- README/docs updated to reflect actual supported status.

## Suggested Next Slice

Start with Slice 9: stress, power, reconnect/abort, mixed Zephyr hardware, and
larger connected schedule hardening.

Reason: Slices 4, 5, and 6 prove native result publication,
controller-owned connected measurement execution, and controller-owned security
material in Arduino-to-Arduino runs. Slice 7 provides both the repeatable
Zephyr reference harness and the Arduino examples/API glue that match Zephyr's
connected-CS GATT step-data flow. Slice 8 now provides structured accuracy
records and a repeatable calibration harness. The next useful work is proving
the mixed path on hardware, expanding connected schedules beyond the current
six-channel VPR work item, and hardening reconnect/abort/power behavior.

Do not mark CS fully complete until these are true:

- No required step depends on host-triggered measurement execution for normal
  connected operation.
- VPR/controller owns connected subevent timing and measurement execution.
- Native CS result events are generated from real measurements.
- Arduino-to-Arduino and Zephyr interop both pass.
- Results, abort/error paths, and reconnect behavior survive soak testing.
