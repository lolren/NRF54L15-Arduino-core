# Thread and Matter Hardening Status

Date: 2026-07-15

This page records what the staged Thread and Matter options actually provide.
It intentionally separates working platform components from standards and
interoperability claims.

## Support Boundary

### Thread

The staged Thread option builds the imported OpenThread FTD core and supplies
the nRF54 radio, alarm, entropy, settings, reset, commissioner/joiner, SRP, and
UDP platform integration. It is suitable for development and two-board testing.
It has not passed a Thread certification suite or broad border-router and
sleepy-device interoperability testing.

### Matter

The staged Matter option now has useful platform foundations: a monotonic
system clock and timer layer, hardware-entropy-backed cryptography, CHIP packet
buffers, IPv6 address handling, an OpenThread UDP adapter, ACL-aware endpoint
operations, onboarding payloads, and project-specific PASE/CASE experiments.

It is **not a complete standard Matter stack**. The project-specific PASE,
CASE, certificate, and command messages are not wire-compatible replacements
for the upstream Matter Secure Channel, Interaction Model, data model, or
Device Attestation flows. Home Assistant, Google Home, Apple Home, or Matter
certification support must not be claimed from the local demos.

## Implemented Hardening

### OpenThread Platform

- Radio capabilities only advertise features actually provided by the driver;
  CSMA and receive timing remain in the OpenThread software path.
- Entropy uses CRACEN RNG and fails closed. There is no deterministic fallback.
- Dataset updater, parent search, and child supervision are enabled in the
  staged configuration.
- `begin()` and `restart()` preserve settings unless an explicit wipe is
  requested.
- Settings operations check namespace open, index/count bounds, stored value
  type and length, capacity, write completion, wipe completion, and reset
  lifecycle.
- UDP sockets support explicit idempotent close and four independently bound
  local ports.
- The two-board runner binds logical roles to CMSIS-DAP UIDs, supports mixed
  FQBNs, validates both traffic directions separately, and compiles the
  checkout under a `localnrf54` namespace so an installed package cannot
  shadow it.

### Matter Platform

- System timers dispatch safely when callbacks mutate timer/work queues.
- The staged clock uses the core's canonical 64-bit monotonic microsecond
  source. Normal-power builds count SysTick wraps in the interrupt; low-power
  builds read the 64-bit GRTC counter.
- SHA-256, HKDF-SHA256, AES-CCM, P-256 ECDH/ECDSA, and DRBG entry points are
  implemented with mbedTLS and fail-closed CRACEN entropy.
- PBKDF2 accepts the documented 128-byte password and salt bounds, while the
  streaming SHA/HMAC path removes the former fixed-message buffer limit;
  boundary vectors cover both.
- IPv6 parsing validates compression and numeric zone identifiers; fabric
  multicast construction is available.
- The CHIP packet-buffer pool accepts a 1280-byte IPv6 datagram plus header
  reserve. Sixteen 1344-byte entries use less static RAM than the former 32 by
  1024-byte pool.
- The OpenThread UDP adapter supports queued bind, listen, receive, send,
  endpoint close, and a bounded four-endpoint pool.
- Remote reads and commands can carry a complete authenticated subject/fabric/
  node context and enforce View or Operate ACL privilege. Incomplete remote
  identity fails closed.
- Private PASE/CASE demos have fail-closed parsing and state checks, but they
  are not the upstream Matter Secure Channel and are not an interoperability
  or production-security claim.
- Development attestation metadata uses process-local test keys that are
  regenerated at boot and whenever runtime vendor/product identity changes.
  This is a private test format, not a standard Matter DAC chain.

## Build and Host Verification

Run the default cross-board matrix:

```bash
python3 scripts/test_thread_matter_compile_matrix.py
```

Compile all 17 selected recovery, system, crypto, transport, and commissioning
matrix cases:

```bash
python3 scripts/test_thread_matter_compile_matrix.py --full
```

Run a single focused case while iterating:

```bash
python3 scripts/test_thread_matter_compile_matrix.py \
  --case xiao_l15_chip_inet_transport
```

Host regressions:

```bash
python3 scripts/test_core_io_regressions.py
python3 scripts/test_thread_platform_contracts.py
python3 scripts/test_matter_attestation.py
python3 scripts/test_matter_system_layer.py
python3 tests/thread_udp_soak_runner_test.py
python3 tests/thread_meshcop_runner_test.py
python3 tests/matter_inet_transport_runner_test.py
```

The full matrix currently contains 17 selected cases. It covers both XIAO
silicon targets, HOLYIOT-25007 and HOLYIOT-25008, the generic module, and the
nRF54L15 DK staged profiles.

Compile the MeshCoP commissioner, joiner, restore, and wrong-PSKd probes for a
mixed XIAO pair:

```bash
python3 scripts/thread_meshcop_validation.py compile \
  --commissioner-fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage \
  --joiner-fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54lm20b:clean_thread=stage
```

## Two-Board Gates

