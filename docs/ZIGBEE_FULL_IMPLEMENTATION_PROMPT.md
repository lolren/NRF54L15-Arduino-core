# Direct Implementation Prompt: Finish Zigbee Correctly

You are taking over an existing, dirty nRF54 Arduino Core working tree at:

```text
/home/lolren/Desktop/eport_nrf54/nrf54-arduino-core
```

Your objective is to implement the missing production Zigbee stack work for
nRF54L15 and nRF54LM20A/LM20B without losing the crucial preparation already in
the tree. Work autonomously through implementation, tests, and available
hardware validation. Do not merely write another plan.

## Mandatory Preflight

Before editing any source:

1. Read `docs/ZIGBEE_FULL_IMPLEMENTATION_HANDOVER.md` completely.
2. Read `docs/ZIGBEE_COMPLETION_IMPLEMENTATION_PLAN.md` completely.
3. Read `git status --short`, `git diff --stat`, and the complete current diff.
4. Read all current Zigbee sources, new untracked Zigbee files, CI, and tests.
5. Read the shared BLE/OpenThread/Channel Sounding RADIO, CLOCK, POWER, TIMER,
   DPPI, IRQ, RRAM, and System OFF paths before changing shared hardware.
6. Read the local Nordic product specifications/schematics in
   `/home/lolren/Desktop/eport_nrf54/datasheets` and the official applicable
   errata.
7. Pin exact IEEE 802.15.4, Zigbee Core R22.1, BDB 3.0.1, ZCL R8, device-library,
   and test-spec revisions and hashes. Create/update a requirement/PICS ledger.
8. Run all existing Zigbee host contracts and representative L15/LM20, BLE,
   OpenThread, and Channel Sounding compiles to establish a baseline.

The current working tree, not tag `v1.0.3`, is the authoritative starting point.
It contains many uncommitted changes. Never run `git reset --hard`, broad
`git restore`, or checkout/reclone over it. Preserve unrelated user changes.

## Non-Negotiable Rules

- Implement code, tests, hardware validation fixtures, and accurate docs; do not
  stop at analysis while implementable work remains.
- Do not claim a layer complete because codecs compile or two direct boards
  exchange a frame.
- Never claim full Zigbee until MAC, NWK, APS, security/persistence, BDB, ZDO,
  selected ZCL, multi-hop, interoperability, and certification gates pass.
- Complete one coherent R22.1/BDB 3.0.1/ZCL R8 baseline first. Do not mix in
  R23.2/BDB 3.1/Zigbee 4.0 behavior until the first baseline is stable.
- Keep Zigbee experimental/default-off until its release gates pass.
- Do not enable concurrent BLE+Zigbee merely because an exclusive lease exists.
  Concurrent support needs a deadline scheduler and qualification.
- Do not copy proprietary/reference stack source. Record license and exact
  revision for any open implementation reference.
- Do not invent standard clauses, constants, errata applicability, or hardware
  behavior. Mark and resolve uncertainty from authoritative documents.
- Do not weaken fail-closed security, counter, persistence, ownership, DMA, or
  timing behavior to make a test pass.
- Do not invoke arbitrary callbacks, crypto, storage, Serial, allocation, or
  full protocol parsing in an ISR.
- Do not add heap allocation after stack begin in the default embedded profile.
- Do not commit generated UF2 files, build trees, logs, captures, credentials,
  keys, install codes, or local measurement output unless they are explicitly
  reviewed public fixtures.
- Do not tag, publish package indexes, create a GitHub release, or bump/release a
  version unless the user explicitly asks after implementation and validation.

## Preserve And Build On Existing Preparation

The dirty tree already includes preparation that must not regress:

- compile-time default-off Zigbee feature gate on all boards;
- capacity-bearing/atomic current codecs and sanitizer tests;
- authenticated-before-copy CCM parsing and a NIST CCM vector;
- version-7 direct-RRAM A/B journal with collision/tombstone/corrupt statuses;
- crash-safe NWK and APS Trust Center 64-counter high-water allocation;
- limited coordinator per-peer NWK replay persistence;
- exclusive generation-qualified RADIO ownership for five clients;
- strict Disabled/DMA scrub/quarantine teardown;
- XOTUNED readiness and anomalies 20/39 handling;
- atomic CCAIDLE-to-TX and duration ED scan;
- CRC-qualified TIMER10/DPPIC10 immediate legacy ACK scheduling;
- fail-closed counter/storage integration in 17 examples;
- CSPRNG coordinator key generation and explicit unsupported key rotation.

