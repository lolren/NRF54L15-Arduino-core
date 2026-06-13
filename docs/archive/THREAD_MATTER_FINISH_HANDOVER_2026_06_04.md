# Thread and Matter finish handover - 2026-06-04

This document is the current handover for finishing Thread and Matter support in
the nRF54L15 Arduino core.

Working tree used:

```text
/home/lolren/Desktop/test_pi_nrf54/NRF54L15-Clean-Arduino-core
```

This file is intended for a future agent or developer continuing the work. It is
more current than the older Thread/Matter audit notes in this folder. Some older
documents were written before the current `mbedtls_stage` and MeshCoP wrapper
work landed, so always verify claims against the current code and this handover.

## Prime directive

The aim is not to make examples appear to compile. The aim is to finish Thread
and Matter support in a way that survives real two-board hardware testing,
reference Thread network testing, and eventually real Matter commissioner
interop.

Do not claim full support until the hardware tests in this document pass.

Do not touch Serial unless the user explicitly asks. Serial has regressed
several times in this project and is not part of this Thread/Matter task.

Do not create another repo or branch unless the user explicitly asks. Work should
stay in the main core tree.

Do not bump/push/release from this handover task unless explicitly requested.

## Current claim level

Thread today:

- Experimental OpenThread stage mode exists and compiles in Arduino.
- Stage mode is CPUAPP-owned.
- Stage mode uses the existing `ZigbeeRadio` IEEE 802.15.4 backend.
- Fixed-dataset leader/router/child attach works in staged examples.
- UDP examples exist and have passed earlier two-board tests.
- Standard MeshCoP Joiner/Commissioner source wrappers and mbedTLS stage source
  are present in this local tree.
- Commissioner and Joiner examples compile and have formed a leader/child pair
  in the current two-board staged MeshCoP tests.
- The 802.15.4 ACK path was improved in this pass and active sender-side
  `NoAck` failures were reduced to zero in two 180-second focused soaks.
- OpenThread key-reference storage hooks now link in the mbedTLS stage build;
  `OpenThreadPlatformSkeletonProbe` verifies import/export/has/destroy coverage.
- `ThreadExperimentalMeshcopRestoreProbe` exists as a restore-only post-joiner
  validation sketch.
- `ThreadExperimentalMeshcopWrongPskdJoiner` exists as a wrong-PSKd negative
  validation sketch.
- Every Thread example folder currently compiles for
  `clean_thread=stage` on XIAO.

Thread not yet production-proven:

- MeshCoP needs repeated clean one-shot success tests that prove the Joiner
  starts with no dataset, commissions through DTLS, persists the dataset, and
  restores it after reboot.
- MeshCoP needs a wrong-PSKd negative test proving failed join does not persist
  a usable dataset.
- Fragmentation and large-payload behavior are not complete enough to claim
  production.
- Sleepy end device behavior and indirect transmission/polling are not fully
  characterized.
- Reference-network attach to a real OTBR or Zephyr/NCS Thread network is still
  required.
- Border Router support is out of scope for the Arduino core unless explicitly
  added later.

Matter today:

- Matter foundation/staged examples exist.
- On-network/on-off-light scaffolding exists.
- Staged PASE/CASE protocol demos over Thread UDP exist and have passed earlier
  two-board smoke tests.
- Matter onboarding code and staged dataset export seams exist.
- `MatterPlatform` can apply a configured operational dataset, configured TLVs,
  demo dataset, restored settings dataset, or active OpenThread dataset and
  reports the selected source in snapshots.
- `MatterPlatform` persists and clears factory data through `Preferences`.
- Every Matter example folder currently compiles for
  `clean_thread=stage,clean_matter=stage` on XIAO.

Matter not yet production:

- The staged PASE/CASE demos are not upstream CHIP secure-session integration.
- No real Matter commissioner interop is proven.
- No Home Assistant Matter commissioning is proven.
- No real mDNS/SRP publication is proven.
- No commissioned Matter reboot/reconnect recovery is proven.
- BLE rendezvous is not part of the current plan.

## Latest implementation slice - 2026-06-04

This slice was done after the older ACK/radio notes below.

### 1. Matter platform Thread dataset/readiness support

Files:

```text
src/matter_platform_stage.h
src/matter_platform_stage.cpp
```

Implemented:

- `MatterPlatformConfig::threadDataset`
- `MatterPlatformConfig::threadDatasetTlvs`
- `MatterPlatformConfig::useDemoThreadDataset`
- `MatterPlatformConfig::threadAttachPolicy`
- `MatterPlatform::setThreadDataset()`
- `MatterPlatform::setThreadDatasetTlvs()`
- `MatterPlatform::useDemoThreadDataset()`
- `MatterPlatform::exportOpenThreadDatasetTlvs()`
- `MatterPlatform::exportOpenThreadDatasetHex()`
- snapshot fields for transport readiness, dataset configured/exportable state,
  dataset source name, RLOC16, attach summary, restore diagnostics, and the
  current readiness blocker.

Why:

- Matter needs a clear seam between commissioning-provided Thread datasets,
  restored Thread settings, demo datasets, and the active OpenThread dataset.
- The earlier platform code could start Thread, but it did not make the dataset
  source/export/readiness state explicit enough for commissioning and reboot
  debugging.

### 2. Matter factory data persistence

Files:

```text
src/matter_platform_stage.h
src/matter_platform_stage.cpp
```

Implemented:

- Factory data is loaded from `Preferences` when the platform starts.
- `setFactoryData()` persists non-empty data when storage is open.
- `setFactoryData(nullptr, 0)` clears the persisted factory data key.

Why:

- Matter commissioning eventually needs stable factory/onboarding/credential
  storage behavior. This is still staged, but the storage path now behaves like
  a real platform seam instead of RAM-only state.

### 3. OpenThread key-reference platform hooks in mbedTLS mode

File:

```text
src/openthread_platform_nrf54l15.cpp
```

Implemented:

- `otPlatCryptoImportKey()`
- `otPlatCryptoExportKey()`
- `otPlatCryptoDestroyKey()`
- `otPlatCryptoHasKey()`

These are now available when `OPENTHREAD_CONFIG_CRYPTO_LIB` is set to
`OPENTHREAD_CONFIG_CRYPTO_LIB_MBEDTLS`. They use the existing in-RAM platform
key slot table and do not switch AES/SHA/DTLS math away from the mbedTLS-backed
OpenThread crypto path.

Why:

- The staged Thread build uses mbedTLS for crypto, but the OpenThread platform
  skeleton probe and key-reference storage path still need platform key-store
  hooks.
- Without this, `OpenThreadPlatformSkeletonProbe` failed to link with missing
  `otPlatCryptoExportKey`, `otPlatCryptoDestroyKey`, and `otPlatCryptoHasKey`.

### 4. PASE session-ID packer warnings fixed

Files:

```text
examples/Matter/MatterPaseCommissionee/MatterPaseCommissionee.ino
examples/Matter/MatterPaseCommissioner/MatterPaseCommissioner.ino
```

Fixed:

- Replaced `memcpy(msg + 66, g_sessionId, 2)` with explicit little-endian byte
  writes.

Why:

- The old code passed a `uint16_t` value as a pointer. It compiled with a
  warning, but it was wrong and could become a runtime fault.

### 5. MeshCoP restore-only probe added

File:

```text
examples/Thread/ThreadExperimentalMeshcopRestoreProbe/ThreadExperimentalMeshcopRestoreProbe.ino
```

Behavior:

- Calls `g_thread.begin(false)`.
- Does not wipe settings.
- Does not seed the demo dataset.
- Does not start the Joiner.
- Prints restore diagnostics, attach summary, RLOC16, partition ID, and the
  restored/exported dataset hex once available.

Expected use:

- First run `ThreadExperimentalCommissioner` and `ThreadExperimentalJoiner` on
  two boards and confirm MeshCoP join success.
- Then flash `ThreadExperimentalMeshcopRestoreProbe` onto the joined board
  without wiping persistent settings.
- A successful restore proof should show:

```text
restore_attempted=1
restore_restored=1
dataset_configured=1
restored_from_settings=1
```

If the probe reports `restore_no_saved_dataset`, the join path did not persist
the dataset or settings were wiped before the probe ran.

### 6. MeshCoP wrong-PSKd negative probe added

File:

```text
examples/Thread/ThreadExperimentalMeshcopWrongPskdJoiner/ThreadExperimentalMeshcopWrongPskdJoiner.ino
```

Behavior:

- Wipes settings on boot with `beginJoinerOnly(true)`.
- Starts Joiner with `BADPSK54`.
- Refuses to proceed if an active dataset is present before Joiner start.
- Treats `OT_ERROR_NONE` in the Joiner callback as fatal.
- Prints `expected_join_failure=1` when the Joiner callback fails as expected.

Expected use:

- Run the normal `ThreadExperimentalCommissioner` on the commissioner board.
- Run `ThreadExperimentalMeshcopWrongPskdJoiner` on the joiner board.
- Expected pass markers:

```text
before_joiner_active_dataset=0
joiner_start=1
expected_join_failure=1
unexpected_success=0
active_dataset=0
```

If `unexpected_join_success=1` or `active_dataset=1` appears, MeshCoP negative
handling is broken or stale settings were not actually wiped.

## Latest compile validation - 2026-06-04

Matter full example compile pass:

```bash
ARDUINO_DIRECTORIES_USER=/tmp/nrf54-local-sketchbook \
arduino-cli compile \
  -b 'nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage' \
  <each examples/Matter sketch folder>
```

Result:

```text
All Matter example folders compile. Failure count: 0.
```

Thread full example compile pass after the key-reference hook fix and MeshCoP
validation-probe additions:

```bash
ARDUINO_DIRECTORIES_USER=/tmp/nrf54-local-sketchbook \
arduino-cli compile \
  -b 'nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage' \
  <each examples/Thread sketch folder>
```

Result:

```text
THREAD_EXAMPLE_COUNT 30
FAIL_COUNT 0
```

This compile pass includes the new
`ThreadExperimentalMeshcopRestoreProbe` and
`ThreadExperimentalMeshcopWrongPskdJoiner`.

The compile pass still prints pre-existing warnings in several radio diagnostic
sketches where `extern "C" __attribute__((used)) volatile ... = {0};` variables
are initialized. Those warnings are test-sketch hygiene, not a Thread runtime
link failure.

## Hardware and local test setup

Boards used in the latest Thread ACK and MeshCoP work:

```text
E91217E8 -> /dev/ttyACM0 -> usually commissioner/router/leader
761FDE87 -> /dev/ttyACM1 -> usually joiner/child
```

FQBN used for Thread-only stage tests:

```text
nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage
```

FQBN used for Matter stage tests:

```text
nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage
```

