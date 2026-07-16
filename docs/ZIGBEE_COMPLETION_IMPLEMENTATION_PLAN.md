# Zigbee Completion Implementation Plan

Status: implementation handoff
Repository code baseline: `25e65916a642` (v1.0.4-based, 2026-07-16)
Primary targets: nRF54L15 and nRF54LM20A/LM20B CleanCore boards
Initial certification baseline: Zigbee 3.0, Zigbee Core R22.1, BDB 3.0.1,
ZCL R8, and the exact role/device PICS selected in Phase Z0
Later compatibility track: Zigbee Core R23.2, BDB 3.1, and the applicable
Zigbee 4.0 Device Type Library/PICS

## 1. Purpose And Non-Negotiable Outcome

This document is the implementation specification for converting the current
experimental Zigbee toolkit into a reusable, testable Zigbee PRO stack. It is
written for an implementing LLM or engineer who will work in this repository.
It intentionally gives source paths, state-machine boundaries, hardware
register requirements, failure behavior, tests, and exit criteria rather than
only listing features.

The work is complete only when all of the following are true:

1. Coordinator, router, always-on end-device, and sleepy end-device roles use
   one event-driven runtime rather than code copied into examples.
2. MAC CSMA-CA, ACK/retry, duplicate rejection, scanning, association,
   indirect transmission, and child polling operate without sketch-level busy
   loops.
3. NWK unicast routing, route discovery/repair, broadcasts, many-to-one
   routing, route records, source routes, neighbor aging, and child management
   pass multi-hop tests.
4. APS delivery, acknowledgements, duplicate rejection, binding/group fan-out,
   and fragmentation/reassembly have bounded production implementations.
5. Trust Center, key tables, per-peer replay protection, counter reservation,
   key update/switch, install-code joining, secure rejoin, leave, and
   factory-new transitions survive resets and injected power loss.
6. BDB and ZDO are complete for every claimed role and selected PICS item.
7. ZCL foundation behavior and every cluster/device type claimed in the public
   feature matrix pass positive, malformed-frame, and interoperability tests.
8. Persistence is versioned, explicitly serialized, CRC-protected, and atomic;
   no C++ object representation is stored directly.
9. The nRF54 RADIO driver obeys Nordic's IEEE 802.15.4 timing, EasyDMA, CRC,
   ED, LQI, CCA, and interrupt requirements.
10. CI compiles every Zigbee example for both XIAO families and runs native
    codec, state-machine, security-vector, simulated-radio, fault-injection,
    and fuzz tests.
11. The selected PICS is fully traced to tests and an authorized Zigbee test
    provider has run the applicable qualification suite before any public
    conformance or certification claim.

"Full Zigbee" must not mean "all constants exist" or "two local boards can
exchange frames." It means complete behavior for explicitly claimed roles,
device types, and optional features. Unsupported optional features remain
listed as unsupported until their own phase and PICS entries pass.

## 2. Normative Baseline: Freeze This Before Coding

### 2.1 Two releases, not a mixed protocol

Do not combine R22.1, R23.2, BDB 3.0.1, BDB 3.1, and Zigbee 4.0 behavior in a
single implicit profile. That produces frames which may look valid while
violating role-specific security and commissioning requirements.

Use these milestones:

| Milestone | Frozen standards | Purpose |
|---|---|---|
| Z3 production baseline | Zigbee Core R22.1, BDB 3.0.1, ZCL R8, selected Zigbee 3.0 device specifications/PICS, IEEE 802.15.4-2020 PHY/MAC requirements used by Zigbee | First coherent platform and product target; widest ecosystem validation target |
| Z4 compatibility uplift | Zigbee Core R23.2, BDB 3.1, Zigbee Device Type Library 1.0 and selected Zigbee 4.0 PICS | Adds current-generation behavior only after the R22.1 baseline is stable |
| Optional features | Green Power 1.1.2, Zigbee Direct 1.1, Touchlink or product-specific specifications | Separate build features and separate test plans; never silently enabled by the base stack |

