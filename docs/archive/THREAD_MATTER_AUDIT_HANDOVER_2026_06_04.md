# Thread/Matter Audit Handover - 2026-06-04

This handover is for the work tree at:

`/home/lolren/Desktop/test_pi_nrf54/NRF54L15-Clean-Arduino-core`

The folder contains several historical summaries from another model. Some of
those summaries are useful context, but several overclaim what exists in the
current code. Treat this document as the current source of truth for the audited
state.

## Executive State

What is proven now:

- Experimental OpenThread stage mode compiles through the Arduino core.
- Fixed-dataset Thread attach, leader/router/child roles, and UDP examples are
  present.
- Staged PASE/CASE Arduino demos over Thread UDP compile.
- PASE/CASE commissioner/commissionee role bug has been fixed by using explicit
  router/child attach roles.
- CASE-only initiator/responder examples now also use explicit roles and pass a
  two-board hardware run.
- Standard MeshCoP Joiner/Commissioner examples compile, but correctly report
  unsupported at runtime because secure transport/DTLS is not compiled in.

What is not proven and must not be claimed:

- Production Thread support.
- Standard MeshCoP Joiner/Commissioner.
- A working DTLS secure transport backend.
- A real mbedTLS source/static-library integration.
- Production Matter commissioning through upstream CHIP PASE/CASE secure
  sessions.
- Real Matter mDNS/SRP discovery, Home Assistant commissioning, or commissioned
  reboot/reconnect recovery.

## Code Reality Check

The current tree does not contain a real mbedTLS build:

- No `src/mbedtls_build/libmbedtls.a`.
- No full mbedTLS C source build integrated into Arduino.
- No `platform/crypto/psa_crypto.h` / `psa_crypto_sw.cpp` tree claimed by old
  scratch notes.
- Only reduced headers/stubs exist under
  `third_party/openthread-core/third_party/mbedtls/repo/include/mbedtls`.

Current OpenThread config still has standard commissioning disabled:

- `OPENTHREAD_CONFIG_COMMISSIONER_ENABLE 0`
- `OPENTHREAD_CONFIG_JOINER_ENABLE 0`
- `OPENTHREAD_CONFIG_BORDER_AGENT_ENABLE 0`
- `OPENTHREAD_CONFIG_BORDER_ROUTER_ENABLE 0`
- `OPENTHREAD_CONFIG_SRP_CLIENT_ENABLE 0`

The `Nrf54ThreadExperimental` APIs for Joiner/Commissioner are compile-safe
front doors. They return unsupported when the corresponding OpenThread config is
off. That is the correct behavior until DTLS is real.

## Fixes Applied In This Audit Pass

Example role fixes:

- `MatterCaseInitiator` now joins as child.
- `MatterCaseResponder` now forms as router/leader.
- `MatterCaseTwoBoardDemo` uses responder as router and initiator as child.
- `MatterCaseOperationalDemo` uses light/server as router and controller as
  child.
- `MatterCommissionAndControl` uses commissioner as router and joiner as child.
- `MatterOnOffLightTwoBoardDemo` uses light node as router and controller as
  child.

Correctness cleanup:

- CASE demos no longer initialize a 16-byte payload with a 17-byte C string
  literal. The encrypted `CASERESPONDER_OK` payload is now an explicit 16-byte
  byte array.

Documentation steering:

- Historical docs now contain audit notes warning that the mbedTLS / MeshCoP /
  Matter commissioning claims were stale.
- The main Thread/Matter implementation plan now describes staged PASE/CASE as
  Arduino demo traffic, not production Matter commissioning.
- The feature matrix now says `Staged PASE/CASE protocol demos`, not production
  PASE commissioning.

Relevant fixes already present in the same dirty work tree from the previous
audit pass:

- `nrf54_thread_experimental.cpp` no longer mutates the active dataset timestamp
  to force child behavior. Child/router role is controlled through attach
  policy, which is the right fix.
- PASE/CASE commissioner/commissionee examples use explicit roles.
- `MatterPaseCommissioningDemo` uses `memcpy(msg + 66, &g_sessionId, 2)`.
- The staged mbedTLS `ssl.h` typo `p_ biodata` was fixed to `p_biodata`.

## Compile Verification

Use a local sketchbook symlink so Arduino CLI builds this working tree, not the
installed package cache:

```bash
rm -rf /tmp/nrf54-local-sketchbook
mkdir -p /tmp/nrf54-local-sketchbook/hardware
ln -s /home/lolren/Desktop/test_pi_nrf54/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean \
  /tmp/nrf54-local-sketchbook/hardware/nrf54l15clean
ARDUINO_DIRECTORIES_USER=/tmp/nrf54-local-sketchbook arduino-cli core list
```

Matter staged FQBN used:

```bash
nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage
```

Thread staged FQBN used:

```bash
nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage
```

Examples compiled successfully in this pass:

- `MatterCaseInitiator`
- `MatterCaseResponder`
- `MatterCaseTwoBoardDemo`
- `MatterCaseOperationalDemo`
- `MatterCommissionAndControl`
- `MatterOnOffLightTwoBoardDemo`
- `ThreadExperimentalJoinerPSK`
- `ThreadExperimentalJoinerPSKCommissioner`
- `ThreadExperimentalJoinerPSKJoiner`
- `ThreadExperimentalCommissioner`
- `ThreadExperimentalJoiner`
- `ThreadExperimentalCommissionerJoinerDemo`

The CASE string-literal warning is fixed and the affected examples recompiled
cleanly.

## Hardware Verification

Boards detected:

- `761FDE87` on `/dev/ttyACM1`
- `E91217E8` on `/dev/ttyACM0`

Representative CASE two-board run:

```bash
BASE=/home/lolren/Desktop/test_pi_nrf54/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter
FQBN='nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage'

ARDUINO_DIRECTORIES_USER=/tmp/nrf54-local-sketchbook \
  arduino-cli compile --upload --fqbn "$FQBN" --port /dev/ttyACM1 \
  "$BASE/MatterCaseResponder"

ARDUINO_DIRECTORIES_USER=/tmp/nrf54-local-sketchbook \
  arduino-cli compile --upload --fqbn "$FQBN" --port /dev/ttyACM0 \
  "$BASE/MatterCaseInitiator"
```

Observed serial:

```text
initiator: case decrypt: CASERESPONDER_OK
initiator: case sigma3 sent, CASE DONE!
initiator: case role=init thread=child udp=1 id=1 eph=1 case=DONE key[0]=0x48
responder: case role=resp thread=leader udp=1 id=1 eph=1 case=DONE key[0]=0x48
```

The capture was held for roughly two minutes. Roles remained stable:

- Initiator stayed `child`.
- Responder stayed `leader`.
- Both stayed `case=DONE`.
- Both reported the same session key byte `0x48`.

Earlier previous-pass hardware evidence for `MatterPaseCaseCommissioner` /
`MatterPaseCaseCommissionee` also passed after the role fix:

```text
Commissioner:
CASE sig2 OK 1409ms
CASE decrypt: CASERESP_OK
CASE DONE (initiator)
CASE complete!
C P=1 C=1 leader

Commissionee:
E P=1 C=1 child
```

## Important Testing Notes

Use `compile --upload` for menu-option tests. A plain `arduino-cli upload` can
reuse a cached artifact and silently flash a build that does not include
`clean_thread=stage` or `clean_matter=stage`.

Serial capture helper:

```bash
python3 - <<'PY'
import serial, time, select, sys
ports = [('/dev/ttyACM1','board1'),('/dev/ttyACM0','board2')]
serials = []
for path, name in ports:
    ser = serial.Serial(path, 115200, timeout=0)
    serials.append((ser, name))
    print(f'OPEN {name} {path}')
buffers = {id(ser): b'' for ser, _ in serials}
end = time.time() + 120
while time.time() < end:
    fds = [ser.fileno() for ser, _ in serials]
    r, _, _ = select.select(fds, [], [], 0.2)
    for ser, name in serials:
        if ser.fileno() not in r:
            continue
        data = ser.read(4096)
        if not data:
            continue
        key = id(ser)
        buffers[key] += data
        while b'\n' in buffers[key]:
            line, buffers[key] = buffers[key].split(b'\n', 1)
            print(f'{time.time():.3f} {name}: '
                  f'{line.decode("utf-8", "replace").rstrip()}')
            sys.stdout.flush()
for ser, _ in serials:
    ser.close()
PY
```

## Next Correct Implementation Path

### Thread MeshCoP / DTLS

Do not “fix” MeshCoP by replacing headers only. That creates compile illusions
and no runtime DTLS.

Required work:

- Bring in a real mbedTLS source subset or another verified DTLS backend.
- Build it as C or a correct C-compatible static library for the Arduino
  package.
- Wire OpenThread secure transport wrappers only after the backend symbols are
  real.
- Enable `OPENTHREAD_CONFIG_COMMISSIONER_ENABLE` and
  `OPENTHREAD_CONFIG_JOINER_ENABLE` only after link and runtime tests pass.
- Validate with a real commissioner/joiner path, not just the staged PSK UDP
  demo.

Minimum tests before claiming done:

- Commissioner board starts MeshCoP commissioner and reports active session id.
- Joiner board starts standard OT Joiner without a preloaded dataset.
- Joiner joins using PSKd through DTLS.
- Dataset persists and joiner reattaches after reboot.
- Negative PSKd test fails cleanly.

### Matter

The current PASE/CASE examples are useful protocol and crypto exercises, but
they are not production Matter commissioning.

Required work:

- Route PASE/CASE through upstream connectedhomeip secure-session primitives.
- Keep the existing secp256r1/software crypto optimization, but plug it into the
  CHIP CryptoPAL/secure-session path rather than sketch-local protocol code.
- Implement or integrate real mDNS/SRP publication.
- Validate Home Assistant or another real Matter commissioner.
- Add commissioned reboot/reconnect recovery tests.

### Reference Thread Network

Before removing “experimental” from Thread:

- Import a real OTBR active dataset as TLVs/hex.
- Attach to the external network as child.
- Test IPv6 UDP to/from the OTBR or another Thread node.
- Reboot and verify settings restore without re-seeding the dataset.

## Steering For Future Agents

- Do not touch Serial unless the user explicitly asks. It has had regressions.
- Do not mutate dataset fields to force roles. Use `beginAsRouter()` and
  `beginAsChild()`.
- Do not create a second repo or branch unless explicitly asked.
- Do not claim standard Thread commissioning until OpenThread DTLS is real and
  hardware tested.
- Do not claim production Matter commissioning until upstream CHIP secure
  sessions and commissioner interop work.
- Prefer small role/API fixes and hardware proof over broad rewrites.
- Keep old scratch docs clearly marked as stale when their claims no longer
  match code.

## Quick Audit Commands

Check whether real mbedTLS was added:

```bash
find hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation \
  -type f \( -name '*.a' -o -name 'libmbedtls*' -o -name '*mbedtls*.c' \) | sort
```

Check remaining generic Thread starts:

```bash
rg -n 'g_thread\.begin\(|gThread\.begin\(' \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples
```

Generic `begin()` is acceptable in diagnostics and child-first demos. In
two-board role-specific examples, prefer explicit `beginAsRouter()` or
`beginAsChild()`.

