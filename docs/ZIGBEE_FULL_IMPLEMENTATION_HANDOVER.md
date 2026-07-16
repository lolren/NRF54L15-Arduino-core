# Zigbee Full Implementation Handover

## 1. Purpose

This document hands the current nRF54 Arduino Core Zigbee work to an
implementing engineer or AI. It is deliberately stricter than a feature list.
It records:

- the exact dirty working-tree baseline that must be preserved;
- preparation already implemented and what that preparation does not prove;
- the required nRF54L15/nRF54LM20 hardware contracts;
- all remaining MAC, NWK, APS, security, persistence, BDB, ZDO, ZCL, OTA, test,
  interoperability, and certification work;
- a dependency-ordered implementation sequence;
- proof required before any layer or release may be called complete.

The companion planning document is
`docs/ZIGBEE_COMPLETION_IMPLEMENTATION_PLAN.md`. Read both documents, then read
the current source. Where prose and source disagree, investigate and update the
prose; never silently discard working source based on an older statement.

This handover does **not** claim that Zigbee is complete. The current tree has
substantial safety and hardware preparation, but it is still an experimental,
blocking, direct-link implementation without a production full-stack runtime.

## 2. Mandatory Read Order

Before modifying code:

1. Read this handover completely.
2. Read `docs/ZIGBEE_COMPLETION_IMPLEMENTATION_PLAN.md` completely.
3. Read the current diff with `git diff` and `git diff --stat`.
4. Read all new/untracked Zigbee files and tests.
5. Read the nRF54L15 and nRF54LM20 product specifications in
   `/home/lolren/Desktop/eport_nrf54/datasheets`.
6. Read the relevant Nordic errata for the exact silicon variants.
7. Pin the exact Zigbee Core, BDB, ZCL, IEEE 802.15.4, device-library, and test
   specification revisions in a normative ledger before implementing behavior.
8. Read the current OpenThread radio PAL and BLE/Channel Sounding timing code
   before changing shared RADIO, CLOCK, POWER, TIMER, DPPI, RRAM, or IRQ code.
9. Run the existing host contract tests and representative compiles before the
   first behavior change.

Do not use blogs, remembered constants, coordinator quirks, or another stack's
source as normative authority. An open source driver can be an implementation
reference only after its exact revision and license are recorded.

## 3. Current Git Snapshot

Snapshot taken 2026-07-16 13:56 Europe/London:

```text
repository: /home/lolren/Desktop/eport_nrf54/nrf54-arduino-core
branch:     main
HEAD:       f22307d7fb12
tag at HEAD: v1.0.3
status entries: 79
tracked diff: 65 files changed, 9771 insertions, 2537 deletions
```

The working tree is intentionally dirty and contains crucial uncommitted work.
It is not equivalent to tag `v1.0.3`. Do not run `git reset --hard`,
`git checkout --`, broad `git restore`, or otherwise return to the tag. Do not
replace this tree with a fresh clone. Preserve all unrelated user changes.

Important untracked preparation includes:

```text
docs/ZIGBEE_COMPLETION_IMPLEMENTATION_PLAN.md
.../src/zigbee_feature.h
.../src/zigbee_frame_counter.h
.../src/zigbee_frame_counter.cpp
scripts/test_exclusive_radio_arbiter_contracts.py
scripts/test_zigbee_codec_capacity_contracts.py
scripts/test_zigbee_codec_security_regressions.py
scripts/test_zigbee_coordinator_security_contracts.py
scripts/test_zigbee_feature_gate.py
scripts/test_zigbee_persistence_journal.py
scripts/test_zigbee_radio_hardware_contracts.py
scripts/test_zigbee_security_hardening.py
scripts/test_zigbee_validation_contracts.py
scripts/zigbee_validation_common.py
```

The next implementer must re-run `git status --short`; concurrent work may have
advanced this snapshot. Generated `.uf2`, build directories, logs, captures,
keys, and local measurements must not be committed unless a reviewed fixture is
explicitly intended for the repository.

## 4. Normative Baseline And Claim Boundary

### 4.1 First complete baseline

Complete and qualify one coherent baseline first:

- Zigbee Core R22.1;
- BDB 3.0.1;
- ZCL R8;
- the exact IEEE 802.15.4 revision referenced by that Core revision;
- the exact device definitions selected in the PICS;
- 2.4 GHz O-QPSK only;
- coordinator, router, always-on end device, and sleepy end device only when
  each role's selected requirements and tests pass.

Do not mix R23.2/BDB 3.1/Zigbee 4.0 behavior into the R22.1 implementation.
After R22.1 is stable and qualified, perform a requirements diff and add the
later revision as a separate phase with separate PICS and mixed-revision tests.

### 4.2 Explicit exclusions until separately completed

The following are not implied by completing basic Zigbee 3.0 behavior:

- Zigbee Direct;
- Green Power proxy/sink/device roles;
- Touchlink;
- NCP/RCP operation;
- Sub-GHz PHYs;
- concurrent BLE/Zigbee scheduling;
- OTA image installation or secure boot integration;
- every ZCL cluster or every Zigbee device type;
- Zigbee 4.0/R23.2 security.

Every public feature matrix entry must say `implemented`, `experimental`,
`unsupported`, or `not selected`. Never use a green check for a codec when the
required state machine, persistence, interoperability, and certification are
missing.

### 4.3 Normative ledger

Create a machine-readable or reviewed Markdown ledger with one row per selected
requirement:

```text
requirement id/clause
spec revision and document hash
role/PICS applicability
mandatory/optional/excluded decision
source symbol(s)
unit/model test(s)
hardware test(s)
interoperability or ZUTH case
current status and evidence artifact
```

Do not invent clause numbers. Populate them only from the pinned official
documents.

## 5. Current Source Map