Local sketchbook symlink method for testing this repo without installing it:

```bash
rm -rf /tmp/nrf54-local-sketchbook
mkdir -p /tmp/nrf54-local-sketchbook/hardware
ln -s /home/lolren/Desktop/test_pi_nrf54/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean \
  /tmp/nrf54-local-sketchbook/hardware/nrf54l15clean

rm -rf /tmp/nrf54-local-sketchbook-joiner
mkdir -p /tmp/nrf54-local-sketchbook-joiner/hardware
ln -s /home/lolren/Desktop/test_pi_nrf54/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean \
  /tmp/nrf54-local-sketchbook-joiner/hardware/nrf54l15clean
```

Always use `compile --upload` when changing menu options. Plain `upload` can
reuse a cached artifact and flash the wrong build.

## Current build flags that matter

`boards.txt` currently defines:

```text
clean_thread=stage:
  -DNRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE=1
  -DNRF54L15_CLEAN_OPENTHREAD_MESHCOP_ENABLE=1
  -DOPENTHREAD_FTD=1
  -DOPENTHREAD_MTD=0
  -DOPENTHREAD_RADIO=0
```

This means stage mode builds the OpenThread core in a non-radio build
(`OPENTHREAD_RADIO=0`) and uses this repo's `otPlatRadio*` glue over the local
`ZigbeeRadio` HAL.

`openthread-core-user-config.h` currently maps:

```text
OPENTHREAD_CONFIG_COMMISSIONER_ENABLE -> NRF54L15_CLEAN_OPENTHREAD_MESHCOP_ENABLE
OPENTHREAD_CONFIG_JOINER_ENABLE       -> NRF54L15_CLEAN_OPENTHREAD_MESHCOP_ENABLE
OPENTHREAD_CONFIG_SECURE_TRANSPORT_ENABLE -> NRF54L15_CLEAN_OPENTHREAD_MESHCOP_ENABLE
OPENTHREAD_CONFIG_SEEKER_ENABLE -> NRF54L15_CLEAN_OPENTHREAD_MESHCOP_ENABLE
```

So with `clean_thread=stage`, MeshCoP code is intended to be enabled in this
local tree.

The same config keeps the MAC services in software:

```text
OPENTHREAD_CONFIG_MAC_SOFTWARE_ACK_TIMEOUT_ENABLE 1
OPENTHREAD_CONFIG_MAC_SOFTWARE_RETRANSMIT_ENABLE 1
OPENTHREAD_CONFIG_MAC_SOFTWARE_CSMA_BACKOFF_ENABLE 1
OPENTHREAD_CONFIG_MAC_SOFTWARE_TX_SECURITY_ENABLE 1
OPENTHREAD_CONFIG_MAC_SOFTWARE_RX_ON_WHEN_IDLE_ENABLE 1
```

Do not switch to `OPENTHREAD_RADIO=1` blindly. That was attempted previously
and caused duplicate OpenThread `SubMac` specialization/linkage problems. If a
future agent wants a radio build, plan that as a separate architecture change.

## Important current source areas

Thread wrapper API:

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54_thread_experimental.h
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54_thread_experimental.cpp
```

OpenThread platform glue:

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread_platform_nrf54l15.h
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread_platform_nrf54l15.cpp
```

OpenThread stage source wrappers:

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread_core_stage/
```

The current tree includes wrappers for many OpenThread core units, including:

```text
api_commissioner_api.cpp
api_joiner_api.cpp
api_coap_secure_api.cpp
coap_coap_secure.cpp
meshcop_commissioner.cpp
meshcop_joiner.cpp
meshcop_secure_transport.cpp
meshcop_seeker.cpp
crypto_platform_mbedtls.cpp
crypto_storage.cpp
```

mbedTLS stage C source set:

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/mbedtls_stage/
```

This local tree has many `mbedtls_*.c` units staged. Treat this as
implemented-but-not-production-proven until the MeshCoP one-shot, negative PSKd,
persistence, and soak tests pass.

IEEE 802.15.4 radio HAL:

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.h
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_802154_rawradio.inc
```

Matter foundation and staged runtime:

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_foundation_target.h
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_foundation_target.cpp
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_onnetwork_onoff_light.h
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_onnetwork_onoff_light.cpp
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_core_stage/
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_pase_commissioning.h
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_pase_commissioning.cpp
```

## What changed in the latest ACK/radio slice

This pass focused on the real Thread blocker seen in two-board tests: missed
802.15.4 ACKs during parent/child traffic. It did not change Serial and did not
touch Matter protocol code.

### 1. Buffered RX ACK threshold widened

File:

```text
src/nrf54l15_hal.h
```

Change:

```text
kBufferedRxAckBccBytes = 11
```

Reason:

- The old value was 4 bytes, effectively PHR + FCF + sequence.
- That was enough to know ACK-request and sequence, but not enough to inspect a
  short destination/source or MAC command ID.
- The new value covers PHR + FCF + sequence + short destination + short source +
  command ID.
- This allows early ACK preparation to know whether a short-address MAC data
  request should set frame pending.

### 2. Early prepared ACK now respects filtering

File:

```text
src/nrf54l15_hal_parts/nrf54l15_hal_802154_rawradio.inc
```

Change:

- `prepareBufferedRxMacAcknowledgementFromPrefix()` now calls
  `shouldAcceptReceivedMacFrame()` before preparing an ACK.
- It also computes frame-pending early using `shouldSetMacAckFramePending()`.