The Alliance's current download page lists R22.1 and R23.2, BDB 3.0.1 and
3.1, ZCL R8, Device Type Library 1.0, Green Power 1.1.2, and Zigbee Direct
1.1. Obtain the exact documents through the
[CSA specification request page](https://csa-iot.org/developer-resource/specifications-download-request/).
This plan does not replace the copyrighted normative specifications.

### 2.2 Required source bundle

Phase Z0 must place a private, non-redistributed manifest of the following
documents in the developer environment. Record document number, revision,
publication date, SHA-256, and permitted use. Do not commit copyrighted PDFs
unless their license explicitly permits redistribution.

| ID | Required document | Used by |
|---|---|---|
| `IEEE802154` | IEEE 802.15.4-2020 or the exact revision referenced by the selected Zigbee Core revision | PHY, MAC, CSMA-CA, ACK timing, scans, association, PIB |
| `CORE_R22` | Zigbee Core R22.1 | NWK, APS, security, ZDO, role behavior |
| `BDB_301` | Zigbee Base Device Behavior 3.0.1 | formation, steering, finding and binding, factory-new lifecycle |
| `ZCL_R8` | Zigbee Cluster Library R8 | foundation types/commands, clusters, OTA |
| `DEVICE_PICS` | Exact device specifications and PICS for each shipped example/product claim | endpoints, clusters, attributes, commands, mandatory/optional features |
| `GP_112` | Zigbee PRO Green Power 1.1.2, only if selected | proxy/sink/source optional milestone |
| `CORE_R232` | Zigbee Core R23.2 | later Zigbee 4.0 uplift |
| `BDB_31` | Zigbee Base Device Behavior 3.1 | later Zigbee 4.0 uplift |
| `DTL_10` | Zigbee Device Type Library 1.0 | later Zigbee 4.0 device claims |
| `DIRECT_11` | Zigbee Direct 1.1, only if selected | BLE-assisted commissioning optional milestone |
| `L15_PS` | `../datasheets/Nordic_nRF54L15_Datasheet_v1.0.pdf` | nRF54L15 RADIO, memory, clock, EasyDMA |
| `LM20_PS` | `../datasheets/nRF54LM20A_nRF54LM20B_Datasheet_v1.0.pdf` | nRF54LM20 RADIO, memory, clock, EasyDMA |
| `BOARD_SCHEMATICS` | XIAO and other supported-board schematics | antenna switch, RF power, clocks, external flash |

The `../datasheets` paths above are relative to the repository directory in
this workspace, not paths shipped in a release archive.

### 2.3 Normative ledger

Create `docs/zigbee/normative-requirements.yaml` before implementing new
protocol behavior. One record represents one testable requirement:

```yaml
- id: MAC-CSMA-001
  baseline: r22_1
  roles: [coordinator, router, end_device]
  feature: unslotted_csma_ca
  source:
    document: IEEE802154
    clause: "<verified clause>"
    table_or_figure: "<verified table>"
  requirement: "<short paraphrase, never a copied long passage>"
  constants:
    macMinBE: "<verified value or configurable range>"
    macMaxBE: "<verified value or configurable range>"
  implementation:
    files: []
    symbols: []
  tests: []
  status: unimplemented
```

Rules for the ledger:

- Every on-air bit value, timer, retry limit, table size minimum, status code,
  security rule, and commissioning transition gets a source clause.
- Values in this plan marked **VERIFY-NORMATIVE** are design guidance only
  until copied into this ledger from the pinned specification.
- Hardware values from the Nordic product specifications get `L15_PS` or
  `LM20_PS` references with section, register, and field names.
- A test may cover several records, but every record must name at least one
  automated test before its status becomes `implemented`.
- Generated constants may be produced from the ledger, but generated code must
  never contain unreviewed values scraped from third-party headers.
- A PICS selection change is a baseline change. Review it like an API change.

### 2.4 Source-of-truth order

When sources disagree, use this order:

1. Selected CSA/IEEE normative specification and accepted errata.
2. Selected certification PICS and official test specification.
3. Nordic nRF54 product specification for peripheral behavior.
4. Board schematic for RF path, clocks, storage, and pins.
5. Captured packets from a known conformant implementation as supporting
   evidence, never as the only authority.
6. Existing CleanCore code and examples.
7. Comments, blogs, coordinator quirks, and third-party source code.

Do not copy ZBOSS or any proprietary/reference stack. Packet captures may be
used to test interoperability, but the implementation must remain independent
and license-clean.

## 3. Repository Baseline Audit

### 3.1 Current source map

All paths in this document are relative to
`hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/`
unless stated otherwise.

| Area | Current source | Baseline facts |
|---|---|---|
| IEEE 802.15.4 HAL | `src/nrf54l15_hal_parts/nrf54l15_hal_802154_rawradio.inc`, declarations in `src/nrf54l15_hal.h` | Raw RADIO configuration, direct TX/RX, optional one-shot CCA, MAC ACK helpers, ED, four DMA RX buffers, eight-frame software queue |
| Protocol codecs/device model | `src/zigbee_stack.h`, `src/zigbee_stack.cpp` | 1,292-line header and 7,378-line implementation containing MAC/NWK/APS/ZDO/ZCL concerns |
| Commissioning | `src/zigbee_commissioning.h`, `src/zigbee_commissioning.cpp` | End-device steering, association, rejoin, Trust Center message handling; several blocking `millis()`/`micros()` loops |
| Security | `src/zigbee_security.h`, `src/zigbee_security.cpp` | AES-CCM*, install-code derivation, NWK/APS secured-frame helpers, selected key commands |
| Persistence | `src/zigbee_persistence.h`, `src/zigbee_persistence.cpp` | Version-6 raw C++ state blob, chunked Preferences storage, non-atomic replacement |
| Examples | `examples/Zigbee/` | 30 sketches and about 40,417 lines; many sketches embed a private runtime |
| CI | repository `.github/workflows/ci.yml` | Main matrix compiles five Zigbee examples; one warning build covers the coordinator |
| Host/hardware scripts | repository `scripts/zigbee_*.py` | Serial and ecosystem scenarios, not a native layer/codec conformance suite |

### 3.2 What can be retained

Retain behavior only after it has characterization tests:

- `ZigbeeRadio` channel/power setup, PHY framing, CRC setup, ACK helpers,
  buffered receive concept, energy detection, RSSI capture, and source-pending
  callback concept.
- Existing MAC frame, beacon, association, orphan, realignment, NWK, APS,
  ZDO, and ZCL encode/decode behavior for currently supported forms.
- AES-CCM* and install-code derivation only after official vectors and boundary
  tests pass.
- Existing network-key transport/update/switch and secure-rejoin parsing as
  candidate code, not assumed-correct state-machine behavior.
- Existing endpoint, descriptor, binding, reporting, group, scene, and selected
  Home Automation cluster behavior as migration input.
- Existing sketches as black-box behavior fixtures until replacements pass the
  same interoperability tests.

### 3.3 Confirmed blockers

1. `ZigbeeRadio::transmit()` and `transmitThenReceive()` default
   `performCca=false`; one optional carrier-sense check is not CSMA-CA.
2. `ZigbeeRadio` exposes `spinLimit` and busy-waits for READY, END, DISABLED,
   ACK, RX windows, CCA, and ED. Commissioning adds more blocking loops and
   `delay()` calls.
3. Buffered receive invokes ACK/filter logic inside the RADIO ISR and uses a
   global `g_activeZigbeeRadioIrq` owner. The shared `RADIO_0_IRQHandler`
   dispatches Channel Sounding first, Zigbee second, and BLE last; there is no
   general radio scheduler.
4. Hardware address matching is disabled, while normal Zigbee examples do not
   consistently install a complete software PAN/address filter. Filtering must
   occur before deciding whether to acknowledge.
5. `ZigbeeFrame` has channel and RSSI but no LQI, CRC metadata, hardware
   timestamp, or explicit buffer ownership.
6. `ZigbeeNetworkFrame` advertises multicast/source-route flags, but current
   codec paths reject those forms. Only a small subset of NWK commands exists.
7. Neighbor and route tables are fixed at eight entries and are not maintained
   by a complete routing engine.
8. APS acknowledgements exist as codecs, but delivery/retry and fragmentation
   live in examples or are missing.
9. `ZigbeePersistentState` stores one global inbound NWK counter and one global
   inbound APS counter, rather than replay state per sender/key/layer.
10. Persistence removes the previous state before all new chunks and the final
    length marker are committed. Power loss can erase the only valid network
    state. It also persists compiler-dependent struct layout and padding.
11. `ZigbeeZclDataType` contains only Boolean, bitmap 8/16/32, unsigned
    8/16/32, signed 16, and short character string.
12. The public model caps descriptors/clusters/bindings/neighbors/routes with
    small arrays and returns mostly `bool`, losing out-of-space, timeout,
    security, routing, and cancellation reasons.
13. The large Home Automation sketches contain duplicate ACK trackers,
    commissioning loops, security handling, persistence, and resend logic.
    Two coordinator demos alone are 5,017 and 4,964 lines.
14. Two boards can validate direct communication but cannot prove multi-hop
    routing, route repair, broadcast propagation, or concentrator behavior.

### 3.4 Current capability is not a conformance baseline

The current repository does provide useful primitives: IEEE 802.15.4 PHY
operation, MAC frame helpers, scanning/association pieces, direct NWK/APS
codecs, AES-CCM*, selected ZDO requests, selected ZCL foundation commands,
bindings/groups/scenes, several clusters, persistence, and coordinator/router/
end-device demonstrations. Preserve those behaviors, but do not mark a layer
complete until the exit gates in this plan pass.

## 4. Claimed Roles And Feature Boundaries

### 4.1 Role matrix for the Z3 baseline

| Capability | Coordinator | Router | End device | Sleepy end device |
|---|---:|---:|---:|---:|
| Form a distributed network / select channel and PAN | Required | No | No | No |
| Trust Center policy and device authorization | Required for centralized-security coordinator | Client behavior | Client behavior | Client behavior |
| Permit joining | Required | Required when allowed by network policy | No | No |
| Admit children and maintain indirect queue | Required | Required | No | No |
| Route discovery, relay, repair, broadcasts | Required | Required | Originates/consumes only | Originates/consumes through parent |
| Many-to-one / route record / source-route relay | Required when selected by PICS | Required relay behavior when selected | No | No |
| Parent selection and rejoin | No | Required when joining an existing network | Required | Required |
| Receiver on when idle | Yes | Yes | PICS/capability dependent | No |
| Data-request polling / end-device timeout | Parent side | Parent side | Optional | Required |
| BDB formation | Required | No | No | No |
| BDB network steering | Required | Required | Required | Required |
| Finding and binding | Per selected PICS | Per selected PICS | Per selected PICS | Per selected PICS |
| Persistent network/key/counter state | Required | Required | Required | Required |

This table is architectural. Replace `Required when selected by PICS` with
the exact normative status from Phase Z0. A feature cannot be inferred as
mandatory merely because a commercial stack implements it.

### 4.2 Explicitly separate these claims

- A reusable Zigbee Compliant Platform or equivalent platform claim is not the
  same as certification of an end product/device type.
- A conformant PHY/MAC primitive is not a conformant Zigbee PRO stack.
- A cluster codec is not a conformant cluster server or client.
- Green Power, Touchlink, Zigbee Direct, OTA Upgrade, and every application
  cluster must be tracked against the selected program/PICS; do not call all of
  them universally mandatory.
- The supported PHY is 2.4 GHz O-QPSK through the nRF54 RADIO. This plan does
  not add Sub-GHz hardware capability.
- Concurrent BLE and Zigbee is not supported until the radio-arbitration phase
  passes deadline and interoperability tests. Fail `begin()` explicitly when
  another exclusive radio owner is active rather than corrupting either link.

## 5. nRF54 RADIO Hardware Contract

This section is grounded in Section 8.17, especially Section 8.17.12
"IEEE 802.15.4 operation," of both local Nordic v1.0 product specifications.
The implementing model must cite the exact local page/register in the
normative ledger before changing a hardware value.

### 5.1 Common nRF54L15 and nRF54LM20 PHY facts

| Property | Required value/behavior |
|---|---|
| RADIO mode | `MODE.MODE = Ieee802154_250Kbit` |
| Band/channels | 16 channels, 11 through 26, in the 2.4 GHz band |
| Center frequency | `2405 + 5 * (channel - 11)` MHz |
| `FREQUENCY.FREQUENCY` | 5, 10, ..., 80 MHz offset for channels 11, 12, ..., 26 with the normal frequency map |
| Symbol duration | 16 us |
| Preamble | Four zero octets, 128 us |
| SFD | `0xA7`, 32 us |
| PHR | One octet, 32 us, carrying PSDU length including FCS in IEEE mode |
| Maximum PSDU | 127 octets including two-octet FCS; upper layers have at most 125 octets before FCS |
| CRC/FCS | Two octets, polynomial `x^16 + x^12 + x^5 + 1` (`0x11021`), init zero, IEEE skip/address mode, little-endian over-the-air behavior |
| Whitening | Disabled |
| ED duration | Eight symbols = 128 us per measurement |
| CCA survey | Eight symbols = 128 us while the RADIO is in RX |
| SIFS | 12 symbols = 192 us for MPDU length at or below 18 octets |
| LIFS | 40 symbols = 640 us for MPDU length above 18 octets |
| ACK exchange timing | Derive the turnaround/deadline from the pinned IEEE clause and measure the first ACK preamble target; do not turn an ambiguous product-spec figure annotation into a software constant |
| RX/TX buffers | `PACKETPTR` must reference DMA-accessible RAM and must be updated before START; RADIO uses one double-buffered pointer for both directions |
| Correct completion event | Use `PHYEND` for IEEE 802.15.4 shortcuts; Nordic explicitly warns not to use END-to-START in this mode |

The local product specifications list IEEE 802.15.4 sensitivity of -102 dBm
for nRF54L15 and -101 dBm for nRF54LM20. These are characterization/test-floor
inputs, not CCA thresholds.

### 5.2 Required packet configuration

The existing configuration is close to the documented Nordic setup and must
be locked by a register-contract test:

```text
MODE.MODE       = Ieee802154_250Kbit
PCNF0.LFLEN     = 8
PCNF0.S0LEN     = 0
PCNF0.S1LEN     = 0
PCNF0.S1INCL    = Automatic
PCNF0.PLEN      = 32bitZero
PCNF0.CRCINC    = Include
PCNF0.TERMLEN   = 0
PCNF1.MAXLEN    = 127
PCNF1.STATLEN   = 0
PCNF1.BALEN     = 0
PCNF1.ENDIAN    = Little
PCNF1.WHITEEN   = Disabled
CRCCNF.LEN      = Two
CRCCNF.SKIPADDR = Ieee802154
CRCPOLY         = 0x11021
CRCINIT         = 0
SFD             = 0xA7
```

Requirements:

- Verify `CRCINC=Include` and every length conversion with lengths 0, 1, 2,
  3, 18, 19, 125, 126, and 127. The DMA buffer contains PHR separately from
  the PSDU; never subtract FCS twice.
- Keep `MAXLEN=127` for this IEEE configuration. Nordic defines MAXLEN as the
  payload plus static add-on and excludes S0/LENGTH/S1; it is not the total C
  array size. With hardware CRC, the maximum RX RAM layout is PHR (1) + MAC
  bytes without FCS (125) + appended LQI (1), so the current aligned 128-byte
  storage is sufficient. Do not rely on a later software check to prevent DMA
  overwrite.
- Keep all DMA packet arrays `alignas(4)` and in DMA-accessible data RAM. Add a
  debug assertion that rejects flash, stack-lifetime-expired, secure-only, or
  otherwise inaccessible pointers before arming RADIO.
- Never repoint or reuse a buffer until the event which guarantees EasyDMA has
  stopped accessing it. Encode ownership states as `Free`, `RadioRx`,
  `RadioTx`, `MacQueued`, and `Application`.
- Use at least two independent RX buffers and a distinct TX pool. Separate
  pools prevent received traffic from consuming every buffer required to send
  an ACK or protocol response.

### 5.3 ED, RSSI, and LQI conversion

The hardware-reported ED/LQI scale must not be exposed directly as Zigbee
quality:

```text
ieee_ed_or_lqi = min(4 * hardware_value, 255)
power_dbm      = -92 + hardware_value
```

Both product specifications describe `ED_RSSISCALE = 4`; confirm the exact
offset and sign convention from the pinned register section when implementing
the conversion tests. Required behavior:

- `sampleEnergyDetect()` becomes asynchronous and returns both raw sample and
  normalized 0..255 value. Saturate instead of wrapping.
- RX metadata records normalized LQI. Nordic appends hardware LQI after the
  received payload in IEEE mode and describes the RSSI-derived value using a
  median-of-three process. Do not copy the appended LQI byte into the PSDU.
- Record signed RSSI dBm separately. Do not substitute RSSI for LQI in routing
  cost unless the Zigbee Core clause explicitly permits the chosen mapping.
- Add boundary tests for raw values 0, 1, 63, 64, and 127.

### 5.4 CCA and CSMA hardware boundary

Nordic exposes:

- Mode 1: energy above `CCAEDTHRES`.
- Mode 2: carrier detection using correlation configuration.
- Mode 3 variants: carrier and energy, or carrier or energy.
- `CCAIDLE`, `CCABUSY`, and `CCASTOPPED` events after the 128 us survey.

The current code overwrites only `CCAMODE` with `CarrierMode` and preserves
the other `CCACTRL` reset fields. Do not assume that is the correct Zigbee
profile. Phase Z4 must select the CCA mode/threshold/correlation values from
the pinned IEEE/Zigbee requirements and Nordic electrical scale, then record
them in the ledger. CCA is one asynchronous PHY operation. Random backoff,
BE/NB state, retransmission, and queueing belong to the MAC, not the RADIO
driver.

### 5.5 Turnaround, shortcuts, and interrupts

- The product specifications list legacy TXEN/RXEN ramp-up near 130 us and
  fast ramp-up near 40 us, with maximum RX-to-TX turnaround near 17 us. Treat
  values as family-specific acceptance inputs and verify the exact table for
  each silicon before choosing `TIMING.RU`.
- Do not retain the current comment that legacy ramp-up is selected for BLE
  direction changes as the justification for Zigbee. Measure both silicon
  families and select a Zigbee-specific setting which meets ACK and energy
  constraints.
- Use DPPI/shortcuts for `READY -> START` and `PHYEND -> DISABLE` where the
  product specification permits them. Use `DISABLED -> RXEN/TXEN` plus `TIFS`
  only for a verified sequence. Do not use `END -> START`.
- Timestamp SFD/FRAMESTART and PHYEND with a hardware timer capture where
  possible. `micros()` sampled after an ISR is not adequate evidence for ACK
  turnaround.
- The RADIO ISR may clear events, capture metadata, move buffer indices, arm a
  prebuilt immediate ACK, and enqueue a small event. It must not parse NWK/APS,
  run AES, access Preferences, print Serial output, allocate memory, or invoke
  arbitrary application callbacks.
- Normal address/PAN filtering must be complete before sending ACK. If a
  hardware prefix match cannot express all legacy Zigbee addressing forms,
  use a bounded early MAC-header parser. `MHRMATCHCONF/MHRMATCHMASK` are generic
  32-bit match facilities, not proof of a full Zigbee address filter.

### 5.6 Clock, RF path, sleep, and shared-radio ownership

- Acquire HFXO and board RF switch through reference-counted platform owners.
  Release them only when no receive, transmit, timer-critical turnaround, BLE,
  Thread, Channel Sounding, or other radio client needs them.
- `System OFF` must first cancel the PHY operation, disable all RADIO shortcuts
  and interrupts, wait for DISABLED with a hardware-derived deadline, release
  buffers, and persist critical security counters.
- Replace `g_activeZigbeeRadioIrq` and priority-ordered global IRQ dispatch with
  a `RadioArbiter`. In the first production Zigbee release the arbiter may
  enforce exclusive ownership. It must return `RadioBusyOtherProtocol` rather
  than allowing simultaneous owners.
- A later multiprotocol scheduler must accept operations with earliest start,
  hard deadline, duration, priority, preemptibility, setup/teardown time, and
  completion callback. It must prove BLE connection/advertising deadlines and
  Zigbee ACK/indirect-poll deadlines under conflict before concurrent operation
  is advertised.

### 5.7 Hardware-level acceptance tests

1. Read back every RADIO configuration field on L15 and LM20 after `begin()`.
2. Transmit PSDU boundary lengths and verify PHR, FCS, SFD, channel, and frame
   duration with a trusted sniffer.
3. Receive valid, bad-FCS, wrong-PAN, wrong-short-address, wrong-extended-
   address, broadcast, beacon, command, and ACK frames; verify only eligible
   frames are acknowledged and queued.
4. Measure SIFS/LIFS, ACK turnaround, CCA survey, channel switch, and RX/TX
   ramp with timer captures or logic analyzer GPIO strobes.
5. Sweep input power around sensitivity and CCA thresholds in a controlled RF
   setup; verify monotonic RSSI, ED, and LQI without integer wrap.
6. Exhaust RX buffers while reserving one TX/ACK path; prove no use-after-free
   or DMA overwrite under AddressSanitizer simulation and hardware stress.
7. Inject every RADIO event order, timeout, cancellation, and late interrupt in
   the fake peripheral model. A stale interrupt must carry an operation
   generation and must not complete a newer request.
8. Start Zigbee while BLE, Thread, and Channel Sounding own RADIO. Until a
   scheduler is certified, every conflicting start must fail deterministically.

### 5.8 Exact nRF54 register/event baseline

The nRF54L15 and nRF54LM20 CMSIS RADIO structures in this repository are
byte-identical for the registers used here. The nonsecure RADIO base is
`0x4008A000`, secure base `0x5008A000`; RADIO IRQ0 and IRQ1 are 138 and 139.
The initial implementation should use one explicitly owned IRQ path rather
than assuming IRQ1 provides automatic isolation.

Important task offsets from RADIO base:

```text
TXEN 0x000, RXEN 0x004, START 0x008, STOP 0x00C, DISABLE 0x010
RSSISTART 0x014, BCSTART 0x018, BCSTOP 0x01C
EDSTART 0x020, EDSTOP 0x024, CCASTART 0x028, CCASTOP 0x02C
PLLEN 0x06C, SOFTRESET 0x0A4
```

Important event offsets:

```text
READY 0x200, TXREADY 0x204, RXREADY 0x208, ADDRESS 0x20C
FRAMESTART 0x210, PAYLOAD 0x214, END 0x218, PHYEND 0x21C
DISABLED 0x220, DEVMATCH 0x224, DEVMISS 0x228
CRCOK 0x22C, CRCERROR 0x230, BCMATCH 0x238
EDEND 0x23C, EDSTOPPED 0x240
CCAIDLE 0x244, CCABUSY 0x248, CCASTOPPED 0x24C
MHRMATCH 0x254, SYNC 0x258, PLLREADY 0x2B0, RXADDRESS 0x2BC
```

Important configuration offsets:

```text
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

RADIO state encodings used by assertions are Disabled 0, RxRu 1, RxIdle 2,
Rx 3, RxDisable 4, TxRu 9, TxIdle 10, Tx 11, and TxDisable 12. `SOFTRESET` is
legal only in Disabled. It does not replace explicit teardown of interrupts,
events, subscriptions/publications, shortcuts, DPPI resources, or packet
pointers.

Relevant shortcut bits are:

```text
READY_START 0, DISABLED_TXEN 2, DISABLED_RXEN 3
ADDRESS_RSSISTART 4, END_START 5, ADDRESS_BCSTART 6
RXREADY_CCASTART 10, CCAIDLE_TXEN 11, CCABUSY_DISABLE 12
FRAMESTART_BCSTART 13, READY_EDSTART 14, EDEND_DISABLE 15
CCAIDLE_STOP 16, TXREADY_START 17, RXREADY_START 18
PHYEND_DISABLE 19, PHYEND_START 20
```

Nordic's IEEE flow uses combinations based on `PHYEND_DISABLE`, not
`END_START`. A receive template may use `RXREADY_START`,
`ADDRESS_RSSISTART`, `ADDRESS_BCSTART`, and `PHYEND_DISABLE`. A transmit
template may use `TXREADY_START` and `PHYEND_DISABLE`. A CCA-to-TX template
uses `RXREADY_CCASTART`, `CCABUSY_DISABLE`, `CCAIDLE_TXEN`,
`TXREADY_START`, and `PHYEND_DISABLE` after normative review.

Energy detection is different: the baseband START task must remain disabled
while RADIO is in RX, and `READY_EDSTART` begins ED. The current
`sampleEnergyDetect()` path enables `RXREADY_START`; Phase Z4 must correct it.
`EDCTRL` reset `0x20000000` encodes a 128 us period. Follow the register
description: `EDCNT=0` means one scan and nonzero `EDCNT=N` means `N+1` scans.
Because surrounding prose/figures can be read ambiguously, add a hardware
duration test for 0, 1, and several larger values.

On receive with hardware CRC, RAM begins with PHR, omits the two FCS octets,
then appends one hardware LQI byte after the MAC bytes. If `macLength` is the
validated PHR length minus two, LQI is at `packet[1 + macLength]`. It is valid
only for qualifying frames of the documented minimum size. Store it separately
and never include it in the MAC payload.

### 5.9 TIFS, ACK scheduling, and current unit defects

`TIFS` is measured in microseconds. The current assignment `TIFS=40` means
40 us, not 40 symbols. It is neither the 12-symbol/192 us ACK turnaround nor
the 40-symbol/640 us LIFS. Do not use one TIFS value as a substitute for MAC
ACK timing and SIFS/LIFS policy.

The product specifications give common timing inputs of roughly 130 us legacy
ramp, 40 us fast ramp, 18 us TX disable, 0.2 us RX disable, maximum 17 us
RX-to-TX turnaround, and about 0.25 us shortcut jitter. Confirm the exact
electrical table for each silicon/package.

For immediate ACK, use a preallocated/prebuilt ACK buffer, staged header
filtering while RX is in progress, hardware PHYEND capture, and TIMER/DPPI-
scheduled TXEN. Nordic's open `nrf_802154` nRF54L reference uses a calculation
equivalent to captured PHYEND plus 192 us, minus 40 us fast ramp, minus a
documented driver/event-latency adjustment of 23 us. Treat that as a pinned
reference implementation input, not a magic formula: record its exact version,
validate the latency on both SoCs, and measure last received bit to first ACK
preamble at 192 us.

Do not busy-wait, copy a complete frame, call user filters, or run arbitrary
callbacks inside the RADIO ISR. Staged filtering should advance BCC only to
known header boundaries: first PHR+FCF, then addressing/PAN fields, then any
security-control/auxiliary-header boundary. The early parser receives only
bytes actually captured. Fall back safely if a BCC event is late.

### 5.10 HFXO and constant-latency requirements

Both product specifications state that `XOSTARTED` quality is not sufficient
for RADIO; RADIO requires `XOTUNED`. The current
`ClockControl::startHfxo(true)` in `nrf54l15_hal_timebase.cpp` accepts Running,
XOSTARTED, or XOTUNED, so its `waitForTuned` contract is broken.

Implement a reference-counted HFXO manager:

1. Trigger PLLSTART before XOSTART as required by anomaly 39.
2. Clear XOSTARTED, XOTUNED, XOTUNEFAILED, and XOTUNEERROR state/events.
3. Trigger XOSTART.
4. Declare `RadioReady` only after XOTUNED, preserving software tuned state
   when an already-running/tuned oscillator emits no fresh event.
5. Retry a tune failure only according to the product-spec procedure and only
   while RADIO is Disabled.
6. Defer XOTUNEERROR recovery until no radio operation is active.
7. After the last owner, XOSTOP then PLLSTOP.

Return `Tuned`, `Timeout`, `TuneFailed`, or `InvalidState`, not `bool`.

Anomaly 20 on L15 and LM20 can corrupt the transmitted payload if CPU/
peripherals in the MCU domain sleep as RADIO starts TX. Before every RXEN/TXEN
operation acquire constant-latency mode through `POWER.TASKS_CONSTLAT`; after
RADIO reaches Disabled release it through `POWER.TASKS_LOWPWR`. This owner is
global/refcounted because BLE and Channel Sounding may require it too.

### 5.11 Mandatory silicon errata layer

Create one `SiliconInfo`/`RadioErrata` module. Read FICR once, decode PART,
PACKAGE and the four VARIANT ASCII bytes, and expose named predicates. Do not
scatter revision magic throughout radio code.

Documented PART values to table-test are:

```text
nRF54L15  0x00054B15
nRF54LM20A 0x054BC20A
nRF54LM20B 0x054BC20B
```

Relevant common FICR base/offsets are base `0x00FFC000`, DEVICEID at 0x304/
0x308, UUID 0x30C, PART 0x31C, VARIANT 0x320, PACKAGE 0x324, RAM 0x328,
RRAM 0x32C, DEVICEADDRTYPE 0x3A0, DEVICEADDR 0x3A4, and TRIMCNF from 0x400.
Startup already copies FICR trim entries; do not invent duplicate analog trims.

Mandatory reviewed anomalies:

- **L15 anomaly 6, Rev1/Rev2:** IEEE 802.15.4 TX above 0 dBm may violate the
  restricted band edge at 2405/2480 MHz. Immediately after selecting IEEE
  mode, the official workaround writes value 2 to secure RADIO offset 0x810
  (`0x5008A810`). Apply only to affected L15 revisions through a secure
  service/attribution-safe wrapper. Never let nonsecure code blindly fault on
  the secure alias.
- **L15/LM20 anomaly 20:** acquire constant latency before every RXEN/TXEN as
  described above.
- **L15/LM20 anomaly 39:** PLLSTART precedes XOSTART; XOSTOP precedes PLLSTOP.
- **L15 anomaly 41:** channel 23 sensitivity is approximately 2 dB worse with
  no workaround. Keep channel 23 supported and include it in PER/sensitivity
  characterization and documentation.
- **L15 anomalies 22/24/25:** TIMER/DPPI compare values near 1 and reuse after
  powerdown have documented missed-event cases. STOP+CLEAR before reuse, avoid
  CC=1 where required, and apply the exact anomaly-25 clear sequence/predicate
  from the pinned erratum.
- **L15/LM20 anomaly 26:** when bridging DPPIC/PPIB security domains, use the
  same channel index through the bridge and aligned security attribution.
- **L15/LM20 anomaly 59:** EGU interrupt security follows DPPI channel 0;
  EGU and DPPIC channel-0 attribution must agree.
- **L15 Rev1 anomaly 33:** affected silicon can emit through P1.09..P1.12.
  XIAO routes signals including SDA/SCL/MIC clock in this area; the hardware/
  resistor/frequency workaround cannot always be fixed in core software.
  Detect the revision, document the board/regulatory limitation, and do not
  claim software alone resolves it.

Do not apply BLE-specific anomaly 49 to IEEE 802.15.4, and do not apply an
external-FEM workaround to the XIAO passive RF switch. Each magic register
write must name an official erratum URL and silicon predicate in code and tests:

- [nRF54L15 Rev1 errata](https://docs.nordicsemi.com/r/bundle/errata_nrf54l15_rev1/)
- [nRF54L15 Rev2 errata](https://docs.nordicsemi.com/r/bundle/errata_nrf54l15_rev2/)
- [nRF54LM20A Rev1 errata](https://docs.nordicsemi.com/r/bundle/errata_nRF54LM20A_Rev1/)

`DEVICEID` is documented as a unique 64-bit ID, not a globally assigned IEEE
EUI-64/OUI. Prefer provisioned EUI-64. If a local address is derived, define
the locally administered transform, validate invalid/broadcast values, persist
it once, and never change it across firmware updates.

### 5.12 DPPI, TIMER, EGU, and resource ownership

Keep symbol-critical timing in the radio power domain:

```text
DPPIC10 NS/S 0x40082000 / 0x50082000, 24 channels, 6 groups
TIMER10 NS/S 0x40085000 / 0x50085000, PCLK32M, 8 CC channels
EGU10   NS/S 0x40087000 / 0x50087000, 16 event/task pairs
```

L15 MCU DPPIC00 has eight channels; LM20 has sixteen. MCU-to-radio PPIB width
is eight on L15 and twelve on LM20. Same-domain event-to-task latency is two
peripheral clock cycles; include wake/domain effects in measured deadlines.
Use GRTC for long sleep deadlines, not symbol timing. Use EGU for deferred
notification, not ACK scheduling.

The current BLE timing code owns DPPIC10 channels 0..3. `RadioArbiter` must
allocate an explicit resource manifest per operation/client:

```text
RADIO owner and IRQ, PACKETPTR, shortcuts, subscriptions/publications,
DPPIC channels/groups, PPIB channels, TIMER/CC channels, EGU events,
CCM/CRACEN operation, HFXO, CONSTLAT, and board RF path
```

Acquisition is atomic or rolls back completely. Teardown reaches RADIO
Disabled, clears all owned SUBSCRIBE/PUBLISH registers, events, interrupt banks,
shortcuts and PACKETPTR, stops/clears TIMER and EGU, and clears only owned DPPI
CHEN/group bits. Snapshot tests must prove BLE -> 802.15.4 -> BLE and Channel
Sounding -> 802.15.4 -> Channel Sounding leave no stale resource.

Nordic's BSD-3-Clause `nrf_802154` nRF54L driver may be used as an architectural
and hardware-sequence reference after recording exact source revision and
license. Port coherent behavior with attribution; do not cherry-pick unexplained
register sequences or import any proprietary binary.

### 5.13 Board RF path, TX limits, and VPR

- XIAO nRF54L15 uses passive RF switch U10 FM8625H. Schematic signals are
  `RF_SW_PWR` on P2.03 and `RF_SW_CTL` on P2.05; control selects RF path. Assert
  power/path before radio ramp and retain them until the last radio owner
  releases them.
- XIAO nRF54LM20A has a direct antenna matching path and no GPIO RF switch;
  its board trait is a no-op.
- Replace the current "was RF path already powered" Boolean with shared
  reference-counted board-RF ownership. Zigbee `end()` must not power down the
  path underneath BLE/Thread/CS.
- Clamp TX power to silicon package, board, channel/errata, antenna, and local
  regulatory limits and return actual selected power. L15 is characterized up
  to +7 dBm in QFN/+8 dBm in CSP; LM20 CSP up to +8 dBm. A header enum which
  contains +8 dBm does not prove every board/package may use it.

VPR firmware is not required for IEEE 802.15.4 or Zigbee. Complete and qualify
the CPU/TIMER/DPPI path first. VPR may be a later offload only with a documented
ABI, per-SoC linker/retention region, radio ownership, security attribution,
source/license provenance, and regression suite. It cannot run concurrently
with the existing Channel Sounding VPR image while both directly own RADIO.
Do not ship Nordic proprietary microcode without explicit redistribution rights.
Both product specifications require `INITPC` to be 64-byte aligned. L15 reserves
the 512-byte context-save region at `0x2003FE00`; LM20 reserves 512 bytes at
`0x2007FD40`. LM20 additionally documents ignored low INITPC bits and interrupt
masking considerations when changing INITPC after start. Keep these as per-SoC
traits rather than one shared linker constant.

### 5.14 Expanded hardware acceptance

In addition to Section 5.7:

1. Test channels 11, 15, 20, 23, and 26 at <=0 dBm and board maximum. On
   affected L15, assert anomaly-6 workaround before >0 dBm edge-channel TX.
2. Test PHR zero rejection, minimum frames, ACK-sized frame, PHR 127/MAC 125,
   good/bad CRC, and MPDU 18/19 SIFS/LIFS boundary.
3. Cover every selected IEEE 2003/2006/2015/2020 frame-version/address/PAN-
   compression/security combination and invalid reserved forms.
4. Measure last RX bit to first ACK preamble at the 192 us target, plus DSN,
   Frame Pending, duplicate, retry, invalid, and late ACK cases.
5. Calibrate ED from approximately -100 through -20 dBm, including saturation;
   keep raw, dBm, and standardized 0..255 energy separate. Test CCA modes under
   modulated and unmodulated interference.
6. Repeatedly sleep/wake and RX/TX to expose anomalies 20/39; no preamble-only
   corrupt payload or oscillator hang is permitted.
7. Force clock tune timeout/failure, no buffer, arbiter denial, TIMER/DPPI
   allocation failure, and abort in every PHY state. Every error rolls back.
8. Cycle BLE/CS/802.15.4 ownership 10,000 times with register snapshots,
   bonded BLE reconnect, and current measurement proving HFXO/CONSTLAT/RF path
   release.
9. Add static assertions for common register offsets and explicit per-SoC
   traits for package power, errata, DPPI/PPIB capacity, and any VPR layout.

## 6. Target Architecture

### 6.1 Required module layout

Split the stack by ownership, not merely by file size. The final layout should
be close to the following:

```text
src/zigbee/
  core/
    status.h
    types.h
    endian.h
    byte_reader.h
    byte_writer.h
    fixed_pool.h
    fixed_queue.h
    event.h
    timer_queue.h
    diagnostics.h
    feature_config.h
  platform/
    radio_phy.h
    radio_phy_nrf54.cpp
    radio_arbiter.h
    radio_arbiter.cpp
    clock.h
    random.h
    storage.h
    monotonic_clock.h
    critical_section.h
  mac/
    mac_types.h
    mac_codec.h
    mac_codec.cpp
    mac_pib.h
    mac_engine.h
    mac_engine.cpp
    csma_ca.h
    csma_ca.cpp
    scan_manager.h
    scan_manager.cpp
    indirect_queue.h
    indirect_queue.cpp
    duplicate_table.h
  nwk/
    nwk_types.h
    nwk_codec.h
    nwk_codec.cpp
    nwk_engine.h
    nwk_engine.cpp
    neighbor_table.h
    child_table.h
    route_table.h
    route_discovery.h
    route_discovery.cpp
    broadcast_table.h
    broadcast_engine.cpp
    concentrator.h
    network_commands.h
    network_commands.cpp
  aps/
    aps_types.h
    aps_codec.h
    aps_codec.cpp
    aps_engine.h
    aps_engine.cpp
    aps_transactions.h
    aps_duplicate_table.h
    fragmentation.h
    fragmentation.cpp
    binding_table.h
    group_table.h
  security/
    crypto_provider.h
    crypto_provider_cracen.cpp
    crypto_provider_software.cpp
    key_store.h
    replay_table.h
    counter_allocator.h
    security_codec.h
    security_codec.cpp
    trust_center.h
    trust_center.cpp
  bdb/
    bdb_types.h
    bdb_engine.h
    bdb_engine.cpp
    commissioning_policy.h
  zdo/
    zdo_types.h
    zdo_codec.h
    zdo_codec.cpp
    descriptor_registry.h
    zdo_transaction_manager.h
    zdo_transaction_manager.cpp
    zdo_server.h
    zdo_server.cpp
  zcl/
    zcl_types.h
    zcl_codec.h
    zcl_codec.cpp
    attribute_store.h
    attribute_store.cpp
    reporting_engine.h
    reporting_engine.cpp
    cluster_registry.h
    foundation_server.h
    foundation_server.cpp
    clusters/
      basic.cpp
      power_configuration.cpp
      identify.cpp
      groups.cpp
      scenes.cpp
      on_off.cpp
      level_control.cpp
      color_control.cpp
      temperature_measurement.cpp
      relative_humidity.cpp
      poll_control.cpp
      time.cpp
      ota_upgrade.cpp
  ota/
    ota_image.h
    ota_client.h
    ota_client.cpp
    ota_server.h
    ota_server.cpp
    ota_storage.h
  persistence/
    schema.h
    codec.h
    codec.cpp
    journal.h
    journal.cpp
    migration.h
  runtime/
    stack_config.h
    stack.h
    stack.cpp
    role_coordinator.cpp
    role_router.cpp
    role_end_device.cpp
    sleepy_end_device.cpp
  compatibility/
    legacy_zigbee_stack.h
    legacy_zigbee_stack.cpp
```

Keep the public include entry small, for example `src/Zigbee.h`. Do not expose
Nordic register types, internal table layouts, buffer nodes, or timer objects.

### 6.2 Dependency rules

1. `core` depends on no Arduino, radio, storage, or protocol module.
2. `platform` implements abstract clocks, random, storage, and PHY interfaces.
3. Codecs depend only on `core`; codec tests must run on a desktop compiler.
4. MAC depends on `core` and `platform/radio_phy`, never on NWK.
5. NWK depends on MAC service primitives and security interfaces, never ZDO or
   ZCL.
6. APS depends on NWK and security interfaces.
7. ZDO, BDB, and ZCL depend on APS services, not raw frame builders.
8. Application callbacks run only from `ZigbeeStack::process()`, never from an
   ISR or storage write.
9. Persistence stores protocol records through explicit codecs. Protocol
   objects do not call `Preferences` directly.
10. Compatibility wrappers may call new modules; new modules must never call
    legacy `ZigbeeHomeAutomationDevice` behavior.

Add a lightweight include-boundary test which rejects forbidden includes and
a linker test which proves all native codec tests build without Arduino.

### 6.3 Run-to-completion event model

Use one non-preemptive stack context. ISR producers and application producers
enqueue bounded events; `ZigbeeStack::process()` drains them. Each handler must
return before processing the next event.

Minimum event types:

```text
PhyRxStarted, PhyRxDone, PhyRxFailed, PhyTxDone, PhyCcaDone, PhyEdDone,
PhyOperationTimeout, MacBackoffExpired, MacAckTimeout, MacScanTimer,
MacIndirectExpired, NwkRouteDiscoveryTimeout, NwkBroadcastTimer,
ApsAckTimeout, ApsFragmentTimeout, BdbTimer, ZdoTransactionTimeout,
ZclReportingDue, PollDue, ChildAgingDue, PersistenceFlushDue,
ApplicationRequest, RadioOwnershipChanged
```

Every event carries an operation generation. A cancellation increments the
generation; stale events are counted and ignored. Do not use a bare callback
pointer as lifetime ownership.

`process()` must support a time and work budget:

```cpp
ProcessResult process(uint32_t maxEvents, uint32_t maxMicroseconds);
uint64_t nextDeadlineUs() const;
bool canSleep() const;
```

`nextDeadlineUs()` is the earliest protocol, PHY, persistence, or poll
deadline. The Arduino core may sleep only until that deadline and only when
the radio/flash owners report quiescent state.

### 6.4 Clock and timer rules

- Use a 64-bit monotonic microsecond time internally. Extend a narrower GRTC or
  Arduino clock in one platform module and test wrap transitions.
- Store absolute deadlines, compare them with signed/wrap-safe helpers, and
  never compute `deadline = millis() + timeout` independently in each layer.
- Timer identifiers contain slot plus generation. Cancelled timers cannot fire
  after slot reuse.
- Timer callbacks enqueue events; they do not recursively run protocol logic.
- Convert normative symbols to microseconds in one checked helper. Preserve
  symbol units where the standard defines randomized slot counts.
- The fake clock can advance directly to the next deadline for deterministic
  tests; no host test sleeps in real time.

### 6.5 Buffer and memory model

No heap allocation is permitted after `ZigbeeStack::begin()` in the default
embedded profile. Use compile-time or startup-selected capacities.

Define at least these pools:

| Pool | Ownership/use | Rule |
|---|---|---|
| PHY RX | EasyDMA receives | Never borrowed for TX; one spare must remain armable |
| PHY immediate TX | ACK/critical MAC response | Reserved from application traffic |
| General TX | MAC/NWK/APS application and management frames | Queue with priority and explicit backpressure |
| Reassembly | APS fragmented inbound SDUs | Per-source quotas and total byte limit |
| Indirect child data | Parent-held sleepy-child traffic | Per-child and global quotas, expiry |
| Events | ISR/timer/application signals | Bounded MPSC or interrupt-safe ring; overflow is observable |
| Transactions | MAC/NWK/APS/ZDO pending operations | Generation IDs and deadlines |

Resource profiles must fit the nRF54L15 VPR-on board limit of 155,648 bytes of
data memory. The LM20 profile may scale tables up, but it must not conceal a
functional dependency on 512 KB RAM. Suggested starting capacities are design
inputs, not normative minima:

| Resource | L15 small profile | LM20 large profile |
|---|---:|---:|
| RX DMA buffers | 4 | 8 |
| General TX buffers | 6 | 16 |
| Event slots | 32 | 96 |
| Neighbors | 16 | 64 |
| Children | 8 | 32 |
| Routes | 16 | 64 |
| Route discoveries | 4 | 16 |
| Broadcast transactions | 16 | 64 |
| APS transactions | 8 | 32 |
| APS reassembly contexts | 2 | 8 |
| Bindings/groups | 16 each | 64 each |

Phase Z1 must measure actual `sizeof()` totals and stack high-water marks, then
adjust profiles. When a table is full, return the correct layer-specific
status and increment a diagnostic counter. Never overwrite the oldest secured
peer, child, transaction, or key entry silently.

### 6.6 Status and request APIs

Replace ambiguous `bool` results at service boundaries with typed statuses:

```cpp
enum class ZigbeeStatus : uint16_t {
  Ok,
  InvalidArgument,
  InvalidState,
  NotSupported,
  FeatureDisabled,
  BufferTooSmall,
  NoBuffers,
  QueueFull,
  TableFull,
  Busy,
  Cancelled,
  Timeout,
  RadioBusyOtherProtocol,
  ChannelAccessFailure,
  NoAck,
  SecurityFailure,
  ReplayRejected,
  NoRoute,
  RouteDiscoveryFailed,
  NotAuthorized,
  StorageFailure,
  CounterExhausted,
  MalformedFrame,
};
```

Layer confirmations retain native MAC/NWK/APS/ZDO/ZCL status values as a
separate field. Do not flatten a remote `TABLE_FULL` response into local
`InvalidState`.

Requests return a transaction handle containing slot and generation. A
completion contains handle, final status, attempt counts, destination, and
optional layer metadata. Support cancellation. Define whether buffers are
copied or borrowed at every API; default to stack-owned copies for Arduino
safety.

### 6.7 Feature gating

The current `.cpp` files are conditionally compiled while public headers remain
visible, which permits a Zigbee-disabled sketch to compile and then fail at
link. Replace this with one stable generated macro and one of these coherent
policies:

1. Hide Zigbee declarations when `NRF54L15_CLEAN_ZIGBEE_ENABLED == 0`, with an
   intentional compile-time diagnostic from `Zigbee.h`; or
2. Keep declarations and ship small disabled stubs returning
   `FeatureDisabled`.

Use the same policy on every board. Add CI for enabled and disabled builds.
OpenThread currently reuses `ZigbeeRadio`; do not compile the physical backend
out merely because the Zigbee protocol stack is off. Rename the eventual
backend to an IEEE 802.15.4 PHY abstraction so Thread does not depend on a
Zigbee feature flag.

## 7. Codec And Memory-Safety Foundation

### 7.1 Checked cursors

Before adding commands, create `ByteReader` and `ByteWriter` using `size_t`:

```cpp
class ByteReader {
 public:
  size_t remaining() const;
  bool readU8(uint8_t*);
  bool readLe16(uint16_t*);
  bool readLe24(uint32_t*);
  bool readLe32(uint32_t*);
  bool readLe64(uint64_t*);
  bool readBytes(MutableByteSpan out);
  bool skip(size_t count);
  ByteSpan remainingSpan() const;
};

class ByteWriter {
 public:
  size_t size() const;
  size_t remaining() const;
  bool writeU8(uint8_t);
  bool writeLe16(uint16_t);
  bool writeLe24(uint32_t);
  bool writeLe32(uint32_t);
  bool writeLe64(uint64_t);
  bool writeBytes(ByteSpan);
};
```

All additions use `if (count > remaining()) return false;`; never compare after
an 8-bit addition. A failed parse leaves the output in its default invalid
state and does not advance a caller-owned cursor. A failed build exposes no
partial frame as sendable.

Migrate these known-dangerous paths first:

- `zigbee_security.cpp` helper `appendBytes()` currently narrows
  `*offset + length` to `uint8_t`, allowing wrap before its capacity check.
- `encryptCcmStar()` appends a four-byte MIC without a destination capacity
  argument.
- Secured NWK builders use fixed 127-byte temporary arrays while public APIs
  accept lengths whose header, payload, and MIC sum is not modeled explicitly.
- `ZigbeeCodec::parseConfigureReportingRequest()` checks for six remaining
  bytes before reading a direction-0 fixed prefix which needs eight; malformed
  six- or seven-byte records can be read past the payload.

Add a regression test for every truncation offset from zero through the valid
frame length for every parser. Run native tests with ASan and UBSan.

### 7.2 Frame-length accounting

Create a `FrameBudget` helper which subtracts, in order:

```text
127-byte PSDU
- 2-byte FCS (not present in the caller's software PSDU if hardware owns FCS)
- MAC header and auxiliary security overhead
- NWK base/optional/security overhead and MIC
- APS base/extended/security overhead and MIC
= maximum ASDU/ZCL payload for this exact route and security mode
```

Do not publish one global "maximum Zigbee payload." It varies with 16-bit or
64-bit MAC addressing, NWK extended addresses, source route relay count,
multicast control, security headers/MIC, APS extended header, and fragmentation.
Every builder takes output capacity and returns exact bytes used.

### 7.3 Parsing and authentication order

1. Validate physical length and FCS metadata.
2. Parse enough MAC header to filter and identify addressing.
3. Reject reserved bits/addressing combinations according to the selected MAC
   frame version.
4. Parse NWK header into a temporary view without exposing payload.
5. Select the key and replay context using authenticated identity fields.
6. Verify CCM* and replay policy before delivery or table mutation.
7. Parse APS and, if applicable, verify APS security.
8. Apply duplicate and fragmentation logic.
9. Deliver ZDO/ZCL/application data only after every required check succeeds.

No unauthenticated frame may refresh neighbor age, update address mappings,
change keys, allocate a child, complete a transaction, or invoke an application
callback unless the normative specification explicitly requires pre-security
processing for that command.

## 8. MAC Implementation

### 8.1 PHY service boundary

Replace blocking `ZigbeeRadio` operations with this conceptual interface:

```cpp
struct PhyTxRequest {
  PacketHandle packet;
  uint8_t channel;
  int8_t txPowerDbm;
  bool useCca;
  bool expectAck;
  uint8_t expectedDsn;
  uint64_t notBeforeUs;
  uint64_t deadlineUs;
};

Status startReceive(const PhyRxRequest&, PhyOperationHandle*);
Status startCca(const PhyCcaRequest&, PhyOperationHandle*);
Status startEd(const PhyEdRequest&, PhyOperationHandle*);
Status startTransmit(const PhyTxRequest&, PhyOperationHandle*);
Status cancel(PhyOperationHandle);
```

PHY completions distinguish `Success`, `CcaBusy`, `NoAck`, `BadCrc`,
`Filtered`, `Timeout`, `Cancelled`, `RadioFault`, and `OwnershipLost`.
Compatibility wrappers may temporarily block by repeatedly calling
`ZigbeeStack::process()`, but production runtime code must not call them.

### 8.2 MAC PIB

Implement a typed PIB containing every selected Zigbee/IEEE attribute. At a
minimum it must model PAN ID, short and extended address, coordinator address,
promiscuous mode, RxOnWhenIdle, association permit, auto-request, beacon/DSN,
transaction persistence time, response wait time, ACK wait duration, minimum
and maximum BE, maximum CSMA backoffs, maximum frame retries, and channel/page.

Each default and range is **VERIFY-NORMATIVE** against the pinned IEEE and Core
clauses. Common values such as `aUnitBackoffPeriod = 20 symbols`, `macMinBE =
3`, `macMaxBE = 5`, `macMaxCSMABackoffs = 4`, and
`macMaxFrameRetries = 3` must not be hard-coded until the ledger confirms their
applicability and mutability for the selected profile.

PIB setters validate ranges and return status. Changing PAN/address/channel
while an operation is active either queues an atomic reconfiguration or returns
`Busy`; never change the filter halfway through a receive.

### 8.3 Unslotted CSMA-CA and frame retries

Implement one state machine per active TX, serialized through the MAC queue:

```text
Queued
  -> InitializeAttempt: NB=0, BE=macMinBE
  -> Backoff: random integer in [0, 2^BE - 1] backoff periods
  -> CCA
       idle -> Transmit
       busy -> NB++, BE=min(BE+1, macMaxBE)
               -> Backoff if NB <= macMaxCSMABackoffs
               -> ChannelAccessFailure otherwise
  -> Transmit
       no ACK requested -> Success after required IFS/accounting
       ACK requested -> AckWait
  -> AckWait
       matching DSN ACK -> Success and capture Frame Pending
       timeout -> retry whole CSMA attempt if retry budget remains
               -> NoAck otherwise
```

Requirements:

- Obtain random backoff slots from the platform CSPRNG. A deterministic fake
  RNG supplies test sequences. Failure to obtain entropy fails closed; do not
  use `millis()`, address bits, or a linear counter as random input.
- Preserve the MAC DSN across retransmissions of the same MPDU. Do not consume
  new NWK/APS/security counters or re-encrypt on a link-layer retry.
- Distinguish CCA backoffs from full-frame retry attempts in diagnostics.
- ACK acceptance requires valid CRC, ACK frame type, matching DSN, and arrival
  in the normative window. A late or unrelated ACK is ordinary RX data or a
  diagnostic, not transaction success.
- Broadcast/multicast MAC destinations never request MAC ACK.
- Apply SIFS or LIFS before the next eligible transmission based on transmitted
  MPDU length, unless a verified hardware shortcut enforces it.
- Priority order must reserve immediate ACK and MAC command responses, then
  management/security, then application traffic. Prevent starvation with a
  bounded policy.

### 8.4 MAC frame codec

Complete the legacy IEEE MAC forms required by Zigbee:

- Beacon, data, ACK, and MAC command frames.
- No address, 16-bit short, and 64-bit extended source/destination modes.
- PAN ID compression combinations valid for the selected frame version.
- Security-enabled bit and auxiliary header only if the selected Zigbee profile
  actually uses MAC security; otherwise reject unsupported forms explicitly.
- Association Request/Response, Disassociation Notification, Data Request,
  PAN ID Conflict Notification, Orphan Notification, Beacon Request,
  Coordinator Realignment, and GTS Request handling as required by the selected
  PICS. Unsupported GTS behavior must return the normative status, not parse as
  application data.

Use table-driven valid-addressing tests for every source/destination mode,
PAN-compression bit, frame version, and truncation boundary.

### 8.5 Filtering, ACK, and duplicate rejection

Filtering sequence before ACK:

1. Reject bad FCS, invalid PHR length, reserved frame type/version, malformed
   address fields, and unsupported security form.
2. In normal mode accept the current PAN, permitted broadcast PAN, beacons
   needed by an active scan, and special commissioning/orphan cases only.
3. Accept unicast only for local short/extended address or applicable
   coordinator/child handling; accept broadcasts according to frame type.
4. Decide Frame Pending from the indirect queue using parsed source identity.
5. Build/send ACK if Ack Request is valid and the destination is not broadcast.
6. Apply duplicate filtering before upper-layer delivery, while still ACKing a
   valid duplicate retransmission when required.

The duplicate table key must include enough identity to avoid cross-peer DSN
collisions: source PAN/address mode/address, frame version, frame type, and DSN,
plus an aging epoch. Define behavior for sequence-number suppression only if
the selected Zigbee MAC form permits it. Promiscuous/sniffer mode disables
normal filtering and automatic ACK unless explicitly requested.

The current BCC path calls a callback after an 11-byte prefix but passes the
advertised full length, allowing a callback to inspect bytes EasyDMA has not
written. Replace it with an early-parser API whose span length is exactly the
captured prefix. If the address is not fully available, defer ACK rather than
reading stale memory.

### 8.6 Scanning and association

Implement MLME-style asynchronous services and confirms:

- Energy-detect scan returns per-channel maximum/summary ED using the
  normalized 0..255 scale.
- Active scan transmits Beacon Request using CSMA-CA and collects unique PAN
  descriptors for the duration derived from scan duration.
- Passive scan listens without transmitting.
- Orphan scan sends Orphan Notification and accepts valid Coordinator
  Realignment.
- Association handles request, response through indirect transfer where
  required, response wait timeout, assigned short address, and failure status.
- Disassociation handles coordinator/device initiation, ACK/confirm, and state
  cleanup.

PAN descriptors contain coordinator address mode/address, PAN ID, logical
channel/page, superframe fields, GTS permit, link quality, timestamp, Zigbee
beacon payload, and security metadata. Deduplicate by extended PAN ID plus PAN
ID/coordinator/channel as required; do not pick solely by strongest RSSI.

### 8.7 Indirect transmission and sleepy children

Maintain per-child queues keyed by IEEE and current short address:

- A queued entry owns an encrypted MPDU/transaction identity, enqueue time,
  expiry, priority, retry state, and completion callback.
- MAC Data Request lookup occurs in a bounded early path. The ACK Frame Pending
  bit reflects whether eligible data remains for that child.
- After the poll ACK, transmit the selected indirect frame using the normative
  timing and keep/retry/drop semantics.
- Expired entries complete with `TransactionExpired`; a full per-child/global
  queue returns `TransactionOverflow` or mapped normative status.
- Child timeout/aging is a NWK concern, while MAC transaction persistence is a
  queue-entry concern. Do not conflate them.

Test two children with the same short address at different generations, rapid
rejoin/address change, duplicate polls, empty poll, queue expiration, parent
reset, and a child that never acknowledges pending data.

### 8.8 MAC acceptance gate

The MAC phase exits only when:

1. Native codec tests cover every addressing combination and truncation.
2. Deterministic fake-PHY tests cover every CSMA busy/idle/retry/ACK event
   interleaving and random-backoff boundary.
3. Two boards pass 10,000 acknowledged unicasts with induced CCA collisions,
   forced ACK loss, duplicates, bad FCS, and queue saturation.
4. Active, passive, ED, and orphan scan confirms match a trusted sniffer.
5. A sleepy child retrieves ordered indirect frames, sees correct Frame Pending,
   and recovers after parent and child reset.
6. OpenThread raw-radio and MAC tests still pass because it currently shares
   this backend.

## 9. NWK Implementation

### 9.1 Complete NWK codec first

Replace the current rejection of secured, multicast, and source-route frames
with a two-stage header codec. The plain NWK codec parses/builds:

- Frame control and all selected protocol-version/discover-route combinations.
- Destination/source short address, radius, sequence.
- Optional destination/source IEEE addresses.
- Multicast control subframe.
- Source-route subframe: relay count, relay index, and relay list with bounds.
- Auxiliary security header as a view delegated to `security_codec`.
- Payload span and exact authenticated-header span.

Reject reserved bits, impossible optional-field combinations, source-route
relay index greater than relay count, relay lists exceeding remaining bytes,
radius underflow, unsupported protocol version, and security levels/key IDs
invalid for NWK. Test maximum relay list length against the exact frame budget.

### 9.2 Tables and identity

Use separate records with explicit validity/generation:

```text
Neighbor: IEEE, short, device type, rxOnWhenIdle, relationship, permitJoining,
          depth, LQI/cost, age/lastSeen, timeout, outgoingCost, incomingBeacon
Child:    IEEE, short, capability, timeout, lastPoll, indirect queue, auth state
Route:    destination, nextHop, status, noRouteCache, manyToOne, routeRecord,
          expiry, failure count, generation
Discovery: routeRequestId, originator, destination, sender, forwardCost,
           residualCost, expiry, pending packet list
AddressMap: IEEE <-> short, generation, source, expiry/persistence policy
Broadcast: source, NWK sequence, expiry, relay state, passive-ACK state
SourceRoute: destination, relay list, validation/expiry
```

Incoming authenticated frames may update last-seen and link observations.
Unauthenticated observations required for joining must be stored separately
from trusted neighbor state. Short-address reuse increments a generation so
old routes, replay contexts, transactions, and indirect frames cannot bind to
a new device accidentally.

### 9.3 NWK command coverage

Implement parse/build and behavior for every command required by the selected
Core/PICS, including at least:

- Route Request.
- Route Reply.
- Network Status.
- Leave.
- Route Record.
- Rejoin Request and Rejoin Response.
- Link Status.
- Network Report and Network Update.
- End Device Timeout Request and Response.

For each command, document allowed sender/receiver roles, broadcast/unicast
addressing, security requirement, radius, duplicate behavior, table mutation,
response, and status mapping. Never implement a command as a codec-only
constant and mark it complete.

### 9.4 Route discovery and repair

Implement the normative distributed route-discovery algorithm as an event
machine:

1. A unicast without an active route consults the route-discovery policy from
   the NWK frame's discover-route field and local role.
2. Allocate a route/discovery entry or return `RouteTableFull`/
   `RouteDiscoveryTableFull` through the proper confirm path.
3. Broadcast Route Request with a new request ID and bounded radius.
4. Each router rejects duplicate/worse requests according to the Core cost and
   request-cache rules, records reverse-path state, adds link cost, and relays
   after the required jitter.
5. Destination or eligible intermediate router emits Route Reply.
6. Replies travel the reverse path, installing/validating forward routes.
7. Origin releases pending packets in order after route activation.
8. Timeout retries or fails according to the verified retry budget.
9. Link/MAC delivery failure invalidates or repairs the route and emits Network
   Status when required.

All cost calculations, saturation values, retry counts, jitter windows, and
aging intervals are **VERIFY-NORMATIVE**. Use wider temporaries and saturate
before narrowing to on-air fields.

Test line, tree, diamond, and asymmetric-link topologies in simulation. A
three-board minimum is required for one physical relay; four or more boards or
a certified simulator/sniffer are required to exercise alternate paths and
repair convincingly.

### 9.5 Broadcasts, multicast, and groups

- Maintain the Broadcast Transaction Table keyed by source plus NWK sequence
  and any additional normative identity. Expire entries by timer.
- Deliver an accepted broadcast locally at most once. Relay only from router/
  coordinator roles, decrement radius exactly once, stop at zero, and apply
  randomized relay delay.
- Implement passive acknowledgement/retry behavior if required by the selected
  Core/PICS. Track which neighboring routers repeated the broadcast without
  unbounded bitsets.
- Separate MAC broadcast, NWK broadcast destinations, APS group delivery, and
  NWK multicast. They have different scope and acknowledgement rules.
- Parse/build multicast control and implement member/nonmember forwarding
  modes exactly as selected. Do not translate every group frame into a global
  broadcast.
- Rate-limit broadcast origination and relay to prevent a malformed peer from
  exhausting all TX buffers.

### 9.6 Many-to-one and source routing

For concentrator support:

1. Coordinator/concentrator emits the correct many-to-one Route Request mode.
2. Routers install the required route toward the concentrator.
3. Devices/routers send Route Record along the learned path as required.
4. Concentrator stores the relay sequence keyed by destination identity.
5. Outbound frames include a validated source-route subframe with correct relay
   ordering/index.
6. Each relay consumes exactly its entry and forwards to the next hop.
7. Stale/broken source routes trigger the normative status/repair path.

Test maximum-depth lists, looped lists, duplicate relays, missing next hop,
address change, and frame budget too small for the route.

### 9.7 Network lifecycle

Implement coordinator/router network behavior outside examples:

- Network formation: ED/active scan, channel selection policy, PAN ID and
  extended PAN ID creation, network update ID, coordinator address, security
  initialization, beacon payload, permit-join default.
- Router start after association/rejoin, child capacity, beacon response, and
  address allocation under the selected scheme.
- Address conflict detection/resolution and deterministic reuse only after the
  previous identity is retired.
- Neighbor and child aging, end-device timeout negotiation, keepalive/poll
  observations, leave/removal, and previous-child handling.
- PAN ID conflict and channel change through Network Report/Update and
  Management Network Update procedures.
- Restart restores identity/security before accepting or transmitting normal
  data. Coordinator restart must not force all children to factory-new.

### 9.8 NWK acceptance gate

1. Native header/command tests cover secure/unsecure, IEEE options, multicast,
   source route, all reserved bits, max lengths, and truncations.
2. Simulated 5- to 50-node topologies pass discovery, concurrent discoveries,
   table exhaustion, broadcast duplicate suppression, alternate-path repair,
   many-to-one, address change, and deterministic reset.
3. At least three physical boards exchange application traffic through a
   router; removing/attenuating the active link causes repair without reboot.
4. A trusted sniffer confirms radius, sequence, command fields, costs, relay
   ordering, security, and no duplicate application delivery.
5. Coordinator/router soak for seven days with churn shows bounded tables and
   no route/broadcast/event leaks.

## 10. APS Implementation

### 10.1 APS codec

Complete data, command, and acknowledgement frames for:

- Unicast, indirect where applicable, broadcast, and group delivery modes.
- Destination/source endpoint, cluster, profile, APS counter.
- APS ACK format and acknowledged transaction identity.
- Extended header, fragmentation control fields, and selected optional fields.
- APS auxiliary security header and authenticated portion.

Validate reserved delivery modes, endpoint zero semantics, profile/cluster
presence by mode, security/extended-header combinations, fragment number/window
bounds, and every truncation. Do not keep the current design where secured APS
data is rejected while secured command helpers bypass the main APS parser.

### 10.2 APS transaction manager

Each reliable outbound request records:

```text
handle/generation, source endpoint, destination identity/endpoint, profile,
cluster, APS counter, payload or immutable packet reference, security context,
attempt count, timeout mode, deadline, route generation, completion callback
```

Behavior:

- Assign APS counter once per APS transaction; keep it across APS retries.
- Rebuild lower-layer headers and use a fresh NWK security frame counter for a
  new NWK transmission, while MAC retries retain the exact MPDU/counter.
- Match ACK on all normative identity fields, not APS counter alone.
- Use extended timeout for sleepy destinations when selected.
- Backpressure when transaction slots or TX buffers are full.
- Cancel on leave/address-generation change and report a deterministic status.
- An APS duplicate table prevents repeat application delivery but still emits
  the ACK required to terminate the peer's retry.

### 10.3 Binding, groups, and fan-out

- A binding key includes local source IEEE/endpoint/cluster. Destination is a
  group or extended IEEE plus endpoint as permitted.
- Binding-based send resolves all matching records into a bounded delivery
  plan. Define whether partial fan-out reports individual confirms or one
  aggregate result; never stop silently after the first destination.
- Resolve IEEE-to-short through the address map/ZDO discovery and hold a
  generation reference. Unknown destinations trigger discovery or an explicit
  status.
- Group membership is scoped by endpoint. APS group delivery reaches every
  local matching endpoint once and uses the correct NWK delivery behavior.
- Persist binding/group changes atomically and implement normative table-full,
  duplicate, not-found, and invalid-endpoint statuses.

### 10.4 Fragmentation and reassembly

If APS fragmentation is selected by the PICS, implement it fully rather than
only splitting payloads:

- Compute fragment payload from the exact per-frame budget after route and
  security overhead.
- Record transaction/block number, total fragments if encoded, window,
  acknowledged bitmap/state, retry count, and extended timeout.
- Permit only bounded concurrent reassemblies, with per-source quotas.
- Accept out-of-order fragments only as specified, reject conflicting duplicate
  content, ACK duplicate valid fragments as required, and never append beyond
  announced/maximum ASDU size.
- Authenticate each secured fragment before storing it. Deliver only a
  complete, validated ASDU.
- Expire and zeroize abandoned contexts. An attacker must not reserve every
  reassembly buffer indefinitely.
- Test one through maximum fragment count, missing/duplicate/out-of-order
  blocks, wrong sender/counter, MIC failure, route change, reset, timeout, and
  allocation exhaustion.

If fragmentation is not selected for the first PICS, keep the codec/runtime
behind a disabled feature and return `NotSupported`; do not advertise arbitrary
large application payloads.

### 10.5 APS acceptance gate

1. Codec corpus covers every delivery/security/extended-header form and every
   truncation.
2. Deterministic tests cover ACK before/at/after deadline, duplicate ACK,
   duplicate data, retry exhaustion, cancellation, address reuse, and table
   exhaustion.
3. Binding/group fan-out passes multiple local endpoints and multiple remote
   targets with individual result reporting.
4. Fragmentation passes maximum-size ASDU, reorder/loss/tamper/flood tests and
   cross-vendor interoperability if claimed.
5. Seven-day sleepy-device traffic does not leak transactions or reassembly
   contexts.

## 11. Security Implementation

### 11.1 Threat model and invariants

The implementation must withstand malformed unauthenticated radio input,
replay, key/counter rollback after power loss, table exhaustion, unauthorized
joining, stale address mappings, and accidental secret disclosure through logs
or crash dumps. Physical extraction and invasive silicon attacks are outside
the first software milestone unless a product threat model adds them.

Non-negotiable invariants:

1. A nonce is never reused with the same key, source identity, security control,
   and layer, including after reset or brownout.
2. Incoming replay state is per sender identity, key sequence/type, and layer.
3. A frame is not delivered or allowed to mutate trusted state before MIC and
   replay validation succeeds.
4. Key selection follows the selected Core/BDB state machine. Code never
   "tries every key" on arbitrary data as a normal policy.
5. Keys, expanded round keys, shared secrets, and plaintext temporary buffers
   are zeroized on release with a compiler-resistant primitive.
6. MIC comparison is constant-time.
7. No key, install code, nonce material, or plaintext security command is
   printed by default or exposed through the public diagnostics snapshot.
8. Counter exhaustion fails closed and initiates the normative key/rejoin
   procedure; a 32-bit counter never wraps to zero.

### 11.2 Crypto provider

Define an interface which supports at least AES-128 block encryption, AES-
CCM* modes used by the pinned Zigbee revision, hash/MMO operations required for
install-code derivation, CSPRNG bytes, constant-time compare, and secure erase.

Use CRACEN only through the core's reviewed hardware-crypto ownership layer.
Required checks before making it default:

- Confirm each algorithm/mode needed by Zigbee, including authentication-only
  CCM* variants if selected.
- Confirm DMA memory and security-domain restrictions on L15 and LM20.
- Serialize access with BLE, Thread, Matter, and other CRACEN users.
- Prove behavior under timeout, cancellation, fault, and System OFF.
- Run the same known-answer corpus against hardware and software providers.

The current table-based software AES is migration input, not an assumed
constant-time production provider. A software fallback may remain for testing
or unsupported hardware, but its threat limitations must be documented.

### 11.3 Key store

Model keys as typed records rather than byte arrays embedded in general state:

```text
NetworkKey: key bytes/handle, sequence number, active/alternate/pending state,
            outgoing reservation, install/activation epoch
LinkKey: peer IEEE, key type, key bytes/handle, verified/authenticated state,
         incoming/outgoing APS counters, last-used, policy flags
InstallCode: peer IEEE, code length, CRC validation, derived-key state,
             provisioning provenance; raw code retained only if required
TrustCenterKey: Trust Center IEEE, initial/preconfigured/unique key type,
                authorization state and counters
TransientKey: peer IEEE, key, expiry, commissioning transaction
```

Key bytes should be replaceable by a secure hardware key handle later. The
protocol code may request an operation by `KeyId`; it must not assume direct
read access to the key.

Implement and verify every key identifier, security level, auxiliary header,
extended-nonce rule, source identity rule, and MIC length selected by the Core
PICS. Reject reserved security-control bits and invalid layer/key combinations
before decrypting. The existing broad acceptance of encryption-plus-MIC32 and
network-key level encoding must become layer-specific policy.

### 11.4 Incoming replay table

Replay identity is at least:

```text
(source IEEE address, layer NWK/APS, key identifier/type, key sequence/epoch)
```

Requirements:

- Resolve source IEEE from the authenticated auxiliary header or a validated
  address map as defined by the exact frame form. Never key replay solely by
  current short address.
- Check `receivedCounter > storedCounter` using non-wrapping 32-bit semantics.
- Commit the new inbound counter only after MIC succeeds and the frame has
  passed structural checks.
- A duplicate lower-layer retransmission must be handled according to the
  normative MAC/NWK duplicate sequence before replay state incorrectly turns a
  valid retry into an attack or vice versa. Pin the order in tests.
- Keep replay state across reset for peers/keys where the Core requires it.
- Table-full behavior is fail closed for secured data from an untracked peer;
  never evict an active authenticated peer merely because a new unknown source
  sent a frame.
- When a key sequence changes, preserve old/new acceptance windows exactly as
  the key-switch procedure defines. Do not reset all peers to counter zero.

### 11.5 Outgoing frame-counter reservation

Persisting the last-used counter periodically is unsafe. Use high-water
reservation:

1. On first use of `(local identity, layer, key epoch)`, read committed
   `reservedExclusiveEnd`.
2. Atomically commit a new end equal to old end plus a configured reservation
   window, after checking overflow.
3. Only after verifying that commit may RAM allocate counters from
   `[oldEnd, newEnd)`.
4. After reset, begin at the persisted exclusive end, intentionally skipping
   unused values from the previous boot.
5. Before the RAM allocator reaches its end, reserve the next window. If the
   commit fails, stop secured transmission before consuming the last available
   value.
6. Key switch creates a new counter namespace only where the selected Core
   explicitly permits it.

A starting reservation window of 1,024 may balance flash wear and counter
loss, but it is an implementation parameter, not a Zigbee constant. Measure
flash endurance and expected packet rate. Test power loss before, during, and
after every persistence write, then verify the next transmitted counter is
strictly above every counter previously observed on air.

### 11.6 Secured-frame processing

NWK secure build:

1. Build the immutable plain NWK header and optional fields into a checked
   writer.
2. Select active network key/sequence and reserve one outgoing NWK counter.
3. Build the exact auxiliary header and nonce from verified source identity,
   counter, and security control.
4. Calculate authenticated-data and encrypted-payload spans without copying
   past capacity.
5. Run CCM*, append exact MIC, then mark the buffer sendable.

NWK secure parse follows the reverse order and returns a view only after MIC
and replay acceptance. APS data/command security uses its own key selection,
nonce, counters, and replay context; do not route APS command security around
the main parser as the current helpers do.

Add official known-answer vectors for:

- AES-128 block primitive.
- Each selected CCM* security level/MIC size with empty, one-byte, block-
  boundary, and maximum payload.
- Install-code CRC and each permitted install-code length.
- Install-code key derivation/MMO.
- NWK and APS secured frame examples with exact nonce/AAD/ciphertext/MIC.
- Tampered header, ciphertext, MIC, source, counter, key ID, key sequence,
  truncated MIC, and maximum-length overflow.

### 11.7 Trust Center and device authorization

Move coordinator security out of the 5,000-line example into `TrustCenter`:

- Persist each joining device's IEEE address, current short address generation,
  authorization state, capability, parent, key policy, install-code/link-key
  state, replay state, and last transition.
- Implement permit-join as a timed policy, not a global sketch Boolean.
- Validate association/rejoin admission against capacity and policy before
  allocating a durable device identity.
- Implement the full selected Transport Key, Update Device, Remove Device,
  Request Key, Switch Key, Verify Key, Confirm Key, and related APS command
  lifecycle required by R22.1/PICS. The exact list/status/security of each
  command comes from the normative ledger.
- Generate network keys with the CSPRNG. Delete the current example's
  deterministic XOR-derived alternate key behavior.
- Rotate network keys through distribute/stage/switch/retire states. Preserve
  old material only for the normative transition window and zeroize it later.
- A coordinator reset restores device authorization, address allocation, keys,
  key sequence, counter reservations, and permit-join expiry before serving
  normal traffic.
- Implement install-code-only and policy-controlled well-known-key joining.
  Production mode must be able to reject the global default key.
- Implement centralized and distributed security only if both are selected in
  the PICS; keep their policies and state machines separate.

### 11.8 R23.2 security uplift

Do not add R23.2 security piecemeal to the R22.1 baseline. In the later uplift:

- Diff every R22.1 security PICS item against R23.2 and BDB 3.1.
- Implement required new key types, dynamic link-key/ECC procedures, Trust
  Center policy, and coordinator obligations from the pinned documents.
- Reuse a tested P-256/cryptographic provider; never implement elliptic-curve
  arithmetic ad hoc in protocol code.
- Treat Zigbee Direct commissioning/key obligations as separate from the BLE
  transport feature. Confirm which obligations apply to coordinator capability
  even when the Direct transport is disabled.
- Add downgrade/mixed-revision tests with R22.1 devices and explicit policy for
  unsupported new procedures.

### 11.9 Security acceptance gate

1. All official and independently generated crypto vectors pass on software
   and CRACEN providers.
2. ASan/UBSan/fuzz tests cover every secured parser and builder length.
3. Per-peer replay tests include two peers using identical counter values,
   address changes, key changes, duplicates, reset, and table exhaustion.
4. Brownout at every journal/counter write boundary never reuses an observed
   outgoing nonce.
5. Install-code-only joining, well-known-key policy, secure/unsecure rejoin,
   leave/remove, key update/switch, and coordinator restart pass against at
   least two independent ecosystems.
6. Logs and release binaries contain no hard-coded production key or accidental
   secret dump. Test credentials are visibly marked and compiled only in
   explicit test examples.

## 12. Persistence Implementation

### 12.1 Replace raw struct storage

The current version-6 state persists compiler padding and deletes the old data
before the replacement is complete. Replace it with explicit little-endian
serialization and an A/B journal.

Conceptual slot header:

```text
magic[4]
schemaVersion u16
recordType u16
generation u64
payloadLength u32
payloadCrc32 u32
headerCrc32 u32
flags u32
payload[payloadLength]
commitMarker u32  // programmed/written last
```

Do not copy this layout blindly into flash. Align it to the actual Preferences/
NVM erase and atomic-write guarantees, then record the final byte layout in
`persistence/schema.h` with static assertions and golden byte fixtures.

### 12.2 Commit algorithm

1. Read and validate both slots independently: magic, supported schema,
   lengths, header CRC, payload CRC, record-level invariants, and commit marker.
2. Choose the newest valid generation using a documented wrap policy. If both
   are invalid, return `NoValidState`; do not fabricate joined state.
3. Encode the complete new payload into bounded scratch or streamed records.
4. Erase/write only the inactive slot.
5. Write header and payload with the slot marked uncommitted.
6. Read back and verify all bytes/CRCs.
7. Write the commit marker last using the smallest atomic operation supported.
8. Re-read the marker/header. Only then expose the new generation as committed.
9. Leave the old valid slot intact until a later successful update needs it.

If Preferences cannot provide these semantics, add a dedicated reserved NVM
region or a lower storage abstraction; do not emulate atomicity with key names
whose backend transaction behavior is unknown.

### 12.3 Record separation

Separate frequently written critical records from large, rebuildable tables:

| Record | Criticality/policy |
|---|---|
| Network identity and BDB state | Atomic on change; required before normal operation |
| Active/alternate keys and key sequence | Atomic, secret handling, commit before activation |
| Outgoing counter reservations | Independent compact journal; synchronous before use |
| Incoming replay high-water state | Persist per Core requirements; batch only if rollback cannot admit replay |
| Trust Center device/address/auth table | Atomic device transition; coordinator restart critical |
| Bindings/groups/scenes/reporting | Debounced journal with explicit dirty records |
| Child table/timeouts | Persist selected durable identity/auth state; rebuild transient link observations |
| Neighbors/routes/broadcast duplicates | Prefer rebuild after restart unless the specification requires persistence |
| OTA progress/image metadata | Atomic at verified block/checkpoint boundaries |
| Diagnostics | Noncritical ring or RAM only; never block protocol deadlines |

Never persist pointers, virtual objects, padding, `bool` representation, or
architecture-native enum widths. Validate every decoded enum, count, channel,
PAN/address, key type, endpoint, cluster, and table relationship.

### 12.4 Migration from versions 1 through 6

Keep the existing raw-state decoders in a one-release migration module:

1. Detect legacy size/magic/version without interpreting it as the new schema.
2. Decode each field into an initialized new model. Do not `memcpy` nested
   structs whose layout changed.
3. Mark legacy global inbound counters as insufficient for multi-peer replay.
   Permit a safe forced rejoin/key refresh or create only the one known-peer
   context when identity is unambiguous.
4. Reserve new outgoing counter ceilings above legacy saved counters before
   transmitting.
5. Commit the new A/B state and verify it before removing legacy keys.
6. Record migration completion. A reset at every migration step must load
   either valid legacy or valid new state, never a half-converted mixture.
7. Remove legacy writers immediately; remove readers only after the documented
   support window.

### 12.5 Fault-injection suite

Use a fake byte-addressable or key/value backend which can fail, tear, reorder,
or reset at every write/erase byte boundary. Required cases:

- Fresh device, one valid slot, both valid with different generations, one bad
  CRC, bad length, unsupported version, committed header with torn payload.
- Power failure before erase, during erase, every header/payload byte, readback,
  and commit-marker write.
- Generation near wrap, full storage, wear/error, retry, and concurrent dirty
  record requests.
- Legacy v1-v6 migration interrupted at every storage operation.
- Counter reservation commit interrupted at every operation, correlated with
  a captured list of counters already transmitted on air.

### 12.6 Persistence acceptance gate

No fault-injection point may lose both previously committed state and the new
state. Every corrupted input must fail boundedly. Counter tests must prove
strict nonce monotonicity. Hardware tests must repeat abrupt power removal
during join, key rotation, report configuration, group/scene changes, and OTA
checkpointing.

## 13. BDB And Commissioning

### 13.1 Replace blocking orchestration

The current `activeScan()`, `performJoin()`, and `performSecureRejoin()` methods
block in `millis()`/`micros()` loops, use fixed waits, and contain protocol
`Serial.print()` calls. Replace them with `BdbEngine`, which consumes confirms
and timers and emits structured signals.

Public concept:

```cpp
Status startCommissioning(BdbMode modes, CommissioningHandle* out);
Status cancelCommissioning(CommissioningHandle);
BdbState state() const;
void setSignalCallback(BdbSignalCallback, void* context);
```

Signals include start, scan result, network selected, association result,
authentication progress, joined, formed, steering complete, finding/binding
complete, rejoin start/result, leave, factory-new, retry scheduled, and final
failure with native status. Logging subscribes to signals outside the protocol
engine.

### 13.2 Explicit state hierarchy

Model at least:

```text
Uninitialized
FactoryNew.Idle
FactoryNew.Forming.ScanEd
FactoryNew.Forming.ScanActive
FactoryNew.Forming.CommitNetwork
FactoryNew.Steering.ScanPrimary
FactoryNew.Steering.ScanSecondary
FactoryNew.Steering.Associate
FactoryNew.Steering.WaitTransportKey
OnNetwork.Authenticated
OnNetwork.FindingBinding
OnNetwork.Rejoin.ScanKnownNetwork
OnNetwork.Rejoin.Secure
OnNetwork.Rejoin.UnsecureIfPolicyAllows
OnNetwork.OrphanRecovery
Leaving
FactoryResetting
Failed
```

Coordinator, router, and end-device policies share the engine but enable only
valid states. Every entry/exit action, accepted event, timer, retry budget,
persistent transition, and cancellation behavior must be in a transition
table tested for unexpected events.

### 13.3 Commissioning policy

Move primary/secondary channel masks, scan durations, retry/backoff, permit-
join policy, install-code policy, Trust Center link-key policy, formation
selection, rejoin policy, and finding/binding modes into validated config.
The current 120 ms scan window, 2 s retry, 4 s association wait, and 12 s key
waits are not normative merely because they exist. Source or replace each in
the Phase Z0 ledger.

Network selection considers extended PAN ID, PAN ID, channel mask, stack/
protocol version, router/end-device capacity, permit joining, update ID, link
quality, security compatibility, known-network identity, and policy. RSSI alone
must not select a parent.

### 13.4 Formation, steering, and finding/binding

- Formation performs ED and active scans, rejects conflicting PAN/extended PAN
  identities, chooses values with CSPRNG, initializes Trust Center/network key
  and counter journals, persists before beacons, then starts coordinator role.
- Steering on a formed coordinator/router opens permit join for a bounded
  duration and signals expiry. Steering on a joining device scans primary then
  secondary masks according to BDB, associates, authenticates, announces, and
  closes transient state.
- Finding and binding implements initiator/target identification, descriptor
  discovery, cluster matching, bind requests, timeouts, duplicate suppression,
  and user-visible completion status. It is not satisfied by a local binding
  table helper.
- Touchlink remains a separate optional mode and build feature. Never route an
  unsupported Touchlink frame into normal inter-PAN or ZCL handling.

### 13.5 Rejoin, leave, and factory reset

- Rejoin selects secure or unsecure procedure from current security state and
  policy. A failed orphan scan, NWK rejoin, or association fallback has an
  explicit result and retry schedule.
- Trust Center verification and key update commands bind to the current rejoin
  transaction/peer generation; stale commands cannot authenticate a later
  join.
- Leave differentiates self-leave, remove-child, remove-children, and rejoin
  intent. Stop application delivery, drain/cancel transactions, persist the
  transition, clear keys/state according to policy, and signal completion.
- Factory reset is idempotent and power-loss safe. It clears network, keys,
  counters, Trust Center device state, bindings/groups/scenes/reporting, OTA
  progress, and transient commissioning records, while retaining only identity
  or manufacturing data explicitly designated immutable.

### 13.6 BDB acceptance gate

Run transition-table tests for every state/event pair, timer boundary, cancel,
reset, and storage failure. Hardware tests must cover formation, primary/
secondary steering, install-code join, denied join, full tables, secure and
policy-allowed unsecure rejoin, parent loss, Trust Center restart, leave with/
without rejoin, and reset during each phase against mixed-vendor devices.

## 14. ZDO Implementation

### 14.1 Endpoint and descriptor registry

- Reserve endpoint 0 for ZDO.
- Register simple descriptors for endpoints 1..240 as permitted by the selected
  specifications; reject duplicate/invalid endpoints.
- Store profile ID, device ID/version, input/output clusters, and cluster-role
  metadata in immutable startup descriptors.
- Build node, power, simple, active endpoint, and match descriptors from the
  actual role/configuration rather than canned global arrays.
- Capacities are configuration, not hidden eight-item limits. Responses page
  correctly and return exact total-entry counts even when only a subset fits.

### 14.2 ZDO transaction manager

An outbound transaction records TSN, destination IEEE/short generation,
request cluster, expected response cluster(s), request payload/handle, deadline,
retry policy, and callback. Support concurrent requests without TSN collision.

- Allocate TSN only when a slot is available.
- Match source identity, response cluster, TSN, and current address generation.
- Cancel/retarget according to route/address changes.
- Expire with a typed result and release all buffers.
- Page management/discovery requests until the reported total is collected or
  the caller's bounded capacity is reached.
- Do not retry a request whose side effects are not idempotent unless the
  normative procedure defines safe duplicate handling.

### 14.3 Required services

Complete both request/response codec and server/client behavior selected by
the PICS, including:

- Network Address and IEEE Address, including associated-device paging.
- Node, Power, Simple, Active Endpoint, Match, Extended Simple, and Extended
  Active Endpoint descriptors.
- Device Announce and Parent Announce where applicable.
- Bind, Unbind, and real End Device Bind procedure. The current unconditional
  `NOT_SUPPORTED` response is not completion.
- Management LQI, Routing, Binding, Leave, Permit Joining, Network Update, and
  any selected cache/discovery/server services.
- System Server Discovery and other role services required by the chosen
  server mask/PICS.

For every service define authorization, permitted destination, request length,
paging fields, response status, table mutation, duplicate behavior, and whether
APS ACK is requested. `Mgmt_Permit_Joining` must call the BDB/Trust Center
policy rather than remain example-owned.

### 14.4 ZDO acceptance gate

Test every request and response at zero, minimum, maximum, and every truncated
length; all status paths; paging with 0/1/exact-page/page+1/max entries;
concurrent TSN wrap; duplicate responses; wrong source; timeout; route change;
and table full. Compare management tables and descriptors with Zigbee2MQTT,
ZHA, and a second independent Zigbee stack.

## 15. ZCL Foundation

### 15.1 Endpoint-scoped cluster registry

The current `handleZclRequest()` has no endpoint argument and effectively uses
one global canned state. Replace it with lookup by:

```text
(endpoint, cluster ID, server/client role, manufacturer code or standard scope)
```

Each cluster instance owns or references its attribute store and command
handler. The same cluster may exist on several endpoints with independent
values/reporting. Direction is checked against role, and responses invert the
incoming direction correctly. Manufacturer-specific requests match the exact
manufacturer code; they must not access standard attributes by accident.

### 15.2 Complete ZCL data-type registry

Populate exact type IDs and rules from ZCL R8 for:

- No-data and unknown types.
- General data 8/16/24/32/40/48/56/64.
- Boolean.
- Bitmap 8/16/24/32/40/48/56/64.
- Unsigned and signed integer 8/16/24/32/40/48/56/64.
- Enumeration 8/16.
- Semi-, single-, and double-precision floating point.
- Short/long octet strings and short/long character strings, including invalid
  length sentinels.
- Array, structure, set, and bag, with bounded recursive decoding.
- Time of day, date, UTC time.
- Cluster ID, attribute ID, BACnet OID, IEEE address, and 128-bit security key.

The registry defines fixed/variable width, analog/discrete classification,
invalid value, comparability/reportable-change behavior, and encode/decode/
skip functions. Bitmaps are discrete; fix the current classification which
adds reportable-change fields to bitmap reporting records.

Bound collection depth, element count, and total encoded bytes. A discover or
skip operation must handle an unknown-but-well-formed type according to the
normative status without reading past the frame.

### 15.3 Attribute store

Attribute metadata includes ID, type, access flags, manufacturer scope,
storage pointer/provider, minimum/maximum/enum validator, persistence policy,
reportability, scene support, and change callback. Global FeatureMap and
ClusterRevision come from each cluster's selected revision.

- Reads obtain a consistent snapshot and enforce access.
- Writes decode into temporary typed values, validate all records, then apply
  atomic/partial semantics appropriate to Write, Write Undivided, and Write No
  Response.
- Failed writes do not leave callbacks or persisted state half-applied.
- Application changes use the same validator/change/reporting path as remote
  writes.
- Read-only, unsupported, invalid-value, insufficient-space, and authorization
  statuses remain distinguishable.

### 15.4 Foundation commands

Implement and test all selected global commands in both directions:

- Read Attributes / Response.
- Write Attributes, Write Attributes Undivided, Write Attributes No Response,
  and Response.
- Configure Reporting / Response.
- Read Reporting Configuration / Response.
- Report Attributes.
- Default Response with exact suppression rules.
- Discover Attributes / Response.
- Discover Attributes Extended / Response.
- Discover Commands Received/Generated and responses.
- Any structured read/write commands selected by the pinned ZCL revision/PICS.

Derive response direction from the request; remove the current hard-coded
`true` response-direction calls. Honor Disable Default Response, broadcast/
group response rules, manufacturer code, cluster direction, malformed record
status, and APS delivery result.

### 15.5 Reporting engine

Reporting entries are keyed by endpoint, cluster, manufacturer scope,
attribute, direction, and destination/binding context as the specification
requires. Record minimum interval, maximum interval, reportable change or
timeout, last reported typed value, last report time, pending state, and
configuration persistence.

- Discrete and analog types use different change rules.
- Minimum interval gates change-triggered reports.
- Maximum interval schedules periodic reports, including special disabled/
  no-periodic values.
- Changes during a pending send coalesce without losing the final value.
- Delivery goes through APS transactions and retries; commit last-reported
  value/time only at the specified success point.
- Sleepy devices align reports with wake/poll policy without violating maximum
  interval rules selected by the PICS.
- Configure/read records with direction 0 and 1 receive exact field-length
  parsing. The six/seven-byte Configure Reporting regression must remain in CI.

### 15.6 ZCL acceptance gate

1. Golden and round-trip tests cover every data type, invalid sentinel,
   boundary value, collection limit, and truncation.
2. Foundation command corpus covers multi-record success/failure, Write
   Undivided rollback, manufacturer scope, direction, default-response rules,
   and response buffer exhaustion.
3. Multiple endpoints hosting the same cluster prove independent state and
   reports.
4. Reporting tests cover every min/max boundary, analog/discrete delta,
   disabled modes, value changes during TX/retry, persistence, and reboot.
5. ZUTH and mixed-vendor reads/writes/discovery/reporting pass for the selected
   PICS before the foundation is marked complete.

## 16. Cluster And Device-Type Completion

### 16.1 Implementation pattern

Each cluster module must provide:

1. Server/client role and revision/feature declaration.
2. Complete selected mandatory attributes with type/access/default/range.
3. Complete selected mandatory commands in each direction.
4. State machine for transitions or timed behavior.
5. Scene serialization hooks where applicable.
6. Reporting metadata.
7. Persistent/nonpersistent attribute classification.
8. Positive, malformed, unsupported, timing, reset, and interoperability tests.
9. A PICS trace. Constants alone do not count as implementation.

Prefer declarative tables or generated metadata reviewed against the pinned ZCL
and device PICS. Handwritten handlers implement behavior but do not duplicate
attribute IDs/types in several switch statements.

### 16.2 Complete the currently advertised clusters first

- **Basic:** all selected mandatory identity/version/power-source attributes,
  reset-to-factory-default semantics, string length/character handling, global
  attributes, and immutable manufacturing fields.
- **Power Configuration:** correct battery/mains attributes, units, unknown/
  invalid values, alarm masks/thresholds and reporting selected by device PICS.
- **Identify:** IdentifyTime countdown, Identify Query/Response, Trigger Effect
  timing/cancellation, endpoint-scoped application callback.
- **Groups:** endpoint group table, Add/View/Get/Remove/RemoveAll/AddIfIdentifying,
  group-name optionality, scenes interaction, status and capacity.
- **Scenes:** scene table keyed endpoint/group/scene, transition time, name,
  extension field sets for every scene-capable cluster, Store/Recall timing,
  invalid extension skipping, group removal interaction, persistence.
- **On/Off:** full selected attributes and Off/On/Toggle plus timed/startup/
  effect commands/features required by selected revision/PICS.
- **On/Off Switch Configuration:** selected attributes and device behavior.
- **Level Control:** implement transition time, Move rate, Step, Stop, with-
  OnOff variants, remaining time, option mask/override, bounds, startup/on-level
  interactions. The current immediate jumps and no-op Stop are insufficient.
- **Color Control:** complete selected hue/saturation, XY, color temperature,
  enhanced hue, loop, move/step/stop, options, capabilities, physical minima/
  maxima, remaining time, and scene state. The current four immediate move-to
  commands are insufficient.
- **Temperature Measurement / Relative Humidity:** measured/min/max/tolerance,
  invalid values, range validation, reporting, sensor error behavior.

### 16.3 Next platform clusters

Implement in this order after the above pass:

1. Poll Control, because production sleepy-device behavior depends on it.
2. Time, for devices/clusters which depend on synchronized time.
3. OTA Upgrade, integrated with Section 17.
4. Diagnostics, exposing bounded stack health without secret material.
5. Illuminance Measurement and Pressure Measurement.
6. Occupancy Sensing.
7. IAS Zone and IAS Warning Device.
8. Electrical Measurement and Metering.
9. Thermostat and Fan Control.
10. Door Lock.
11. Window Covering and other device-driven clusters selected by actual PICS.

Do not claim "all ZCL clusters." ZCL contains domain-specific clusters beyond
this core's product scope. Claim exact clusters, roles, revisions, features,
and device types in the README matrix.

### 16.4 Device type definitions

Create reviewed device declarations for each shipped example. A declaration
generates or registers endpoints, profile/device ID/version, server/client
clusters, mandatory attributes, feature bits, and application callbacks.
Validate declarations at compile/startup against device PICS rules.

Examples should become roughly 100-300 lines of board I/O and application
logic. No example may contain private NWK security, APS ACK, commissioning,
routing, persistence, or child-queue implementation.

### 16.5 Cluster/device acceptance gate

For each claimed device, run its exact PICS test subset, interview in
Zigbee2MQTT and ZHA without relying exclusively on a local converter, exercise
all commands/attributes/reports, reset during transitions, fill tables, and
verify scenes/groups across restart. Keep the public feature matrix at
`Experimental` until that evidence exists.

## 17. OTA Upgrade

### 17.1 Separate cluster protocol from boot installation

The OTA cluster transfers and verifies an image; the bootloader authenticates,
installs, boots, and rolls it back. Both are required for a production OTA
claim.

Implement:

- OTA image header and subelement parser with checked lengths, manufacturer,
  image type, file version, stack version, optional hardware version range, and
  total size.
- Client discovery, Query Next Image, Image Block/Page Request, Image Notify,
  Upgrade End, randomized query timing, server wait/delay responses, abort, and
  resume.
- Server image catalog, per-client session/rate limit, block/page response,
  authorization and status.
- Persistent client checkpoint containing image identity, verified offset/
  bitmap, running hash, server identity, and generation.
- Cryptographic image authenticity using the core's secure update policy. A
  Zigbee transfer MIC/network security is not firmware signature validation.
- Final complete-image hash/signature verification before boot metadata is
  committed.
- Bootloader A/B or equivalent fail-safe handoff, confirmation deadline, and
  rollback after failed boot.

### 17.2 Board storage plan

- Map actual internal NVM, running image, bootloader, Preferences, counter
  journal, and OTA staging regions for each L15 board.
- Use external flash on LM20A boards only after validating the board schematic,
  JEDEC device, power control, erase geometry, and bootloader access.
- If a board cannot stage the maximum supported image safely, declare OTA
  unsupported for that board/profile or implement a proven swap/scratch scheme.
  Never overwrite the running image in place without rollback.

### 17.3 OTA tests

Test every block size and boundary, lost/duplicate/out-of-order responses,
server change, wrong manufacturer/type/version/hardware range, truncated and
oversized image, bad hash/signature, reset/power loss at every checkpoint and
boot phase, full flash, downgrade policy, confirmed boot, and rollback.

## 18. Optional Feature Tracks

### 18.1 Green Power

First use the selected certification PICS to decide whether Proxy Basic is
required for a routing-capable claim. If selected, implement it as an isolated
module with its own tables, security, duplicate filtering, commissioning,
translation, timeout, persistence, and conformance suite. Proxy, sink, and
source are distinct roles. Do not mark Green Power complete because a proxy
frame codec exists.

### 18.2 Zigbee Direct

Zigbee Direct combines BLE transport/commissioning with Zigbee security and a
shared radio. Defer until:

1. R23.2/BDB 3.1 baseline behavior is stable.
2. The BLE stack passes its own connection/security regression matrix.
3. `RadioArbiter` can schedule BLE and Zigbee deadlines without missed Zigbee
   ACKs, dropped BLE connections, or shared buffer/key ownership.
4. Direct 1.1 PICS and security procedures are in the normative ledger.

BLE advertisements or GATT characteristics alone do not constitute Zigbee
Direct.

### 18.3 Touchlink, NCP/RCP, and other modes

Touchlink/inter-PAN commissioning, an NCP serial protocol, an RCP interface,
Smart Energy, and manufacturer-specific ecosystems are independent products.
Give each a feature flag, architecture note, threat model, PICS, tests, and
README row. None blocks the first coherent Zigbee 3.0 platform unless selected
by its PICS.

## 19. Diagnostics And Power Behavior

Expose structured counters and snapshots outside timing-critical paths:

```text
PHY: RX valid/bad CRC/filter/overflow, TX, CCA idle/busy, ED, radio faults,
     late/stale IRQ, ownership conflict
MAC: backoffs, attempts, retries, ACK/no ACK/late ACK, duplicates, indirect
NWK: route requests/replies/repairs/failures, broadcasts/duplicates, table full
APS: transactions/retries/duplicates/fragments/timeouts/table full
Security: MIC/replay/key-policy/counter-reservation failures (no secret bytes)
BDB/ZDO/ZCL: transitions, timeouts, malformed/status counts, reporting backlog
Storage: commits, fallback slot, CRC failure, migration, fault/retry
```

Use an optional observer or pull snapshot. Protocol source must not call
`Serial.print()` unconditionally. Trace records use numeric event IDs, bounded
payloads, timestamps, and rate limiting; release builds may compile detailed
tracing out.

Power requirements:

- Sleepy end device keeps only the clocks/state required for its next poll,
  BDB timer, reporting maximum, or application wake.
- Parent poll cadence, fast-poll windows, indirect pending data, and Poll
  Control interaction are one policy.
- Router/coordinator receiver-on roles are not advertised with sleepy current.
- Flash commits and HFXO/RF switch ownership appear in power traces so a
  persistence loop cannot hide as radio current.
- Add measured active RX, TX, idle-on-network, sleepy interval, poll exchange,
  join, rejoin, and OTA energy cases for both board families. Power regressions
  are CI artifacts or release qualification records, not guessed README values.

## 20. File-By-File Migration Plan

### 20.1 Radio and HAL

| Existing file/symbol | Migration | Removal condition |
|---|---|---|
| `src/nrf54l15_hal.h:1657-1870` Zigbee public frame/radio types | Introduce protocol-neutral `Ieee802154Phy` public/internal interface; keep `ZigbeeRadio` adapter | All Zigbee examples and OpenThread PAL use new async backend or tested adapter |
| `src/nrf54l15_hal_parts/nrf54l15_hal_802154_rawradio.inc` | Extract register setup, PHY operations, ISR event capture, ED/LQI conversions, and buffer ownership into `zigbee/platform/radio_phy_nrf54.cpp` or a shared `ieee802154/` module | Blocking methods no longer called by runtime; adapter tests pass |
| `src/nrf54l15_hal.cpp:271-280` shared RADIO IRQ | Route through `RadioArbiter`/registered active operation; retain one top-level ISR | CS, BLE, Thread, and Zigbee regression suites pass and no global priority steal remains |
| System-off Zigbee special case | Replace with generic radio-owner quiesce contract | Every radio client registers quiesce and hardware tests pass |

OpenThread declares that it uses the Zigbee radio backend. Every commit which
touches PHY register setup, buffers, IRQ, channel, power, CCA, or ED must run
OpenThread PAL/native and two-board tests before merge.

### 20.2 Protocol sources

| Existing file | First extraction | Later destination |
|---|---|---|
| `src/zigbee_stack.h` | Preserve umbrella includes and legacy public types | Thin compatibility header plus new `Zigbee.h` API |
| `src/zigbee_stack.cpp` | Move pure helpers/codecs byte-identically with golden tests | `mac_codec`, `nwk_codec`, `aps_codec`, `zdo_codec`, `zcl_codec`, cluster modules |
| `src/zigbee_commissioning.h/.cpp` | Preserve state interpretation behind adapter | `bdb_engine`, role policy, Trust Center client, timers |
| `src/zigbee_security.h/.cpp` | First migrate to checked spans/capacities and add vectors | `security_codec`, crypto providers, key/replay/counter managers |
| `src/zigbee_persistence.h/.cpp` | Freeze legacy v1-v6 readers; stop expanding raw state | `persistence/journal`, schema codecs, migrations |

Move one symbol family per commit. Do not combine a mechanical move with a
wire-format change. During extraction, compile both old and new golden tests
against the same fixtures and compare exact output bytes/status.

### 20.3 Example cleanup

The 30 sketches total about 40,417 lines. Exact duplicates exist among several
legacy root and categorized Basic/ping/pong examples. The large HA examples
embed runtime behavior and must remain characterization fixtures until the new
runtime passes equivalent tests.

Migration order:

1. Inventory every example's endpoints, clusters, board I/O, serial commands,
   commissioning policy, persistence, and validation script.
2. Give each behavior a named integration test before removing it.
3. Convert one simplest end device, one router, and one coordinator to the new
   runtime.
4. Convert sleepy polling and indirect-queue examples after MAC/NWK sleepy
   behavior is complete.
5. Convert each HA light/sensor to declarative endpoint registration and board
   callbacks.
6. Turn exact legacy duplicates into documented aliases only if Arduino example
   discovery supports it cleanly; otherwise remove them with a release note.
7. Keep diagnostics separate from production examples. No production example
   should require trace mode or hard-coded keys.

The flagship coordinator currently keeps its node table/address allocator in
RAM, hard-codes network material/counters, and derives an alternate key
deterministically. It must be replaced by runtime Trust Center and persistent
device-table APIs, not shortened while preserving those behaviors.

### 20.4 Build metadata

- Keep Zigbee default/off board-menu behavior intentional and documented for
  XIAO L15, XIAO LM20, HOLYIOT 25007/25008, generic module, and nRF54L15 DK.
- Decouple the IEEE 802.15.4 PHY backend from the Zigbee protocol feature macro
  because OpenThread uses it.
- Add one generated feature/config header so declarations and definitions agree
  in enabled/disabled builds.
- Add RAM/flash map artifacts for the L15 VPR-on, L15 VPR-off, and LM20 profiles.
- Keep all release-archive verification paths repository-relative.

## 21. Test System

### 21.1 Native test layout

Create:

```text
tests/zigbee/
  CMakeLists.txt
  fixtures/
    mac/
    nwk/
    aps/
    security/
    zdo/
    zcl/
    persistence/
    pcaps/
  unit/
  model/
  integration/
  fuzz/
  fake/
    fake_clock.*
    fake_radio.*
    fake_rng.*
    fault_storage.*
    topology_simulator.*
```

The native build must compile with `-Wall -Wextra -Werror -Wconversion
-Wsign-conversion` where practical, plus ASan/UBSan in a separate job. Embedded
build warnings remain `all` and may not be waived globally to silence Zigbee.

### 21.2 Fixture provenance

Every fixture has a sidecar manifest:

```yaml
name: secured_nwk_transport_key_001
source: zuth_capture
source_revision: "<version>"
pcap_sha256: "..."
frame_number: 42
requirements: [SEC-NWK-..., APS-CMD-...]
notes: "Secrets replaced only if the replacement oracle was independently recomputed"
```

Expected bytes must come from normative examples, published official vectors,
ZUTH, a trusted independent implementation, or a small independently reviewed
reference model. Never encode with the DUT and paste its output as expected.

### 21.3 Unit and model tests

For each codec:

- Golden parse and build.
- Build-then-parse and parse-then-build where canonical encoding is defined.
- Truncate at every byte.
- Append unrelated trailing bytes and verify consumed-length policy.
- Flip each reserved/control bit.
- Exercise all integer/length boundaries and output capacities.
- Prove failed parse/build does not mutate destination state.

For each state machine:

- Enumerate every state/event pair, including unexpected and stale events.
- Test timer one tick before, exactly at, and one tick after deadline.
- Test generation wrap policy and stale callbacks.
- Force every allocation/table/storage/radio failure.
- Serialize/restore at every persistent transition.
- Assert final state, emitted frames, callbacks, timers, table mutations, and
  diagnostics, not just a log substring.

### 21.4 Fuzzing

Add libFuzzer/AFL-compatible entry points for MAC, NWK, APS, security headers,
ZDO, ZCL frames/types, OTA image headers, and persistence records. Seed from
golden fixtures. Run ASan/UBSan and a bounded allocator. In extended CI, run at
least one million inputs per parser or a time budget with stored crashing
corpora. A fuzzer result is only fixed when its minimal reproducer becomes a
permanent unit test.

### 21.5 Deterministic topology simulator

The simulator operates the real MAC/NWK/APS/runtime against fake PHYs. It must
model:

- Channel occupancy and CCA.
- Frame duration, propagation, collision, CRC failure, loss, duplication, and
  delayed delivery.
- LQI/RSSI per directed link.
- Node clocks and independent resets.
- Topology/link changes.
- Bounded packet/event/storage resources.

Required topology fixture:

```text
C ---- R1 ---- R2 ---- E
 \     |       /
  ---- R3 ----
```

Tests discover `C -> E`, create competing paths, break the active link, repair
through R3, exercise many-to-one/route-record/source-route, and deliver exactly
once before/after repair. Scale deterministic randomized tests to at least 50
simulated nodes and save the seed for any failure.

### 21.6 Existing script repairs

Review all repository `scripts/zigbee_*.py` tools. Required initial fixes:

- Derive the repository root from `Path(__file__).resolve()`; remove stale
  `/home/lolren/Desktop/Nrf54L15/...` defaults.
- Required failed assertions must exit nonzero. Several current validators
  write false values to `summary.txt` and still return zero; this is not a gate.
- Produce JSON and JUnit with firmware commit, FQBN, board serial/probe, role,
  configuration, timestamps, packet capture, measurements, and assertion
  results.
- Separate informational observations from required assertions.
- Add timeouts and teardown which leave boards in a known state.
- Assert packet fields/timing through PCAP where relevant; serial substring
  presence alone is not protocol proof.

### 21.7 CI matrix

Required pull-request jobs:

1. Native Zigbee unit/model tests.
2. ASan/UBSan native tests.
3. Short fuzz smoke run.
4. Compile all discovered Zigbee examples on XIAO nRF54L15 VPR-on and XIAO
   nRF54LM20.
5. Compile representative Zigbee-disabled and OpenThread examples.
6. Warning-as-error representative coordinator, router, sleepy end device,
   light, sensor, OTA, and Green Power targets when those phases land.
7. Static size budget and forbidden-include/ISR-call checks.
8. Exact release-archive compile verification before release.

Nightly/hardware jobs add long fuzzing, two-board direct traffic, multi-node
routing, mixed-vendor join/rejoin, sleepy/power, counter brownout, OTA, and
protocol-regression suites.

## 22. Physical Hardware And Interoperability Matrix

### 22.1 What two connected boards can prove

With two nRF54 boards, validate:

- Register configuration, channel/power, frame format, direct MAC ACK/retry,
  CCA collision behavior, association, direct secure NWK/APS data, key
  transport/switch, ZDO/ZCL requests, one parent/one sleepy child, and reset/
  persistence cases.
- Run both board-family permutations when one L15 and one LM20 are available.
- Use one board as traffic generator only for tests which do not require an
  independent oracle.

Two boards cannot prove a routed path because coordinator/source, router, and
destination require at least three logical radios. They also cannot
simultaneously provide an independent passive sniffer.

### 22.2 Minimum multi-hop lab

Use at least:

```text
1 coordinator/Trust Center
2 routers, preferably from different vendors
1 always-on or sleepy end device
1 independent sniffer/test controller
```

Five active nodes allow alternate path and repair. RF boxes/attenuators or
controlled placement must force topology; proximity on a desk usually allows
all nodes to hear each other and invalidates the route test.

### 22.3 Ecosystem matrix

Test as available against independent coordinators/stacks from Zigbee2MQTT/
zigbee-herdsman, Home Assistant ZHA, TI, Silicon Labs, NXP, and Nordic/reference
products. Record exact coordinator firmware and stack revision. The local
external Zigbee2MQTT converter is useful for example UX but cannot be the only
interoperability oracle and needs a pinned compatibility test.

### 22.4 Soak and fault gates

Minimum release-candidate runs:

- 10,000 acknowledged MAC unicasts with zero duplicate application deliveries.
- 10,000 APS transactions under deterministic 10% frame loss.
- 1,000 injected reset/power-loss points across counter reservation and key
  switch, with no observed nonce reuse.
- At least 100 formation/join/rejoin/leave/factory-reset cycles per claimed
  role.
- Five-node route discovery/failure/repair with 1,000 pre-failure and 1,000
  post-repair messages delivered exactly once.
- 72-hour sleepy-device run with parent reset and network-key rotation.
- Seven-day coordinator/router churn soak with table capacity, aging, and
  memory high-water monitoring.
- OTA reset/power-loss campaign at every state and at least 100 randomized
  data offsets.

Every run archives PCAP, structured events, firmware SHA, board/FQBN/options,
test script revision, RF topology, and machine-readable verdict.

## 23. Dependency-Ordered Implementation Phases

Do not implement phases solely in feature-list order. The safety, runtime,
radio, and persistence foundations are prerequisites for reliable protocol
features.

### Phase Z0: freeze requirements and current behavior

Deliverables:

- Pinned standards manifest and role/device/optional-feature PICS.
- `docs/zigbee/normative-requirements.yaml` and generated coverage report.
- Current packet golden fixtures and PCAPs for direct examples.
- Repaired validation scripts with real nonzero failure exit.
- Compile inventory for all 30 examples and memory/flash baseline.

Exit:

- Every planned feature is mandatory, selected optional, or excluded with a
  clause/PICS reference.
- Current working direct paths have independent captures so later migration
  regressions are visible.
- No implementation constant is taken from an unpinned blog/header.

### Phase Z1: memory-safety foundation and native harness

Deliverables:

- Native test build, fake clock/RNG/radio/storage, ASan/UBSan, fuzz targets.
- Checked `ByteReader`/`ByteWriter`, spans, capacity-returning builders.
- Fix Configure Reporting six/seven-byte OOB and bitmap analog/discrete bug.
- Fix every 8-bit offset/capacity wrap in security and frame builders.
- Characterization adapters keep example output byte-compatible.

Exit:

- Every current parser passes truncate-at-every-byte tests.
- All current examples compile; golden packets are unchanged except fixes with
  explicit reviewed fixture changes.
- No sanitizer finding remains.

### Phase Z2: behavior-preserving source split

Deliverables:

- `src/zigbee/core`, pure layer codecs, feature config, compatibility facade.
- One concern moved per commit from `zigbee_stack.cpp`.
- Include-boundary and native-build tests.

Exit:

- Old public APIs/examples still compile and pass current direct behavior.
- Codecs have no Arduino/platform dependency.
- `zigbee_stack.cpp` no longer owns several protocol layers in one switch
  monolith.

### Phase Z3: runtime, events, timers, pools, and API

Deliverables:

- Generation-tagged event/timer queues and fixed buffer/table pools.
- `ZigbeeStack::process()`, `nextDeadlineUs()`, sleep/quiesce contract.
- Typed statuses/transaction handles and structured observer.
- Common ingress/egress pipeline behind legacy adapters.

Exit:

- No application callback, crypto, parse, Serial print, or storage operation
  runs in ISR context.
- Fake-time tests cover cancellation/stale events/wrap/table exhaustion.
- One simple coordinator/end-device pair uses the common runtime.

### Phase Z4: radio ownership, async PHY, and full MAC

Deliverables:

- Protocol-neutral async IEEE 802.15.4 PHY and explicit `RadioArbiter`.
- DMA ownership, LQI metadata, hardware timing capture, operation generation.
- PIB, filtering, duplicate table, unslotted CSMA-CA, retries, scans,
  association/disassociation, indirect queue, poll.
- Blocking legacy adapter only at compatibility boundary.

Exit:

- Section 5 hardware and Section 8 MAC gates pass on L15 and LM20.
- OpenThread, BLE, and Channel Sounding radio regressions pass.
- Conflicting protocols fail cleanly until true scheduling is implemented.

### Phase Z5: crash-safe persistence and security identity

Deliverables:

- A/B journal, explicit schema, v1-v6 migration, fault storage tests.
- Key store, per-peer replay, high-water counter allocator, crypto provider.
- Secured NWK and APS data/command unified processing.
- Trust Center device/auth tables and CSPRNG network-key generation.

Exit:

- Section 11/12 security and fault gates pass.
- Coordinator reset preserves network/devices/counters.
- No reset/brownout test reuses a captured nonce.

### Phase Z6: NWK lifecycle and routing

Deliverables:

- Full NWK header/command codecs, neighbor/child/address/route/discovery/
  broadcast/source-route tables.
- Formation/start, forwarding, route discovery/repair/aging, broadcasts,
  many-to-one, route record/source route, conflicts/channel update.

Exit:

- Deterministic 50-node simulator and physical five-node topology gates pass.
- Router claim is still experimental but now backed by actual relay/repair.

### Phase Z7: APS delivery

Deliverables:

- Complete APS codec/extended header/security path.
- ACK/retry/duplicate manager, bindings/groups/fan-out, backpressure.
- Fragmentation/reassembly if selected; otherwise explicit unsupported status.

Exit:

- Section 10 gates pass, including loss, duplicate, sleepy timeout, and table
  exhaustion.

### Phase Z8: BDB, Trust Center lifecycle, and ZDO

Deliverables:

- All-role asynchronous BDB engine, formation/steering/finding-binding/rejoin/
  leave/reset.
- Complete selected Trust Center command/authorization lifecycle.
- Endpoint registry, ZDO transaction manager, descriptors and management
  services including Permit Join and End Device Bind.

Exit:

- Role-specific commissioning and ZDO PICS tests pass in simulation and mixed-
  vendor hardware.
- No commissioning/runtime behavior remains in sketches.

### Phase Z9: ZCL foundation

Deliverables:

- Complete R8 type registry, endpoint/manufacturer/direction-aware cluster
  registry, attribute store, foundation commands, reporting engine.

Exit:

- Section 15 gates and applicable ZUTH foundation tests pass.
- Multi-endpoint same-cluster state is independent.

### Phase Z10: current clusters and example migration

Deliverables:

- Complete Basic, Power, Identify, Groups, Scenes, OnOff, switch config, Level,
  Color, Temperature, Humidity and selected device-type PICS.
- Thin declarative versions of every shipped light/sensor/basic example.
- Duplicate example cleanup with migration release notes.

Exit:

- All selected device-type mandatory behavior passes ZHA/Zigbee2MQTT and at
  least one additional independent stack.
- All examples compile on both XIAO families and contain no private stack.

### Phase Z11: sleepy device and Poll Control

Deliverables:

- Runtime parent/child timeout, indirect delivery, fast/long poll, Poll Control,
  wake/sleep deadline integration.
- Thin versions of every sleepy example and PPK2 evidence.

Exit:

- 72-hour, 1,000+ poll, parent reset, route/key change, and power gates pass.

### Phase Z12: OTA and additional selected clusters

Deliverables:

- OTA client/server/image/storage/bootloader handoff and signed image policy.
- Time/Diagnostics and any next device-driven clusters selected by PICS.

Exit:

- Section 17 OTA fault/rollback gates and cluster PICS tests pass per board.

### Phase Z13: Green Power and other selected R22.1 options

Deliverables:

- Green Power Proxy Basic if required by the routing PICS.
- Other options only with separate module, PICS, threat model, and test suite.

Exit:

- Applicable ZUTH and mixed-vendor tests pass. Unsupported GP roles remain
  explicitly unsupported.

### Phase Z14: R23.2/BDB 3.1/Zigbee 4.0 uplift

Deliverables:

- Fresh requirements diff and feature flags, R23.2 security/key lifecycle,
  selected DTL device declarations, interoperability with R22.1.
- Zigbee Direct only after BLE/Zigbee arbitration and Direct 1.1 PICS are ready.

Exit:

- R23.2/BDB3.1 PICS and qualification suite pass without regressing Z3 devices.

### Phase Z15: qualification and release

Deliverables:

- Frozen PICS, requirement-to-test report, ZUTH evidence, authorized-lab
  results, final PCAP/soak/power/size evidence.
- README feature matrix with exact role/cluster/optional/certification status.
- Exact release archive compiled and retested, not only the workspace.

Exit:

- All selected mandatory and optional PICS items pass.
- Public wording distinguishes certified, conformant-but-not-certified,
  experimental, partial, and unsupported.
- No release occurs from a dirty or evidence-mismatched tree.

## 24. Instructions For The Implementing LLM

### 24.1 Work-unit protocol

For every work unit:

1. Read the relevant normative-ledger entries and cited source clauses.
2. Read the current source, tests, adapters, and callers. State exact behavior
   being retained or changed.
3. Add or update a failing native/model test before implementation whenever the
   behavior can be tested off-target.
4. Implement the smallest coherent state/codec/module change.
5. Run focused tests, all Zigbee native tests, relevant example compiles, and
   OpenThread/BLE/CS regressions when shared radio/crypto/storage changes.
6. Run `git diff --check`, sanitizer job, and inspect the diff for debug output,
   secrets, generated binaries, unrelated churn, and raw pointer lifetime.
7. Update requirement records with exact files/symbols/tests/evidence. Do not
   change status to complete before the exit evidence exists.
8. Commit one behavior with a precise message. Do not mix mechanical moves,
   feature changes, formatting, release metadata, or unrelated fixes.
9. Stop at the phase gate. Do not compensate for a failing lower layer by
   adding retries/delays to an example.

### 24.2 Required proof bundle per pull request

```text
Requirements addressed
Specification/PICS references
Files and public API changed
Wire behavior changed/preserved
New unit/model/fuzz fixtures
Commands/tests run and exact results
Target-board compiles and size deltas
Hardware topology, firmware SHA and PCAP/evidence links
Known limitations and next dependent task
```

### 24.3 Prohibited implementation shortcuts

- Do not guess normative constants, statuses, timers, bit fields, or security
  rules from memory.
- Do not copy ZBOSS/proprietary code or third-party headers without a reviewed
  compatible license and provenance.
- Do not add another protocol monolith or put runtime logic back into `.ino`.
- Do not use `delay()`, `millis()` polling loops, `spinLimit`, or unbounded
  waits in runtime protocol paths.
- Do not allocate heap memory in steady state.
- Do not parse with unchecked indexes or `uint8_t` offset arithmetic.
- Do not log keys/plaintext security material.
- Do not update replay/counters/tables before authentication.
- Do not erase the only valid persistent state before the replacement commits.
- Do not make the implementation pass only by increasing a timeout.
- Do not claim routing with two directly communicating boards.
- Do not mark a codec, cluster constant, example, or local interop result as
  certification.
- Do not enable BLE/Zigbee concurrency until the arbiter is deadline-tested.

### 24.4 Mandatory stop conditions

Stop and record a blocking decision instead of inventing behavior when:

- The pinned normative document/PICS is missing or ambiguous.
- The requested feature belongs to a different revision baseline.
- A required frame fixture has no independent oracle.
- NVM atomicity/erase semantics are unknown.
- A buffer/table capacity cannot fit the L15 profile.
- CRACEN or RADIO ownership conflicts with another subsystem.
- A security transition could reuse a nonce, accept rollback, or expose a key.
- A lower-layer test fails after an upper-layer change.

### 24.5 Prompt template for a single task

```text
Implement requirement IDs <IDs> from
docs/zigbee/normative-requirements.yaml on baseline <revision>.

Read first:
- <normative clauses/PICS>
- <current files/symbols>
- <existing tests/fixtures>

Constraints:
- Preserve <named legacy behavior/API> through the compatibility adapter.
- Use checked spans/size_t and typed statuses.
- No heap, blocking wait, ISR parsing/logging/storage, or unverified constants.
- Update no unrelated file.

Deliver:
- implementation
- native/model negative and boundary tests
- hardware test if applicable
- requirement traceability update
- focused and regression test results

Do not proceed to dependent requirements until <exit condition> passes.
```

## 25. Independent Golden Fixtures

These small fixtures are useful smoke tests. Enter them with provenance in the
fixture manifest and still defer complex secured/routed/fragmented examples to
the pinned specifications/ZUTH/certified captures.

### 25.1 Channel mapping

```text
channel 11 -> FREQUENCY 5  -> 2405 MHz
channel 15 -> FREQUENCY 25 -> 2425 MHz
channel 26 -> FREQUENCY 80 -> 2480 MHz
channels 10 and 27 -> InvalidArgument
```

### 25.2 Legacy short-address MAC data and ACK

Data frame with PAN compression and ACK request, DSN `0x5a`, PAN `0x1234`,
destination `0x0000`, source `0x5678`, payload `aa bb`, software PSDU excluding
FCS:

```text
FCF = 0x8861
61 88 5a 34 12 00 00 78 56 aa bb
```

Matching ACK:

```text
02 00 5a
```

Deterministic CSMA model fixture, once the Phase Z0 ledger confirms the common
defaults:

```text
BE=3, random slot 7  -> 7 * 20 symbols = 140 symbols = 2240 us
CCA busy             -> NB=1, BE=4
random slot 10       -> 200 symbols = 3200 us
CCA busy             -> NB=2, BE=5
random slot 0        -> immediate CCA
CCA idle             -> transmit
```

With maximum backoffs four, verify the exact normative failure comparison so
the fifth consecutive busy CCA produces Channel Access Failure. With maximum
frame retries three, four absent ACK results produce `NoAck`; an ACK on the
fourth transmission succeeds. All MAC retries retain the identical MPDU/DSN.

### 25.3 Plain NWK data

NWK data, protocol version 2, destination `0x0000`, source `0x1234`, radius
`0x1e`, sequence `0x42`, payload `aa bb`:

```text
08 00 00 00 34 12 1e 42 aa bb
```

### 25.4 APS and ZCL

APS unicast, APS ACK requested, destination endpoint 1, cluster `0x0006`,
profile `0x0104`, source endpoint 1, APS counter `0x5a`, carrying ZCL On with
TSN `0x33`:

```text
40 01 06 00 04 01 01 5a 01 33 01
```

Read OnOff attribute `0x0000`, TSN `0x10`:

```text
00 10 00 00 00
```

Successful response with Boolean true:

```text
08 10 01 00 00 00 10 01
```

Configure OnOff reporting, minimum 1 second, maximum 300 seconds:

```text
00 12 06 00 00 00 10 01 00 2c 01
```

Successful Configure Reporting response:

```text
08 12 07 00
```

Temperature report for 21.50 C (`0x0866` signed hundredths):

```text
08 14 0a 00 00 29 66 08
```

### 25.5 ZDO

Node Descriptor Request, TSN `0x44`, target `0x1234`:

```text
44 34 12
```

Simple Descriptor Request for endpoint 1:

```text
44 34 12 01
```

Bind Request for source IEEE `0x00124b0001abcdef`, endpoint 1, OnOff cluster,
to destination IEEE `0x00158d0001020304`, endpoint 1:

```text
55 ef cd ab 01 00 4b 12 00 01 06 00
03 04 03 02 01 00 8d 15 00 01
```

### 25.6 Crypto smoke vectors

FIPS AES-128:

```text
key        000102030405060708090a0b0c0d0e0f
plaintext  00112233445566778899aabbccddeeff
ciphertext 69c4e0d86a7b0430d8cdb78070b4c55a
```

ZigbeeAlliance09 key bytes:

```text
5a6967426565416c6c69616e63653039
```

Independent AES-CCM with a four-byte tag (also recomputed locally with the
Python `cryptography` AESCCM provider while preparing this plan):

```text
key        000102030405060708090a0b0c0d0e0f
nonce      000102030405060708090a0b0c
AAD        0001020304050607
plaintext  202122232425262728292a2b2c2d2e2f
cipher+tag 361596ab772cdca572be198cb11930f70692d726
```

This CCM vector proves the primitive/provider API, not Zigbee nonce/AAD
construction. Import Zigbee NWK/APS and install-code vectors from the pinned
official material. Flip every bit of nonce, AAD, ciphertext, and tag in separate
negative tests; reject without plaintext delivery or replay-state advancement.

## 26. Open Decisions That Must Not Be Guessed

Resolve and record these in Phase Z0 or at the named phase gate:

1. Exact R22.1 role and device PICS, including Green Power Proxy Basic and APS
   fragmentation selection.
2. Which coordinator/Trust Center certification claim the Arduino core itself
   will pursue versus which device examples are demonstrations.
3. L15/LM20 capacity profiles and advertised maxima after measured memory use.
4. Exclusive-only radio ownership for the first release versus a separately
   scheduled BLE/Zigbee multiprotocol claim.
5. CRACEN provider availability, ownership, security-domain, and fallback
   policy on each board family.
6. Dedicated NVM layout/atomic primitive for journals and counter reservations.
7. Production provisioning path for EUI-64, install codes, Trust Center/device
   keys, and firmware-signing trust roots.
8. OTA bootloader, internal/external staging layout, signature algorithm,
   rollback, and per-board support.
9. Exact device types/clusters retained as release examples and whether legacy
   duplicate paths are removed or aliased.
10. Hardware lab topology and independent certified products/sniffer/ZUTH
    access needed for route and qualification gates.
11. R23.2/Zigbee 4.0 schedule; it must not silently alter the R22.1 baseline.

## 27. Definition Of Done Checklist

The Zigbee completion project is done only when all boxes selected by the
frozen PICS are checked:

- [ ] Normative standards, errata, PICS, and device types are pinned and hashed.
- [ ] Every selected requirement has implementation, unit/model test, hardware
      evidence where applicable, and qualification mapping.
- [ ] One event-driven runtime owns all coordinator/router/end-device behavior.
- [ ] Radio/PHY register, DMA, CCA, ED/LQI, ACK timing, filtering, and ownership
      gates pass on nRF54L15 and nRF54LM20.
- [ ] Full selected MAC services, CSMA/retries, scans, association, duplicates,
      indirect delivery, and sleepy polling pass.
- [ ] NWK join/rejoin/leave, forwarding, routing/repair, broadcasts, many-to-one,
      source route, conflict/update, neighbor/child aging pass multi-node tests.
- [ ] APS reliability, duplicates, binding/group delivery, security, and selected
      fragmentation pass loss/exhaustion/reset tests.
- [ ] Keys, Trust Center authorization, replay, high-water counters, key rotation,
      install-code policy, leave/reset survive arbitrary power failure safely.
- [ ] A/B persistence, migration, CRC, generations, and record validation pass
      every fault-injection point.
- [ ] All-role BDB and selected ZDO services pass their transition/PICS tests.
- [ ] Endpoint-scoped ZCL R8 foundation and reporting pass type, command,
      malformed, manufacturer, timing, and multi-endpoint tests.
- [ ] Every advertised cluster/device type passes its exact mandatory PICS.
- [ ] OTA and Green Power pass only if advertised; otherwise remain explicit
      unsupported/experimental rows.
- [ ] All examples are thin, compile on both XIAO families, and have no private
      protocol/security/persistence runtime.
- [ ] Native, sanitizer, fuzz, simulated topology, two-board, multi-node,
      mixed-vendor, soak, brownout, power, and release-archive gates pass.
- [ ] BLE, Thread, Channel Sounding, core I/O, and power regressions pass after
      shared radio/crypto/storage work.
- [ ] ZUTH and authorized-lab evidence exists for every certification claim.
- [ ] README language exactly matches tested/certified scope.

Until then, the README must continue to call Zigbee experimental/partial. A
working direct example or a successful Zigbee2MQTT interview is useful evidence
but not completion.

## 28. References

### Local hardware sources

- `../datasheets/Nordic_nRF54L15_Datasheet_v1.0.pdf`, especially RADIO Section
  8.17 and IEEE 802.15.4 operation Section 8.17.12.
- `../datasheets/nRF54LM20A_nRF54LM20B_Datasheet_v1.0.pdf`, especially RADIO
  Section 8.17 and IEEE 802.15.4 operation Section 8.17.12.
- Board schematics in `../datasheets/` for RF switch, antenna, storage, and
  board-specific power control.

### Official standards and qualification sources

- [CSA specifications download request](https://csa-iot.org/developer-resource/specifications-download-request/)
- [CSA Zigbee certification tools and ZUTH](https://csa-iot.org/certification/tools/)
- [CSA certification process](https://csa-iot.org/certification/why-certify/)
- [CSA Zigbee Direct FAQ](https://csa-iot.org/all-solutions/zigbee/zigbee-direct-faq/)

### Repository context

- `README.md`, current protocol matrix and explicit Zigbee limitations.
- `docs/ZIGBEE_FULL_SUPPORT_HANDOFF.md`, earlier high-level handoff; this
  document supersedes it for implementation sequencing and acceptance detail.
- `docs/ZIGBEE2MQTT_INTEGRATION.md`, current ecosystem workflow.
- `extras/zigbee2mqtt/cleancore_nrf54_examples.mjs`, current external converter.
- `.github/workflows/ci.yml`, current five-example Zigbee compile coverage.

## 29. Final Sequencing Warning

Do not start by adding more clusters or NWK command constants. The critical
path is:

```text
normative PICS and independent tests
  -> checked codecs and memory safety
  -> one event/timer/buffer runtime
  -> asynchronous timed MAC and radio ownership
  -> crash-safe persistence, key identity, replay and counter reservation
  -> real NWK forwarding/routing
  -> APS reliability
  -> BDB/Trust Center/ZDO
  -> endpoint-scoped ZCL and clusters
  -> sleepy/OTA/optional features
  -> qualification
```

Adding features to the current example-owned blocking loops would create more
surface area without producing a conformant stack. Every later phase depends
on the timing, ownership, restart, and security guarantees established earlier.