All library paths below are relative to:

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/
Nrf54L15-Clean-Implementation/
```

Primary files:

| Area | Current file | Current role |
|---|---|---|
| Public HAL/PHY types | `src/nrf54l15_hal.h` | `ZigbeeRadio`, buffers, raw/ACK/ED APIs, exclusive-radio API |
| nRF54 IEEE radio | `src/nrf54l15_hal_parts/nrf54l15_hal_802154_rawradio.inc` | Direct blocking TX/RX, buffered IRQ RX, CCA, ED, immediate ACK, errata |
| Shared HAL | `src/nrf54l15_hal.cpp` | Exclusive arbiter, DMA scrub, RADIO IRQ dispatch, System OFF quiesce |
| Clock | `src/nrf54l15_hal_timebase.cpp`, `src/nrf54l15_hal_oscillators.h` | XOTUNED readiness, anomaly-39 ordering |
| Shared CONSTLAT | core `HardwareSerial.cpp` files and `nrf54l15_hal_internal_ble_timing.inc` | Counted anomaly-20 lease ABI |
| Protocol model/codecs | `src/zigbee_stack.h`, `src/zigbee_stack.cpp` | MAC/NWK/APS/ZDO/ZCL codecs and selected HA device behavior |
| Commissioning | `src/zigbee_commissioning.h`, `src/zigbee_commissioning.cpp` | Join/rejoin/security helpers; still blocking |
| Security | `src/zigbee_security.h`, `src/zigbee_security.cpp` | Software AES-CCM*, NWK/APS secured codecs, install-code support |
| Persistence | `src/zigbee_persistence.h`, `src/zigbee_persistence.cpp` | Version-7 direct-RRAM journal and legacy migration |
| Outgoing counters | `src/zigbee_frame_counter.h`, `src/zigbee_frame_counter.cpp` | Crash-safe NWK and APS-TC high-water allocator |
| Feature gate | `src/zigbee_feature.h` | Default-off compile-time policy/conflict checks |
| OpenThread consumer | `src/openthread_platform_nrf54l15.cpp` | Reuses `ZigbeeRadio`; shared-regression dependency |
| Examples | `examples/Zigbee/` | 30 sketches, many with private runtime logic |
| CI/contracts | repository `.github/workflows/ci.yml`, `scripts/test_zigbee_*.py` | Compile, sanitizer, source/model, and validation-script contracts |

Current approximate source scale at the snapshot:

```text
zigbee_stack.h          1527 lines
zigbee_stack.cpp        8255 lines
zigbee_commissioning.*  2862 lines
zigbee_security.*       1912 lines
zigbee_persistence.*    2014 lines
30 Zigbee sketches     42394 lines
```

This scale is evidence for controlled extraction, not permission for a broad
rewrite. Preserve wire behavior behind tests while moving one concern at a
time.

## 6. Implemented Preparation: Accurate Status

This section describes code present in the dirty tree. It does not imply full
protocol conformance or release readiness.

### 6.1 Feature gating

Implemented:

- `zigbee_feature.h` normalizes the current and legacy feature macros.
- Zigbee is disabled by default on all six supported board definitions.
- `zigbee_stack.h` fails at compile time when the protocol stack is disabled.
- Enabled and disabled host-link tests exist.
- Zigbee+BLE and Zigbee+OpenThread builds fail explicitly.

Not implemented:

- runtime coexistence or multiprotocol scheduling;
- a protocol-neutral public IEEE 802.15.4 backend name/API;
- typed feature-disabled stubs;
- a stable public `Zigbee.h` runtime API.

### 6.2 Checked codec/security preparation

Implemented:

- capacity-bearing builders for the current codec surface;
- staged output commit so failed builders do not expose partial frames;
- parser output initialization and atomic commit for many current parsers;
- malformed/truncation/canary tests under ASan/UBSan;
- strict ascending/nonduplicate discovery response identifiers;
- CCM decrypt authenticates before copying plaintext to caller output;
- constant-time four-byte MIC comparison;
- an exact NIST CAVS 11.0 AES-CCM vector with Zigbee parameter sizes;
- NWK and APS command security-control validation for the supported forms.

Not implemented:

- complete MAC/NWK/APS/ZDO/ZCL codecs for every selected form;
- a complete official vector corpus;
- compiler-resistant secret zeroization;
- a production CRACEN Zigbee provider and provider-equivalence tests;
- complete typed key storage and replay manager;
- complete security status taxonomy.

### 6.3 Direct RRAM journal and high-water counters

Implemented:

- a dedicated 4 KB `.zigbee_storage` page on L15 VPR-on, L15 VPR-off, and LM20;
- state version 7;
- two namespace partitions, each owning two 1 KB A/B slots;
- explicit little-endian 464-byte payload encoding;
- 64-byte header with namespace identity, generation, payload length/CRC,
  header CRC, live/tombstone state, and commit marker;
- serialized RRAMC transactions;
- invalidate, body/header write/readback, commit-marker-last, final decode verify;
- live/empty/tombstone/corrupt/collision/not-open load statuses;
- collision failure without evicting a foreign namespace;
- legacy versions 1 through 6 decode and migration;
- committed tombstone clear;
- outgoing exclusive high-water allocation for NWK and APS Trust Center domains;
- 64-counter reservation blocks;
- eight coordinator peer NWK replay records.

Not implemented:

- separate compact journals per record type;
- complete per-peer/per-layer/per-key replay state;
- per-link-key APS outgoing counter namespaces;
- full Trust Center device/auth table durability;
- measured RRAM endurance policy;
- physical brownout campaigns at each write phase;
- OTA, child, route, group, scene, and complete BDB durable schemas.

### 6.4 Exclusive RADIO lease

Implemented:

- five exclusive owners: BLE, Zigbee/802.15.4, proprietary raw, raw Channel
  Sounding, and MPSL Channel Sounding;
- PRIMASK-serialized generation tokens;
- wrong-owner/stale-token rejection;
- permanent quarantine on generation exhaustion or unsafe teardown;
- begin-before-hardware acquisition ordering;
- release only after strict Disabled proof and DMA scrub;
- owner-gated RADIO IRQ dispatch;
- destructor fail-stop when member-backed DMA could outlive its object;
- owner-aware System OFF handling;
- executable model/source contracts.

Not implemented:

- a deadline-aware multiprotocol scheduler;
- typed busy/ownership statuses;
- a dynamic TIMER/DPPI/EGU/CRYPTO resource allocator;
- concurrent BLE/Zigbee qualification;
- physical protocol-switch stress evidence.

### 6.5 HFXO and silicon preparation

Implemented:

- XOTUNED, not XOSTARTED or XO running state, proves synchronous RADIO quality;
- already-running XO is retuned when no unconsumed tuned event proves quality;
- one XOTUNEERROR retry occurs only while RADIO is Disabled;
- XOTUNEFAILED fails closed;
- anomaly 39 start order is PLLSTART then XOSTART;
- anomaly 39 stop order is XOSTOP then PLLSTOP;
- anomaly 20 uses a shared counted CONSTLAT lease;
- Zigbee operation paths fail if the lease cannot be acquired;
- IEEE mode applies the current anomaly-6 predicate/workaround or fails closed
  when secure access is unavailable.

Not implemented:

- a reference-counted HFXO manager shared by every client;
- typed HFXO result codes;
- one unified `SiliconInfo`/`RadioErrata` module;
- measured family/revision acceptance for every relevant erratum;
- complete hardware/regulatory documentation for non-software errata.

### 6.6 CCA and ED preparation

Implemented:

- default ED-mode CCA with a staged configurable threshold;
- atomic CCAIDLE-to-TX using RADIO shortcuts;
- CCABUSY-to-DISABLE;
- READY-to-EDSTART with baseband START disabled;
- EDEND-to-DISABLE;
- asynchronous duration ED scan using 128 us periods and EDCNT=N-1;
- OpenThread energy-scan callback deferral through its process loop.

Not implemented:

- unslotted CSMA-CA NB/BE/backoff/retry state;
- default use of CCA by direct transmit APIs;
- normalized IEEE ED output in Zigbee APIs;
- LQI metadata;
- RF-calibrated thresholds and CCA modes;
- asynchronous Zigbee MLME scan confirmations.

### 6.7 CRC-qualified immediate ACK preparation

Implemented:

- RADIO TIFS is reset; it is not misused as a conditional ACK timer;
- Fast TX ramp;
- TIMER10 at 1 MHz, 32-bit, compare1=129 us;
- anomaly-25 STOP+CLEAR sequence;
- DPPIC10 channel 7: RADIO.DISABLED -> TIMER10.START;
- DPPIC10 channel 23: TIMER10.COMPARE1 -> RADIO.TXEN;
- ACK TX DPPI is armed only from the CRCOK path;
- CRCERROR, invalid length, filter rejection, and queue exhaustion do not ACK;
- short broadcast, sequence-suppressed, and unsupported frame-version ACKs fail
  closed;
- timer capture drops an already-late ACK instead of transmitting late;
- RX queue visibility is delayed until ACK completion or late-drop resolution;
- timer/DPPI resources are cleaned on cancel/end.

Not implemented and safety-critical:

- staged BCC/BCMATCH parsing while bytes arrive;
- a bounded internal-only prefix parser;
- precomputed PAN/address acceptance and indirect-frame-pending lookup;
- removal of arbitrary application callbacks from the RADIO ISR;
- measured 192 us last-RX-bit-to-first-ACK-preamble proof on both SoCs;
- enhanced ACK and newer frame-version behavior if selected by PICS;
- SIFS/LIFS enforcement;
- complete duplicate/retry interaction.

The current scheduler is CRC-qualified and late-safe, but it is not yet
deadline-deterministic because full-frame filter and frame-pending callbacks run
inside `serviceBufferedReceiveIrq()` before the DPPI TX path is armed.

### 6.8 Strict DMA cleanup

Implemented:

- release refuses to scrub a non-Disabled RADIO;
- active AUX DMA receives AUXDATADMASTOP and must report AUXDATADMAEND;
- both AUX DMA pointers/counts/enables, DFEPACKET pointer/count, and PACKETPTR
  are cleared and read back before release;
- Zigbee has aligned distinct TX, ACK, four RX DMA buffers, and an eight-frame
  software queue;
- failure retains/quarantines ownership instead of freeing live member buffers.

Not implemented:

- explicit per-buffer ownership states;
- per-operation generation and stale-completion rejection;
- LQI/CRC/hardware timestamp metadata;
- a fake-peripheral event-order test covering every DMA race;
- physical high-rate overflow/switch stress.

### 6.9 Examples and coordinator security

Implemented in 17 modified HA/coordinator/light/sensor/sleepy examples:

- strict journal load status;
- outgoing high-water allocator;
- fail-closed storage/counter behavior.

Implemented in both large coordinator examples:

- fresh active NWK key from `CracenRng`;
- all-zero key rejection;
- persisted active/alternate keys and key sequence;
- persisted outgoing high-water marks;
- eight peer IEEE/short/key-sequence/incoming-NWK-counter records;
- durable replay update before upper-layer dispatch;
- explicit security factory reset;
- intentionally unsupported nonconformant key-update rollout.

Not implemented:

- a reusable Trust Center runtime outside examples;
- complete durable node authorization/interview/parent state;
- conformant key rotation;
- full key command lifecycle;
- removal of compile-time demo IEEE/install-code/well-known-key fixtures;
- declarative small examples using one shared event-driven runtime.

## 7. nRF54 IEEE 802.15.4 Hardware Contract

### 7.1 RADIO baseline

The currently used nonsecure RADIO base is `0x4008A000`; secure is
`0x5008A000`. RADIO IRQ0/IRQ1 are 138/139. Important offsets that must be
checked against the pinned product specifications before edits:

```text
tasks:
TXEN 0x000, RXEN 0x004, START 0x008, STOP 0x00C, DISABLE 0x010
RSSISTART 0x014, BCSTART 0x018, BCSTOP 0x01C
EDSTART 0x020, EDSTOP 0x024, CCASTART 0x028, CCASTOP 0x02C
PLLEN 0x06C, SOFTRESET 0x0A4