Use stable probe UIDs when `/dev/ttyACM*` ordering can change.

MeshCoP fresh join, persisted-dataset restore, and wrong-PSKd rejection:

```bash
python3 scripts/thread_meshcop_validation.py all \
  --commissioner-uid <l15-probe-uid> \
  --joiner-uid <lm20a-probe-uid> \
  --commissioner-fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage \
  --joiner-fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54lm20b:clean_thread=stage \
  --timeout 180 --dump-lines
```

The MeshCoP `all` gate is destructive. Before fresh commissioning it performs
a UID-bound chip erase of the Joiner, then reflashes both boards. The
wrong-PSKd sketch also explicitly wipes its Thread settings. The restore phase
between them resets the already-commissioned Joiner without reflashing it, then
verifies that the saved dataset is restored and MeshCoP is not started again.
It validates the local OpenThread MeshCoP callbacks and persistence
path, not Thread certification or an external commissioner's interoperability.

Thread UDP, both unicast directions and multicast:

```bash
python3 scripts/test_thread_udp_soak.py \
  --uid1 <l15-probe-uid> \
  --fqbn1 nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage \
  --uid2 <lm20a-probe-uid> \
  --fqbn2 nrf54l15clean:nrf54l15clean:xiao_nrf54lm20b:clean_thread=stage \
  --require-fragmentation --timeout 240
```

CHIP packet-buffer and Inet adapter over the same mixed-board Thread network:

```bash
python3 scripts/test_matter_inet_transport.py \
  --uid1 <l15-probe-uid> --uid2 <lm20a-probe-uid> --timeout 240
```

The Inet gate is destructive: its sketch wipes Thread settings on both boards.
It also assigns fixed roles, with XIAO nRF54L15 as leader and XIAO nRF54LM20A
as child. It requires a common partition and distinct RLOC16 values, performs a
small multicast discovery probe, then echoes unicast payloads of 8, 64, 512,
960, and 1200 bytes in both directions through the CHIP endpoint callback.

The Thread soak owns and checks each unicast direction plus multicast
separately. The UDP and Inet gates validate local Thread and UDP/Inet transport;
they do not validate PASE, CASE, ACL, SRP/DNS-SD, fabric persistence, or
controller interoperability.

### 2026-07-15 Hardware Snapshot

- Mixed XIAO Thread soak: L15 leader `0x9C00`, LM20A child `0x9C01`, partition
  `0x3B19B1F4`; 8, 16, 31, 63, 95, 127, 191, 255, and 512-byte payloads passed
  uplink, downlink, and multicast. Logs:
  `build/thread-udp-soak-validation/20260715-140259`.
- CHIP Inet gate: L15 leader `0x1C00`, LM20A child `0x1C01`, partition
  `0x546CD185`; multicast discovery and 8, 64, 512, 960, and 1200-byte
  bidirectional unicast payloads completed with `pass=5 fail=0`. Logs:
  `build/matter-inet-transport-validation/20260715-140506`.
- MeshCoP gate: fresh commissioning completed with Joiner callback
  `OT_ERROR_NONE`; a settings-preserving reset restored the dataset and the
  LM20A reattached as child `0x5001`; a clean wrong-PSKd attempt returned
  `OT_ERROR_SECURITY` without finalize, acceptance, or dataset persistence.
  Logs: `build/thread-meshcop-validation/20260715-144929`.
- Sleepy-child current, multi-hop, external border-router, standards-compliant
  Matter controllers, and formal conformance remain in the work list below.

## Remaining Work

1. Integrate the upstream Matter Secure Channel, exchange/session manager,
   Interaction Model, generated data model, and standard Device Attestation
   provider instead of extending the private demo protocol.
2. Connect the Inet/System/Crypto platform layers to that upstream server and
   commission through a real OTBR plus at least two independent controllers.
3. Validate SRP/DNS-SD records from the border-router side, including removal,
   reboot recovery, and commissioning-window transitions.
4. Run OpenThread conformance, multi-hop, interference, long-duration reattach,
   broader commissioner/joiner negative and interoperability cases, and
   sleepy-child power tests.
5. Add a journaled or dual-bank Preferences backend and run controlled
   brownout/power-cut recovery tests. The current OpenThread directory preserves
   the old mapping across reported storage API failures, but the shared RRAM
   blob is not an all-or-nothing power-loss transaction.
6. Add production credential provisioning and multi-fabric lifecycle support;
   generated development keys are not product credentials.
7. Decide and implement a standards-compatible Matter BLE commissioning path.

## Change Rules

- Compile and flash from a distinct local vendor namespace; never trust an
  installed Board Manager package to represent the checkout.
- Keep destructive settings wipes explicit.
- Require source/native regressions for platform contracts and two-board logs
  for radio/transport behavior.
- Do not describe local SRP readiness or private PASE/CASE demos as successful
  Home Assistant or standard Matter commissioning.
- Do not publish a release solely from compile success; external
  interoperability and protocol boundaries must remain visible.
