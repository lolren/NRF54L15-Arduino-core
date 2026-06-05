# Thread and Matter Handover - 2026-06-05

Working tree:

```text
/home/lolren/Desktop/test_pi_nrf54/NRF54L15-Clean-Arduino-core
```

This handover supersedes the older Thread/Matter notes for the current local
state. Older files are still useful as history, but several of them describe
failures that are now either fixed or were caused by stale validation logic.

Do not touch Hardware Serial or USB Serial while continuing this work unless the
task is explicitly about Serial. Serial regressions have repeatedly broken
unrelated BLE and diagnostic examples.

## Current Claim Level

Thread stage mode is now past the first meaningful MeshCoP hardware gate:

- Four staged MeshCoP examples compile against the local checkout.
- Fresh clean Joiner commissioning works on two XIAO nRF54L15 boards.
- Commissioner sees MeshCoP finalize and `JOINER_ACCEPTED`.
- Joiner receives the active dataset through standard MeshCoP/DTLS, persists it,
  and attaches as a child.
- Restore probe reflashed onto the Joiner board restores the commissioned
  dataset from settings and attaches again.
- Wrong-PSKd Joiner fails as expected and does not persist an active dataset.

This does not mean full production Thread is complete. It means the earlier
"DTLS does not start / Joiner cannot complete MeshCoP" blocker is no longer the
current blocker in this checkout.

Matter is still staged. Current Matter examples are Arduino proof traffic and
platform seams; they are not upstream CHIP commissioning, secure sessions,
Interaction Model, mDNS/SRP, or Home Assistant commissioning.

## Changes Made In This Slice

### 1. OpenThread Time Source Fixed

File:

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread_platform_nrf54l15.cpp
```

Changed the OpenThread platform timing path to use the core monotonic
microsecond clock:

- `otPlatTimeGet()`
- `otPlatAlarmMilliGetNow()`
- `otPlatAlarmMicroGetNow()`
- radio timestamp tracking
- pseudo-entropy time mixing

Reason:

The previous `otPlatTimeGet()` implementation reconstructed time from SysTick
register polling. That is fragile under low-power entry, masked interrupts, and
cooperative radio processing. OpenThread MeshCoP, DTLS retransmission timers,
and MLE attach timers need one consistent monotonic clock.

### 2. Joiner Start Made Idempotent

File:

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54_thread_experimental.cpp
```

`Nrf54ThreadExperimental::startJoiner()` now returns success if the OpenThread
Joiner is already active instead of treating a retry as a failed start.

Reason:

The handoff reported an impossible-looking state: `startJoiner()` false with
`lastError=0`. In the current wrapper, that state should not happen when
`otJoinerStart()` directly returns `OT_ERROR_NONE`, but retrying while the
Joiner is already discovering/connecting can still be surfaced poorly. The
wrapper now treats "already running" as a successful idempotent request.

### 3. Platform Snapshot Exposes Pending Radio Events

Files:

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread_platform_nrf54l15.h
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread_platform_nrf54l15.cpp
```

Added snapshot fields:

- `radioTxDonePending`
- `radioRxDonePending`
- `radioEnergyScanDonePending`
- `radioReceiveAtTimeoutPending`

Reason:

The OpenThread radio backend is cooperative and pending-flag based. When Thread
or Matter stalls, these flags tell whether a PAL event was queued but not
processed. This is useful for serial diagnostics and future SWD/memory snapshot
debugging.

### 4. MeshCoP Validator Fixed

File:

```text
scripts/thread_meshcop_validation.py
```

The wrong-PSKd negative check now accepts current repeated status markers:

- `callback_seen=1` as proof that the failure callback happened.
- `active_dataset=0` as proof that no dataset was persisted, even if serial
  capture missed the early `before_joiner_active_dataset=0` line after upload.

Reason:

The board behavior was correct, but the validator could fail because it opened
serial after upload and missed early boot lines. The validator now matches both
fresh-upload logs and already-running boards.

### 5. Local Build Logs Ignored

File:

```text
.gitignore
```

Added `build/` because the validation harness writes compile/upload/serial logs
there and those should not be committed.

### 6. UDP Soak Validation Made Honest

File:

```text
scripts/test_thread_udp_soak.py
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/ThreadExperimentalUdpSoak/ThreadExperimentalUdpSoak.ino
```

The two-board UDP soak runner now:

- tests the local checkout through a temporary Arduino sketchbook symlink
  instead of silently using the installed Boards Manager package.
- compiles and uploads through `arduino-cli compile --upload`.
- writes compile/upload and serial logs under
  `build/thread-udp-soak-validation/`.
- requires explicit payload-size pass matrices instead of returning success
  when any single unicast payload passes.
- defaults to the currently safe gate:
  unicast `8,16,31,63,95`, multicast `8,16,31,63`.
- exposes `--require-fragmentation` to require the full
  `8,16,31,63,95,127,191,255,512` sweep.
- the example now attempts the full payload list instead of pre-failing
  payloads above the old 95-byte unicast / 80-byte multicast caps.
- the example emits repeated final `soak_result` matrix lines after completion
  so the runner can still validate exact payload sizes if serial capture starts
  after the earliest packet logs.

Reason:

The old runner could report success after one passing packet. That was not a
real reliability gate and could hide fragmentation or multicast regressions.

### 7. Thread UDP Callback No Longer Truncates At 256 Bytes

Files:

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54_thread_experimental.h
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54_thread_experimental.cpp
```

