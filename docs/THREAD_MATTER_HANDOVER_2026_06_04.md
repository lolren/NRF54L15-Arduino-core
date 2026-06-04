# Thread and Matter handover - 2026-06-04

Workspace used for this pass:

```text
/home/lolren/Desktop/test_pi_nrf54/NRF54L15-Clean-Arduino-core
```

Local Arduino sketchbook used for testing this repo without installing it:

```bash
rm -rf /tmp/nrf54-local-sketchbook
mkdir -p /tmp/nrf54-local-sketchbook/hardware
ln -s /home/lolren/Desktop/test_pi_nrf54/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean /tmp/nrf54-local-sketchbook/hardware/nrf54l15clean
ARDUINO_DIRECTORIES_USER=/tmp/nrf54-local-sketchbook arduino-cli core list
```

## What was fixed

- PASE/CASE commissioner/commissionee examples now use explicit Thread attach roles.
- Commissioner examples use `beginAsRouter()`.
- Commissionee/joiner examples use `beginAsChild()`.
- The staged Thread core no longer mutates active dataset timestamps for child-only attach. Child-only attach already prevents a node from becoming router/leader; changing the dataset made the child fail to attach to the leader.
- `MatterPaseCommissioningDemo` had a bad `memcpy(msg + 66, g_sessionId, 2)`. It now passes `&g_sessionId`.
- The staged mbedTLS `ssl.h` stub had an invalid field name `p_ biodata`; it is now `p_biodata`.

## Hardware verification

Boards present during the test:

```text
761FDE87 -> /dev/ttyACM1
E91217E8 -> /dev/ttyACM0
```

Fresh build and upload commands used:

```bash
ARDUINO_DIRECTORIES_USER=/tmp/nrf54-local-sketchbook arduino-cli compile --upload \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage \
  --port /dev/ttyACM1 \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterPaseCaseCommissioner

ARDUINO_DIRECTORIES_USER=/tmp/nrf54-local-sketchbook arduino-cli compile --upload \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage \
  --port /dev/ttyACM0 \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterPaseCaseCommissionee
```

Observed serial result after the fix:

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

This confirms the specific bug from `SESSION_COMPLETE_SUMMARY.md` is fixed: the commissionee no longer becomes leader and PASE/CASE no longer loops back to itself.

## Compile verification

Commands run successfully:

```bash
ARDUINO_DIRECTORIES_USER=/tmp/nrf54-local-sketchbook arduino-cli compile \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterPaseCaseCommissionee

ARDUINO_DIRECTORIES_USER=/tmp/nrf54-local-sketchbook arduino-cli compile \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterPaseCaseCommissioner

ARDUINO_DIRECTORIES_USER=/tmp/nrf54-local-sketchbook arduino-cli compile \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterPaseCommissioningDemo

ARDUINO_DIRECTORIES_USER=/tmp/nrf54-local-sketchbook arduino-cli compile \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/ThreadExperimentalJoinerPSK

ARDUINO_DIRECTORIES_USER=/tmp/nrf54-local-sketchbook arduino-cli compile \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/ThreadExperimentalJoinerPSKJoiner

ARDUINO_DIRECTORIES_USER=/tmp/nrf54-local-sketchbook arduino-cli compile \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/ThreadExperimentalUdpPing
```

Also run:

```bash
git diff --check
```

No whitespace errors were reported.

## Remaining Thread and Matter work

### 1. Standard MeshCoP commissioning

Current state:

- `OPENTHREAD_CONFIG_COMMISSIONER_ENABLE` is still `0`.
- `OPENTHREAD_CONFIG_JOINER_ENABLE` is still `0`.
- Current examples use staged/prototype PASE/CASE over Thread UDP, not the real OpenThread MeshCoP DTLS joiner/commissioner path.

Do not mark standard Thread Joiner/Commissioner as complete until this is implemented and hardware tested.

Proper implementation path:

1. Add OpenThread source wrappers for the missing secure commissioning units:
   - `src/core/meshcop/secure_transport.cpp`
   - `src/core/meshcop/commissioner.cpp`
   - `src/core/meshcop/joiner.cpp`
   - `src/core/coap/coap_secure.cpp`
   - any dependent `meshcop`, `coap`, `crypto`, or TLS glue source that link errors expose.
2. Replace the staged mbedTLS headers-only stubs with a real mbedTLS 3.x source subset, or wire OpenThread to a verified in-core TLS/DTLS backend with the exact API OpenThread expects.
3. Only after the source set links cleanly, enable:
   - `OPENTHREAD_CONFIG_COMMISSIONER_ENABLE 1`
   - `OPENTHREAD_CONFIG_JOINER_ENABLE 1`
   - the required secure CoAP / DTLS config options.
4. Test with two boards:
   - commissioner forms a network and accepts a PSKd.
   - joiner starts with PSKd and receives an operational dataset.
   - joiner reboots and restores dataset from settings.
   - commissioner rejects wrong PSKd.
   - rejoin works after commissioner reboot.

### 2. Matter over Thread

Current state:

- Matter foundation compile target exists.
- Staged PASE/CASE demo over Thread UDP passes on two boards after this fix.
- This is still not full CHIP/Matter commissioning.

Proper implementation path:

1. Keep staged examples as diagnostics only.
2. Use the Matter foundation target to expand from compile-only to runtime:
   - event loop integration
   - persistent storage
   - timers
   - entropy/DRBG
   - SPAKE2+/PASE
   - CASE
   - operational credentials
   - exchange manager
   - secure session manager
   - Interaction Model on/off cluster
3. TLS/crypto must be handled through the same verified crypto backend used for Thread secure commissioning where possible.
4. Add one runtime milestone at a time and compile/test each:
   - platform bring-up only
   - on-network UDP transport
   - PASE verifier/session
   - CASE session
   - on/off command between two boards
   - persistence/reboot recovery

### 3. CASE-only examples

The CASE-only examples still contain generic `g_thread.begin()` calls. They were not changed in this pass because their authority roles are less obvious than commissioner/commissionee. If they show the same self-leader behavior, fix them using the same rule:

- role that owns/forms the network uses `beginAsRouter()`.
- peer/device that must join an existing network uses `beginAsChild()`.
- do not mutate the active dataset timestamp to force child behavior.

### 4. Fragmented UDP

`SESSION_COMPLETE_SUMMARY.md` says fragmented UDP remains an expected failure. Do not treat Matter large-payload reliability as complete until:

- fragmentation and reassembly are deterministic,
- out-of-order fragments are handled or explicitly rejected,
- missing fragment timeout is implemented,
- duplicate fragment behavior is defined,
- payload sizes are swept on two boards.

## Test notes

Use `compile --upload`, not plain `upload`, when testing menu options. Plain `arduino-cli upload` can reuse a cached/default artifact and may flash a build with `clean_thread=off`, which gives misleading `thread=disabled` serial output.

Serial capture without reset:

```bash
stty -F /dev/ttyACM1 115200 raw -echo
timeout 30s cat /dev/ttyACM1
```

Reset with pyOCD if needed:

```bash
pyocd reset -W -t nrf54l -u 761FDE87 -m sysresetreq -O auto_unlock=false
pyocd reset -W -t nrf54l -u E91217E8 -m sysresetreq -O auto_unlock=false
```

USB serial can disappear briefly after reset. Wait until `/dev/serial/by-id` returns before opening serial again.

## Do not touch without a reason

- Hardware Serial/USB Serial paths. They were fragile in earlier work and are not part of this Thread/Matter fix.
- BLE power timing while validating Thread/Matter, unless the test explicitly depends on coexistence.
- Board Manager packaging/versioning from this workspace unless the release task explicitly asks for it.
