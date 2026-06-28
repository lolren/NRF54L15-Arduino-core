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
████████████████████████░░░░░░░░  72%
done                 remaining
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

The older `87%` tracker is too optimistic for "full working CS". It mostly
counts support infrastructure. The current stricter estimate is approximately
72% complete after the VPR measurement execution refactor and first
controller-owned auto-execution pass.

## Current Verified Baseline

Repository state used for this status:

```text
base commit: 6007ac20 cs: refactor VPR measurement execution worker
branch: main
worktree: Slice 2 VPR controller-owned auto measurement execution applied
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
work_result_timed_all=1
work_result_timed_matches=6/6/6
work_timed_obs=6:2,3,4,5,6,7
cs_connected_sweep=PASS
cs_ll_physical_followup=PASS
physical reflector replies=25
```

This proves the current implementation can move real-shaped CS LL-control PDUs
over a real BLE link, select work from VPR state, execute real RF primitives,
capture nonzero DFE data, and feed real Mode 2 measurements into the host result
path. It does not yet prove full Zephyr parity because final ownership is still
split between VPR/controller state and CPUAPP diagnostic orchestration.

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
- Connected diagnostic flow showing LL-control negotiation plus real RF
  follow-up can work in one two-board run.

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

Estimated remaining work: 8 focused slices.

Realistic session count: 8 to 13 sessions. The risk is concentrated in slices
2-5 because they touch VPR firmware size, RADIO ownership, connection-event
timing, and BLE coexistence.

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

Current caveat:

- `scripts/test_cs_ll_workflow_bridge.sh` still uses the existing bridge
  diagnostic sequence, which sends `CS_ABORT` before the later connected sweep.
  That regression proves compatibility and RF execution still pass, but the next
  test improvement should assert the new `0x80`/`0x10` flags before aborting.

Verification:

- `scripts/test_cs_ll_workflow_bridge.sh` passes with two boards after this
  change.
- `git diff --check` passes.
- Remaining verification: add or run a focused no-host-execute workflow that
  waits after `CS_START`, reads measurement work, and proves the `0x80` auto flag
  plus `0x10` execution-snapshot flag without calling the diagnostic execute
  hook first.

### Slice 3: Move Connected Timing Ownership Into VPR/RADIO

Goal: VPR/RADIO owns the timed measurement window relative to BLE connection
events.

Current issue:

- CPUAPP currently plans and waits for connection-event timing.
- This is useful for diagnostics, but it is not Zephyr-like controller
  behavior.

Required work:

- Consume connection timing snapshots in the controller path.
- Schedule CS subevents with guard-before and guard-after timing.
- Keep BLE connection events stable while CS work is inserted.
- Avoid long CPUAPP busy loops.

Verification:

- Connected workflow still passes.
- Debug output must prove VPR/controller-owned timing, not sketch-owned timing.
- No BLE disconnect/regression during CS procedure.

### Slice 4: Native VPR CS Result Publication

Goal: VPR/controller emits native `CS Subevent Result` and `CS Subevent Result
Continue` events from physical measurement output.

Current issue:

- The host has result serializers and ingestion paths.
- The connected diagnostic can preserve VPR completed results and attach an
  estimate.
- But final native result generation from controller-owned physical execution is
  not complete.

Required work:

- Generate local and peer Mode 2 result objects from VPR-owned measurement
  execution.
- Fragment result events correctly.
- Preserve config ID, connection handle, procedure counter, subevent index, step
  count, done status, and abort reasons.
- Ensure host reassembler receives the same layout as a real controller.

Verification:

- Connected workflow PASS must prove native result publication.
- `work_result_timed_all=1` must remain true.
- Result counters must increment through the controller event stream.

### Slice 5: Integrate Real RF Measurements Into Connected Procedure

Goal: the real RF sweep happens inside the negotiated connected CS procedure,
not as a staged follow-up.

Current issue:

- Raw RF works.
- Connected LL-control works.
- Host ingestion works.
- But real RF execution is still effectively diagnostic/follow-up orchestration.

Required work:

- Move the raw Mode 2 measurement primitive under the connected procedure
  execution path.
- Use the negotiated channel plan/work item.
- Keep per-channel evidence for all negotiated work channels.
- Feed the resulting measurements into native result publication.

Verification:

- Connected workflow PASS must prove real RF data is generated inside the
  connected procedure.
- Raw follow-up should become a regression baseline, not the only physical proof.

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

Start with a focused Slice 2 verification follow-up, then move to Slice 3.

Reason: VPR now owns first-pass measurement execution, but the existing bridge
regression aborts before it can prove the new debug flags. Lock that proof down
before moving more timing ownership into VPR/RADIO.

Do not mark CS fully complete until these are true:

- No required step depends on host-triggered measurement execution for normal
  connected operation.
- VPR/controller owns connected subevent timing and measurement execution.
- Native CS result events are generated from real measurements.
- Arduino-to-Arduino and Zephyr interop both pass.
- Results, abort/error paths, and reconnect behavior survive soak testing.