`Nrf54ThreadExperimental::handleUdpReceive()` now copies into a 1280-byte
object-owned UDP receive buffer and drops packets larger than that with
`OT_ERROR_NO_BUFS`. It no longer silently truncates received payloads to a
256-byte stack buffer.

Reason:

A 512-byte UDP fragmentation/reassembly test could never pass through the old
callback path because the packet was truncated before the sketch parsed its
declared length/checksum. Matter will also need larger UDP payload handling, so
this belongs in the wrapper rather than in the example.

## Verified Commands And Evidence

Boards present during validation:

```text
/dev/ttyACM0 - XIAO nRF54L15 / Sense class board
/dev/ttyACM1 - XIAO nRF54L15 / Sense class board
```

Primary validation command:

```bash
python3 scripts/thread_meshcop_validation.py all \
  --commissioner-port /dev/ttyACM0 \
  --joiner-port /dev/ttyACM1 \
  --timeout 75
```

Result:

```text
compile commissioner: PASS
compile joiner: PASS
compile restore: PASS
compile wrong-pskd: PASS
upload commissioner: PASS
upload joiner: PASS
upload restore: PASS
upload wrong-pskd: PASS
fresh MeshCoP join: PASS
MeshCoP restore: PASS
wrong-PSKd MeshCoP negative test: PASS
```

Log directory:

```text
build/thread-meshcop-validation/20260605-193735/
```

Earlier same-session evidence before the validator adjustment:

```text
build/thread-meshcop-validation/20260605-191951/
```

That run showed the same important on-board behavior:

- Joiner printed `JOIN_SUCCESS`.
- Commissioner printed MeshCoP finalize / accepted markers.
- Joiner role became `child`.
- Restore probe restored settings and attached.
- Wrong-PSKd Joiner printed `expected_join_failure=1`,
  `unexpected_success=0`, and `active_dataset=0`.

UDP soak compile check against the local checkout:

```bash
mkdir -p /tmp/nrf54-thread-udp-soak-sketchbook/hardware
ln -sfn /home/lolren/Desktop/test_pi_nrf54/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean \
  /tmp/nrf54-thread-udp-soak-sketchbook/hardware/nrf54l15clean
ARDUINO_DIRECTORIES_USER=/tmp/nrf54-thread-udp-soak-sketchbook \
  arduino-cli compile \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage \
  --build-path build/check-thread-udp-soak-local \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/ThreadExperimentalUdpSoak
```

Result:

```text
Sketch uses 310768 bytes (19%) of program storage space.
Global variables use 40668 bytes (26%) of dynamic memory, leaving 114980 bytes.
```

Two-board UDP safe-size soak:

```bash
python3 scripts/test_thread_udp_soak.py \
  --port1 /dev/ttyACM0 \
  --port2 /dev/ttyACM1 \
  --timeout 180 \
  --dump-lines
```

Result:

```text
Unicast:   8, 16, 31, 63, 95, 127, 191, 255, 512 all pass
Multicast: 8, 16, 31, 63, 95, 127, 191, 255, 512 all pass
Required safe gate: PASS
```

Log directory:

```text
build/thread-udp-soak-validation/20260605-214851/
```

Two-board UDP fragmentation-required parser/gate check against the same
already-flashed boards:

```bash
python3 scripts/test_thread_udp_soak.py \
  --port1 /dev/ttyACM0 \
  --port2 /dev/ttyACM1 \
  --skip-flash \
  --timeout 20 \
  --require-fragmentation \
  --dump-lines
```

Result:

```text
Unicast required:   8, 16, 31, 63, 95, 127, 191, 255, 512 all PASS
Multicast required: 8, 16, 31, 63, 95, 127, 191, 255, 512 all PASS
```

Log directory:

```text
build/thread-udp-soak-validation/20260605-215054/
```

## How To Reproduce

Use the local checkout, not the installed Boards Manager package:

```bash
cd /home/lolren/Desktop/test_pi_nrf54/NRF54L15-Clean-Arduino-core
python3 scripts/thread_meshcop_validation.py compile
```

Two-board hardware validation:

```bash
python3 scripts/thread_meshcop_validation.py all \
  --commissioner-port /dev/ttyACM0 \
  --joiner-port /dev/ttyACM1 \
  --timeout 180 \
  --dump-lines
```

Two-board UDP safe-size soak:

```bash
python3 scripts/test_thread_udp_soak.py \
  --port1 /dev/ttyACM0 \
  --port2 /dev/ttyACM1 \
  --timeout 180 \
  --dump-lines
```

Two-board UDP fragmentation-required soak:

```bash
python3 scripts/test_thread_udp_soak.py \
  --port1 /dev/ttyACM0 \
  --port2 /dev/ttyACM1 \
  --timeout 240 \
  --require-fragmentation \
  --dump-lines
```

Expected safe-size pass matrix:

```text
Unicast required:   8, 16, 31, 63, 95
Multicast required: 8, 16, 31, 63
```

Expected fragmentation pass matrix when this slice is complete:

```text
Unicast required:   8, 16, 31, 63, 95, 127, 191, 255, 512
Multicast required: 8, 16, 31, 63, 95, 127, 191, 255, 512
```

Short no-upload negative-test check when the wrong-PSKd sketches are already
flashed:

```bash
python3 scripts/thread_meshcop_validation.py wrong-pskd \
  --commissioner-port /dev/ttyACM0 \
  --joiner-port /dev/ttyACM1 \
  --skip-upload \
  --timeout 25
```

The validation script creates a temporary Arduino sketchbook symlink under:

```text
/tmp/nrf54-thread-meshcop-sketchbook
```

The default staged Thread FQBN is:

```text
nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage
```

For Matter staged examples:

```text
nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage
```

## Diagnostic Markers To Watch

Fresh MeshCoP join should show:

- Commissioner: `JOINER_ACCEPTED` or `meshcop_joiner_finalize_callback=1`.
- Commissioner: `meshcop_finalize_seen=1`.
- Joiner: `JOIN_SUCCESS` or `meshcop_joiner_callback_success=1`.
- Joiner: `joiner_complete=1`.
- Joiner: `preexisting_dataset_before_joiner=0`.
- Joiner later: `role=child`, non-`0xFFFE` RLOC16, active dataset TLV length
  near 106 after attach.

Restore should show:

- `restore_attempted=1`
- `restore_restored=1`
- `dataset_configured=1`
- `attach_phase=attached`

Wrong-PSKd should show:

- `expected_join_failure=1` or later `callback_seen=1`.
- `unexpected_success=0`.
- `active_dataset=0`.
- no `FATAL`.

Platform/radio counters to inspect during failures:

- `tx_req`, `tx_done`, `rx_done`, `rx_poll`
- `filtered`, `crc`, `invalid`
- `rxat_sched`, `rxat_start`, `rxat_timeout`, `rxat_late`
- `radioTxDonePending`, `radioRxDonePending`
- `radioEnergyScanDonePending`, `radioReceiveAtTimeoutPending`
- `last_err`, `last_tx_len`, `last_rx_len`, `last_rx_dst`
- recent OT logs and MLE logs printed by the examples

## Remaining Thread Work

### Thread Slice 1: Repeatability Gate

Run the full validation at least three times with two boards and save the log
directories.

Required to tick:

- Three complete `thread_meshcop_validation.py all` passes.
- At least one run with `--timeout 180`.
- No preloaded Joiner dataset.
- Fresh join, restore, and wrong-PSKd all pass.
- No rising CRC/invalid counters that suggest hidden radio instability.

### Thread Slice 2: Reference Network Attach

Attach to a real external Thread network, ideally OTBR or a Zephyr/NCS Thread
network.

Required:

- Obtain or generate a valid Active Operational Dataset TLV hex.
- Apply it through the Thread command surface.
- Confirm child/router attach, stable partition ID, stable RLOC16, and no
  parent churn.
- Reboot/reflash restore path and verify the device rejoins without reseeding a
  demo dataset.