Why:

- ACKs must not be sent for frames that are not addressed to the local node.
- But the ACK must also be prepared early enough to meet the 802.15.4 ACK
  turnaround timing.

### 3. Prepared ACK is sent before queue/memcpy work

File:

```text
src/nrf54l15_hal_parts/nrf54l15_hal_802154_rawradio.inc
```

Change:

- In `serviceBufferedReceiveIrq()`, a prepared ACK with matching sequence is now
  sent immediately after CRC and length validation.
- Full receive filtering and queue/memcpy work happen after the ACK send.

Why:

- The previous path did CRC, filtering, frame-pending, queue accounting, and
  memcpy before or around the ACK decision.
- That can miss the ACK window under Thread parent/child direct-unicast traffic.

### 4. Frame-pending semantics fixed

Files:

```text
src/nrf54l15_hal_parts/nrf54l15_hal_802154_rawradio.inc
src/openthread_platform_nrf54l15.cpp
```

Change:

- `shouldSetMacAckFramePending()` now returns false unless the received frame is
  a valid MAC command data request and the data-request callback reports pending
  data.
- `threadMacDataRequestPendingCallback()` now returns false when the frame
  cannot be parsed as a MAC data request.

Why:

- The old behavior could mark frame-pending for non-data-request command frames.
- For Thread source-match behavior, frame-pending should only be set on MAC data
  requests from a child that has pending indirect data.

### 5. OpenThread ACK frame length includes FCS

File:

```text
src/openthread_platform_nrf54l15.cpp
```

Current retained behavior:

- The synthetic ACK frame delivered back to OpenThread now has
  `mLength = ackLength + 2` and appends two dummy FCS bytes.

Reason:

- OpenThread `otRadioFrame::mLength` is PSDU length including FCS.
- RX frames already include the two-byte FCS when reported to OpenThread.
- ACK frames delivered to OpenThread must follow the same contract.

## Latest hardware test results

Commissioner compile:

```bash
ARDUINO_DIRECTORIES_USER=/tmp/nrf54-local-sketchbook \
arduino-cli compile \
  --fqbn 'nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage' \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/ThreadExperimentalCommissioner
```

Result:

```text
Sketch uses 306868 bytes.
Global variables use 38148 bytes.
```

Joiner compile:

```bash
ARDUINO_DIRECTORIES_USER=/tmp/nrf54-local-sketchbook-joiner \
arduino-cli compile \
  --fqbn 'nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage' \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/ThreadExperimentalJoiner
```

Result:

```text
Sketch uses 308172 bytes.
Global variables use 38140 bytes.
```

Uploads:

```bash
ARDUINO_DIRECTORIES_USER=/tmp/nrf54-local-sketchbook \
arduino-cli upload -p /dev/ttyACM0 \
  --fqbn 'nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage' \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/ThreadExperimentalCommissioner

ARDUINO_DIRECTORIES_USER=/tmp/nrf54-local-sketchbook-joiner \
arduino-cli upload -p /dev/ttyACM1 \
  --fqbn 'nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage' \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/ThreadExperimentalJoiner
```

Upload result:

```text
/dev/ttyACM0 -> pyOCD UID E91217E8 -> upload complete
/dev/ttyACM1 -> pyOCD UID 761FDE87 -> upload complete
```

First 180-second soak after ACK patch:

```text
lines=1822
NoAck=0
Leader=64
Child=62
Detached=1
Duplicated=0
Crc=120
Assert=0
```

Interpretation:

- Strong improvement over earlier ACK-path attempts.
- Joiner was child at the end.
- No active sender-side ACK misses in this run.

Second 300-second soak:

```text
lines=3034
NoAck=11
Leader=105
Child=106
Detached=0
Duplicated=41
Crc=200
Assert=0
ChildTimeout=0
ChildRemoved=0
```

Interpretation:

- Stability was good: attached throughout, no child timeout/removal.
- Some `NoAck` still appeared over 300 seconds.
- The duplicate counter needs careful interpretation because the example
  reprints stored OpenThread log lines on every heartbeat.

Focused 180-second follow-up:

```text
lines=1821
NoAck=0
Duplicated=16
Leader=63
Child=63
Detached=0
Crc=120
Assert=0
```

Interpretation:

- Active radio ACK behavior looked clean again.
- Duplicate lines were mostly repeated stored OpenThread log output, not
  necessarily new packets.
- The ACK slice is worth keeping, but it is not enough to claim final Thread
  radio correctness.

## Known current issues and open questions

### Issue 1: MeshCoP success path needs repeated clean proof and persistence proof

Current symptoms:

- Commissioner reports `commissioner_supported=1`.
- Commissioner can become leader and active commissioner.
- Joiner reports `joiner_supported=1`.
- Joiner can attach as child and current local two-board output shows a
  commissioned leader/child pair:
  - commissioner `attached=1`, `role=leader`, `meshcop_last_event=end`
  - joiner `attached=1`, `role=child`, `preexisting_dataset_before_joiner=0`
  - joiner active dataset TLVs are present after the join path
- This is enough to treat the source/API path as implemented, but not enough to
  claim production MeshCoP yet.

Why this matters:

- Production claim needs repeatability and negative proof, not one good local
  run.
- We still need to rule out hidden preloaded state and prove the persisted
  dataset restores after a reset without sketch-side dataset injection.
- A wrong PSKd must fail cleanly and must not leave a usable active dataset.

Required next test:

- Wipe settings on both boards.
- Commissioner starts with known dataset and PSKd.
- Joiner starts in `beginJoinerOnly(true)` with no active dataset.
- Joiner must print callback success from `otJoinerStart`.
- Commissioner must print a joiner event from `otCommissionerAddJoiner`.
- Joiner must then have an active dataset and attach.
- Reboot joiner with no sketch dataset injection and verify settings restore.
- Repeat the fresh settings-wipe success path three times.
- Repeat once with wrong PSKd and verify no dataset persists.

If callback markers disappear again, instrument the examples and wrapper before
claiming MeshCoP complete. The code path compiles and works locally, but the
release claim should be gated on the repeat/negative/reboot evidence above.

### Issue 2: Remaining occasional NoAck over longer runs

Current status:

- Two 180-second runs had `NoAck=0`.
- One 300-second run had `NoAck=11`.
- No detach, no child timeout, and no child removal occurred in the 300-second
  run.

Likely next work:

- Add one-shot counters for ACK-requested frames, prepared ACK sent, late ACK
  sent, filtered frames with ACK request, and ACK send failure.
- Keep those counters in RAM so they can be read over serial or SWD.
- Separate "new OpenThread duplicate event" from "example reprinted the same
  stored log line".
- Run 30-minute soak with commissioner/joiner active.
- Sweep channel 11, 15, 20, and 25 if interference is suspected.

Do not re-add the TX shortcut that was tested earlier. It regressed attach.

### Issue 3: Fragmentation and large payloads are not production ready

Historical test notes say:

- 8, 16, 31, 63, and 95 byte UDP payloads passed.
- Larger fragmented payloads such as 127, 191, 255, and 512 bytes had failures
  in earlier experiments.

For Matter, large messages can matter. Before claiming production:

- Test 6LoWPAN fragmentation and reassembly.
- Test out-of-order or missing fragments.
- Test duplicate fragment behavior.
- Test timeout and recovery.
- Test payload sweeps with the actual Matter message sizes.

### Issue 4: Sleepy end device behavior is not finished

Current work mostly uses always-on receive behavior for reliable bring-up.

Need:

- Define MTD/sleepy API surface.
- Validate data poll behavior.
- Validate indirect transmission/source match.
- Validate child timeout/supervision.
- Measure current against Zephyr/NCS reference sketches.

### Issue 5: Matter is still staged, not production CHIP runtime

Current staged Matter examples exercise useful crypto and Thread UDP paths, but
they are not a real Matter device stack.

Need:

- Upstream CHIP secure-session integration.
- CHIP CryptoPAL binding.
- SystemLayer/event loop integration.
- Inet/UDP transport binding to OpenThread.
- ExchangeManager.
- SessionManager.
- PASE verifier/commissioning window.
- CASE operational session.
- Operational credentials storage.
- Interaction Model server/client for on/off cluster.
- Discovery/publication using SRP/mDNS as required by Matter over Thread.

## Recommended implementation order to finish Thread

### Thread slice 1: make MeshCoP proof deterministic and repeatable

Goal:

- Prove standard OpenThread Joiner/Commissioner with DTLS works repeatedly and
  cannot accidentally pass through stale settings.

Implementation:

- Add explicit "settings wiped" prints to both examples.
- Add explicit "dataset exists before joiner start" print to joiner.
- Add explicit one-shot callback prints that include `otError` names.
- Add final "commissioning path used" marker.
- Make joiner refuse to attach through normal dataset flow until joiner callback
  succeeds.
- Make commissioner print every `otCommissionerJoinerEvent` value, not just the
  final error.
- Keep the current working two-board path intact; do not rewrite it unless a
  repeat/negative/reboot test exposes a real bug.

Tests:

- Fresh flash both boards.
- `ThreadExperimentalCommissioner` on `/dev/ttyACM0`.
- `ThreadExperimentalJoiner` on `/dev/ttyACM1`.
- Confirm:
  - `commissioner_supported=1`
  - `commissioner_state=active`
  - `joiner_supported=1`
  - `joiner_start=1`
  - joiner callback `OT_ERROR_NONE`
  - commissioner joiner event success
  - joiner becomes child
  - joiner has active dataset after success

Negative test:

- Flash joiner with wrong PSKd.
- Confirm join fails and no dataset persists.

Exit criteria:

- Three consecutive fresh setting-wipe runs pass.
- Wrong PSKd fails cleanly.
- No hidden preloaded dataset path is involved.

### Thread slice 2: MeshCoP persistence and reboot recovery

Goal:

- Prove a commissioned joiner persists and restores its operational dataset.

Implementation:

- Add a joiner example mode that does not call `buildDemoDataset()` and does not
  call `setActiveDataset()`.
- On reboot, call `beginJoinerOnly(false)` or a dedicated restore path that
  attempts settings restore first.
- Print `restored=1`, restored TLV length, role, RLOC16, and partition ID.

Tests:

- Commission once.
- Reset joiner only.
- Verify it reattaches without commissioner re-adding it.
- Power-cycle both boards.
- Verify commissioner network returns and joiner reattaches.

Exit criteria:

- 10 reboot cycles pass.
- No dataset is injected by sketch after first commissioning.

### Thread slice 3: radio ACK/duplicate hardening

Goal:

- Remove remaining intermittent `NoAck` and clarify duplicate logs.

Implementation:

- Add RAM counters in `ZigbeeRadio`:
  - `ackRequestedFrames`
  - `preparedAckBuilt`
  - `preparedAckSent`
  - `lateAckSent`
  - `ackSendFailed`
  - `ackFilteredNoSend`
  - `dataRequestFramePendingSet`
  - `dataRequestFramePendingClear`
- Mirror counters into `OpenThreadPlatformSkeletonSnapshot`.
- Print deltas, not only totals.
- Optionally expose counters through SWD-readable struct.

Tests:

- 30-minute commissioner/joiner soak.
- Capture no more than one new `NoAck` per 30 minutes before accepting as
  "good enough"; target should be zero.
- Run with boards at 20 cm, 0.7 to 1 m, and with antenna path set as default.
- Repeat on channels 11, 15, 20, 25.

Exit criteria:

- Stable child role.
- No child timeout/removal.
- No repeated active `NoAck`.
- Duplicate logs are proven to be stale log reprints or fixed.

### Thread slice 4: UDP and fragmentation matrix

Goal:

- Confirm Thread transport can carry realistic payload sizes.

Tests:

- UDP echo payload sweep:
  - 1, 8, 16, 31, 63, 95, 120 bytes
  - 127, 191, 255, 512 bytes if fragmentation is intended
- Test both directions.
- Test multicast and unicast.
- Test repeated packets for 30 minutes.
- Log checksum and sequence numbers.

Exit criteria:

- All non-fragmented packets pass.
- Fragmented packet behavior is either fixed and documented, or explicitly
  marked unsupported with examples avoiding it.

### Thread slice 5: reference network interop

Goal:

- Prove this is real Thread behavior, not only two Arduino boards talking to
  each other.

Setup options:

- Zephyr/NCS nRF52840 or nRF54 Thread sample.
- OTBR on Raspberry Pi.
- Nordic reference OpenThread CLI device.

Tests:

- Import external active dataset.
- Attach Arduino board as child.
- Send UDP to/from reference node.
- Reboot restore.
- Confirm channel, PAN ID, extended PAN ID, network key, and RLOC behavior.

Exit criteria:

- Arduino can join and communicate with an external Thread network.
- Settings restore works after reboot.

### Thread slice 6: sleepy/low-power Thread

Goal:

- Add low-power Thread behavior only after normal always-on Thread is stable.

Implementation:

- Define MTD/sleepy menu option or API.
- Validate data polling interval.
- Validate indirect data from parent to child.
- Validate source-match frame-pending behavior.
- Measure current with PPK2 against Zephyr/NCS.

Exit criteria:

- Sleepy child stays attached.
- Parent can deliver queued data.
- Current consumption is measured and documented.

## Recommended implementation order to finish Matter

Do not start by replacing everything. Keep the staged examples as diagnostics
and add production CHIP pieces one layer at a time.

### Matter slice 1: define the real CHIP runtime boundary

Goal:

- Decide exactly which upstream connectedhomeip units are owned by CHIP and
  which are repo platform glue.

Keep repo-owned:

- Board bring-up.
- Timebase.
- Entropy.
- Persistent storage backend.
- Thread platform instance ownership.
- RF path/antenna handling.
- Arduino examples/API surface.

CHIP-owned:

- Secure sessions.
- Exchange manager.
- Interaction model.
- Device model.
- PASE/CASE protocol.
- Operational credentials.
- Message encoding/decoding.

Exit criteria:

- One document and code constants agree on ownership.
- No sketch-local "Matter-like" secure session is treated as production.

### Matter slice 2: CHIP CryptoPAL integration

Goal:

- Wire the current software/hardware crypto primitives into the upstream CHIP
  CryptoPAL shape.

Must cover:

- SHA-256.
- HMAC-SHA256.
- HKDF.
- AES-CCM or AES primitives needed by CHIP.
- PBKDF2.
- SPAKE2+.
- secp256r1 ECDH/ECDSA.
- DRBG/entropy.

Important:

- nRF54L15 ECC hardware is not usable without Nordic CRACEN microcode.
- Software secp256r1 optimizations already exist in the core direction and
  should be reused.
- Do not use fake/stub crypto for anything called production.

Tests:

- Known-answer tests for every primitive.
- CHIP test vectors where available.
- Two-board PASE/CASE examples should still pass.

### Matter slice 3: CHIP SystemLayer and event loop

Goal:

- Let CHIP timers/events run in the Arduino cooperative loop without blocking
  Thread processing.

Implementation:

- Map CHIP time/timers to the HAL/GRTC-backed timebase.
- Pump CHIP event loop from `loop()` or the same service mechanism used by
  Thread.
- Avoid blocking waits.

Tests:

- Timer ordering.
- Concurrent Thread process and Matter timer events.
- Long soak without watchdog or missed Thread heartbeats.

### Matter slice 4: Inet/UDP transport over Thread

Goal:

- Use OpenThread UDP as the actual Matter transport.

Implementation:

- Bind CHIP InetLayer/UDP endpoints to `otUdpSocket` or a clean wrapper.
- Support IPv6 link-local and mesh-local addresses.
- Keep message ownership/lifetimes clear.

Tests:

- Send/receive unencrypted test messages between two boards.
- Verify no leaks in repeated open/close.
- Verify Thread stays attached during Matter traffic.

### Matter slice 5: PASE commissioning window

Goal:

- Replace staged PASE demo with upstream CHIP PASE secure session.

Implementation:

- Manual pairing code and QR code generation already exist as staged helpers.
- Use upstream verifier/session logic.
- Store discriminator, setup PIN/verifier, and commissioning state.

