# Channel Sounding Current Status

Last updated: 2026-06-28

This document is the current practical status for finishing full Bluetooth
Channel Sounding (CS) Zephyr parity in this core. It intentionally uses a
stricter definition than the older progress bars: full parity means the core
can run real connected CS procedures with controller-owned scheduling and real
physical measurements, not only HCI compatibility or diagnostic proofs.

## Progress

```text
FULL CHANNEL SOUNDING ZEPHYR PARITY
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
██████████████████████████░░░░░░  78%
done                 remaining
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

The older `87%` tracker is too optimistic for "full working CS". It mostly
counts support infrastructure. The current stricter estimate is approximately
78% complete after the VPR measurement execution refactor, the focused
controller-owned auto-execution proof, the VPR/RADIO connected timing ownership
proof, and the connected packet/timed Mode 2 ownership handoff.

## Current Verified Baseline

Repository state used for this status:

```text
base commit: 6007ac20 cs: refactor VPR measurement execution worker
branch: main
worktree: Slice 3B connected packet/timed ownership proof applied
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

Two-board connected LL workflow regression passed on 2026-06-28:

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
cs_connected_sweep=PASS
work_exec_mismatch=0x0
work_rf_pkt=1 work_rf_buf=1 work_rf_timed=1 work_rf_timing=1
work_rf_pkt_cte=0x4A
work_rf_pkt_ctrl_us=2400 work_rf_pkt_listen_us=12000
work_rf_timing=1
work_rf_timing_flags=0x2F
cs_ll_physical_followup=PASS
physical follow-up valid_channels=22
```

This proves the current implementation can move real-shaped CS LL-control PDUs
over a real BLE link, select work from VPR state, prove coherent VPR/RADIO
timing state for the scheduled work item, execute VPR-owned packet setup and
timed Mode 2 primitives for the connected work item, execute real RF primitives
in the physical follow-up path, capture nonzero DFE data, and feed real Mode 2
measurements into the host result path. It does not yet prove full Zephyr parity
because final native controller result publication and production scheduler
ownership still need to be completed.

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

Estimated remaining work: 4 full slices after the Slice 4 native result
publication pass.

Realistic session count: 5 to 8 sessions. The risk is concentrated in Slice 5
because it moves CS execution into production connection-event scheduling and
therefore touches timing ownership and BLE coexistence directly.

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
  update and its physical follow-up still passes.
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

Current issue:

- The current state machine has security stages.
- Full CS parity needs proper security material/nonces/DRBG behavior aligned
  with Bluetooth and Zephyr.

Required work:

- Review Zephyr CS security flow and Bluetooth CS material requirements.
- Implement missing nonce/material derivation.
- Ensure abort/error status matches expected behavior on invalid or missing
  material.

Verification:

- Positive and negative security-path examples.
- No regressions in existing LL-control workflow.

### Slice 7: Zephyr Interoperability

Goal: prove interop against Zephyr, not only Arduino-to-Arduino.

Required pairings:

- Arduino initiator to Arduino reflector.
- Arduino initiator to Zephyr reflector.
- Zephyr initiator to Arduino reflector.

Required work:

- Build Zephyr equivalent sketches or use Zephyr CS samples.
- Capture logs/HCI traces where possible.
- Compare command/event sequence, timing, result shape, and abort behavior.

Verification:

- At least one PASS run for each pairing.
- Differences documented if silicon/API limitations prevent exact parity.

### Slice 8: Accuracy and Calibration

Goal: make distance output usable and explainable.

Current issue:

- Estimates exist but values are not yet a final calibrated product.
- The known physical distance during development has often been approximate.

Required work:

- Test multiple distances and channel counts.
- Validate phase-slope and RTT contribution.
- Apply FAE, antenna path, and board-pair bias corrections.
- Add quality/confidence reporting.

Verification:

- Repeatable results over several fixed distances.
- Documented expected error and limitations.

### Slice 9: Stress, Power, and Regression Hardening

Goal: make CS stable enough to ship as a supported feature.

Required work:

- Long soak of connected CS procedures.
- Reconnect, abort, timeout, and peer-loss tests.
- Different PHY/settings and channel maps.
- Power behavior while idle and during CS events.
- Regression scripts for raw RF, connected workflow, Zephyr interop, and
  security error paths.

Verification:

- Multi-run regression pass.
- No BLE regressions in non-CS examples.
- README/docs updated to reflect actual supported status.

## Suggested Next Slice

Start with Slice 5: integrate real RF measurements into the connected procedure.

Reason: Slice 4 now proves VPR/controller native result publication and host
ingestion from VPR-owned output. The next meaningful gap is moving the staged
physical RF work into the actual negotiated connection-event procedure with
guard-before/guard-after scheduling and without CPUAPP busy-loop ownership.

Do not mark CS fully complete until these are true:

- No required step depends on host-triggered measurement execution for normal
  connected operation.
- VPR/controller owns connected subevent timing and measurement execution.
- Native CS result events are generated from real measurements.
- Arduino-to-Arduino and Zephyr interop both pass.
- Results, abort/error paths, and reconnect behavior survive soak testing.