events:
READY 0x200, TXREADY 0x204, RXREADY 0x208, ADDRESS 0x20C
FRAMESTART 0x210, PAYLOAD 0x214, END 0x218, PHYEND 0x21C
DISABLED 0x220, DEVMATCH 0x224, DEVMISS 0x228
CRCOK 0x22C, CRCERROR 0x230, BCMATCH 0x238
EDEND 0x23C, EDSTOPPED 0x240
CCAIDLE 0x244, CCABUSY 0x248, CCASTOPPED 0x24C
MHRMATCH 0x254, SYNC 0x258, PLLREADY 0x2B0, RXADDRESS 0x2BC

configuration:
SHORTS 0x400
INTENSET00/01 0x488/0x48C, INTENCLR00/01 0x490/0x494
INTENSET10/11 0x4A8/0x4AC, INTENCLR10/11 0x4B0/0x4B4
MODE 0x500, PHYENDTXDELAY 0x518, STATE 0x520
EDCTRL 0x530, EDSAMPLE 0x534, CCACTRL 0x538
TIMING 0x704, FREQUENCY 0x708, TXPOWER 0x710, TIFS 0x714
RSSISAMPLE 0x718, CRCSTATUS 0xE0C, RXCRC 0xE14
PCNF0 0xE20, PCNF1 0xE28, CRCCNF 0xE44
CRCPOLY 0xE48, CRCINIT 0xE4C, BCC 0xE94
MHRMATCHCONF 0xEB4, MHRMATCHMASK 0xEB8, SFD 0xEBC
PACKETPTR 0xED0
```

RADIO state values used for strict proof:

```text
Disabled 0, RxRu 1, RxIdle 2, Rx 3, RxDisable 4,
TxRu 9, TxIdle 10, Tx 11, TxDisable 12
```

`EVENTS_DISABLED` is sticky and is not sufficient proof after an arbitrary
failure. Strict teardown reads `STATE == Disabled` before SOFTRESET, DMA pointer
scrub, HFXO/RF/CONSTLAT release, or ownership release.

### 7.2 Packet configuration invariants

The selected PHY must retain and test:

- `MODE = Ieee802154_250Kbit`;
- SFD `0xA7`;
- 8-bit PHR length field;
- no whitening;
- little-endian packet handling;
- `MAXLEN=127`;
- two-byte IEEE CRC, polynomial `0x11021`, initial value zero;
- IEEE CRC skip/start behavior from the product specification;
- `CRCINC=Include` length semantics;
- `PHYEND`, not `END->START`, for IEEE shortcuts;
- channel 11..26 mapping to 2405..2480 MHz in 5 MHz increments;
- FCS handled by hardware without double subtraction;
- PHR stored separately in RAM;
- appended hardware LQI never copied into the PSDU.

Test PHR/MPDU boundaries 0, 1, 2, 3, 18, 19, 125, 126, and 127. Reject
impossible forms before DMA or buffer copy. Account for the maximum RAM layout:
one PHR byte, up to 125 MAC bytes when FCS is omitted from RAM, and one appended
LQI byte.

### 7.3 Symbol and timing invariants

At 2.4 GHz IEEE 802.15.4:

```text
1 symbol = 16 us
CCA survey = 8 symbols = 128 us
SIFS = 12 symbols = 192 us for the selected short-MPDU boundary
LIFS = 40 symbols = 640 us above that boundary
```

Verify the exact MPDU boundary and ACK rules in the pinned IEEE revision.
Immediate ACK target evidence must measure last received bit to first ACK
preamble, not ISR entry or software timestamp. Do not use `TIFS` as a substitute
for MAC IFS policy.

### 7.4 TIMER/DPPI resources

Current immediate ACK resources:

```text
DPPIC10 nonsecure base: 0x40082000
TIMER10 nonsecure base: 0x40085000
EGU10 nonsecure base:   0x40087000
DPPIC10 channel 7:      RADIO.DISABLED -> TIMER10.START
DPPIC10 channel 23:     TIMER10.COMPARE1 -> RADIO.TXEN
TIMER10 CC1:            129 us ramp-start target
TIMER10 CC3:            deadline pre-arm capture
```

Do not casually change these values. First prove resource non-conflict, anomaly
22/24/25 handling, DPPI security attribution, and teardown. The production
architecture should eventually allocate a reviewed resource manifest rather
than rely on hardcoded global channels.

### 7.5 Errata invariants

At minimum retain reviewed handling for:

- anomaly 6 IEEE mode workaround and secure attribution;
- anomaly 20 shared constant latency before RADIO RX/TX exposure;
- anomaly 39 PLL/XO start and stop ordering;
- L15 channel-23 sensitivity characterization;
- TIMER anomalies 22/24/25;
- DPPI/PPIB security-domain anomaly 26;
- EGU/DPPI channel-0 security coupling anomaly 59;
- L15 Rev1 anomaly 33 board/regulatory limitation.

Every workaround needs an exact silicon predicate, official erratum reference,
source test, and applicable board/revision matrix. Never apply a BLE-only or FEM
workaround to IEEE mode without normative evidence.

## 8. Target Architecture

The final stack must not remain a set of copied sketch loops. Move toward this
dependency structure in small behavior-preserving commits:

```text
src/zigbee/
  api/
    Zigbee.h
    status.h
    handles.h
    observer.h
  core/
    runtime.*
    event_queue.*
    timer_queue.*
    buffer_pool.*
    configuration.*
  platform/
    ieee802154_phy.h
    radio_phy_nrf54.*
    clock.*
    random.*
    crypto_provider.*
    storage.*
  mac/
    codec.*
    pib.*
    filter.*
    duplicate_table.*
    csma_ca.*
    transaction.*
    scan.*
    association.*
    indirect_queue.*
  nwk/
    codec.*
    tables.*
    forwarding.*
    route_discovery.*
    broadcast.*
    concentrator.*
    lifecycle.*
  aps/
    codec.*
    transaction.*
    duplicate_table.*
    binding.*
    groups.*
    fragmentation.*
  security/
    codec.*
    key_store.*
    replay.*
    frame_counter.*
    trust_center.*
    providers/
  persistence/
    journal.*
    schema.*
    migration.*
    records.*
  bdb/
    engine.*
    formation.*
    steering.*
    finding_binding.*
  zdo/
    codec.*
    registry.*
    transaction.*
    server.*
  zcl/
    codec.*
    types.*
    attributes.*
    reporting.*
    foundation.*
    clusters/
  ota/
    cluster.*
    image.*
    storage.*