Suggested command shape:

```bash
python3 scripts/thread_command_surface_attach_probe.py \
  --port /dev/ttyACM0 \
  --dataset-hex <ACTIVE_DATASET_TLV_HEX> \
  --wipe-settings \
  --timeout-s 90 \
  --dump-lines
```

### Thread Slice 3: UDP Reliability And Fragmentation

Matter will need larger and repeated payloads. The first two-board UDP gate now
passes through 512-byte payloads, but do not treat this as complete production
Thread reliability yet.

Current status:

- One child-to-leader unicast sweep passed all sizes through 512 bytes.
- One multicast sweep with ACK echo passed all sizes through 512 bytes.
- The runner now has a real full-matrix fragmentation gate.

Still required:

- Sweep unicast UDP payload sizes both directions, including leader-to-child.
- Include payloads that force 6LoWPAN fragmentation.
- Run repeated attach/reconnect cycles.
- Track loss, retry, ACK, CRC, invalid-length, and reassembly timeout counters.
- Compare against Zephyr behavior where possible.

If fragmentation fails, inspect the OpenThread frame path before changing the
Arduino examples. The correct fix should be in the OpenThread platform/radio
adapter or buffer/reassembly configuration, not in sketch-level workarounds.

### Thread Slice 4: Sleepy End Device Support

Current staged mode is not a finished low-power Thread stack.

Required:

- Decide SED/MED/CSL scope.
- Implement parent polling and indirect transmission handling.
- Prove parent retention through long idle periods.
- Measure current consumption on XIAO and non-XIAO boards.
- Confirm Zigbee and raw 802.15.4 paths still work because they share radio
  ownership concerns.

### Thread Slice 5: Radio Ownership Hardening

BLE, Zigbee, raw 802.15.4, and Thread all touch radio-adjacent state.

Required:

- Document one radio-owner state machine.
- Ensure Thread stage mode refuses or safely arbitrates incompatible radio use.
- Keep XIAO RF switch behavior board-specific.
- Do not add fixed Zigbee channels globally; sketches should choose channels.

## Remaining Matter Work

### Matter Slice 1: Define The Upstream CHIP Boundary

The core should not reimplement Matter protocol logic in Arduino sketches.

Required:

- Identify exactly which upstream CHIP components are compiled as source.
- Identify which platform layers this repo owns.
- Keep staged examples clearly labelled until real CHIP commissioning works.
- Remove or avoid claims that staged PASE/CASE demos are production Matter.

### Matter Slice 2: CryptoPAL And Secure Sessions

Matter secure sessions must use the upstream CHIP path.

Required:

- Wire CHIP CryptoPAL to software P-256 and available CRACEN RNG.
- Keep hardware ECC disabled unless Nordic microfirmware support is legally and
  technically available and tested.
- Verify SHA256, HMAC, HKDF, PBKDF2, AES-CCM, SPAKE2+, and P-256 vectors.
- Document performance impact of software P-256.

TLS guidance:

- Do not build a sketch-local TLS stack for Matter.
- MeshCoP DTLS belongs to OpenThread/mbedTLS.
- Matter commissioning and secure messaging belong to CHIP secure sessions.

### Matter Slice 3: Commissioning Transport

Required:

- Decide whether BLE rendezvous is in scope for initial Matter.
- If BLE rendezvous is used, keep it isolated from existing Bluefruit central,
  peripheral, NUS, and secure-connection paths.
- Prove non-secure BLE examples still pass after any Matter BLE work.

### Matter Slice 4: Network Services

Required:

- SRP client integration over Thread.
- mDNS/DNS-SD publication as expected by Matter commissioners.
- Basic IPv6 UDP/TCP behavior needed by CHIP.
- Home Assistant commissioning smoke test.

### Matter Slice 5: Minimal Real Device

Required:

- Build one minimal real on/off light using upstream CHIP, not staged demo
  messages.
- Commission it from a real commissioner.
- Reboot and verify it rejoins and remains controllable.
- Confirm factory reset clears Matter and Thread state consistently.

## Rules For The Next AI Or Developer

- Do not touch Serial unless the task is explicitly Serial.
- Do not hide failures by seeding datasets into Joiner sketches.
- Do not claim Matter production support until a real CHIP commissioner works.
- Do not claim Thread low-power support until SED behavior and current are
  measured.
- Keep generated validation logs out of git.
- Use the validation harness after each Thread/Matter slice.
- Prefer fixing platform/radio glue over adding sketch workarounds.
