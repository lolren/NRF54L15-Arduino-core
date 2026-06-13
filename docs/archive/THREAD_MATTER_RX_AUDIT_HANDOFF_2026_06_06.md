# Thread/Matter RX Audit Handoff - 2026-06-06

## Scope

This pass audited the outside-session work referenced by:

`/home/lolren/Desktop/test_pi_nrf54/THREAD_MATTER_SESSION_REPORT_2026_06_06_FINAL.md`

The goal was to treat that work as untrusted, fix the reported RX bug, remove or avoid faulty implementations, and leave a clear continuation path for Thread and Matter.

## Current Result

The compile-blocking and correctness issues found in this pass are fixed in the working tree. The staged Thread/Matter examples compile, the obvious RX memory corruption bug is fixed, and speculative implementations that were not real functional integrations were not kept.

No git commit or push was made in this pass.

## Fixes Applied

### 1. Sleepy-child UDP RX buffer overflow fixed

File:

`hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/ThreadExperimentalSleepyChild/ThreadExperimentalSleepyChild.ino`

The outside-session UDP echo callback used a 64-byte buffer and then nul-terminated at `buffer[len]`. When a received payload was exactly 64 bytes, this wrote one byte past the buffer. That could corrupt local state and produce random RX/Thread behavior.

Fix:

- Buffer increased to 65 bytes.
- Incoming length is clamped to `sizeof(buffer) - 1`.
- `memcpy()` is skipped for zero-length payloads.
- Termination is always in-bounds.

This is the concrete RX bug found in the reviewed changes.

### 2. Direct sleepy-child rx-off attach path preserved

The session report claimed that a two-phase `rx-on attach then switch to rx-off` flow was required. That is not the path to preserve.

The current correct behavior is:

- A sleepy end device must start with `rxOnWhenIdle=false`.
- OpenThread must attach directly as an rx-off child.
- Parent/child traffic must rely on MAC data polls and ACK frame-pending behavior.

Do not reintroduce the two-phase rx-on attach workaround unless a new hardware test proves direct rx-off attach has regressed.

### 3. ACK frame-pending propagation retained

File:

`hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread_platform_nrf54l15.cpp`

The OpenThread platform transmit completion now propagates the raw-radio ACK frame-pending result into:

`mInfo.mRxInfo.mAckedWithFramePending`

This is required for sleepy-child indirect traffic handling. Without it, the child can miss pending data after polling.

### 4. Matter credentials first-boot behavior fixed

Files:

- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_credentials.h`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_credentials.cpp`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_foundation_target.cpp`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_onnetwork_onoff_light.cpp`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_onnetwork_onoff_light.h`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_pase_commissioning.h`

The new `MatterCredentials` helper now:

- Loads persisted credentials when present and valid.
- Seeds documented staging defaults on first boot or erased storage.
- Persists those defaults so later boots are deterministic.
- Uses Matter test VID `0xFFF1`, not the reserved CSA/CHIP VID.
- Centralizes default setup PIN, discriminator, VID, and PID.

Important: this is still staging identity handling. It is not production Matter factory provisioning.

### 5. Matter random generation hardened

Files:

- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_rng.h`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_rng.cpp`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_case_session.cpp`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_case_session.h`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_pase_commissioning.cpp`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_pase_commissioning.h`

The outside code still had weak pseudo-random paths in Matter-sensitive code. This pass adds a single `MatterRng` helper backed by `CracenRng` and uses it for:

- PASE PBKDF salt.
- PASE initiate/respond randoms.
- CASE Sigma randoms.
- CASE local session IDs.

If CRACEN RNG is unavailable, these paths now fail instead of silently falling back to time-based or LCG pseudo-random data.

### 6. Weak secp256r1 random helper hidden again

File:

`hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_secp256r1.h`

The outside work exposed `Secp256r1::randomBytes()` publicly. That helper is not suitable as a Matter-facing cryptographic RNG API. It is private again. Matter code should use `MatterRng`.

### 7. Faulty speculative radio ownership layer rejected

The outside-session work introduced `nrf54_radio_owner.*` and hooked it into BLE and 802.15.4 paths. That was not a real scheduler or arbitration layer:

- Callers did not consistently honor acquisition failures.
- It did not serialize all RADIO state transitions.
- It risked creating false confidence that BLE/Thread/Matter coexistence was solved.

That code is not kept in the current working tree.

### 8. Unverified CRACEN ECC wrapper rejected

The outside-session work introduced `matter_cracen_ec.*`, but it was unused and overstated CRACEN P-256 readiness. It was not kept.

CRACEN hardware crypto is still the right direction, but it needs a tested wrapper with known vectors before replacing the current staging software P-256 paths.

## Validation Run

All commands were run from:

`/home/lolren/Desktop/test_pi_nrf54/NRF54L15-Clean-Arduino-core`

### Static checks

```bash
git diff --check
```

Result: pass.

Added-line non-ASCII check:

```bash
git diff --unified=0 | grep '^+' | grep -v '^+++' | LC_ALL=C grep -nP '[^\x00-\x7F]' || true
```

Result: no newly added non-ASCII.

### Thread compiles

```bash
arduino-cli compile --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/ThreadExperimentalSleepyChild
```

Result: pass.

```bash
arduino-cli compile --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/ThreadExperimentalSleepyParent
```

Result: pass.

### MESHCoP compile validation

```bash
python3 scripts/thread_meshcop_validation.py compile
```

Result: pass.

Compiled examples:

- `ThreadExperimentalCommissioner`
- `ThreadExperimentalJoiner`
- `ThreadExperimentalMeshcopRestoreProbe`
- `ThreadExperimentalMeshcopWrongPskdJoiner`

### Matter compiles

```bash
arduino-cli compile --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterPaseCryptoTest
```

Result: pass.

```bash
arduino-cli compile --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterPaseCaseFullDemo
```

Result: pass.

```bash
arduino-cli compile --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnNetworkOnOffLightCommandSurfaceDemo
```

Result: pass.

```bash
arduino-cli compile --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnOffLightFoundationCompileTarget
```

Result: pass.

```bash
arduino-cli compile --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnNetworkRebootRecoveryProbe
```

Result: pass.

```bash
arduino-cli compile --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterFoundationProbe
```

Result: pass.

## What Was Not Validated In This Pass

No two-board hardware soak was run after these exact edits in this pass.

The following still require hardware validation:

- Sleepy-child UDP echo with payload sizes 0, 1, 63, 64, 65, and larger than 65 bytes.
- Parent indirect transmit path while child is rx-off.
- ACK frame-pending behavior with actual queued parent data.
- MESHCoP commissioner/joiner success and wrong-PSKd failure on two boards.
- Matter PASE/CASE staging examples on two boards.

## Remaining Thread Work

Thread is much closer than Matter, but it is not finished.

High-priority remaining work:

1. Run a two-board SED parent/child soak after this exact tree.
2. Add a hardware validation sketch that sends UDP payloads across all boundary sizes, especially 64 bytes, to prevent the RX bug from returning.
3. Add a source-match/frame-pending regression test that proves parent indirect data wakes the sleepy child through data polls.
4. Add automatic SWD marker counters for:
   - poll TX count
   - ACK frame-pending true count
   - child UDP RX count
   - child UDP TX count
   - parent indirect enqueue count
   - parent indirect transmit count
5. Keep direct rx-off attach as the default SED path.
6. Re-test MESHCoP commissioner/joiner with two boards after every radio/platform change.

## Remaining Matter Work

Matter is still staging/prototype. Do not call it full Matter support yet.

The major missing pieces are:

1. Real Matter message layer / exchange handling.
2. MRP reliable messaging.
3. Full CHIP-compatible PASE with test vectors.
4. Full CHIP-compatible CASE with certificate validation.
5. Operational credentials, fabric table, node operational identity, and fabric-scoped state.
6. Access control.
7. mDNS/SRP/DNS-SD discovery over Thread.
8. Production factory data and credential provisioning.
9. AES-CCM and full Matter secure session framing.
10. CRACEN-backed P-256 wrapper validated with known ECDSA/ECDH/SPAKE2+ vectors.
11. Clear separation between staging examples and production CHIP-compatible Matter.

## Notes For The Next AI

The file `docs/MATTER_PLATFORM_BOUNDARY_CLEANUP.md` was produced before this audit. It remains useful as a planning/audit document, but it is now partially stale:

- Credential hardcoding has been partially addressed through `MatterCredentials`.
- Random generation in PASE/CASE now uses `MatterRng` backed by CRACEN RNG.
- The hardware crypto, CHIP message layer, mDNS/SRP, certificate/fabric, and production provisioning sections are still valid open work.

Be careful with any outside-session implementation that claims full Matter compatibility. Compile success is not enough for Matter. PASE/CASE must pass known vectors and two-board traffic before being marked complete.

## Recommended Next Slice

The next useful slice is a hardware RX regression test for Thread SED:

1. Flash `ThreadExperimentalSleepyParent` to one XIAO.
2. Flash `ThreadExperimentalSleepyChild` to another XIAO.
3. Send UDP payloads of 0, 1, 63, 64, 65, 100 bytes from parent to child.
4. Confirm the child stays attached as `rx_on_when_idle=0`.
5. Confirm no reset, no marker corruption, and correct RX/TX counts.
6. Read SWD markers with pyOCD before and after traffic.
7. Only after this passes, continue with MESHCoP two-board validation.