```

Dependency rules:

1. Pure codecs depend on no Arduino, RADIO, time, storage, Serial, or application
   callback.
2. Platform modules implement narrow interfaces and know no Zigbee role policy.
3. MAC owns PHY operations and IEEE timing.
4. NWK uses MAC services; it never programs RADIO directly.
5. APS uses NWK services; it never sends raw MAC frames.
6. ZDO/ZCL/BDB use APS/NWK runtime transactions, never blocking radio helpers.
7. Examples register endpoints/policy/callbacks and call `process()`; they do not
   implement security, ACK tracking, routing, or persistence.
8. Compatibility wrappers may block only at the public legacy boundary and must
   repeatedly drive the same event runtime.
9. No heap allocation after successful stack `begin()` in the default embedded
   profile.

## 9. Runtime Invariants

### 9.1 Run-to-completion

The stack needs one bounded event loop:

```text
ISR captures minimal event and metadata -> fixed queue
process() drains bounded work -> state transitions -> starts async operation
timer expiry -> fixed queue event
application callback -> process context only
```

No ISR may parse NWK/APS/ZDO/ZCL, run AES, touch persistence, print, allocate,
or invoke arbitrary application code. ACK prefix parsing is the sole special
hard-deadline parser and must be bounded, internal, and operate only on captured
bytes.

### 9.2 Handles and stale events

Every operation, timer, transaction, and borrowed buffer uses a slot plus
generation handle. Cancellation increments or invalidates generation. A late
IRQ/timer/confirm cannot complete a new operation that reused the slot.

### 9.3 Typed status

Do not continue expanding `bool`. At minimum distinguish:

```text
Success, Pending, Busy, QueueFull, TableFull, NoBuffer, FeatureDisabled,
InvalidArgument, InvalidState, Unsupported, Cancelled, Timeout, CcaBusy,
NoAck, BadCrc, Filtered, RadioFault, RadioOwnedByOtherProtocol,
SecurityFailure, ReplayRejected, NotAuthorized, StorageFailure,
CounterExhausted, RouteUnavailable, MalformedFrame
```

Preserve remote protocol status separately from local execution status.

### 9.4 Memory ownership

Use fixed pools with explicit state:

```text
Free -> RadioRx -> MacQueued -> UpperLayer -> Free
Free -> GeneralTx -> RadioTx -> confirmation -> Free
ReservedImmediateTx -> ACK only -> ReservedImmediateTx
```

Have separate RX, immediate TX, general TX, reassembly, indirect-child,
transaction, neighbor, route, binding, group, scene, replay, and event budgets.
Every full condition must produce a defined status and diagnostic counter.

## 10. Remaining MAC Work

### 10.1 Protocol-neutral async PHY

Replace runtime use of blocking `ZigbeeRadio` methods with request/confirm APIs:

```text
startReceive(request, handle)
startTransmit(request, handle)
startCca(request, handle)
startEd(request, handle)
cancel(handle)
```

Requests include channel, power, buffer handle, expected DSN, ACK requirement,
not-before time, deadline, duration, and generation. Confirms include exact
status, timestamps, RSSI, LQI, CRC, attempt counts, ACK Frame Pending, and
buffer ownership.

### 10.2 MAC PIB

Implement typed, validated attributes selected by the PICS, including:

- PAN ID, short/extended address, coordinator address;
- channel/page;
- promiscuous and RxOnWhenIdle;
- association permit and auto-request;
- beacon sequence and data sequence;
- transaction persistence and response wait;
- ACK wait;
- minimum/maximum BE;
- maximum CSMA backoffs;
- maximum frame retries.

Verify defaults and ranges against the pinned IEEE revision. Do not assume
commonly remembered values without a ledger entry.

### 10.3 Unslotted CSMA-CA

Implement one serialized state machine per TX:

```text
Queued
-> initialize NB=0, BE=macMinBE
-> random backoff [0, 2^BE-1] unit-backoff periods
-> CCA
   idle -> transmit
   busy -> NB++, BE=min(BE+1, macMaxBE)
        -> retry CCA or ChannelAccessFailure