Treat these as tested foundations, not as full-stack completion.

## Required Implementation Order

Work in the dependency order below. Split each phase into bounded work units and
continue autonomously while meaningful independent work remains.

### 1. Freeze requirements and baseline evidence

- Build a clause/PICS/source/test/evidence ledger.
- Capture existing packet bytes and direct behavior.
- Run and record baseline tests/compiles.
- Correct stale docs without changing claims beyond evidence.

### 2. Complete memory-safety and native infrastructure

- Finish checked reader/writer/span use across every current parser/builder.
- Test truncation at every byte and capacity at every boundary.
- Add ASan/UBSan and fuzz targets for every untrusted decoder.
- Add compiler-resistant secure erase and full official crypto vectors.
- Add deterministic fake clock, RNG, storage, radio/peripheral, and topology
  interfaces.

### 3. Extract pure codecs without behavior changes

- Move one symbol family per commit from `zigbee_stack.cpp`.
- Keep the compatibility facade and exact golden bytes.
- Never mix a mechanical move with a protocol behavior change.

### 4. Implement the shared event-driven runtime

- Fixed generation-tagged event, timer, operation, transaction, and buffer
  pools.
- Typed statuses and slot+generation handles.
- `process()`, `nextDeadlineUs()`, cancellation, structured diagnostics.
- Application callbacks only in process context.
- No heap after begin.

### 5. Implement protocol-neutral async IEEE 802.15.4 PHY and full MAC

- Preserve nRF54 packet/CRC/PHYEND/errata/DMA invariants.
- Replace runtime use of blocking radio calls with async request/confirm APIs.
- Add explicit buffer ownership and LQI/CRC/RSSI/hardware timestamps.
- Replace full-frame ISR callbacks with staged BCC/BCMATCH prefix filtering and
  precomputed address/Frame Pending state.
- Measure and enforce immediate ACK timing on L15 and LM20.
- Implement PIB, unslotted CSMA-CA, random backoff, CCA, retries, ACK window,
  duplicate table, SIFS/LIFS, scans, association/disassociation, indirect
  transactions, child polling, expiry, cancellation, and typed confirms.
- Keep MPDU/DSN/security material unchanged across link retries.

### 6. Complete security identity and persistence

- Crypto provider abstraction with software and CRACEN equivalence.
- Typed network/link/Trust Center/install-code/transient key store.
- Per-peer/per-layer/per-key replay protection.
- Outgoing high-water allocators for every required counter domain.
- Separate critical journal records where atomicity/wear requires it.
- Full fault-injection and physical brownout/no-nonce-reuse tests.
- Move durable Trust Center device/auth/address state out of examples.
- Implement complete selected key command and conformant key-rotation lifecycle.

### 7. Complete NWK

- Complete selected header/command codecs.
- Neighbor/child/address/route/discovery/broadcast/source-route tables.
- Formation/start, local delivery, forwarding, radius, route discovery/repair,
  route aging, broadcast duplicate suppression, many-to-one, route record,
  source route, conflicts, channel/network updates, leave/rejoin/restart.
- Deterministic multi-hop topology simulator.

### 8. Complete APS

- Data/command/ACK transaction manager.
- ACK timeout/retry/cancel and duplicate rejection.
- Binding/group fan-out and statuses.
- Fragmentation/reassembly if selected by PICS, with bounded quotas and timers.
- Unified secured data/command processing and replay.

### 9. Complete BDB, Trust Center lifecycle, and ZDO

- Event-driven factory-new/restored/formation/steering/rejoin/leave/finding and
  binding state hierarchy.
- Persisted permit-join and restart state.
- Endpoint/descriptor registry.
- Bounded ZDO transaction manager and all selected mandatory client/server
  services, pagination, matching, timeout, and statuses.
- Remove private coordinator protocol/security runtime from sketches.

### 10. Complete ZCL foundation, clusters, and examples

- Full selected ZCL R8 type registry and checked codec.
- Endpoint-scoped cluster/attribute metadata and persistence policy.
- Complete selected global/foundation commands and reporting semantics.
- Complete every currently advertised HA cluster/device before new claims.
- Convert shipped examples to small declarative users of the shared runtime.
- Preserve example behavior with tests; remove duplicate paths only deliberately.

### 11. Complete sleepy behavior and optional selected work

