# Thread and Matter Next-AI Handover - 2026-06-04

This is the current handover for continuing Thread and Matter work in
`/home/lolren/Desktop/test_pi_nrf54/NRF54L15-Clean-Arduino-core`.

The immediate goal is not to claim production Thread or Matter. The immediate
goal is to turn the staged implementation into repeatable evidence, then close
the remaining runtime gaps in the right order.

## Current State

- The local tree contains staged OpenThread source wrappers for Commissioner,
  Joiner, CoAP Secure, secure transport, crypto storage, mbedTLS-backed crypto,
  MeshCoP commissioner/joiner/seeker, and related support files.
- The local tree contains `mbedtls_stage` plus a broader set of mbedTLS headers
  and sources under the OpenThread third-party tree.
- `Nrf54ThreadExperimental` exposes staged MeshCoP Joiner/Commissioner APIs,
  dataset export/import, restore diagnostics, attach diagnostics, and Thread
  command-surface helpers.
- `MatterPlatform` exposes Thread dataset source/readiness handling and
  Preferences-backed factory-data persistence. This is a foundation seam, not
  production CHIP commissioning.
- Matter staged PASE/CASE examples are Arduino demo traffic over Thread UDP.
  They are useful validation traffic, but they are not upstream CHIP PASE/CASE
  commissioning yet.
- Do not touch Hardware Serial or USB Serial internals while continuing this
  work. Serial regressions were a major source of breakage earlier.

## Important Paths

- Thread examples:
  `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread`
- Matter examples:
  `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter`
- Thread wrapper:
  `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54_thread_experimental.h`
- OpenThread platform glue:
  `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread_platform_nrf54l15.*`
- Matter platform seam:
  `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_platform_stage.*`
- MeshCoP validation harness:
  `scripts/thread_meshcop_validation.py`

## Build Method

Use the local sketchbook symlink method so Arduino CLI builds this repo, not the
installed Boards Manager package:

```bash
rm -rf /tmp/nrf54-thread-meshcop-sketchbook
mkdir -p /tmp/nrf54-thread-meshcop-sketchbook/hardware
ln -s /home/lolren/Desktop/test_pi_nrf54/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean \
  /tmp/nrf54-thread-meshcop-sketchbook/hardware/nrf54l15clean
ARDUINO_DIRECTORIES_USER=/tmp/nrf54-thread-meshcop-sketchbook arduino-cli core list
```

Default Thread FQBN:

```text
nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage
```

Default Matter staged FQBN:

```text
nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage
```

## New Slice Added In This Pass

Added `scripts/thread_meshcop_validation.py`.

It compiles the four staged MeshCoP proof sketches against this checkout:

- `ThreadExperimentalCommissioner`
- `ThreadExperimentalJoiner`
- `ThreadExperimentalMeshcopRestoreProbe`
- `ThreadExperimentalMeshcopWrongPskdJoiner`

It can also upload to two boards, capture serial, and check pass/fail markers
for:

- fresh clean MeshCoP join
- restored commissioned dataset after flashing the restore-only probe
- wrong-PSKd negative Joiner failure without dataset persistence

Compile-only gate:

```bash
python3 scripts/thread_meshcop_validation.py compile
```

Full two-board gate:

```bash
python3 scripts/thread_meshcop_validation.py all \
  --commissioner-port /dev/ttyACM0 \
  --joiner-port /dev/ttyACM1 \
  --timeout 180 \
  --dump-lines
```

Logs are written under:

```text
build/thread-meshcop-validation/<timestamp>/
```

If the hardware capture phase is used, `pyserial` must be installed in the
host Python environment.

## Pass Markers The Harness Watches

Fresh join:

- Commissioner: `JOINER_ACCEPTED`, `meshcop_joiner_finalize_callback=1`, or
  `meshcop_finalize_seen=<nonzero>`
- Joiner: `JOIN_SUCCESS`, `meshcop_joiner_callback_success=1`, or
  `joiner_complete=1`
- Joiner clean-start proof: `preexisting_dataset_before_joiner=0`
- Fail markers: `FATAL`, `preexisting_dataset_before_joiner=1`, `JOIN_FAILED`

Restore:

- `restore_attempted=1`
- `restore_restored=1`
- `dataset_configured=1`
- Fail markers: `FATAL`, `begin=0`

Wrong PSKd:

- `expected_join_failure=1`
- `before_joiner_active_dataset=0`
- Fail markers: `unexpected_join_success=1`, `active_dataset=1`, `FATAL`

## Remaining Thread Work

### Thread Slice 1: Make MeshCoP Evidence Repeatable

Run the new harness with two boards and clean settings. Do not count one lucky
run as complete.

Required before ticking:

- Three complete `thread_meshcop_validation.py all` passes.
- Saved logs from each pass.
- Joiner proves clean settings before Joiner start.
- Restore probe proves commissioned dataset survives reflashing.
- Wrong-PSKd probe proves failed Joiner does not persist a dataset.

If this fails:

- Inspect `OpenThreadPlatformSkeletonSnapshot` counters in the sketch logs.
- Check `radioReceiveAt*`, `radioRxDoneCount`, `radioTxDoneCount`, `last_err`,
  `last_rx_len`, `last_tx_len`, recent OT logs, and recent MLE logs.
- Do not work around failures by preloading a dataset in the Joiner sketch.

### Thread Slice 2: Reference Network Attach

Attach to a real Thread network through an external border router or OTBR.

