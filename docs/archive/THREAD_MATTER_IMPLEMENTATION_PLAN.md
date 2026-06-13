# Thread And Matter Implementation Plan

Status baseline:

- Audit date: `2026-04-26`
- Reality re-audit: `2026-06-04`
- First supported board target: `XIAO nRF54L15 / Sense`
- First runtime direction: `CPUAPP-first`
- Thread upstream: `OpenThread`
- Matter upstream: `connectedhomeip`
- Radio backend: existing `ZigbeeRadio` / IEEE 802.15.4 path

This is the current public plan for Thread and Matter in this repo. Older
phase-by-phase scratch notes were removed because they were written before the
staged OpenThread and Matter foundation work landed.

June 2026 audit note: the staged tree now includes a local mbedTLS stage source
set plus OpenThread wrappers for Commissioner, Joiner, CoAP Secure, secure
transport, crypto storage, and MeshCoP. Standard MeshCoP is no longer merely a
stub in this local tree: the current XIAO two-board path has formed a
leader/child pair through the staged Joiner/Commissioner flow. It is still
experimental until reboot persistence, wrong-PSKd negative testing, reference
network attach, and longer soaks are clean. Matter PASE/CASE examples remain
staged Arduino protocol exercises over Thread UDP, not production upstream CHIP
commissioning.

## Current Claim Level

| Area | Claim today | Not claimed yet |
|---|---|---|
| Thread | Experimental staged OpenThread path with fixed dataset, leader/child/router paths, PSKc/passphrase helpers, UDP examples, dataset-restore diagnostics, reboot-recovery probe, staged mbedTLS/MeshCoP wrappers, and a passing all-Thread-example compile pass. | Production Thread stack, reference-network attach, reboot recovery validation, sleepy-device depth, long soak, and external interop. |
| Matter | Foundation-only on-network/on-off-light shape with onboarding helpers, Thread dataset export/import seam, Thread restore diagnostics/probe, Preferences-backed factory data, structured staged DNS-SD/SRP records, publish lifecycle diagnostics, staged PASE/CASE demos over Thread UDP, and a passing all-Matter-example compile pass. | Production Matter commissioning, real mDNS/SRP registration, commissioner/Home Assistant control, upstream CHIP secure-session integration, reboot/reconnect recovery validation. |
| VPR | Available as a future offload seam, not the first Thread/Matter owner. | VPR-owned Thread radio/controller or Matter runtime. |

## Architecture Decisions

- `CPUAPP` owns the first OpenThread core and Matter foundation path.
- `ZigbeeRadio` remains the first IEEE 802.15.4 backend for Thread.
- `VPR` is intentionally out of the first Thread radio path.
- `Preferences` is the first settings/persistence backend.
- `CracenRng` provides entropy.
- Existing CRACEN-backed and software fallback crypto glue is used only where
  the staged upstream paths need it.
- Thread Border Router is out of scope for this repo; use an external border
  router for product networks.
- Matter BLE rendezvous is out of first-pass scope; first Matter direction is
  on-network Thread commissioning.

## Phase Checklist

| Box | Phase | Status | Remaining work |
|---|---|---|---|
| [x] | 0. Ownership freeze | Done | Keep ownership docs synchronized with code constants. |
| [x] | 1. OpenThread platform skeleton | Done | Maintain compile coverage when upstream snapshots change. |
| [x] | 2. Real 802.15.4 radio backend | Done | Keep Zigbee regression coverage because Thread shares the same radio path. |
| [x] | 3. Experimental Thread runtime | Partial / experimental | Fixed dataset, role, and UDP examples exist; production validation remains open. |
| [x] | 4. Arduino Thread wrapper | Partial / experimental | Keep API explicitly experimental until reference-network and reboot tests pass. Joiner and Commissioner APIs now build against staged OpenThread MeshCoP/DTLS support and have a local two-board success path, but are not production claimed. |
| [x] | 5. Matter foundation | Foundation done | On-network on/off-light, encrypted IM over Thread (2-board), staged PASE/CASE demos, Thread dataset source/export readiness, Preferences-backed factory data, PBKDF2-HMAC-SHA256, and software secp256r1 ECC paths exist. CRACEN IKG keygen 0ms; PK engine needs proprietary microcode. |
| [ ] | 6. Matter commissioning | Staged only | PASE/CASE demo traffic can run over Thread UDP, but this is not upstream CHIP commissioning. | Wire upstream CHIP PASE/CASE secure sessions, real mDNS/SRP, commissioner interop, Home Assistant validation, reboot recovery. |
| [ ] | 7. Hardening | Not done | Soak tests, failure recovery, storage migration, interop matrix, and docs for production limits. |

## Thread Next Ticks

- [x] PSK Joiner/Commissioner implemented (MAC verified with PSKd derivation)
- [x] Add self-contained saved-dataset restore probes for Thread and Matter-on-network
- [x] Make staged OpenThread key-reference storage available in mbedTLS mode
- [x] Compile every Thread example with `clean_thread=stage`
- [x] Prove staged MeshCoP Joiner/Commissioner can form a local two-board leader/child pair
- [x] Add `ThreadExperimentalMeshcopRestoreProbe` for post-commissioning restore validation
- [x] Add `ThreadExperimentalMeshcopWrongPskdJoiner` for negative MeshCoP validation
- [x] Add `scripts/thread_meshcop_validation.py` to compile and optionally run the fresh-join, restore, and wrong-PSKd MeshCoP probes against this local checkout
- [ ] Run three complete two-board `thread_meshcop_validation.py all` passes from clean settings and save logs
- [ ] Validate attach to a reference Thread network through an external border router
- [ ] Add two-board reboot recovery test for saved dataset/settings
- [ ] Treat wrong-PSKd negative testing as passed only after the harness proves failed Joiner does not persist a dataset
- [ ] Expand sleepy-device behavior beyond the current staged runtime
- [ ] Keep Zigbee examples green while Thread shares the 802.15.4 backend

## Matter Next Ticks

- [x] On/off-light model integrated (verified 2-board encrypted IM over Thread)
- [x] Staged PASE SPAKE2+ demo flow implemented and verified on 2 boards
- [x] Staged CASE Sigma demo protocol implemented with message fragmentation
- [x] Thread PSK Joiner/Commissioner implemented (MAC verified)
- [x] Matter platform exposes configured/demo/restored/active Thread dataset source diagnostics
- [x] Matter platform persists/clears factory data through `Preferences`
- [x] Compile every Matter example with `clean_thread=stage,clean_matter=stage`
- [ ] Replace staged PASE/CASE demos with upstream CHIP secure-session integration
- [ ] Enable real mDNS/SRP registration and prove discovery from a commissioner
- [ ] Validate with Home Assistant
- [ ] Prove reboot/reconnect recovery after commissioning

## Evidence Pointers

- Thread examples live under
  `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread`.
- Matter examples live under
  `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter`.
- Current status rollup lives in
  `docs/NRF54L15_FEATURE_MATRIX.md`.
- Host-side staged MeshCoP validation harness lives in
  `scripts/thread_meshcop_validation.py`.
- Runtime ownership is documented in
  `docs/THREAD_RUNTIME_OWNERSHIP.md` and
  `docs/MATTER_RUNTIME_OWNERSHIP.md`.

## Do Not Claim Yet

- Production Thread support.
- Thread Border Router support.
- Matter commissioning.
- Matter Home Assistant support.
- Matter BLE rendezvous.
- VPR-owned Thread or Matter runtime.