- Indirect parent queues, Poll Control if selected, System OFF deadlines, parent
  loss, rejoin, reporting, reboot, and security persistence.
- OTA only with cluster, image validation, storage, bootloader, resume, rollback,
  and per-board proof.
- Green Power/Direct/Touchlink/NCP only as separately selected later tracks.

### 12. Qualification

- Full host/fuzz/model suite.
- L15 and LM20 compile matrix for every shipped Zigbee example.
- Two-board mixed-SoC direct matrix in both role directions.
- Multi-hop physical topology.
- Independent coordinator/device ecosystems and sniffer captures.
- RF/CCA/ED/PER/ACK timing/power evidence.
- Reset, power-cut, table-full, loss/reorder, long-soak campaigns.
- ZUTH and selected PICS mapping.

## Hardware Requirements

Start each hardware session with:

```bash
pyocd list
```

Use both connected nRF54 boards when present. Record probe UID, board, build
commit/diff, FQBN/options, role, channel/PAN/address, serial logs, and capture.
Test L15->LM20 and LM20->L15.

At minimum validate channels 11/15/20/23/26, PSDU boundaries, good/bad CRC,
correct/wrong PAN/address, broadcast/unicast, all basic frame types, ACK/DSN/
Frame Pending, queue full, late ACK drop, duplicates/retries, CCA idle/busy, ED
duration/conversion, cancel/end/begin stress, owner switching, System OFF,
rejoin, reset, and counter monotonicity.

Use a trusted sniffer or timer/logic-analyzer capture for the 192 us immediate
ACK target. Two boards alone cannot prove multi-hop or certification; state that
limitation plainly and build the required larger lab instead of claiming success.

## Existing Regression Commands

Run these directly and add focused tests as work proceeds:

```bash
python3 scripts/test_zigbee_codec_security_regressions.py
python3 scripts/test_zigbee_codec_capacity_contracts.py
python3 scripts/test_zigbee_security_hardening.py
python3 scripts/test_zigbee_feature_gate.py
python3 scripts/test_zigbee_persistence_journal.py
python3 scripts/test_zigbee_coordinator_security_contracts.py
python3 scripts/test_zigbee_radio_hardware_contracts.py
python3 scripts/test_exclusive_radio_arbiter_contracts.py
python3 scripts/test_zigbee_validation_contracts.py
python3 scripts/test_ble_background_connection_handoff.py
python3 scripts/test_ble_connection_regression_contracts.py
python3 scripts/test_ble_controller_state.py
python3 scripts/test_ble_robust_caching_contracts.py
python3 scripts/test_ble_security_policy_contracts.py
python3 scripts/test_ble_multibond_contracts.py
python3 scripts/test_core_io_regressions.py
```

Use warning-free Arduino CLI builds with distinct build paths for L15 Zigbee,
LM20 Zigbee, BLE HID normal/trace, OpenThread radio, raw Channel Sounding, and
MPSL Channel Sounding whenever shared hardware code changes.

## Work And Commit Discipline

For every bounded work unit:

1. Cite the exact selected normative requirement.
2. Add or identify the failing test.
3. Implement the smallest coherent change.
4. Run focused, adjacent, and shared-hardware regressions.
5. Compile L15 and LM20 warning-free where applicable.
6. Run applicable hardware validation.
7. Update the ledger and factual documentation.
8. Run `git diff --check` and inspect `git status --short`.
9. Remove generated source-tree artifacts.
10. Commit one logical unit with a precise message.

Do not batch an architectural move, wire change, timing change, and example
rewrite in one commit. Do not overwrite or revert unrelated dirty changes. If
the initial dirty preparation has not yet been committed, verify and group it
into coherent preparation commits without dropping any part or pretending you
authored unverified behavior.

## Completion Reporting

At every milestone report:

- exact commits/files changed;
- requirements completed;
- tests and compiles actually run with results;
- hardware boards/captures actually used;
- remaining gaps and unavailable equipment;
- any residual risk.

Use `implemented and unit-tested`, `hardware-validated`, `interoperability-
validated`, and `qualified` as separate statuses. Never write `full Zigbee`,
`complete`, `production ready`, or `certified` until every selected gate in the
handover is objectively satisfied.

Continue autonomously through all implementable phases. If equipment,
normative access, or a true external dependency blocks a gate, record the exact
blocker, do not weaken the gate, and proceed with independent work. Do not
publish a release unless the user explicitly requests it after reviewing the
final evidence.