-> optional ACK wait
   matching valid ACK -> success
   timeout -> whole-frame retry or NoAck
```

Randomness comes from the CSPRNG through an injectable provider. Link retries
reuse the same MPDU/DSN/security counters/ciphertext. Track CCA attempts and
frame retries separately. Apply SIFS/LIFS before the next eligible operation.

### 10.4 Full MAC codec/filter/duplicate handling

Implement and table-test every selected valid combination of:

- beacon/data/ACK/command frame type;
- none/short/extended source and destination;
- PAN compression by frame version;
- sequence-number suppression only where supported;
- security auxiliary header forms;
- command payloads required by Zigbee;
- truncation and reserved encodings.

Before ACK:

1. CRC/PHR/form checks;
2. bounded staged header parse;
3. PAN and special scan/commissioning policy;
4. local short/extended/broadcast destination check;
5. indirect-source Frame Pending lookup;
6. immediate ACK arm;
7. duplicate decision before upper delivery, while still ACKing valid retries.

Duplicate identity must include source identity, frame type/version, DSN, and
aging epoch. Never acknowledge a queue-full frame unless the MAC has a defined
way to retain/process it.

### 10.5 MAC management

Complete asynchronous ED/active/passive/orphan scans, association,
disassociation, coordinator realignment, indirect transaction queues, Data
Request polling, transaction expiry, sleepy-child capacity, and confirms.

## 11. Remaining NWK Work

### 11.1 Codec and commands

Complete selected NWK header forms and the full command ledger, including route
request/reply, network status, leave, route record, rejoin, link status, network
report/update, end-device timeout, and any selected many-to-one/source-route
forms. Reject unsupported/reserved forms explicitly.

### 11.2 Tables

Implement bounded typed tables for neighbors, children, addresses, routes,
route discovery, broadcasts, source routes, and network identity. Entries need
state, generation, expiry/aging, ownership, and table-full policy. Fixed arrays
of eight with manual setters are not a routing engine.

### 11.3 Forwarding and routing

Implement:

- local delivery versus forwarding;
- radius decrement and rejection;
- next-hop lookup;
- route discovery transaction and duplicate suppression;
- route repair and failure status;
- route aging;
- broadcast duplicate table and propagation;
- multicast/groups per selected requirements;
- many-to-one concentrator behavior;
- route record/source-route behavior;
- address conflict and channel/network update handling.

Tests require at least a deterministic topology simulator and a physical
multi-hop lab. Two directly connected boards cannot prove these features.

### 11.4 Lifecycle

Complete formation/start, join/rejoin, leave, child aging, parent loss, orphan
recovery, channel change, restart restoration, and role-specific behavior.

## 12. Remaining APS Work

Implement:

- complete APS data/command/ACK codec forms selected by PICS;
- bounded APS transaction slots keyed by peer/endpoints/cluster/counter;
- ACK wait, retry, cancellation, duplicate rejection, and status propagation;
- binding table semantics and fan-out;
- group delivery semantics;
- fragmentation and reassembly if selected, with per-peer/global byte quotas,
  timeout, duplicate fragment behavior, and reset cleanup;
- secure data and command processing through one key/replay pipeline;
- correct broadcast/group/unicast behavior without pretending group delivery
  has unicast acknowledgement semantics.

No APS retry/fragmentation logic may remain private to an example.

## 13. Remaining Security And Persistence Work

### 13.1 Invariants

1. Never reuse a nonce for the same key/source/security-control/layer.
2. Incoming replay identity includes source IEEE, layer, key type/id, key
   sequence/epoch.
3. Authenticate before plaintext delivery or trusted-state mutation.
4. Counter exhaustion fails closed without wrap.
5. A persistence failure stops secured transmit before an unreserved counter.
6. Keys and plaintext temporaries are compiler-resistantly erased.
7. MIC compare is constant-time.
8. Logs never print keys, install codes, nonces, or plaintext commands by
   default.
9. Key selection follows policy; never try arbitrary keys until one decrypts.

### 13.2 Provider and key store

Create a crypto provider abstraction supporting AES-128, required CCM* modes,
MMO/install-code derivation, CSPRNG, constant-time compare, and secure erase.
Validate CRACEN ownership, DMA/security-domain restrictions, timeout, reset,
System OFF, and concurrency with BLE/Thread/Matter. Run identical vectors
against software and CRACEN.

Create typed NetworkKey, LinkKey, TrustCenterKey, InstallCode, and TransientKey
records. Protocol code receives a key identifier/handle, not unrestricted key
bytes.

### 13.3 Replay and counter persistence

Replace global inbound counters with per-peer/per-layer/per-key records. Update
only after structural and MIC success. Define link-retry versus replay ordering.
Fail closed on table full; do not evict a live authenticated peer for an unknown
sender.

Extend outgoing high-water allocation to every required security domain. Keep
the current commit-before-use property. Test every power-loss phase correlated
with counters observed on air.

Split critical records where required:

- network identity/BDB state;
- active/alternate keys and activation state;
- outgoing counter reservations;
- incoming replay state;
- Trust Center device/auth records;
- bindings/groups/scenes/reporting;
- durable child identity;
- OTA metadata.

Retain explicit little-endian schemas, CRC, commit-last, collision safety,
legacy migration, and fail-closed corruption behavior.

### 13.4 Trust Center

Move Trust Center behavior out of examples. Implement policy and durable state
for admission, authorization, permit join, install-code-only joining, optional
well-known key, unique link keys, Transport Key, Update Device, Remove Device,
Request Key, Switch Key, Verify Key, Confirm Key, key rotation, rejoin, leave,
restart, and address generation as required by the selected Core/PICS.

Network-key rotation must distribute/stage, prove delivery as required,
broadcast Switch Key under the correct old key, activate atomically, preserve
the normative transition window, and retire/erase old material. Never promote a
key merely because one MAC transmission succeeded.

## 14. Remaining BDB Work

Convert `ZigbeeCommissioning` helpers into an event-driven engine with explicit
hierarchical states for factory-new, restored, formation, steering scan,
association, Trust Center key wait, joined, rejoin, leave, finding/binding,
failed, and reset.

Implement:

- primary/secondary channel masks and retry budgets;
- formation and coordinator start with durable commit before beacons;
- network steering and candidate policy;
- finding and binding;
- centralized security policy;
- install-code and well-known-key policy;
- secure/unsecure rejoin according to selected requirements;
- permit-join duration restored across reboot;
- factory reset with durable tombstone and key erasure;
- role-specific restart behavior;
- cancellation and typed failures.

Remove all `while(millis...)`, `while(micros...)`, `delay()`, and nested receive
loops from production commissioning paths.

## 15. Remaining ZDO Work

Build an endpoint/descriptor registry and bounded ZDO transaction manager.
Selected role requirements must cover address discovery, node/power/simple
descriptors, active/match/extended discovery, Device Announce, Bind/Unbind, End
Device Bind if selected, management LQI/routing/binding/leave/permit-join, and
all mandatory status/error responses.

Requirements:

- transaction sequence allocation without collision;
- peer/cluster/sequence matching;
- timeout/retry/cancel;
- pagination and total/start-index validation;
- strictly increasing discovery identifiers;
- no response-state mutation on malformed input;
- role/PICS-specific server enablement;
- NWK/APS status propagation;
- no fixed four-endpoint/eight-binding public claim unless documented as a
  profile capacity.

## 16. Remaining ZCL And Device Work

### 16.1 Foundation

Complete the ZCL R8 data type registry and checked value codec. The current enum
has only Boolean, selected bitmaps/integers, signed-16, and short character
string. Add all selected fixed-width, string, collection, time/date, identifier,
float, invalid/unknown, and manufacturer-specific behavior from the normative
ledger.

Implement an endpoint-scoped cluster/attribute registry with type, access,
manufacturer, security, default/current value, provider, persistence, and
reporting policy.

Complete selected foundation commands:

- read/write/write-undivided/write-no-response;
- configure/read reporting;
- report attributes;
- default response;
- discover attributes/extended;
- discover commands received/generated;
- structured read/write/discovery if selected.

Write Undivided must validate and stage the whole request before any commit.

### 16.2 Reporting

Implement minimum interval, maximum interval, reportable change by analog type,
discrete change semantics, manufacturer/direction identity, persisted
configuration, retry/backpressure, pending-value update, and sleep-aware
deadlines. Commit report baseline only after the corresponding send result.

### 16.3 Clusters and devices

Finish and test every cluster currently advertised by examples before adding
new claims: Basic, Power Configuration, Identify, Groups, Scenes, On/Off,
On/Off Switch Configuration, Level Control, Color Control, Temperature, and
Relative Humidity. `0x0019` OTA being defined is not an OTA implementation.

For every device type, validate mandatory clusters, endpoint direction, device
version, attributes, commands, persistence, malformed frames, and independent
ecosystem interoperability.

## 17. OTA And Optional Tracks

Treat OTA cluster protocol and image installation as separate security
boundaries. A production OTA claim requires client/server cluster flows,
discovery, query, block/page transfer, integrity/authenticity, version policy,
resume, storage map, bootloader handoff, rollback, and per-board support.

Green Power, Direct, Touchlink, and NCP/RCP are later selected tracks. Do not
start them before the R22.1 core, resource ownership, security, and qualification
baseline is stable.

## 18. Dependency-Ordered Phases

### Phase 0: freeze and preserve

- Snapshot dirty state and run all existing tests/compiles.
- Pin specs/hashes and create PICS/requirement ledger.
- Capture existing direct-link behavior and packet fixtures.
- Repair documentation statements that contradict current source.

Exit: no preparation is lost; every future claim maps to a selected requirement.

### Phase 1: safety foundation completion

- Finish checked cursors/spans and atomic builders/parsers.
- Expand sanitizer truncation/capacity/alias tests.
- Add secure erase and full official crypto vectors.
- Add fake clock/RNG/storage/radio interfaces.

Exit: every current parser truncation and builder capacity boundary is tested.

### Phase 2: behavior-preserving source extraction

- Extract pure codecs one family per commit.
- Keep compatibility facade and exact golden bytes.
- Do not mix mechanical moves with wire changes.

Exit: monolith shrinks while all existing examples still compile.

### Phase 3: runtime, queues, timers, pools, API

- Implement fixed generation-tagged event/timer/transaction/buffer pools.
- Add typed statuses, request handles, `process()`, `nextDeadlineUs()`.
- Move callbacks out of ISR.

Exit: one simple coordinator/end-device path uses the shared runtime.

### Phase 4: async PHY and full MAC

- Protocol-neutral PHY adapter over current safe hardware work.
- Staged BCMATCH immediate ACK filter.
- Metadata, buffer ownership, operation generations.
- PIB, CSMA/retry/IFS, scans, association, duplicate table, indirect/poll.

Exit: all Section 7/10 gates pass on L15 and LM20; BLE/Thread/CS regressions pass.

### Phase 5: security identity and persistence completion

- Typed key store/provider, per-peer replay, all counter domains.
- Split critical journal records and full fault model.
- Trust Center durable device/auth state and CSPRNG keys.

Exit: resets/brownouts never reuse an observed nonce or admit a replay.

### Phase 6: NWK lifecycle/routing

- Complete codecs/tables/forwarding/discovery/repair/broadcast/concentrator.
- Add deterministic multi-hop simulator.

Exit: line, tree, route failure/repair, broadcast, many-to-one tests pass.

### Phase 7: APS delivery

- Transactions, ACK/retry/duplicates, binding/groups, fragmentation/reassembly.

Exit: loss/reorder/duplicate/table-full tests pass with bounded resources.

### Phase 8: BDB, Trust Center, ZDO

- Event-driven role lifecycle, formation/steering/finding-binding.
- ZDO registry/transaction/server completion.
- Remove coordinator security runtime from sketches.

Exit: coordinator/router/end-device restart and commissioning matrix passes.

### Phase 9: ZCL foundation

- Full selected types, attributes, foundation commands, reporting.

Exit: positive, malformed, pagination, atomic-write, and reporting tests pass.

### Phase 10: clusters/examples

- Complete current HA clusters/devices.
- Convert examples to declarative shared-runtime clients.
- Remove/alias duplicates only with release notes.

Exit: every shipped example compiles on supported profiles and has a behavior test.

### Phase 11: sleepy devices and Poll Control

- Parent queues, polling, timeouts, System OFF, rejoin, reporting deadlines.

Exit: long hardware sleep/reboot/parent-loss tests pass without lost security state.

### Phase 12: OTA and selected additional clusters

- OTA protocol, image policy, storage, bootloader handoff.

Exit: interruption/resume/rollback/security tests pass on each claimed board.

### Phase 13: selected optional R22.1 features

- Green Power or other optional PICS only if explicitly selected.

### Phase 14: R23.2/BDB 3.1/Zigbee 4.0

- Fresh requirements diff, security/key uplift, mixed-version tests.

### Phase 15: qualification and release readiness

- ZUTH/PICS mapping, multi-product interoperability, RF/timing/power evidence,
  soak/fault/brownout gates, docs and feature matrix.

Exit: all selected requirements have objective evidence. Do not release merely
because the code compiles.

## 19. Existing Test Commands

Run repository scripts directly; pytest is not required for these entry points:

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

Representative strict compiles:

```bash
arduino-cli compile --clean --warnings all \
  --fqbn 'localnrf54:nrf54l15clean:xiao_nrf54l15:clean_zigbee=on,clean_ble=off' \
  --build-path /tmp/zigbee-l15 \
  hardware/nrf54l15clean/nrf54l15clean/libraries/\