Tests:

- Local two-board commissioner-style test if available.
- Real commissioner later.
- Wrong PIN must fail.
- Repeated commissioning attempts must not leak state.

### Matter slice 6: CASE and operational credentials

Goal:

- Establish real operational CASE sessions using upstream CHIP.

Implementation:

- Add operational credentials storage.
- Add certificate/key storage or a minimal test-only operational credential
  model if full certification is not yet the target.
- Route CASE through upstream secure-session manager.

Tests:

- CASE success after commissioning.
- Wrong credential/session fails.
- Reboot and reconnect.

### Matter slice 7: Interaction Model on/off light

Goal:

- Turn the staged on/off-light model into a real CHIP Interaction Model endpoint.

Implementation:

- Root node endpoint.
- On/off light endpoint.
- Attribute read/write.
- On/off command handling.
- Basic cluster metadata.

Tests:

- Two-board controller/light test.
- Real Matter commissioner/controller test when discovery is ready.

### Matter slice 8: discovery, SRP, and commissioner interop

Goal:

- Make the device visible and usable by real Matter commissioners.

Implementation:

- Enable or implement SRP client path in OpenThread.
- Publish Matter service records.
- Validate with OTBR and Home Assistant.

Tests:

- Device appears in commissioner.
- Commissioning starts.
- PASE completes.
- CASE completes.
- On/off control works.
- Reboot and reconnect works.

## Required test matrix before claiming full Thread

Thread compile:

- `ThreadExperimentalCommissioner`
- `ThreadExperimentalJoiner`
- `ThreadExperimentalCommissionerJoinerDemo`
- `ThreadExperimentalJoinerPSK`
- `ThreadExperimentalJoinerPSKCommissioner`
- `ThreadExperimentalJoinerPSKJoiner`
- `ThreadExperimentalUdpPing`
- `ThreadExperimentalUdpSoak`
- `ThreadExperimentalRebootRecoveryProbe`
- `OpenThreadRadioTxAckPeer`
- `OpenThreadRadioTxAckResponder`
- `OpenThreadRadioSourceMatchRequester`
- `OpenThreadRadioSourceMatchResponder`

Thread hardware:

- Two-board fixed dataset leader/child.
- Two-board MeshCoP commissioner/joiner fresh settings.
- Wrong PSKd.
- Joiner reboot restore.
- Commissioner reboot restore.
- UDP payload sweep.
- 30-minute attach/heartbeat soak.
- Reference network attach with external OTBR/Zephyr/NCS node.
- PPK2 current measurement for always-on and sleepy modes.

Acceptance:

- No asserts/hard faults.
- No child timeout/removal during soak.
- No repeated `NoAck`.
- Settings persistence proven.
- Wrong credentials fail.
- External network interop proven.

## Required test matrix before claiming full Matter

Matter compile:

- `MatterFoundationProbe`
- `MatterOnOffLightFoundationCompileTarget`
- `MatterOnNetworkOnOffLightNodeDemo`
- `MatterOnNetworkOnOffLightCommandSurfaceDemo`
- `MatterOnNetworkRebootRecoveryProbe`
- `MatterOnOffLightApiDemo`
- `MatterOnOffLightTwoBoardDemo`
- staged PASE/CASE examples as diagnostics
- future real CHIP PASE/CASE examples

Matter hardware:

- On-network Thread attach before Matter start.
- QR/manual code generation sanity.
- Real PASE with wrong PIN fail.
- Real PASE with correct PIN pass.
- Real CASE pass.
- On/off cluster command pass.
- Reboot after commissioning.
- Home Assistant or another real commissioner.
- 30-minute Matter-over-Thread soak.

Acceptance:

- A real Matter commissioner can commission the device.
- A real Matter controller can control the on/off endpoint.
- Device survives reboot and reconnects without re-commissioning.
- No staged sketch-local protocol is being mistaken for production Matter.

## Serial capture helper

Use this when testing both boards without resetting them:

```bash
python3 - <<'PY'
import serial
import time
import select
import sys

ports = [('/dev/ttyACM0', 'C'), ('/dev/ttyACM1', 'J')]
serials = []
buffers = {}

for path, name in ports:
    ser = serial.Serial(path, 115200, timeout=0)
    ser.dtr = True
    ser.rts = True
    serials.append((ser, name))
    buffers[id(ser)] = b''
    print(f'OPEN {name} {path}')

end = time.time() + 180
while time.time() < end:
    fds = [ser.fileno() for ser, _ in serials]
    readable, _, _ = select.select(fds, [], [], 0.2)
    for ser, name in serials:
        if ser.fileno() not in readable:
            continue
        data = ser.read(4096)
        if not data:
            continue
        key = id(ser)
        buffers[key] += data
        while b'\n' in buffers[key]:
            raw, buffers[key] = buffers[key].split(b'\n', 1)
            line = raw.decode('utf-8', errors='replace').rstrip()
            if line:
                print(f'{time.time():.3f} {name}: {line}')
                sys.stdout.flush()

for ser, _ in serials:
    ser.close()
PY
```

## 180-second soak counter script

This is the script used in the latest ACK work. It catches the most important
Thread role and radio symptoms:

```bash
python3 - <<'PY'
import serial
import time
import re
from collections import Counter, deque

ports = {'C': '/dev/ttyACM0', 'J': '/dev/ttyACM1'}
serials = {}
for tag, port in ports.items():
    s = serial.Serial(port, 115200, timeout=0)
    s.dtr = True
    s.rts = True
    serials[tag] = s

start = time.time()
end = start + 180
counts = Counter()
last = deque(maxlen=100)
buf = {tag: b'' for tag in ports}
patterns = {
    'NoAck': re.compile(r'NoAck', re.I),
    'JoinSuccess': re.compile(r'JOIN_SUCCESS=1|join success', re.I),
    'JoinAccepted': re.compile(r'JOINER_ACCEPTED|Joiner accepted|accepted', re.I),
    'Leader': re.compile(r'role=leader', re.I),
    'Child': re.compile(r'role=child', re.I),
    'Detached': re.compile(r'role=detached', re.I),
    'Disabled': re.compile(r'role=disabled', re.I),
    'Duplicated': re.compile(r'Duplicated', re.I),
    'Crc': re.compile(r'crc', re.I),
    'Assert': re.compile(r'assert|panic|fault|hardfault', re.I),
}
try:
    while time.time() < end:
        for tag, s in serials.items():
            data = s.read(4096)
            if not data:
                continue
            buf[tag] += data
            while b'\n' in buf[tag]:
                raw, buf[tag] = buf[tag].split(b'\n', 1)
                line = raw.decode('utf-8', errors='replace').strip()
                if not line:
                    continue
                t = time.time() - start
                counts['lines'] += 1
                last.append(f'{t:7.2f} {tag}: {line}')
                for name, pat in patterns.items():
                    if pat.search(line):
                        counts[name] += 1
        time.sleep(0.01)
finally:
    for s in serials.values():
        s.close()

print('SUMMARY')
for key in ['lines','NoAck','JoinSuccess','JoinAccepted','Leader','Child',
            'Detached','Disabled','Duplicated','Crc','Assert']:
    print(f'{key}={counts[key]}')
print('LAST_LINES')
for line in last:
    print(line)
PY
```

## SWD / pyOCD debugging notes

Use SWD memory counters when serial logs perturb timing or when the board loses
USB serial during reset.

General read command:

```bash
pyocd cmd -t nrf54l -u 761FDE87 -O auto_unlock=false \
  -c "halt" \
  -c "read32 0x20000000" \
  -c "resume"
```

Use `nm` to find a symbol address in the compiled ELF:

```bash
arm-none-eabi-nm -n /home/lolren/.cache/arduino/sketches/*/*.elf | \
  rg 'g_thread|gOpenThreadPlatformState|snapshot|debug'
```

Then read the exact address:

```bash
pyocd cmd -t nrf54l -u 761FDE87 -O auto_unlock=false \
  -c "halt" \
  -c "read32 0x20001234 16" \
  -c "resume"
```

Counter pattern to add when needed:

```cpp
extern "C" volatile uint32_t g_thread_debug_ack_requested = 0;
extern "C" volatile uint32_t g_thread_debug_ack_prepared = 0;
extern "C" volatile uint32_t g_thread_debug_ack_sent = 0;
extern "C" volatile uint32_t g_thread_debug_ack_failed = 0;
```

Rules:

- Mark counters `volatile`.
- Keep them global with stable names.
- Increment only at the exact branch you want to prove.
- Use `nm` after compile to find their addresses.
- Read with pyOCD while the sketch runs.
- Avoid printing in timing-critical ACK paths.

There are existing memory debugging docs in the workspace root:

```text
/home/lolren/Desktop/test_pi_nrf54/SWD_MEMORY_COUNTER_DEBUGGING.md
/home/lolren/Desktop/test_pi_nrf54/SWD_MEMORY_DEBUGGING_GUIDE.md
/home/lolren/Desktop/test_pi_nrf54/PYOC_MEMORY_READING.md
```

## What not to do

- Do not fake DTLS success with mbedTLS stubs.
- Do not claim Matter production support from the staged PASE/CASE demos.
- Do not mutate Thread dataset timestamps to force roles.
- Do not let a joiner example preload a dataset when testing MeshCoP join.
- Do not change Serial while chasing radio or Matter issues.
- Do not optimize power before protocol correctness unless measuring an already
  correct protocol path.
- Do not remove Zigbee regression coverage: Thread shares the same radio HAL.
- Do not assume one successful boot is proof. Use repeated wipe/reboot/soak.

## Immediate next action

The best next slice is Thread slice 1: make MeshCoP proof deterministic.

Reason:

- The current radio ACK slice made two-board attach stable enough to proceed.
- `mbedtls_stage` and MeshCoP wrappers now exist.
- The current examples show support flags and attached child/leader behavior.
- The remaining uncertainty is whether the successful state is a clean standard
  MeshCoP DTLS commissioning path or a side effect of restored/preloaded state.

Concrete next steps:

1. Add explicit wipe/dataset-before/dataset-after instrumentation to
   `ThreadExperimentalCommissioner` and `ThreadExperimentalJoiner`.
2. Make joiner-only mode refuse any normal attach before `otJoinerStart`
   callback success.
3. Print `otError` names for joiner and commissioner callbacks.
4. Run three fresh two-board commissioning attempts.
5. Run a wrong-PSKd attempt.
6. Run reboot-restore test.
7. Only then move to fragmentation and reference-network interop.

If that passes, the next high-value target is Matter slice 2/3/4: begin wiring
real CHIP CryptoPAL, event loop, and UDP transport, while keeping the staged
PASE/CASE demos as diagnostics only.