Required:

- Generate or obtain a valid Active Operational Dataset TLV hex.
- Use `ThreadExperimentalCommandSurface`.
- Apply dataset with `scripts/thread_command_surface_attach_probe.py`.
- Confirm attach as child/router, stable partition, stable RLOC16, and no
  repeated parent churn.

Command shape:

```bash
python3 scripts/thread_command_surface_attach_probe.py \
  --port /dev/ttyACM0 \
  --dataset-hex <ACTIVE_DATASET_TLV_HEX> \
  --wipe-settings \
  --timeout-s 90 \
  --dump-lines
```

### Thread Slice 3: UDP Reliability And Fragmentation

The old notes mention fragmented UDP as an expected failure. Matter will need
larger reliable payloads, so this must be closed.

Required:

- Sweep payload sizes from small frames through the maximum intended Matter
  message size.
- Test unicast both directions.
- Test repeated attach/reconnect.
- Track retry counts, ACK counts, loss counts, CRC failures, invalid-length
  failures, and radio state transitions.

### Thread Slice 4: Sleepy Device Behavior

Current support is staged and mostly always-on. Do not claim low-power Thread
until sleepy behavior is explicitly implemented and measured.

Required:

- Define whether SED/MED/CSL is in scope for this core.
- Implement the smallest correct sleepy end-device path first.
- Prove polling interval, parent retention, reattach, and current consumption.
- Ensure Zigbee/802.15.4 behavior still works because the radio backend is
  shared.

## Remaining Matter Work

### Matter Slice 1: Define The Real CHIP Runtime Boundary

The current Matter examples are staged Arduino traffic. Before implementing
more protocol logic, decide exactly where upstream CHIP owns the runtime.

Required:

- Document which CHIP components are compiled as source.
- Document which platform layers are repo-owned.
- Avoid duplicating CHIP secure-session or Interaction Model logic in sketches.
- Keep CPUAPP as the first runtime owner unless there is a concrete reason to
  move work to VPR.

### Matter Slice 2: CHIP CryptoPAL And TLS/Session Integration

Current staged crypto is useful, but production Matter needs the upstream CHIP
secure-session path.

Required:

- Wire CHIP CryptoPAL to the available software ECC and CRACEN RNG paths.
- Keep hardware ECC claims out unless Nordic microcode support is actually
  available and tested.
- Verify SPAKE2+, PBKDF2, HKDF, SHA256, HMAC, AES-CCM, and P-256 vectors.
- Add explicit timing notes because software P-256 is slower than hardware ECC.

TLS guidance:

- Do not invent a sketch-local TLS stack.
- Use upstream CHIP secure sessions for Matter.
- Use OpenThread/mBedTLS only where OpenThread MeshCoP needs DTLS.
- Keep the Matter secure-session path and OpenThread MeshCoP DTLS path separate
  so one cannot regress the other.

### Matter Slice 3: CHIP SystemLayer And Event Loop

Matter needs timers, event dispatch, packet processing, and Thread background
processing to coexist.

Required:

- Integrate CHIP SystemLayer timers without busy-spinning.
- Ensure `nrf54l15_clean_idle_service()` and Thread processing stay cooperative.
- Verify no Serial dependency.
- Verify no long blocking section that breaks radio receive windows.

### Matter Slice 4: Inet/UDP Transport Over Thread

Required:

- Use OpenThread UDP as the Matter transport.
- Prove inbound and outbound IPv6 UDP paths.
- Prove fragmented packets if Matter traffic requires them.
- Verify the Thread node stays attached during Matter traffic.

### Matter Slice 5: PASE Commissioning Window

Required:

- Implement real CHIP commissioning window state.
- Publish correct onboarding payloads.
- Support timeout/close/reopen behavior.
- Validate with a real commissioner, not only another sketch.

### Matter Slice 6: CASE And Operational Credentials

Required:

- Store operational credentials in Preferences or a deliberate storage backend.
- Reboot and reconnect after commissioning.
- Validate failed credential and missing credential cases.

### Matter Slice 7: Interaction Model On/Off Light

Required:

- First production device type should remain a simple on/off light.
- Implement actual Matter attributes and commands through upstream IM.
- Validate control from a real Matter controller.

### Matter Slice 8: Discovery, SRP, mDNS, And Home Assistant

Required:

- Make the node discoverable by a real commissioner/controller.
- Publish correct SRP/mDNS service records for Matter over Thread.
- Validate Home Assistant commissioning and control.
- Validate reboot recovery after Home Assistant commissioning.

## Testing Rules For The Next AI

- After each major implementation slice, compile all touched examples.
- After Thread radio/backend changes, rerun Zigbee/802.15.4 smoke tests because
  the same radio path is shared.
- After Matter platform changes, rerun all Matter staged example compiles.
- After MeshCoP changes, rerun `scripts/thread_meshcop_validation.py compile`
  and then the two-board hardware harness.
- Do not update marketing/readme claims until the matching validation evidence
  exists.
- Do not touch Serial internals while doing this work.
- Do not change BLE/power timing unless the test explicitly requires BLE or
  coexistence.

## Current Honest Claim

Thread: staged and promising, not production.

Matter: foundation/staged, not production commissioning.

Best next move: run the new MeshCoP harness on two boards, fix any real failures
there, then move to reference Thread network attach. Only after Thread attach
and UDP reliability are stable should the next AI spend serious time wiring the
real CHIP Matter runtime.