Nrf54L15-Clean-Implementation/examples/Zigbee/Coordinator/ZigbeeCoordinatorBasic

arduino-cli compile --clean --warnings all \
  --fqbn 'localnrf54:nrf54l15clean:xiao_nrf54lm20b:clean_zigbee=on,clean_ble=off' \
  --build-path /tmp/zigbee-lm20 \
  hardware/nrf54l15clean/nrf54l15clean/libraries/\
Nrf54L15-Clean-Implementation/examples/Zigbee/Coordinator/ZigbeeCoordinatorBasic

arduino-cli compile --clean --warnings all \
  --fqbn 'localnrf54:nrf54l15clean:xiao_nrf54l15:clean_ble=on' \
  --build-path /tmp/ble-hid-regression \
  hardware/nrf54l15clean/nrf54l15clean/libraries/\
Bluefruit52Lib/examples/HID/blehid_mouse

arduino-cli compile --clean --warnings all \
  --fqbn 'localnrf54:nrf54l15clean:xiao_nrf54l15:clean_thread=stage' \
  --build-path /tmp/thread-radio-regression \
  hardware/nrf54l15clean/nrf54l15clean/libraries/\
Nrf54L15-Clean-Implementation/examples/Thread/OpenThreadRadioDiagInitiator
```

Every shared RADIO/CLOCK/POWER/DPPI/TIMER/RRAM change must run Zigbee, BLE,
OpenThread, raw Channel Sounding, MPSL Channel Sounding, System OFF, and serial
CONSTLAT regressions. Use distinct build directories. Treat every compiler
warning as a failure in strict builds.

## 20. Required New Test Systems

### 20.1 Native unit/model tests

Add deterministic tests for:

- every parser truncated at every byte;
- every builder capacity from zero through exact size and one less;
- output canaries and unchanged output on failure;
- enum/reserved-bit rejection;
- event/timer/handle generation and cancellation;
- table/pool exhaustion;
- CSMA RNG sequences, BE/NB/retry state;
- ACK matching/late/unrelated/bad-CRC behavior;
- duplicate identity and aging;
- routing topology, repair, broadcasts, many-to-one/source routes;
- APS retry/fragment loss/reorder/duplicate/timeouts;
- BDB state transitions and restart;
- ZDO transaction matching/pagination;
- ZCL type/foundation/reporting semantics;
- storage failure at every write phase;
- replay and counter exhaustion.

Use ASan/UBSan and fuzz targets for every untrusted decoder. A source-string
contract is useful for guarding a hardware sequence, but it is not a substitute
for behavioral simulation or hardware evidence.

### 20.2 Fake radio/peripheral

Model RADIO, TIMER10, DPPIC10, CLOCK, POWER, IRQ ordering, sticky events,
EasyDMA ownership, delayed/missing events, timeout, cancellation, and stale IRQ.
Inject every relevant event order. Verify that no stale event completes a newer
operation and no pointer is scrubbed while DMA can still access it.

### 20.3 Deterministic topology simulator

Support at least line, tree, diamond, partition, moving-link, interference, and
sleepy-child topologies. Inject loss, duplication, reorder, delay, asymmetric
links, table exhaustion, reboot, key switch, and channel change.

## 21. Two-Board Hardware Gate

The workspace historically has one XIAO nRF54L15 and one XIAO nRF54LM20A. Do
not assume identities or presence; begin with:

```bash
pyocd list
```

Record board, probe UID, firmware hash, FQBN/options, roles, channel, PAN,
addresses, build log, serial log, and capture path for each test.

Two-board tests must cover both direction permutations:

- L15 initiator/coordinator and LM20 responder/device;
- LM20 initiator/coordinator and L15 responder/device.

Minimum PHY/MAC hardware cases:

1. channels 11, 15, 20, 23, 26;
2. minimum, 18/19 boundary, typical, and maximum PSDU;
3. valid and deliberately bad FCS;
4. correct/wrong PAN;
5. correct/wrong short and extended destination;
6. broadcast and direct unicast;
7. data, command, beacon, and ACK;
8. ACK request/no request, DSN, Frame Pending;
9. queue-full no-ACK policy;
10. delayed filter causing safe ACK drop;
11. repeated duplicate/retry frames;
12. CCA idle/busy and threshold sweep;
13. ED duration/raw/dBm/normalized checks;
14. repeated receive cancel/end/begin and owner switching;
15. System OFF/reboot/rejoin/counter monotonicity;
16. abrupt power removal during journal/key/counter transitions.

Immediate ACK acceptance requires timer/logic-analyzer or trusted sniffer
evidence of 192 us last-bit-to-first-preamble on both SoCs. Board-to-board
success alone does not prove timing.

Two boards can prove direct interoperability, mixed-SoC PHY behavior, selected
commissioning/security/reboot flows, and resource cleanup. They cannot prove
multi-hop routing, broadcast propagation, concentrator behavior, route repair,
or certification.

## 22. Multi-Hop, Ecosystem, And Certification Gate

Before a full Zigbee claim, obtain:

- at least coordinator, two routers, and two end devices for multi-hop tests;
- at least one sleepy child;
- a trusted IEEE 802.15.4 sniffer with timestamped PCAP output;
- at least two independent certified Zigbee ecosystems/products;
- Home Assistant/Zigbee2MQTT coverage where relevant to examples;
- ZUTH access and the exact selected PICS/test plan;
- controllable interference/RF attenuation for CCA/PER/route tests;
- repeatable power-cut/fault equipment for persistence validation.

Qualification artifacts must map every selected PICS item to source, host test,
hardware test, ZUTH case, result, firmware commit, and capture/log. A passing
example against one coordinator is not certification.

Required soak/fault campaigns include:

- long join/leave/rejoin cycles;
- coordinator/router/device resets at every state;
- route loss/repair and partitions;
- full tables and buffer pressure;
- key update/switch interrupted at every stage;
- repeated brownouts during journal commits;
- maximum frame rate and sleepy polling;
- counter near exhaustion;
- mixed R22.1 devices and later-revision downgrade policy.

## 23. Work-Unit And Commit Protocol

For each bounded work unit:

1. Identify exact normative requirements and current source symbols.
2. Add failing unit/model/source tests first where practical.
3. Make the smallest coherent implementation change.
4. Run focused tests.
5. Run adjacent layer tests.
6. Run BLE/OpenThread/CS/System OFF regressions when shared hardware changes.
7. Compile representative L15 and LM20 examples warning-free.
8. Run required hardware cases when behavior touches radio/timing/persistence.
9. Update requirement ledger and docs with evidence, not aspiration.
10. Inspect `git diff --check` and generated artifacts.
11. Commit one logical change with a precise message.

Do not combine source extraction, wire-format changes, hardware-timing changes,
and example rewrites in one commit. Do not commit secrets or local artifacts.
Do not squash away intermediate verified milestones until review.

## 24. Stop Conditions

Stop and investigate rather than papering over:

- missing/ambiguous normative requirement;
- unknown silicon revision or erratum applicability;
- unproven NVM atomicity;
- missed ACK/BLE/Thread timing;
- failure to prove RADIO Disabled;
- DMA pointer still live at owner release;
- counter rollback/reuse or replay acceptance;
- sanitizer/fuzzer finding;
- unexplained packet difference;
- compiler warning in strict builds;
- need to copy proprietary stack code or ship an incompatible binary;
- test requiring equipment that is unavailable.

Record the blocker and continue only with independent work that cannot hide it.
Never weaken a test or return success to keep progress moving.

## 25. Definition Of Full Completion

Zigbee is complete only when all selected PICS requirements have:

- reviewed implementation;
- unit/model tests;
- malformed/fuzz coverage for untrusted input;
- L15 and LM20 compile evidence;
- applicable L15 and LM20 hardware evidence;
- multi-hop/ecosystem evidence;
- ZUTH/qualification mapping;
- documented limits and resource capacities.

Additionally:

- no production protocol loop blocks on `millis`, `micros`, or `delay`;
- no arbitrary callback, crypto, parser, storage, print, or allocation runs in
  ISR context;
- MAC CSMA/retries/ACK/filter/duplicate/scan/association/indirect/poll work;
- NWK routing/repair/broadcast/many-to-one/source routing work;
- APS ACK/retry/duplicates/binding/groups/fragmentation work;
- security survives reset/brownout without nonce reuse or replay rollback;
- BDB role lifecycles and Trust Center restart work;
- ZDO transactions and selected services work;
- selected ZCL types/foundation/clusters/reporting work;
- every shipped example uses the shared runtime and compiles on claimed boards;
- feature matrix is accurate and exclusions are explicit;
- BLE, OpenThread, Channel Sounding, low-power, serial, and core regressions pass.

Do not tag, publish a package index, create a GitHub release, or call the core a
new release unless the user explicitly requests release work after these gates.

