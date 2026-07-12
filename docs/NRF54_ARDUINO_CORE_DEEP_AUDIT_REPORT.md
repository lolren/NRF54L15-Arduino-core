# nRF54 Arduino Core Deep Audit Report

Audit date: 2026-07-09  
Remediation update: 2026-07-12
Repository: `/home/lolren/Desktop/eport_nrf54/nrf54-arduino-core`  
Local reference set: `/home/lolren/Desktop/eport_nrf54/datasheets`

The original audit below used only the repository, supplied local PDFs/text
extracts, and locally reproduced compiler/linker output. The v1.0.0-rc1 BLE
security follow-up additionally checked the official Bluetooth Core 6.2 HTML
for SMP transaction timing, repeated-attempt handling, and encrypted data-PDU
semantics. The supplied local reference set contains product specifications and
two XIAO schematics, but no standalone Nordic anomaly/errata document and no
HOLYIOT or PCA10156 schematic. Claims that require those missing documents are
explicitly marked as unverified.

## High/Critical Revalidation Correction - 2026-07-12

The blanket all-resolved statement recorded in the 2026-07-11 remediation
update is superseded by this revalidation. The targeted review reclassifies
FINDING-003 and the status-position premise of FINDING-065 as audit errors, and
confirms FINDING-034 resolved in the current tree after adjacent `CracenIkg`
hardening. This targeted disposition does not independently re-prove every
other historical finding or replace the complete release gate. No current
all-fixed claim should be inferred from the historical finding text or the
original machine-readable appendix.

### FINDING-003 correction: audit false positive

FINDING-003 incorrectly interpreted `PDM.SAMPLE.MAXCNT` as a count of 16-bit
samples. Nordic's authoritative nrfx integration selects unified byte-access
DMA for both target families and deliberately converts an `int16_t` element
count to bytes:

* `/home/lolren/Desktop/test_pi_nrf54/ncs-workspace/modules/hal/nordic/nrfx/bsp/stable/soc/nrfx_mdk_fixups.h:619-628` defines `DMA_BUFFER_UNIFIED_BYTE_ACCESS` for nRF54L15, and lines 752-773 define it for nRF54LM20A.
* `/home/lolren/Desktop/test_pi_nrf54/ncs-workspace/modules/hal/nordic/nrfx/hal/nrf_pdm.h:1053-1060` writes `num * sizeof(int16_t)` to `SAMPLE.MAXCNT` under that device contract.
* Nordic's generated device headers describe `BUFFSIZE` as a byte length at `/home/lolren/Desktop/test_pi_nrf54/ncs-workspace/modules/hal/nordic/nrfx/bsp/stable/mdk/nrf54l15_types.h:20562-20569` and `/home/lolren/Desktop/test_pi_nrf54/ncs-workspace/modules/hal/nordic/nrfx/bsp/stable/mdk/nrf54lm20a_types.h:20230-20237`.

The current `Pdm::capture` implementation therefore correctly validates the
element count and writes `sampleCount * sizeof(int16_t)` in
`hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_crypto_analog.inc`.
The focused contract is `scripts/test_lm20a_pdm_contract.py`; its source-local
compile record is `build/lm20a-mic-pdm-byte-count-final.json`. Connected-board
evidence in `measurements/lm20a_pdm_v1_byte_count/summary.json` records 108
fresh 8,192-sample captures on LM20A UID `3377B9D6`, each taking 504-505 ms,
with zero timeouts, underfills or DMA guard failures. The earlier direct-count
experiment is retained in
`measurements/lm20a_pdm_v1_final/serial_and_summary.log`; it produced zeroed,
premature buffers rather than proving an overrun.

Revalidated classification: **False positive; superseded by authoritative
Nordic unified-byte DMA evidence.** The original FINDING-003 text is retained
below as audit history. Programming `MAXCNT = sampleCount` would be a
regression, not a fix.

### FINDING-065 correction: status positions were misread

FINDING-065's claimed LM20 status positions were wrong. The authoritative
Nordic nRF54LM20A header defines `ANYHEALTHTESTFAIL` at bit 6,
`STARTUPFAIL` at bit 10, per-share repetition failures at bits 12-15,
per-share proportion failures at bits 16-19, and
`CONDITIONINGISTOOSLOW` at bit 20:

* `/home/lolren/Desktop/test_pi_nrf54/ncs-workspace/modules/hal/nordic/nrfx/bsp/stable/mdk/nrf54lm20a_types.h:4600-4657`
* `/home/lolren/Desktop/eport_nrf54/datasheets/m20a.txt:10171-10201` is the source table whose compressed PDF-to-text bit diagram was misread by the original audit.

The current tree uses those positions in
`hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/nrf54lm20b_types.h:4218-4243`,
adds the aggregate bit-6 mask, and uses the LM20 `WARMUPPERIOD`,
`SAMPLINGPERIOD` and start-pulse sequence in
`hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_crypto_analog.inc:137-175`.
The specific claim that copied bit-10/12-20 definitions cause the HAL to miss
LM20 health failures is therefore **false and superseded**.

This correction does not certify the entire raw LM20 device header as a clean
generated artifact. `cores/nrf54lm20b/nrf54lm20b_types.h` is still a locally
maintained derivative with targeted LM20 corrections; replacing or
mechanically reconciling the complete file against Nordic's current generated
header remains worthwhile provenance and maintenance work. That raw-header
cleanup is distinct from, and must not be cited as evidence for, the erroneous
FINDING-065 status-position claim.

### FINDING-034 and `CracenIkg`: resolved fail-closed in the current tree

The register-map defect described by FINDING-034 is not present in the current
wrapper. In
`hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_cracen_pke.h:161-177`,
the base addresses come from the active device header, CRACEN `ENABLE` is
`+0x400`, interrupt disable is `INTENCLR +0x308`, and PKE status is the real
`PK.STATUS +0x200C`. The active headers select L15 CRACEN/CRACENCORE bases
`0x50048000`/`0x51800000` and LM20 bases
`0x50059000`/`0x50010000`; adding the CryptoRAM offset `+0x8000` therefore
produces the documented data addresses `0x51808000` and `0x50018000` rather
than a hard-coded cross-chip alias.

The adjacent typed implementation is hardened in
`hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.h:581-670`
and
`hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_security.cpp:315-801`:

* CryptoRAM uses the active `coreBase + 0x8000`, 0x200-byte slots, 16 L15 slots and 15 LM20 slots. Null, zero-length, oversized and out-of-range accesses are rejected, as are CPU-inaccessible pages outside 8-12 after IKG enters Secure Mode.
* Enabling waits for PKE zeroization to finish. Operand reads never clear the write-once `PROTECTEDRAMLOCK` to gain access, and setting that lock requires active CRACEN plus successful readback.
* Success and key-presence queries require a live core. IKG start and key generation require a pre-valid hardware seed; the API cannot validate an unknown seed or synthesize deterministic seed material. LM20 exposes SEEDVALID/SEEDLOCK as KMU-managed state, while L15 locking requires an already-valid seed and verified readback.
* High-level ECDSA, point-multiplication and result APIs whose validated microcode protocol is not implemented zero their outputs and return false instead of reporting fabricated success. The two affected examples are non-destructive status and CryptoRAM-boundary diagnostics.

Verification evidence:

* `python3 scripts/test_cracen_ikg_contract.py` passes all 7 focused contract tests.
* `build/cracen-ikg-seed-example-compiles.json` records 2/2 passing builds of `KmuCracenIkgSeedProof` on L15 and LM20.
* `build/cracen-operand-ram-example-compiles.json` records 2/2 passing builds of `CracenEccTest` on L15 and LM20.

Revalidated classification: **Resolved in the current tree, with unsupported
operations fail-closed.** No runtime IKG key-generation claim is made. Safe
production seed provisioning and a validated PSA/NCS PKE/microcode integration
remain outside this Arduino HAL and must not be inferred from these static and
compile checks.

## Remediation Update - 2026-07-11

Status recorded at that checkpoint: **all 71 original audit items were reported
as addressed in the 1.0.0-rc1 source tree**. The 2026-07-12 revalidation above
supersedes that blanket status. The findings below are retained as the original
audit record; the 0.9.216 remediation baseline and subsequent follow-ups record
the source-tree history rather than a current release decision.

The fixes cover the critical IRQ/vector map defects, cache/RRAMC/MEMCONF register definitions, PDM/EasyDMA sizing, UARTE/Serial1 routing and baud handling, SPI/TWIM/GPIOTE/GPIO/analog/tone API defects, System OFF/GRTC timed wake, low-power board state handling, Windows upload/APPROTECT retry behavior, release archive/index generation tooling, and the BLE HID mouse pairing/encryption/reporting path. The two items originally marked as needing human verification were converted into code-level mitigations, regression checks and focused example builds; external BLE certification and radio/power characterization still require dedicated lab equipment and are not claimed by this source audit.

Verification completed in this remediation pass:

* `CXX=/usr/bin/g++ python3 scripts/test_core_io_regressions.py` passed.
* `python3 scripts/test_upload_helper.py` passed.
* `python3 -m py_compile scripts/build_all_examples.py scripts/build_release.py scripts/test_all_examples.py scripts/verify_package_index.py scripts/verify_release_archive.py hardware/nrf54l15clean/nrf54l15clean/tools/upload.py` passed.
* `.github/workflows/ci.yml` and `.github/workflows/release.yml` parsed successfully as YAML.
* Focused source-local example builds passed for `SystemOffWake`, `SerialDual`, `blehid_mouse`, `HalRegisterContracts`, `PwmDatasheetStress` and `HighSpeedSpi32MHzProbe` on the L15 and LM20 board profiles.
* Full source-local example matrix passed: `846/846` jobs in `/tmp/nrf54-full-build-report-0.9.218-final.json`.
* The initial `./tools/release.sh 0.9.216` passed, including package-index verification and release-archive Thread/Matter stage-probe compilation. Its historical archive was `nrf54l15clean-0.9.216-d5ce457a0d02.tar.bz2`, SHA-256 `d5ce457a0d02c347c1812a0e84e204cd88552eff5721e380681967b6cc25128b`, size `26944797`.
* Final `./tools/release.sh 0.9.218` passed package-index verification and release-archive Thread/Matter stage-probe compilation. Its archive is `nrf54l15clean-0.9.218-33bf48c9834a.tar.bz2`, SHA-256 `33bf48c9834a6cbb1494b80e348fb9bef9f7f783c9e50e0c85f6543aabe9810c`, size `26944208`.
* The initial L15 hardware pass ran `Power/SystemOffWakeDiag` on UID `761FDE87`; the LM20 board UID `3377B9D6` was not enumerating during that earlier remediation session. The v0.9.218 follow-up below records the later two-board CLI hardware evidence.

Nordic documents System OFF as emulated while debugging. Under that condition the diagnostic can show a debug/software reset bit rather than a physical OFF reset bit, but the fixed code now treats a fired GRTC compare after `WFI` as a wake event and forces the reset path needed by the Arduino API. True non-debug System OFF still enters through `NRF_REGULATORS->SYSTEMOFF`.

### Hardware Follow-up - 2026-07-10 (v0.9.218)

A CLI retest used the connected XIAO nRF54L15 (`761FDE87`, `/dev/ttyACM0`) and XIAO nRF54LM20A (`3377B9D6`, `/dev/ttyACM1`). It found that the earlier Sense rail-retention sketch was not a valid proof: a BLE-disabled build still reserved and cleared the sketch's GRTC compare channel, and enabling bridge `Serial` before the measured delay suppressed the board-state transition being examined. The report's aggregate source-level remediation claim remains valid, but this hardware-test gap required a follow-up correction.

Version 0.9.218 releases BLE-disabled GRTC ownership, keeps the BLE IRQ service from clearing unused BLE compares in that configuration, and rewrites `SenseDelayRailRetentionProbe` as a one-shot measurement before bridge serial is configured. The probe now uses the free CC5 while the core's tickless delay uses CC6, rejects a BLE-enabled build with a clear configuration error, waits 10 ms for the IMU rail, and reports retained measurement data after wake.

Hardware evidence from this follow-up:

* L15 `SystemOffWakeDiag` passed three timed wake cycles with `status=timed_system_off_wake_ok` and a GRTC reset flag.
* Final `CoreVersionProbe` captures reported `Core version heartbeat: 0.9.218` on both connected boards.
* L15 rail retention passed: `mid_count=1`, IMU/VBAT stayed high, RF power dropped during idle and restored after wake, `post_rfctl=0`, `vbat_raw=573`, `WHO_AM_I=0x6A`, and `retention_status=PASS`.
* L15 serial-fabric initialization passed for UARTE22/UARTE30, TWIM22/TWIM30 and SPIM22/SPIM30; the runtime probe now reports the all-pass status every second for CLI monitoring.
* LM20A passed timed System OFF wake, QSPI JEDEC/status/deep-power-down, IMU `WHO_AM_I=0x6A`, live PDM microphone capture, and a BLE-disabled IMU regression run.

At the v0.9.218 checkpoint, this did not change the explicit remaining limits below: Windows PowerShell execution, external UART loopback, BLE HID pairing, RF/protocol conformance, current characterization and other board-specific lab tests still needed their relevant external equipment or reporters.

### LM20A Power Follow-up - 2026-07-10 (v0.9.219)

The LM20A `delay()` path now requests the board's populated LFXO without waiting for crystal startup, allowing SystemLFCLK to switch from LFRC to LFXO without a first-delay stall. The board clock-load values are aligned with the XIAO LM20A design, external flash is returned to deep power-down when the Adafruit transport ends, and the nPM1300 cleanup path disables persistent IBAT measurement, parks PMIC I2C input buffers, and is linked into ordinary SYSTEM OFF sketches.

Connected-board functional evidence on UID `3377B9D6`:

* First `delay(1)` measured 1,028 us; 10/100/1000 ms delays were within 26 us of target after LFXO selection.
* 27 of 27 measured idle loops entered WFI, with no skipped-WFI events; LFCLK reported LFXO and constant-latency mode was disabled.
* The QSPI flash reported JEDEC `85 20 17`, woke after deep power-down, and re-entered deep power-down through the Adafruit transport path.
* A forced nPM1300 IBAT-enable state was cleared by a generic SYSTEM OFF sketch that did not include the PMIC library; PMIC SCL/SDA returned to `PIN_CNF=0x2`.

The connected CMSIS-DAP wakes SYSTEM OFF through the debug interface (`RESETREAS.DIF`), so it cannot establish the board's microamp floor. A PPK2 or battery test with USB and SWD detached remains required for the 7.5 uA System ON and 3 uA System OFF targets.

### BLE HID Interoperability Follow-up - 2026-07-10 (v0.9.220)

The BLE HID mouse path was retested after Sony Xperia interoperability regressed from Pixel/Fairphone-only success. The production fix now keeps zero-length encrypted data-channel ACKs as empty plaintext LL data PDUs, accepts peer zero-length ACKs while encryption is active, clears zero/zero PHY update indications without queuing a reserved instant, removes the incomplete GATT robust-caching attributes until a compliant Database Hash implementation exists, and groups multiple advertised service UUIDs into one complete-list AD structure.

Connected-board evidence on XIAO nRF54L15 UID `761FDE87`:

* The HID pairing probe paired, encrypted, completed HID discovery, and was accepted as an input device by Pixel, Fairphone 5, and Sony Xperia phones.
* Source-local builds passed for `Bluefruit52Lib/examples/Diagnostics/hid_pairing_probe` with BLE trace enabled, `Bluefruit52Lib/examples/HID/blehid_mouse`, and `Bluefruit52Lib/examples/Peripheral/bleuart`.
* `./tools/release.sh 0.9.220` passed package-index verification, release-archive verification, and the archive Thread/Matter stage-probe compiles. Its archive is `nrf54l15clean-0.9.220-181902d5c1e0.tar.bz2`, SHA-256 `181902d5c1e0731d7f15169d8f1231f0207d2eddfd3309c0b87474ac08e7b06f`, size `26940757`.

This v0.9.220 follow-up does not claim full Bluetooth qualification or RF conformance. It addressed the initial Android HID interoperability regression; the v0.9.221 follow-up below records the additional fragmentation, retransmission, and multi-phone bond-selection fixes found during broader handset cycling.

### BLE Multi-Phone Pairing Follow-up - 2026-07-11 (v0.9.221)

Version 0.9.221 extends outbound fixed-channel L2CAP handling so SMP responses larger than the negotiated link-layer payload, including the 65-byte Secure Connections public-key message plus its L2CAP header, are split into an initial data PDU and LLID `0x01` continuation PDUs. Immediate, deferred and same-event follow-up response paths preserve the complete SDU when another PDU already occupies the pending slot. The link layer also retains the original More Data bit, SN, payload and encryption counter for ordinary retransmissions and uncertain local RADIO TX completion, preventing a continuation from advancing ahead of its predecessor.

Bond reuse for rotating peer addresses is now deterministic. The core selects the stored LTK only after an exact connection-address match, an exact distributed identity-address match, hardware AAR resolution, or a software `ah(IRK, prand)` check. It no longer treats `LL_ENC_REQ` EDIV/Rand as peer identity proof, because LE Secure Connections bonds use zero for both fields. An unresolved phone therefore receives a Security Request and can pair afresh instead of attempting encryption with another phone's LTK.

Connected-board evidence on XIAO nRF54L15 UID `761FDE87`:

* The BLE-trace HID pairing probe compiled, flashed, completed Secure Connections public-key fragmentation, paired, encrypted, completed HID discovery, enabled the HID CCCD, and transmitted mouse reports.
* Five successful traced HID sessions ended with clean phone-initiated disconnects. Operation was user-confirmed on Google Pixel, Fairphone 5, Huawei P30 Pro, Sony Xperia, and iPhone handsets.
* Clean source-local builds passed for `Bluefruit52Lib/examples/HID/blehid_mouse` and `Bluefruit52Lib/examples/Peripheral/bleuart` without warnings.
* `./tools/release.sh 0.9.221` passed package-index verification, release-archive verification, and the archive Thread/Matter stage-probe compiles. Its archive is `nrf54l15clean-0.9.221-89ab4ffaf500.tar.bz2`, SHA-256 `89ab4ffaf5006ead5dc0587155fb339f43f3c346981339cd580c8cfdb23ccda7`, size `26941243`.

The current persistence format stores one bond and one associated CCCD record. Switching to a phone whose older bond was replaced may require forgetting and pairing again; transparent retained switching among several hosts requires a future multi-bond storage migration. This follow-up does not claim full Bluetooth qualification or RF conformance, and affected users should retest the published package on their devices.

### Release Hardening Follow-up - 2026-07-11 (v0.9.222)

The PCA10156 variant documented `Serial1` as an alternate UART but aliased its default pins to `Serial`, putting both globals on P1.04/P1.05. Version 0.9.222 keeps the DK/VCOM `Serial` route on P1.04/P1.05 and moves the independent UARTE21 `Serial1` default to P1.02/P1.03, which are exposed by the variant as D16/D17 and are valid UARTE20/21 pins according to the supplied nRF54L15 product specification. Those two pads reset as NFC1/NFC2 with GPIO input disabled, so both L15 `HardwareSerial` and the GPIO configuration path now release NFCT `PADCONFIG` before using them; the corresponding LM20 path uses that chip's P1.01/P1.02 NFC mapping.

Regression coverage checks that the two DK routes remain distinct, verifies NFCT release for both chip families, adds PCA10156 assertions and runtime register diagnostics to `SerialDualBaudRemapProbe`, and compiles that probe under the DK FQBN in CI. The probe also selects an independent P1.02/P1.03 route on HOLYIOT-25008 instead of remapping both UART objects to D0/D1. A separate Windows CI harness false failure was corrected: the intentional uploader failure-propagation test now clears its captured expected exit code before returning to the GitHub Actions shell wrapper.

A post-v0.9.221 CI matrix run exposed three Thread-only PSK joiner link failures. `nrf54_all.h` intentionally exposes `MatterPbkdf2` as the shared PBKDF2/HMAC/SHA-256 helper for Thread and Matter, but its implementation file was guarded only by the Matter feature flag. The implementation now compiles when either staged core is enabled, restoring the Thread-only link path without enabling the rest of Matter.

A strict compiler-warning audit then corrected behavior-sensitive defects across BLE, Matter, OpenThread and Zigbee. Custom-GATT declarations, storage and Bluefruit integration now preserve the advertised 512-byte characteristic-value capacity end to end; notifications larger than the negotiated single-PDU limit are rejected before any eight-bit length conversion. BLE central disconnects distinguish MIC failure, peer termination and internal termination. Secured Zigbee parsers commit frame and security output only after complete authenticated parsing, and malformed simple parsers leave their outputs invalid. Typed resets preserve nonzero defaults including the Matter SPAKE2+ PBKDF iteration count of 2000, invalid OpenThread address sentinels and channel-sounding quality sentinels. Persisted Matter and Zigbee blobs retain deterministic padding while applying typed defaults, and OpenThread settings key formatting uses bounded unsigned indices so the package's GCC 7 toolchain cannot truncate chunk keys.

Verification completed for this follow-up:

* `CXX=/usr/bin/g++ python3 scripts/test_core_io_regressions.py` passed, including the NFC/PCA10156, Thread PBKDF2, BLE disconnect/GATT, Zigbee parser/persistence and typed-default contracts; `python3 scripts/test_upload_helper.py` passed all 11 tests.
* Strict source-local `--warnings all` builds passed without warning lines for `blehid_mouse`, the 512-byte custom-GATT probe, `ThreadExperimentalJoinerPSK`, `MatterPaseCryptoTest`, `ZigbeeCoordinator` with BLE disabled, and the PCA10156 `SerialDualBaudRemapProbe`. CI now enforces the same representative warning gate across L15 and LM20 audit targets.
* Thread-only source-local builds passed for `ThreadExperimentalJoinerPSK`, `ThreadExperimentalJoinerPSKCommissioner`, and `ThreadExperimentalJoinerPSKJoiner`.
* The Windows-native tooling job passed on GitHub Actions after the harness correction.
* The canonical `./tools/release.sh 0.9.222` build passed package-index and exact-archive verification. Its final archive is `nrf54l15clean-0.9.222-1c64dda86ec3.tar.bz2`, SHA-256 `1c64dda86ec349864af6d7ec5c31f7c4e7a47c8fa6159f2f468e62b061bdae02`, size `26940046`; all three checked-in indexes identify those exact bytes.
* Six additional `--warnings all` builds from the extracted final archive and package-declared GCC 7 toolchain passed for HID, 512-byte custom GATT, Thread PSK, Matter PASE, BLE-disabled Zigbee and PCA10156 Serial.
* The final extracted-archive/GCC 7 HID image was loaded through pyOCD onto XIAO nRF54L15 UID `761FDE87`, reset, and observed advertising as `XIAO nRF54L15` without pairing. Only that probe enumerated during the final v0.9.222 pass; the second previously used board was unavailable.

The five-phone HID interoperability result above remains the user-confirmed v0.9.221 baseline. The v0.9.222 final package still needs reporter retesting and does not claim Bluetooth qualification, RF conformance, multi-bond persistence, or PPK2 current characterization.

### Channel Sounding Controller Follow-up - 2026-07-11 (v0.9.223 source tree)

The public Channel Sounding path now replaces the experimental raw-RADIO examples with `BleCsControllerRuntime`, backed by Nordic's SoftDevice Controller (SDC) and Multiprotocol Service Layer (MPSL). The Arduino IDE exposes exactly two public Channel Sounding examples, `BleChannelSoundingInitiator` and `BleChannelSoundingReflector`; former raw-radio, parser, transport and interoperability sketches are retained only as test fixtures under `extras/tests/channel_sounding`. Both public examples require the 128 MHz CPU profile and fail explicitly instead of starting under the 64 MHz profile.

The bundled binary controller components are SoftDevice Controller multirole, MPSL and MPSL FEM common for the `nrf54l` and `nrf54lm` families, taken from Nordic nrfxlib revision `7a07f89ee8c32658ebfd2034b4cae92fde63e122` (`v3.4.0-rc1-12-g7a07f89ee`). They are not covered by the core's MIT license. The checked-in Nordic 5-Clause license restricts the binaries to Nordic Semiconductor integrated circuits, requires retention of its notices, and prohibits reverse engineering, decompilation, modification and disassembly; the accompanying attribution notice is also retained.

Connected-board validation used XIAO nRF54L15 UID `761FDE87` and XIAO nRF54LM20A UID `3377B9D6`, with warning-clean builds at 128 MHz:

* With the L15 as initiator and LM20A as reflector, the final positive run recorded 20 accepted ranges, 23 reflector results and 20 exact session-token/transfer-ID/CRC matches.
* With the reflector deliberately absent, the initiator accepted zero ranges. Restoring it then produced 37 complete accepted range lines, 42 reflector results and 38 matching result sessions; serial capture ended while the final accepted initiator line was being printed.
* Reversing the roles, with LM20A as initiator and L15 as reflector, produced 12 accepted ranges, 15 reflector results and 12 exact session matches. This exercises both controller roles on both chip families.
* Accepted procedures reassembled both HCI LE CS Subevent Result `0x31` and continuation `0x32`, yielding 42 controller steps per side, 34 or 35 usable PBR channels, matching peer envelopes and no reported parser drops or rejections. The on-board parser regression also rejected malformed, reordered, duplicate-channel, incoherent-phase and one-RTT-pair inputs.
* The canonical `./tools/release.sh 0.9.223` build passed all package-index, exact-archive, extracted Thread and extracted Matter verification. The resulting archive is `nrf54l15clean-0.9.223-dd1189998b11.tar.bz2`, SHA-256 `dd1189998b1161a1ba0f85b4dc59d8fe1faaa22b1c7573311af135e3d85a1ba1`, size `27532154`; all three checked-in indexes identify those exact bytes.

The timing-critical sounding phase is a controller-executed Bluetooth LE CS Test. This core's proprietary CRC-protected protocol first establishes a per-cycle session token and shared DRBG nonce. After SDC releases RADIO, the reflector returns its step buffer in an envelope correlated by that token, profile, role, controller counters and step count. Those pre/post-test exchanges are not a connected-ACL Channel Sounding procedure, a Bluetooth profile, or a cross-vendor result transport.

This evidence establishes repeatable operation of the two-board Arduino test path on the attached hardware. It does not claim Bluetooth SIG qualification, RF conformance or certification, connected-procedure interoperability, universal antenna-delay calibration, certified distance accuracy, or production power performance. The reported PBR/RTT ranges remain engineering measurements that require calibration and validation for each board, antenna, placement and enclosure.

### BLE Security and Privacy RC Follow-up - 2026-07-11 (v1.0.0-rc1)

The release candidate adds asynchronous Numeric Comparison accept/reject, mutual
and both one-way LE Secure Connections OOB modes, and host privacy built around
a stable identity, a `d1(IR, 1, 0)`-derived local IRK, rotating RPAs, role-ordered
identity-key distribution, hardware AAR resolution, and retained bonded
reconnect. Security hardening also applies negotiated 7-16 octet encryption-key
sizes to derived, received, persisted, and restored keys; uses the Bluetooth
30-second SMP transaction timer and bounded single-peer repeated-attempt
backoff; and obtains security nonces, OOB records, and LL encryption material
only from fail-closed CRACEN entropy. Encrypted RX/TX paths retain exact
ciphertext, SN, and CCM counters across retransmission and authenticate
duplicates without advancing counters.

The final full hardware gate is
`measurements/two_board_release_gate_20260711_v1.0.0-rc1_full`. It ran for
1,093.872 seconds on XIAO nRF54L15 UID `761FDE87` and XIAO nRF54LM20A UID
`3377B9D6`; all 14 phases passed:

* both boards booted and reported `1.0.0-rc1`;
* ATT discovery/CCCD, MTU 247, DLE 251, and the 2M/1M/2M long-notification cycle;
* fresh Just Works pairing, bond save/load, encrypted reconnect, subscription,
  and write;
* matching Numeric Comparison value `157817`, authenticated traffic, retained
  reconnect, and a separate rejection run with no encryption or bond save;
* mutual OOB and both one-way OOB directions with encrypted bidirectional UART;
* RPA self-test and over-air rotation across two addresses;
* stable identities, IRK distribution, AAR bond resolution, changed RPAs, and
  encrypted reconnect without re-pairing;
* reset recovery, timed System OFF wake on both boards, and positive,
  silent-reflector-negative, and recovery Channel Sounding checks.

The canonical release build produced
`dist/nrf54l15clean-1.0.0-rc1-f6f3430c4342.tar.bz2`, size `27516689`, SHA-256
`f6f3430c43422c6e745e775c64076a12d95d3e51e4b6d0aa4bd991493f854b7f`.
Exact-archive compilation passed for Numeric Comparison, both OOB board roles,
RPA/active scanning, both public Channel Sounding roles, OpenThread, and Matter
on the applicable L15 and LM20 profiles.

This is two-board regression evidence for the documented single-link LE Secure
Connections/privacy surface, not complete Bluetooth Core conformance, PTS/BQB,
or product qualification. CSRK/signed writes, locally generated legacy bond-key
distribution, a multi-bond/controller privacy policy, an in-place stored-bond
security-upgrade hardware test, and broad cross-vendor negative testing remain
outside the claim.

The remainder of this document is retained as the pre-remediation audit record. Present-tense defect descriptions and priority tables below describe the audited baseline, not the corrected 1.0.0-rc1 source tree.

## Executive Summary

* Total files reviewed: **2,517 regular files** outside `.git` (2,538 tracked path entries and 21 symlinks), including 1,206 vendored `third_party` files, 114 core files, 15 variant files, 445 sketches, five linker scripts and two startup assembly files.
* Total functions reviewed: **10,355 function-like definitions were indexed and static-screened** repository-wide. Approximately **2,250 first-party/non-vendored definition candidates** received semantic triage; the 45 core implementation/startup units and the high-risk register-level HAL units received line-level review. Vendored protocol/crypto code was inventory- and integration-reviewed rather than re-proven algorithm by algorithm.
* Total definite issues: **65**.
* Total likely issues: **4**.
* Total needs-human-verification items: **2**.
* Most dangerous issue: the PDM wrapper can program twice the caller's allocated EasyDMA capacity and overwrite adjacent RAM; the cache wrapper is similarly dangerous because its supposed cache operations actually write RRAMC or ECB registers.
* Most likely board-breaking issue: IRQ enums and vector tables use incorrect peripheral IDs for UARTE/TWIM/SPIM/SAADC and several LM20 blocks. A correctly configured driver can therefore enable an unrelated interrupt while the real vector remains `Default_Handler`.
* Most likely low-power issue: System OFF entry does not implement the mandatory EasyDMA/HFXO shutdown sequence, and the LM20 path normally takes a WFI/software-reset fallback before attempting to start the board's populated LFXO.
* Most likely datasheet mismatch: `nrf54l15_hal_cache.h` uses `0x4004B000` and fabricated offsets although the cache is at `0xE0082000`; on L15 this is the header's fabricated non-secure RRAMC address (the PS exposes only the secure RRAMC base), and on LM20 it is ECB00.

The six default board profiles compile with the package-declared GCC 7-2017q4 toolchain. Source-local OpenThread and Matter stage probes also compile. That build success does **not** validate the register maps: 13 targeted examples exercising broken HAL surfaces also compile. Of 83 top-level examples compiled on an applicable XIAO target, 81 linked and two duplicate `SenseDelayRailRetentionProbe` copies failed with a strong-symbol collision. A release archive staged with the default exclusion list fails to compile OpenThread/Matter because `third_party/openthread-core` is omitted.

## Audit Scope And Method

The audit map covered:

* recursive file/type inventory, board/FQBN/menu discovery, conditional-compilation search, symbol/ISR/weak/global-object indexing, address/mask/magic-number search and first-party function indexing;
* both core trees, startup/vector tables, system initialization, linker scripts, syscall heap support and Arduino API implementations;
* every variant, board stanza, platform recipe, programmer recipe, package index and release/test script;
* the register-level HAL surface and its examples, including GPIO, GPIOTE, GRTC, TIMER, WDT, CLOCK/OSCILLATORS, RESET, REGULATORS/System OFF, SPIM, TWIM/TWIS, UARTE, SAADC, PWM, I2S, PDM, QDEC, NFCT, RADIO/BLE, CRACEN/RNG/KMU/TAMPC, RRAMC/MEMCONF/cache and board PMIC/external-flash integration;
* local builds in a separate Arduino hardware namespace so the installed 0.9.212 core could not shadow this checkout.

The static function count is intentionally described as an indexed screen, not a claim that 1,206 imported third-party files were manually re-audited line by line. Findings are limited to behavior supported by local code and local documentation.

## Coverage Map

| Area | Files reviewed | Chips / boards covered | Datasheet sections checked | Confidence | Notes |
|---|---:|---|---|---|---|
| Startup, CMSIS, vectors, reset | 14 | L15/L10/L05 map; LM20A/B; all boards | Both PS instantiation tables; L15 ch. 5.8; LM20 ch. 5.8 | High | Peripheral-ID IRQ rule cross-checked against vector slots and bases. No standalone errata supplied. |
| Linker, memory, heap/startup ABI | 9 | L15 boards; XIAO LM20A/B | Memory maps, RRAM/RAM chapters | High | All five scripts and `_sbrk` inspected; produced ELF symbols checked. |
| GPIO, GPIOTE, pin routing | 24 | All six FQBNs and five variants | GPIO/GPIOTE chapters and pin capability tables | High | XIAO schematics checked; other board schematics absent. |
| Clock, reset, GRTC and power | 18 | L15 and LM20 cores; all boards | System ON/OFF, CLOCK/OSCILLATORS, RESET, GRTC | High | Runtime clock rules and System OFF sequence checked. Wake timing still needs hardware confirmation. |
| UARTE / Arduino `Serial` | 6 plus headers/examples | All boards | UARTE instance/config/EasyDMA/register tables | High | Pin-instance routing, event offsets and API modes checked. |
| SPIM / Arduino `SPI` | 6 plus flash integration | All boards | SPIM instance/frequency/register tables | High | Source clocks and shared-instance ownership checked. |
| TWIM/TWIS / Arduino `Wire` | 8 plus examples | All boards | TWIM/TWIS transaction and register chapters | High | Empty transaction and error-source behavior checked. |
| SAADC, PWM, PDM, I2S, QDEC, WDT | 17 implementation units | L15 and LM20 where instantiated | Corresponding peripheral chapters/register tables | High | EasyDMA units/lifetimes and instance availability checked. |
| NFCT | 3 | L15 and LM20 | NFCT register tables and instantiation IDs | Medium | Polling map matches; interrupt API/vector integration does not. |
| RADIO/BLE | 90+ first-party units plus examples | L15 and LM20 | RADIO task/event/register chapter; clock integration | Medium | Register-level periodic helper deeply checked; complete BLE conformance needs RF interoperability tests. |
| CRACEN, RNG, KMU, TAMPC | 12 plus generated headers | L15 and LM20 | Security subsystem chapters | High | LM20 copied type layout is materially incompatible. Matter-standard requirements were not supplied. |
| RRAMC, cache, MEMCONF, EEPROM/Preferences | 12 | L15 and LM20 | Cache, RRAMC, MEMCONF, TrustZone maps | High | Address maps and buffering rules checked. Non-secure configuration is latent/unadvertised. |
| VPR/RISC-V, IPC/DPPI | linker/startup/HAL integration | L15 boards; LM20 menus | Local memory/instantiation tables | Medium | Builds pass with VPR on/off; no VPR firmware/hardware execution was available. |
| Board variants and onboard devices | 15 variant files, `boards.txt`, docs/schematics | Six FQBNs | Pin tables, XIAO schematics, nPM1300 PS | High for XIAO; Low otherwise | HOLYIOT/generic/DK schematics were not supplied. |
| Build, release, upload and package metadata | 3 platform files, 3 indexes, 20+ scripts | Six FQBNs | Repository recipes and compiler output | High | JSON parsed; archive staging and upload recipes reproduced locally. |
| Examples and public APIs | 445 sketches; all core APIs indexed | Applicable XIAO target plus six-board smoke matrix | Arduino behavior inferred from local headers/docs and implementation | Medium | 83 top-level examples and 13 targeted HAL examples compiled; not all 445 sketches on all six boards. |
| OpenThread, Matter, Zigbee and imported libraries | 1,206 vendored plus integration wrappers | L15 and LM20 build targets | Local code/docs only | Low to Medium | Build/integration audit only; no external protocol specifications or radios-in-the-loop. |

## Findings

### FINDING-001: Peripheral IRQ numbers and vector slots use the wrong hardware IDs

Severity: Critical  
Confidence: High  
Status: Definite  
Affected chips/boards: All L15 boards and XIAO LM20A/B; exact affected peripherals differ by chip  
Affected files: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/cmsis.h`; both `startup_*.S`; `cores/nrf54lm20b/cmsis.h`; serial/Wire/SAADC users  
Affected functions: Vector tables; `HardwareSerial::begin`; Wire target interrupt setup; SAADC interrupt paths; LM20 clock interrupt users  
Peripheral/system block: NVIC/vector table, UARTE, SPIM/TWIM, SAADC, CLOCK

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/cmsis.h`
* Line(s): 99-108, 121; corresponding slots in `startup_nrf54l15.S:48-112` and `startup_nrf54lm20b.S:48-119`
* Code summary: Serial20/21 are assigned IRQ 140/141 or 149/150 rather than IDs 198/199; SAADC is 157 rather than 213. LM20 also reuses L15 AAR/CCM/ECB/SERIAL00 IDs and CLOCK 261 although LM20 uses 74/75/77 and 270. Real slots remain `Default_Handler`.

Datasheet / errata evidence:

* Document: `Nordic_nRF54L15_Datasheet_v1.0.pdf`; `nRF54LM20A_nRF54LM20B_Datasheet_v1.0.pdf`
* Section / table / page: Both PS §8.1.10 and peripheral instantiation tables; L15 serial IDs 198/199 and SAADC 213; LM20 AAR/CCM/ECB/SERIAL00 IDs 74/75/77 and CLOCK 270
* Requirement: A peripheral with a single interrupt uses the interrupt number equal to its peripheral ID.

Why this is a problem:

The driver enables an unrelated NVIC line. The real event can enter `Default_Handler`, while a different block may spuriously call the intended ISR. This breaks asynchronous Serial, target Wire, SAADC and LM20 clock behavior and can hang the system.

How to reproduce or test:

Enable one affected peripheral interrupt, trigger its documented event, and inspect `IPSR`/NVIC pending bits. A static ELF vector dump also shows handler symbols at the wrong indices.

Suggested fix:

Generate chip-specific IRQ enums and vector tables directly from each PS/SVD, place handlers at IDs 198/199/213 and the LM20-specific IDs, and add compile-time assertions tying base-address ID fields to IRQ constants.

Patch sketch, if safe:

Replace copied IRQ tables rather than applying isolated numeric edits; add weak handlers for every exposed interrupt API.

Risk of fix:

High integration risk because existing code may accidentally depend on the wrong names; the hardware mapping itself is unambiguous.

Related findings: FINDING-008, FINDING-067

### FINDING-002: Cache operations target RRAMC or ECB instead of the cache controller

Severity: Critical  
Confidence: High  
Status: Definite  
Affected chips/boards: All  
Affected files: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_cache.h`  
Affected functions: All `Cache::*` methods  
Peripheral/system block: CPU cache, RRAMC, ECB

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_cache.h`
* Line(s): 16-165
* Code summary: The wrapper uses base `0x4004B000` and fabricated task/status offsets. The L15 header labels that address as non-secure RRAMC although the PS lists only secure RRAMC at `0x5004B000`; its copied offsets mirror RRAMC configuration/clear/commit fields. On LM20 the address belongs to ECB00.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: Cache §4.2.3 (L15 pp. 29-32; LM20 corresponding section); L15 RRAMC §4.2.6 pp. 47-58; instantiation maps
* Requirement: ICACHE registers are in the CPU system-control space at `0xE0082000`; `0x4004B000` is not a cache instance.

Why this is a problem:

Calling `Cache::enable`, invalidate or clean can fault on an inaccessible address, trigger unrelated ECB behavior on LM20, or wait forever on a nonexistent status bit. If the fabricated L15 alias is made accessible by an external security configuration, the mirrored offsets can alter flash-write controls. None of these accesses performs cache maintenance.

How to reproduce or test:

Run `CacheDmaCoherence` under a debugger and watch accesses to `0x4004B000`; compare RRAMC/ECB state before and after the nominal cache call.

Suggested fix:

Delete the invented map and implement the documented ICACHE register interface separately per CPU, including required barriers.

Patch sketch, if safe:

No small safe patch: regenerate definitions from the CPU/cache chapter and unit-test every operation against a hardware debugger.

Risk of fix:

Medium; consumers may currently avoid the API, but correct cache maintenance affects DMA coherency.

Related findings: FINDING-029, FINDING-066

### FINDING-003: PDM EasyDMA count is doubled and can overwrite caller memory

> **2026-07-12 revalidation correction:** This finding is an audit false
> positive. Nordic's unified-byte DMA contract requires an `int16_t` element
> count to be multiplied by `sizeof(int16_t)` before writing `MAXCNT`. See the
> correction and connected-board guard evidence at the top of this report.
> The original finding text below is retained only as audit history.

Severity: Critical  
Confidence: High  
Status: Definite  
Affected chips/boards: L15 and LM20; especially microphone examples/boards  
Affected files: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_crypto_analog.inc`  
Affected functions: PDM start/capture setup around `Pdm::start`  
Peripheral/system block: PDM EasyDMA

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_crypto_analog.inc`
* Line(s): 1889-1911
* Code summary: The API accepts a sample count, computes `bytes = sampleCount * 2`, then writes that byte count to `SAMPLE.MAXCNT`. A 256-element `int16_t` example buffer (512 bytes) therefore authorizes a 1,024-byte DMA transfer.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 §8.14.4 p. 400 Table 47; LM20 §8.14.4 p. 394 Table 45
* Requirement: PDM allocation size in bytes is `SAMPLE.MAXCNT * 2`.

Why this is a problem:

PDM can overwrite adjacent globals, heap objects or stack data. This is deterministic memory corruption, not a mere sample-count mismatch.

How to reproduce or test:

Place guard words immediately after a 256-sample buffer, capture one block and inspect the guards with DWT watchpoints or ASAN-equivalent host modelling.

Suggested fix:

Program `MAXCNT = sampleCount`, validate the hardware field range, and keep the public count explicitly in 16-bit samples.

Patch sketch, if safe:

`regs->SAMPLE.MAXCNT = sampleCount;` with overflow/range checking and a regression test using guard regions.

Risk of fix:

Low for the register correction; callers that compensated for the bug would receive fewer samples.

Related findings: FINDING-058, FINDING-059

### FINDING-004: LM20 global SPI and Serial1 claim the same shared peripheral instance

Severity: Critical  
Confidence: High  
Status: Definite  
Affected chips/boards: XIAO nRF54LM20A/B  
Affected files: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/SPI.cpp`; `cores/nrf54lm20b/HardwareSerial.cpp`  
Affected functions: Global-object construction; `SPIClass::begin`; `HardwareSerial::begin`  
Peripheral/system block: Shared SERIAL instance 21

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/SPI.cpp`; `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/HardwareSerial.cpp`
* Line(s): `SPI.cpp:137`; `HardwareSerial.cpp:1123`
* Code summary: Global `SPI` uses `NRF_SPIM21` and global `Serial1` uses `NRF_UARTE21`; both resolve to base `0x500C7000`. Each `begin()` disables/reprograms the shared register bank without shared ownership state.

Datasheet / errata evidence:

* Document: `nRF54LM20A_nRF54LM20B_Datasheet_v1.0.pdf`
* Section / table / page: SPIM §8.19.5 p. 587 and instance table
* Requirement: Same-ID serial peripherals share resources/registers; all same-ID alternatives must be disabled and their shared configuration coordinated.

Why this is a problem:

`Serial1.begin(); SPI.begin();` silently destroys the UART configuration while the Serial object still believes it is initialized; reverse order breaks SPI. The two advertised global APIs cannot operate concurrently.

How to reproduce or test:

Start Serial1 loopback, call `SPI.begin()`, then transmit and inspect ENABLE/config registers and wire output.

Suggested fix:

Assign SPI to a route-valid unused instance after a full resource audit, or introduce a common serial-instance arbiter that rejects conflicting claims.

Patch sketch, if safe:

Do not simply change the instance number without validating XIAO pins, IRQs and other globals; add a compile-time board resource table.

Risk of fix:

Medium because instance reassignment affects pins, vectors and compatibility.

Related findings: FINDING-001, FINDING-010

### FINDING-005: nPM1300 voltage lookup indices are written as hardware voltage codes

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: XIAO nRF54LM20A/B and any user of this PMIC wrapper  
Affected files: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/npm1300.cpp`  
Affected functions: BUCK/LDSW voltage setters and board rail setup  
Peripheral/system block: nPM1300 PMIC

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/npm1300.cpp`
* Line(s): 146-148, 376-381, 449-455, 514-529, 545-549
* Code summary: A four-element supported-voltage array returns position 0..3 and that position is written directly to voltage-selection registers. Requesting 3.3 V writes code 3, which represents 1.3 V.

Datasheet / errata evidence:

* Document: `nPM1300_datasheet.pdf`; `XIAO_nRF54LM20A_Schematic.pdf`
* Section / table / page: nPM1300 §6.3.8.9-.12 pp. 65-67; §6.4.3.11-.12 pp. 79-80; XIAO rail sheet
* Requirement: Codes 0..23 encode 1.0 V through 3.3 V in 0.1 V steps; LSOUT1 supplies the board's IMU/MIC_3V3 rail.

Why this is a problem:

Board peripherals can be undervolted by up to 2 V while the API reports success, causing non-deterministic sensor/microphone failures and potentially invalid bus levels.

How to reproduce or test:

Request 3.3 V, read back the register and measure LSOUT1 with a DMM.

Suggested fix:

Encode `round((millivolts - 1000) / 100)` after bounds/step validation, or store explicit register codes in the lookup table.

Patch sketch, if safe:

Use `{1800, 1800-code}, ...` value/code pairs and verify readback before enabling the rail.

Risk of fix:

Medium: correcting a live rail can change behavior immediately; verify every board consumer's allowed voltage first.

Related findings: FINDING-021

### FINDING-006: Runtime PLL switching violates the documented clock rule

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: All L15 and LM20 boards  
Affected files: both `system_*.c`; both `main.cpp`; L15 `SPI.cpp`; both `wiring_time.c`; HAL timebase  
Affected functions: CPU-frequency setters, idle scaling enter/exit, SPI transaction boost/restore  
Peripheral/system block: CLOCK/PLL, CPU timing

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/system_nrf54l15.c`
* Line(s): 255-324; `SPI.cpp:498-515`; `wiring_time.c:90-99`; LM20 `system_nrf54lm20b.c:156-194`, `main.cpp:49-54`
* Code summary: Public and internal paths change PLL frequency while the system is running, including around idle and high-speed SPI.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 §5.5.3 p. 90; LM20 §5.5.3 p. 97
* Requirement: System frequency selection must be made during startup; changing it while running has undefined behavior and can cause malfunction.

Why this is a problem:

Clock-sensitive peripherals, flash access, delay calibration and CPU execution can malfunction during apparently ordinary loop idle or SPI operations.

How to reproduce or test:

Trace PLL writes after reset and stress SPI plus interrupts at both menu frequencies; the prohibited write is statically visible regardless of observed symptoms.

Suggested fix:

Select the required frequency once in `SystemInit`, remove runtime setters/idle scaling, and derive peripheral divisors from fixed documented sources.

Patch sketch, if safe:

Make frequency-selection APIs reject changes after boot; remove the SPI boost path.

Risk of fix:

Medium: timing and power profiles change, but retaining undefined hardware behavior is not acceptable.

Related findings: FINDING-007, FINDING-011, FINDING-012

### FINDING-007: LM20 128 MHz board menu changes only compile-time constants

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: XIAO nRF54LM20A/B with `128 MHz` selected  
Affected files: `hardware/nrf54l15clean/nrf54l15clean/boards.txt`; `cores/nrf54lm20b/system_nrf54lm20b.c`  
Affected functions: `SystemInit`, delay/timing users  
Peripheral/system block: CPU clock/build configuration

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/boards.txt`; `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/system_nrf54lm20b.c`
* Line(s): `boards.txt:212-217`; `system_nrf54lm20b.c:132-154`
* Code summary: The menu defines `F_CPU=128000000` and a PLL macro, but LM20 `SystemInit` leaves the default 64 MHz selection and never consumes that macro.

Datasheet / errata evidence:

* Document: `nRF54LM20A_nRF54LM20B_Datasheet_v1.0.pdf`
* Section / table / page: §5.5.3 p. 97
* Requirement: PLL operating frequency is selected by the documented startup configuration.

Why this is a problem:

Software timing, baud/divisor calculations and reported CPU frequency assume twice the actual clock.

How to reproduce or test:

Build the 128 MHz profile and measure a cycle-counted GPIO pulse or read PLL selection under a debugger.

Suggested fix:

Apply the compile-time selection once in LM20 `SystemInit`, matching the L15 boot-only pattern.

Patch sketch, if safe:

Add a compile-time 64/128 branch before normal runtime starts; add a measured-clock build test.

Risk of fix:

Medium because latent timing workarounds may depend on the current mismatch.

Related findings: FINDING-006

### FINDING-008: GPIO interrupts use the wrong GPIOTE instance and LM20 P3 is rejected

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: All; P0 interrupts on both chips, P3 on LM20  
Affected files: both `wiring_digital.c`; `nrf54l15_regs.h`; HAL GPIO support  
Affected functions: `attachInterrupt`, `detachInterrupt`, pin decode/configure helpers  
Peripheral/system block: GPIO/GPIOTE/NVIC

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/wiring_digital.c`; LM20 copy
* Line(s): interrupt mode mapping 95-106 and GPIOTE setup 332-418; HAL pin decode `nrf54l15_regs.h:13-15`, `nrf54l15_hal_support.cpp:39-49`; LM20 reverse pin helpers 309-318
* Code summary: Both cores hardwire GPIOTE20 for Arduino interrupts, including P0. Generic/HAL helpers omit P3 entirely.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 GPIOTE §8.9.3-.5 pp. 286-287; LM20 GPIOTE §8.10 pp. 304-305
* Requirement: GPIOTE20 serves P1 (and LM20 P3); GPIOTE30 serves P0. L15 P2 lacks GPIOTE; LM20 exposes P3.

Why this is a problem:

P0 callbacks do not receive the correct event, while valid LM20 P3 interrupt-capable pins cannot be represented. Wrong-vector defects compound the failure.

How to reproduce or test:

Attach a RISING callback to a P0 board pin, toggle it externally and inspect GPIOTE30 versus GPIOTE20 events.

Suggested fix:

Dispatch by port to the documented GPIOTE instance, provide both vector handlers and add chip-specific P3 decode support.

Patch sketch, if safe:

Introduce a `gpioteForPort(port)` table generated per chip and reject unsupported P2 interrupts explicitly.

Risk of fix:

Medium because channels and callback state must be split safely across two instances.

Related findings: FINDING-001, FINDING-064, FINDING-068

### FINDING-009: Several board UART routes are bound to impossible UARTE instances

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: HOLYIOT-25007, generic 36-pad module, PCA10156 DK  
Affected files: `boards.txt`; L15 `HardwareSerial.cpp`; affected variants  
Affected functions: Global Serial/Serial1 construction and `begin`  
Peripheral/system block: UARTE pin routing

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/boards.txt`; `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/HardwareSerial.cpp`
* Line(s): `boards.txt:321,595,729,778-782`; `HardwareSerial.cpp:1110-1120`; affected pin tuples in each variant header
* Code summary: 25007/generic default header routing sends P2.08/P2.07 through UARTE20 although that pair supports UARTE00/21. The DK default P1 VCOM route is valid, but its alternate P0.00/P0.01 menu removes the swap and leaves the globals on UARTE20/21 rather than P0's UARTE30. XIAO L15's UARTE21 P2 route is valid and is not included.

Datasheet / errata evidence:

* Document: `Nordic_nRF54L15_Datasheet_v1.0.pdf`
* Section / table / page: §10.1.1 pp. 859-860 and pin rows around p. 864; UARTE instance table §8.25 p. 721
* Requirement: UARTE pin routing is instance/port constrained; P2.07/P2.08 support UARTE00/21 and P0 belongs to UARTE30.

Why this is a problem:

The objects initialize successfully in software but signals cannot appear on the selected pads, silently breaking default Serial APIs on advertised boards.

How to reproduce or test:

Build each affected FQBN, call `Serial.begin`/`Serial1.begin`, transmit a pattern and scope the declared TX pad.

Suggested fix:

Correct board-specific instance selection and ensure each resulting IRQ vector exists.

Patch sketch, if safe:

Use a per-board `(instance, RX, TX)` resource tuple with static assertions against a generated route table.

Risk of fix:

Medium because changing instances can collide with SPI/Wire and changes interrupt assignments.

Related findings: FINDING-001, FINDING-004, FINDING-010

### FINDING-010: Runtime pin setters accept physically impossible peripheral routes

Severity: Medium  
Confidence: High  
Status: Definite  
Affected chips/boards: All; readily visible on both XIAO variants  
Affected files: both `SPI.cpp`, `Wire.cpp`, `HardwareSerial.cpp` and headers  
Affected functions: `SPIClass::setPins`, `TwoWire::setPins`, Serial pin setters/constructors  
Peripheral/system block: SPIM/TWIM/UARTE routing

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/Wire.cpp` and peer core files
* Line(s): L15 `SPI.cpp:188`, `Wire.cpp:471`, `HardwareSerial.cpp:490`; LM20 `SPI.cpp:181`, `Wire.cpp:472`, `HardwareSerial.cpp:499`; their begin/configuration callers
* Code summary: Setters decode a generic GPIO number but do not validate it against the fixed peripheral instance. Examples include assigning P0 pins to a P1 TWIM22 or P1 pins to TWIM30, and arbitrary pins to P2-only SPIM00.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 SPIM §8.19 p. 588, TWIM §8.23 pp. 658-659, UARTE §8.25 p. 721; LM20 SPIM §8.19 pp. 588-590, TWIM §8.24 register instances beginning p. 697, UARTE §8.26 instances pp. 760-761
* Requirement: Each instance supports only the listed GPIO port(s).

Why this is a problem:

Arduino APIs report success but produce no bus activity, and subsequent debugging is obscured because register configuration appears plausible.

How to reproduce or test:

Call each setter with a valid GPIO on an unsupported port, call `begin`, and verify it returns success despite no waveform.

Suggested fix:

Validate `(peripheral ID, port, role)` before mutating state and return a failure-capable result or preserve the previous valid route.

Patch sketch, if safe:

Centralize a chip-specific route table and test every board alias plus setter permutation.

Risk of fix:

Low to medium; sketches relying on silent acceptance will begin receiving errors.

Related findings: FINDING-004, FINDING-008, FINDING-009, FINDING-064

### FINDING-011: L15 SPIM00 frequency calculation uses the wrong source and advertises impossible low rates

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: All L15 boards  
Affected files: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/SPI.cpp`; `SPI.h`  
Affected functions: SPI settings conversion and `SPIClass::beginTransaction`  
Peripheral/system block: SPIM00

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/SPI.cpp`
* Line(s): 74-83, 133-153, 160-161, 526-530; `SPI.h:28-33`
* Code summary: Divisors are calculated from current CPU frequency although both globals use SPIM00. At the 64 MHz default, requested clocks are doubled. The API advertises 125/250/500 kHz even though the clamped maximum divisor of 126 on a fixed 128 MHz source cannot go below about 1.016 MHz.

Datasheet / errata evidence:

* Document: `Nordic_nRF54L15_Datasheet_v1.0.pdf`
* Section / table / page: SPIM instance/configuration table §8.19 p. 588
* Requirement: SPIM00 has a fixed 128 MHz source and documented prescaler/divisor range.

Why this is a problem:

Devices see a clock different from `SPISettings`, sometimes more than eight times the requested maximum. Slow peripherals can fail or be electrically overclocked.

How to reproduce or test:

At the default 64 MHz profile request 1 MHz and 125 kHz, transfer continuously, and measure SCK.

Suggested fix:

Use the fixed SPIM00 source in the divisor calculation, clamp only to documented achievable rates, and expose failure/nearest-rate behavior rather than silently overclocking.

Patch sketch, if safe:

Calculate `ceil(128 MHz / requested)` within the documented range and add measured-frequency tests at every public constant.

Risk of fix:

Medium; correcting common settings changes existing bus timing and low-speed support may require another instance.

Related findings: FINDING-004, FINDING-006, FINDING-012

### FINDING-012: LM20 high-speed SPI writes a reserved offset and never programs PRESCALER

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: XIAO nRF54LM20A/B, including external-flash/QSPI paths  
Affected files: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/SPI.cpp`  
Affected functions: SPI clock configuration and high-speed transaction setup  
Peripheral/system block: LM20 SPIM

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/SPI.cpp`
* Line(s): 20-21, 420-432
* Code summary: The high-speed path writes offset `0x524`, which is reserved for this block, and never writes documented `PRESCALER` at `0x52C`. The reset divisor remains 64, so a nominal high-speed bus can remain near 2 MHz.

Datasheet / errata evidence:

* Document: `nRF54LM20A_nRF54LM20B_Datasheet_v1.0.pdf`
* Section / table / page: SPIM overview/register table §8.19 pp. 588-590
* Requirement: Frequency is controlled by `PRESCALER` at `+0x52C` from the fixed 128 MHz source; reserved registers must retain reset value.

Why this is a problem:

Clock selection is ineffective and writes a reserved register. Board flash timing/performance claims therefore do not match hardware behavior.

How to reproduce or test:

Request each high-speed setting, read `PRESCALER`, and scope SCK.

Suggested fix:

Use the documented LM20 register layout and a chip-specific divisor function; remove all reserved writes.

Patch sketch, if safe:

Replace the raw offset constants with generated `NRF_SPIM_Type` fields and static offset assertions.

Risk of fix:

Medium because corrected flash speed may expose signal-integrity constraints.

Related findings: FINDING-004, FINDING-006, FINDING-011

### FINDING-013: Low-baud Serial transmits directly from arbitrary caller memory via EasyDMA

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: All  
Affected files: both `HardwareSerial.cpp`  
Affected functions: `HardwareSerial::write(const uint8_t *, size_t)` and low-baud transmit path  
Peripheral/system block: UARTE EasyDMA

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/HardwareSerial.cpp`; LM20 peer
* Line(s): L15 845-880, 1017-1045; LM20 854-889 and 1033 onward
* Code summary: One path assigns the user's pointer directly to EasyDMA rather than copying to the owned DMA buffer. The API accepts flash-resident string literals, short-lived stack arrays and unaligned pointers.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 UARTE §8.25.2 p. 714 and TXD.PTR §8.25.13.43; LM20 §8.26.2 p. 753 and PTR around p. 788
* Requirement: EasyDMA buffers must reside in accessible Data RAM, remain valid for the transfer and satisfy pointer/alignment constraints.

Why this is a problem:

Serial writes can HardFault, read invalid memory, or transmit corrupted data depending on pointer origin/lifetime.

How to reproduce or test:

At a baud selecting this path, write a string literal, an intentionally unaligned buffer and a stack buffer whose frame is reused while DMA is active.

Suggested fix:

Always stage public `Print` data into owned EasyDMA-safe storage or block until a caller buffer has completed after validating its memory region/alignment.

Patch sketch, if safe:

Unify all write paths through the existing owned TX queue; do not retain caller pointers after return.

Risk of fix:

Medium: throughput and blocking semantics change.

Related findings: FINDING-014, FINDING-020, FINDING-058

### FINDING-014: Serial end disables UARTE before TXSTOPPED

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: All  
Affected files: both `HardwareSerial.cpp`  
Affected functions: `HardwareSerial::end`  
Peripheral/system block: UARTE shutdown

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/HardwareSerial.cpp`; LM20 peer
* Line(s): L15 451-466; LM20 460-475
* Code summary: Shutdown requests stop but disables/reconfigures the block without waiting for `TXSTOPPED`.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 UARTE shutdown sequence §8.25.11 p. 720; LM20 §8.26.11 p. 759
* Requirement: After `STOPTX`, software must wait for `TXSTOPPED` before disabling UARTE or reusing resources.

Why this is a problem:

The last bytes can be truncated and EasyDMA may still own memory when pins/registers are repurposed.

How to reproduce or test:

Queue a long transfer, immediately call `end`, and inspect the final bytes and `TXSTOPPED` ordering with a logic analyzer/debug trace.

Suggested fix:

Clear `TXSTOPPED`, issue `STOPTX`, wait with a bounded timeout, then disable and clear pending events/IRQs.

Patch sketch, if safe:

Use a common quiesce helper on `end`, re-`begin` and System OFF entry.

Risk of fix:

Low to medium; `end` can block until timeout on a hardware fault.

Related findings: FINDING-013, FINDING-020

### FINDING-015: UARTE00 baud programming assumes the 16 MHz serial instances

Severity: Medium  
Confidence: High  
Status: Definite  
Affected chips/boards: Custom users constructing `HardwareSerial` on UARTE00; no current default global  
Affected files: both `HardwareSerial.cpp`  
Affected functions: baud-to-register conversion and `begin`  
Peripheral/system block: High-speed UARTE00

Repository evidence:

* File: both `hardware/nrf54l15clean/nrf54l15clean/cores/*/HardwareSerial.cpp`
* Line(s): 105-152
* Code summary: All instances use fixed BAUDRATE presets for a 16 MHz peripheral clock. There is no UARTE00/source-frequency branch.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 §8.25.1 p. 714 and instance p. 721; LM20 §8.26.1 p. 753 and pp. 760-761
* Requirement: UARTE00 is the high-speed CPU-scaling instance and BAUDRATE must be calculated from its actual `fPCLK`; instances 20+ use 16 MHz.

Why this is a problem:

A valid custom UARTE00 object runs at a multiple of the requested baud.

How to reproduce or test:

Construct a UARTE00 object on a valid P2 route, request 115200 and measure TX bit width at each CPU profile.

Suggested fix:

Calculate UARTE00 BAUDRATE from its fixed-at-boot PCLK and retain presets only for 16 MHz instances.

Patch sketch, if safe:

Branch on peripheral ID and use the documented formula with 64-bit rounding.

Risk of fix:

Low for default boards; medium for custom sketches that compensated manually.

Related findings: FINDING-006, FINDING-009

### FINDING-016: Advertised Serial frame formats always configure eight data bits

Severity: Medium  
Confidence: High  
Status: Definite  
Affected chips/boards: All  
Affected files: both `Arduino.h`; both `HardwareSerial.cpp`  
Affected functions: `HardwareSerial::begin(baud, config)`  
Peripheral/system block: UARTE CONFIG

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/Arduino.h`; `HardwareSerial.cpp`
* Line(s): `Arduino.h:76-99`; `HardwareSerial.cpp:155-188`; LM20 copies
* Code summary: Public constants expose 5-, 6- and 7-bit modes, but the implementation unconditionally selects 8-bit frame size.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 UARTE §8.25.9 p. 719 and CONFIG register p. 743; LM20 §8.26.9 pp. 758-759 and CONFIG register p. 783
* Requirement: `FRAMESIZE` must match the selected data-bit count.

Why this is a problem:

The API silently emits/receives the wrong wire format, breaking protocols that use non-8-bit frames.

How to reproduce or test:

Open `SERIAL_7E1`, transmit `0x7f`, and decode the frame on a logic analyzer.

Suggested fix:

Map every public config constant to supported `FRAMESIZE`, parity and stop fields; reject configurations the instance cannot represent.

Patch sketch, if safe:

Add table-driven config conversion tests for all constants in `Arduino.h`.

Risk of fix:

Low.

Related findings: FINDING-017

### FINDING-017: Serial uses a wrong DMA-ready offset and LM20 writes reserved registers

Severity: Medium  
Confidence: High  
Status: Definite  
Affected chips/boards: All for the event offset; LM20 additionally for reserved writes  
Affected files: both `HardwareSerial.cpp`  
Affected functions: DMA-ready event handling and LM20 `begin`  
Peripheral/system block: UARTE register map

Repository evidence:

* File: both `hardware/nrf54l15clean/nrf54l15clean/cores/*/HardwareSerial.cpp`
* Line(s): constant definitions around line 26; LM20 begin 400-405
* Code summary: `U_EVENTS_DMA_TX_READY` is defined as `0x164` instead of `0x16C`. LM20 also writes offsets `0x51C`/`0x520`, which are reserved between ENABLE and BAUDRATE.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 UARTE §8.25.13 register overview p. 721; LM20 §8.26.13 register overview p. 760; both list EVENTS_DMA.TX.READY at `+0x16C`
* Requirement: DMA.TX.READY is `+0x16C`; reserved locations must retain reset value.

Why this is a problem:

The intended event is not observed/cleared, and reserved writes have undefined forward-compatibility behavior.

How to reproduce or test:

Set a watchpoint on `base+0x164` and `base+0x16C` during TX; log LM20 writes during `begin`.

Suggested fix:

Use generated register structs/offset assertions and remove reserved writes.

Patch sketch, if safe:

Replace raw offsets with typed fields from a chip-correct header.

Risk of fix:

Low.

Related findings: FINDING-001, FINDING-014

### FINDING-018: An empty Wire transaction sends a destructive zero data byte

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: All  
Affected files: both `Wire.cpp`; scanner/probe examples  
Affected functions: `TwoWire::endTransmission` and empty-address probe path  
Peripheral/system block: TWIM

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/Wire.cpp`; LM20 copy
* Line(s): 530-567
* Code summary: When the TX length is zero, code creates a byte with value zero and performs a one-byte write. Standard scanner-style address probes therefore write to every acknowledging target.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 TWIM transaction §8.23.4 pp. 653-654; LM20 §8.24.4 p. 692
* Requirement: A transmit transaction sends the programmed EasyDMA byte count; zero-length address probing must not be emulated by adding payload.

Why this is a problem:

Scanning can change a device register pointer, issue a command, clear state or write configuration. It is an externally destructive API mismatch.

How to reproduce or test:

Connect an I2C target that records payloads, call `beginTransmission(addr); endTransmission();`, and observe `0x00`.

Suggested fix:

Implement a documented address-only probe sequence if the hardware supports it, or return an explicit unsupported/error result without transmitting user data.

Patch sketch, if safe:

Separate `probe(address)` from normal `endTransmission` and regression-test a zero-length call against a bus analyzer.

Risk of fix:

Medium; scanners depend on current acknowledgement behavior, so a verified hardware sequence is required.

Related findings: FINDING-019

### FINDING-019: Wire ANACK and DNACK masks are shifted and error codes are wrong

Severity: Medium  
Confidence: High  
Status: Definite  
Affected chips/boards: All  
Affected files: both `Wire.cpp`  
Affected functions: error-source decode in transfer completion  
Peripheral/system block: TWIM ERRORSRC

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/Wire.cpp`; LM20 copy
* Line(s): 64-66, 183-193
* Code summary: Code defines ANACK as bit 0/value 1 and DNACK as bit 1/value 2. Hardware uses OVERRUN bit 0, ANACK bit 1/value 2 and DNACK bit 2/value 4.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 TWIM §8.23.10.25; LM20 TWIM §8.24.10.25
* Requirement: Error-source bits have the documented OVERRUN/ANACK/DNACK positions and are W1C.

Why this is a problem:

Address NACK, data NACK and overrun are confused or missed, so Arduino return codes and recovery behavior are wrong.

How to reproduce or test:

Exercise a missing address, a target that NACKs a data byte and an induced overrun; compare raw ERRORSRC to `endTransmission` result.

Suggested fix:

Use generated masks, decode before W1C clearing, and map each source to the documented Arduino error result.

Patch sketch, if safe:

Replace literals with the chip header symbols and add three negative-path tests.

Risk of fix:

Low.

Related findings: FINDING-018

### FINDING-020: System OFF entry omits mandatory DMA quiescence and HFXO shutdown

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: All  
Affected files: both `wiring_time.c`; peripheral shutdown hooks  
Affected functions: `systemOffWakeReset`, `nrf54*l*_core_prepare_system_off`  
Peripheral/system block: REGULATORS/System OFF, EasyDMA, CLOCK

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/wiring_time.c`; LM20 peer
* Line(s): L15 779-795, 1015-1037; LM20 790-804, 1032 onward
* Code summary: Entry does not centrally stop/wait for every active EasyDMA peripheral, does not issue/wait for HFXO stop, and does not implement the full reset-reason preparation sequence before `SYSTEMOFF`.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 System OFF §5.2 p. 68; LM20 §5.2 pp. 69-70
* Requirement: All DMA transactions must finish, HFXO must be stopped, and documented reset/wakeup preparation must be completed before System OFF.

Why this is a problem:

Entry can fail, truncate transfers, retain excess current or wake/reset with stale state. The impact depends on which Arduino peripherals were active.

How to reproduce or test:

Start UARTE/PDM/SPIM DMA and HFXO, request System OFF, monitor events/current and read RESETREAS after wake.

Suggested fix:

Create an ownership-aware shutdown coordinator that rejects new DMA, stops each active channel, waits with bounded error handling, stops HFXO, clears/records reset causes and only then enters OFF.

Patch sketch, if safe:

No single register patch is safe; add per-driver `quiesceForSystemOff()` hooks and a verified ordered coordinator.

Risk of fix:

High integration risk, but required for a reliable low-power API.

Related findings: FINDING-013, FINDING-014, FINDING-021, FINDING-022

### FINDING-021: LM20 normally takes a WFI/software-reset fallback before starting its populated LFXO

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: XIAO nRF54LM20A/B  
Affected files: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/wiring_time.c`; XIAO schematic  
Affected functions: `enterSystemOffWakeReset`, `ensureSystemOffLfxoRunning`, LF clock selection  
Peripheral/system block: LFXO, GRTC, System OFF

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/wiring_time.c`
* Line(s): 140-170, 692-700, 776-805
* Code summary: Entry first checks whether LFXO is already running and immediately falls back to timed WFI plus AIRCR reset when it is not. The helper that starts LFXO is called only later, on the already-running branch. A stale comment says LM20B has no crystal.

Datasheet / errata evidence:

* Document: `XIAO_nRF54LM20A_Schematic.pdf`; `nRF54LM20A_nRF54LM20B_Datasheet_v1.0.pdf`
* Section / table / page: Schematic sheet with X1 on P1.20/XL1 and P1.21/XL2; PS §5.2 p. 70 and §5.7.2.1 p. 107
* Requirement: True System OFF powers down the core and uses the configured GRTC wake source; the board physically provides LFXO.

Why this is a problem:

The advertised API usually never writes `REGULATORS.SYSTEMOFF`, so current and reset semantics are System ON/WFI rather than System OFF.

How to reproduce or test:

On a default boot call `systemOffWakeReset`, measure current and inspect RESETREAS.OFF/GRTC after restart.

Suggested fix:

Call `ensureSystemOffLfxoRunning()` before testing the source, take the fallback only on a bounded start failure, and correct the board comment.

Patch sketch, if safe:

Reorder the helper call and add an instrumented cold-wake test; do not enter OFF unless GRTC compare synchronization succeeds.

Risk of fix:

Medium; the path will begin entering real cold-reset System OFF and expose startup/retention assumptions.

Related findings: FINDING-005, FINDING-020, FINDING-024, FINDING-025

### FINDING-022: Shared HAL System OFF hooks are disconnected on LM20

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: XIAO nRF54LM20A/B  
Affected files: `nrf54l15_hal_timebase.cpp`; LM20 `wiring_time.c`; board power guards  
Affected functions: PowerManager System OFF preparation and wake-timebase preparation  
Peripheral/system block: GRTC, RRAMC/retention, board QSPI/power

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_timebase.cpp`
* Line(s): 8-12, 245-252, 417-421; LM20 `wiring_time.c:530-570,1050-1052`
* Code summary: The shared HAL weak-links only `nrf54l15_core_*` names while the LM20 core exports `nrf54lm20b_core_*`; the pointers are null. LM20 board sleep code also tests L15 board macros, skipping board preparation.

Datasheet / errata evidence:

* Document: LM20 PS and XIAO schematic
* Section / table / page: System OFF §5.2 pp. 69-70; GRTC §8.11; external-flash/PMIC connections in schematic
* Requirement: Clock, memory-retention and active external-device state must be deliberately prepared before OFF; GRTC configuration must remain coherent.

Why this is a problem:

HAL callers bypass the only core paths intended to stop clocks, prepare wake state and disable retention. External flash/rails can also stay active.

How to reproduce or test:

Set breakpoints on the LM20 core hooks and invoke HAL PowerManager System OFF; they are not called.

Suggested fix:

Provide chip-neutral hook names or a selected adapter layer and use the correct LM20 board macro guards.

Patch sketch, if safe:

Export `nrf54_core_*` wrappers from each core and link them strongly in the matching build.

Risk of fix:

Medium because dormant shutdown code will begin running.

Related findings: FINDING-020, FINDING-021

### FINDING-023: `sd_power_system_off` never enters System OFF

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: All compatibility-library users  
Affected files: both `nrf52_compat.cpp`  
Affected functions: `sd_power_system_off`  
Peripheral/system block: nRF52 compatibility/power

Repository evidence:

* File: both `hardware/nrf54l15clean/nrf54l15clean/cores/*/nrf52_compat.cpp`
* Line(s): 24-31
* Code summary: The compatibility API loops on `WFI` and never writes the System OFF register.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: System OFF §5.2 and `REGULATORS.SYSTEMOFF` register
* Requirement: WFI in System ON is not System OFF; OFF requires the documented preparation and register write.

Why this is a problem:

Ported software expecting non-returning lowest-power state instead remains in System ON and can repeatedly wake on interrupts.

How to reproduce or test:

Call the function with a periodic interrupt active and measure current/control flow.

Suggested fix:

Route the shim to the verified core System OFF implementation or explicitly report unsupported status.

Patch sketch, if safe:

Do not add a raw register write without the FINDING-020 shutdown sequence.

Risk of fix:

High semantic change: callers will cold-reset on wake as originally expected.

Related findings: FINDING-020, FINDING-027

### FINDING-024: GRTC TIMEOUT and WAKETIME violate the required strict inequality

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: All  
Affected files: both `wiring_time.c`  
Affected functions: GRTC/System OFF wake setup  
Peripheral/system block: GRTC

Repository evidence:

* File: both `hardware/nrf54l15clean/nrf54l15clean/cores/*/wiring_time.c`
* Line(s): constants 34-35; L15 programming 658-662; LM20 651-655
* Code summary: `TIMEOUT=5` LFCLK cycles and `WAKETIME=4`; with the minimum one-cycle guard, 5 is not strictly greater than 4+1.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 GRTC §8.10.2 p. 298; LM20 GRTC §8.11.2 pp. 314-316
* Requirement: `TIMEOUT` must be greater than `WAKETIME + guard_time`, with guard time at least one LFCLK cycle.

Why this is a problem:

GRTC can return to sleep too close to a compare and fail to maintain the intended wake/event restoration window.

How to reproduce or test:

Stress the shortest supported wake intervals while tracing SYSCOUNTER active state and compare events.

Suggested fix:

Set TIMEOUT to at least six cycles, with additional measured margin for the actual restore path.

Patch sketch, if safe:

Change the constant only after adding a static assertion `TIMEOUT > WAKETIME + GUARD`.

Risk of fix:

Low; slightly more GRTC active time.

Related findings: FINDING-021, FINDING-025

### FINDING-025: GRTC compare is not synchronized and early wake restoration is not guaranteed

Severity: High  
Confidence: Medium  
Status: Likely  
Affected chips/boards: All timed System OFF paths  
Affected files: both `wiring_time.c`; both `main.cpp`  
Affected functions: `programSystemOffWakeUs`, wake bootstrap path  
Peripheral/system block: GRTC/System OFF wake

Repository evidence:

* File: both `hardware/nrf54l15clean/nrf54l15clean/cores/*/wiring_time.c`; L15 `main.cpp`
* Line(s): L15 670-739; LM20 683-733; L15 `main.cpp:15,59-69`
* Code summary: Code delays instead of waiting for `RTCOMPARESYNC`/COMPARE and enters OFF after retry failure. The declared `nrf54l15_core_bootstrap_low_power_timebase` has no caller; constructors and `main` can run before lazy GRTC restoration. WAKETIME leaves only about 122 microseconds total.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 GRTC §8.10.2 pp. 298-299 and electrical §11.4.2; LM20 GRTC §8.11.2 pp. 314-316 and System OFF timing in §11.4.2
* Requirement: Wait for compare synchronization (or handle an already-fired COMPARE) before OFF, and restore required GRTC registers before the next compare after wake.

Why this is a problem:

A timed OFF can miss or mishandle the wake compare. Exact manifestation depends on startup latency and hardware state, hence Likely rather than Definite.

How to reproduce or test:

Instrument reset-to-first-restore latency, use minimum delays, and capture RTCOMPARESYNC/COMPARE across hundreds of cold wakes.

Suggested fix:

Wait on the documented event with a bounded timeout, abort OFF if COMPARE already fired, and invoke minimal GRTC restoration from the earliest safe startup point.

Patch sketch, if safe:

Add a startup assembly/C hook before constructors and a hardware-in-loop short-delay stress test.

Risk of fix:

High because moving initialization earlier changes startup ordering.

Related findings: FINDING-020, FINDING-024, FINDING-026

### FINDING-026: GRTC compare API enables channels even when disabled and ignores sleeping CCADD semantics

Severity: Medium  
Confidence: High  
Status: Definite  
Affected chips/boards: All HAL timebase users  
Affected files: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_timebase.cpp`  
Affected functions: Compare scheduling/configuration methods around lines 659-706  
Peripheral/system block: GRTC

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_timebase.cpp`
* Line(s): 659-706
* Code summary: The API writes CCH/CCADD even when its `enable` argument is false; those writes intrinsically enable a compare. It also uses CCADD without ensuring SYSCOUNTER is active, although CCADD is ignored while it sleeps.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 §8.10.2 pp. 296-297; LM20 §8.11.2 pp. 314-315
* Requirement: CCH/CCADD writes enable the channel, and CCADD has no effect when SYSCOUNTER is sleeping.

Why this is a problem:

Disabled timers can fire and enabled relative timers can silently fail to arm, producing intermittent scheduling and wake errors.

How to reproduce or test:

Call the API with `enable=false`, inspect channel enable/event state, then repeat CCADD while allowing SYSCOUNTER sleep.

Suggested fix:

Separate value programming from enable state, use the documented disable task/register, and hold SYSCOUNTER active across CCADD plus synchronization.

Patch sketch, if safe:

Add explicit `disableCompare(channel)` and test active/sleeping counter states.

Risk of fix:

Low to medium because existing timers may rely on accidental implicit enable.

Related findings: FINDING-024, FINDING-025

### FINDING-027: `delaySystemOff` and `delaySystemOffNoRetention` are identical WFI delays

Severity: Medium  
Confidence: High  
Status: Definite  
Affected chips/boards: All  
Affected files: both `wiring_time.c`; public headers/docs/examples  
Affected functions: `delaySystemOff`, `delaySystemOffNoRetention`, internal timed path  
Peripheral/system block: Public low-power API

Repository evidence:

* File: both `hardware/nrf54l15clean/nrf54l15clean/cores/*/wiring_time.c`
* Line(s): L15 741-765, 954-962; LM20 735-763, 971-979
* Code summary: Both public functions enter the same System ON WFI loop and return; the no-retention name does not alter retention or enter OFF.

Datasheet / errata evidence:

* Document: Both Nordic product specifications and repository low-power documentation
* Section / table / page: System ON/OFF §5.2; repository API descriptions
* Requirement: System OFF is a core-power-down, cold-wake state; no-retention behavior requires explicit RAM retention control and cannot be represented by ordinary WFI.

Why this is a problem:

Names/documentation promise materially different power and retention behavior while both calls consume System ON current and preserve state.

How to reproduce or test:

Measure current and a `.noinit` sentinel across both APIs; both return through the same call stack.

Suggested fix:

Rename/document these as System ON sleep, or implement true non-returning System OFF APIs separately with explicit reset semantics.

Patch sketch, if safe:

Keep a returning `delayLowPower()` and make true OFF functions `noreturn` with wake-reason examples.

Risk of fix:

High API-compatibility risk.

Related findings: FINDING-020, FINDING-023

### FINDING-028: LM20 heap can grow directly into the reserved stack

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: XIAO nRF54LM20A/B  
Affected files: both LM20 linker-script copies; `syscalls.c`  
Affected functions: Link layout and `_sbrk`  
Peripheral/system block: RAM/heap/stack

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/nrf54lm20b_linker_script.ld`; duplicate under `cores/nrf54l15`; core syscall implementation
* Line(s): linker 157-169; `_sbrk` around 47-77
* Code summary: Produced ELF symbols show `__heap_end__ == __stack_start__ == 0x20070000`, while the reserved stack extends down to `0x2006F000`. `_sbrk` permits the heap to reach the initial stack pointer rather than the bottom of the reserved stack.

Datasheet / errata evidence:

* Document: LM20 PS memory map; local linker ABI
* Section / table / page: RAM memory map chapter; linker-defined stack reservation
* Requirement: Heap growth must stop below the complete reserved stack region.

Why this is a problem:

Dynamic allocation can overwrite live stack frames before `_sbrk` reports exhaustion, causing nonlocal corruption.

How to reproduce or test:

Allocate until the break passes `0x2006F000` while filling a deep stack guard; inspect the collision.

Suggested fix:

Set `__heap_end__` to `__stack_end__` (the low address) and add link/assert/runtime guard symbols.

Patch sketch, if safe:

Define unambiguous `__stack_bottom__`/`__stack_top__` and assert `_end <= __heap_end__ <= __stack_bottom__`.

Risk of fix:

Low; usable heap decreases by the stack reservation that was never safely allocatable.

Related findings: FINDING-047

### FINDING-029: MEMCONF wrapper is based on nonexistent tasks, events and protection registers

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: All  
Affected files: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_memconf.h`  
Affected functions: All `Memconf::*` task/status/interrupt/protection methods  
Peripheral/system block: MEMCONF/RAM power

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_memconf.h`
* Line(s): 16-93, 107-123
* Code summary: The wrapper invents task, event, status, interrupt and NVMC-protection offsets. It models eight regions and the wrong stride/layout for LM20.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 MEMCONF §4.2.5 pp. 44-46; LM20 §4.2.5 pp. 46-48
* Requirement: MEMCONF exposes `POWER[n].CONTROL`, `RET`, and `RET2` at the documented offsets; LM20 has 16 blocks.

Why this is a problem:

Power/retention calls write reserved or unrelated addresses and return fabricated state. RAM may not be retained or powered as requested.

How to reproduce or test:

Run `MemconfPower`, trace all bus writes, and compare them to the published register table; test retention with per-block patterns across sleep.

Suggested fix:

Replace the API implementation with the real POWER array model and chip-specific block count; remove unsupported task/interrupt/protection claims.

Patch sketch, if safe:

Regenerate a typed MEMCONF struct and implement only documented fields.

Risk of fix:

Medium because public methods must be removed or redefined.

Related findings: FINDING-002, FINDING-022

### FINDING-030: Oscillator wrapper sends CLOCK tasks to the OSCILLATORS block

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: All  
Affected files: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_oscillators.h`  
Affected functions: All oscillator start/stop/status/calibration methods  
Peripheral/system block: OSCILLATORS/CLOCK

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_oscillators.h`
* Line(s): 22-218, 230-263
* Code summary: Code uses base `0x40120000` (OSCILLATORS) for start/stop tasks, events and status that belong to CLOCK at `0x4010E000`; many offsets do not exist in OSCILLATORS.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 §§5.4.3, 5.5.4 pp. 73-85, 90-91; LM20 §§5.4-5.5 pp. 72-98
* Requirement: OSCILLATORS holds trim/frequency configuration; CLOCK owns source start/stop/tasks/events/status.

Why this is a problem:

The API writes reserved configuration space, cannot reliably start/stop clocks and can busy-wait on nonexistent status.

How to reproduce or test:

Run `OscillatorsState` with bus watchpoints and compare observed CLOCK events; a compile pass is not evidence of correct hardware access.

Suggested fix:

Split trim configuration and clock-control drivers using the two documented bases and exact per-chip registers.

Patch sketch, if safe:

Delete raw overlay structs and use generated types plus bounded ready waits.

Risk of fix:

Medium; clock consumers must be retested for ownership and startup order.

Related findings: FINDING-006, FINDING-020

### FINDING-031: Periodic RADIO helper uses an obsolete register map and omits START

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: All raw periodic-advertising HAL users  
Affected files: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_ble_periodic.h`  
Affected functions: Raw RADIO initialization/transmit sequence  
Peripheral/system block: RADIO/BLE

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_ble_periodic.h`
* Line(s): 157-200
* Code summary: Constants such as MODE `0x188`, a fabricated ENABLE `0x184` and END `0x148` follow an older/different RADIO layout. The task sequence does not issue documented `START` after readying.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 RADIO sequence §8.17.6 p. 470 and register overview §8.17.14 pp. 486-489; LM20 corresponding §8.17
* Requirement: Use the nRF54 RADIO task/event offsets and READY->START transaction sequence.

Why this is a problem:

The helper writes wrong registers and cannot produce a compliant packet transaction; it may also alter reserved state.

How to reproduce or test:

Run `BlePeriodicAdvertising`, capture register writes and RF output with a peer/sniffer.

Suggested fix:

Reimplement from the local nRF54 RADIO chapter, ideally sharing the already-audited lower-level radio abstraction rather than maintaining a raw parallel map.

Patch sketch, if safe:

No isolated offset patch; replace the complete state machine and test over the air.

Risk of fix:

High protocol/timing risk.

Related findings: FINDING-001

### FINDING-032: TWIS TX EasyDMA pointer/count/amount offsets are shifted

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: All TWIS target-mode users  
Affected files: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_twis.h`  
Affected functions: TWIS TX buffer setup and amount query  
Peripheral/system block: TWIS EasyDMA

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_twis.h`
* Line(s): 111-117, 150-152
* Code summary: The driver treats `+0x744/+0x748/+0x74C` as PTR/MAXCNT/AMOUNT. Actual TXD PTR/MAXCNT/AMOUNT are `+0x73C/+0x740/+0x744`; later locations are AMOUNT/reserved or LIST on LM20.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 TWIS §8.24.10 pp. 691-692; LM20 §8.25.10 pp. 729-730
* Requirement: Program exact TXD EasyDMA registers at their documented offsets.

Why this is a problem:

The target does not transmit from the requested buffer and may write reserved state or report a bogus amount.

How to reproduce or test:

Use a controller to read a known TWIS response and watch register writes/amount.

Suggested fix:

Use generated typed fields and add compile-time `offsetof` assertions for both chips.

Patch sketch, if safe:

Correct PTR/MAXCNT/AMOUNT offsets and explicitly configure LIST only where present.

Risk of fix:

Low.

Related findings: FINDING-001, FINDING-018

### FINDING-033: TIMER00 helper misreads the counter and misprograms channel features

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: All HAL Timer00 users  
Affected files: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_timer00.h`  
Affected functions: count read, compare setup, interrupt mask and one-shot setup  
Peripheral/system block: TIMER00

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_timer00.h`
* Line(s): 50-105
* Code summary: A live-count read returns CC[3] without issuing CAPTURE; compare interrupt masks use an incorrect `16 + 4*channel` pattern; ONESHOT is treated as a bitmap rather than a per-channel array; the `autoClear` argument is ignored. The `16+channel` STOP shortcut is correct and is not part of this finding.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 TIMER §8.22.5 and register tables pp. 646-652; LM20 §8.23
* Requirement: Capture tasks latch counter values into CC, compare event bits follow channel indices, and ONESHOT is an indexed register array.

Why this is a problem:

Time reads are stale, interrupts can target wrong/undefined bits, and one-shot/auto-clear behavior differs from the API.

How to reproduce or test:

Run `Timer00`, compare returned values to a reference timer, and inspect each channel's INTEN/ONESHOT state.

Suggested fix:

Issue a dedicated CAPTURE task before reading an owned CC channel, use generated masks/array fields, and either implement or reject autoClear.

Patch sketch, if safe:

Reserve one capture channel and add per-channel register-layout tests.

Risk of fix:

Medium because CC-channel ownership must be coordinated.

Related findings: FINDING-026

### FINDING-034: CRACEN PKE wrapper uses wrong control offsets and an invalid disable operation

> **2026-07-12 revalidation status:** Resolved in the current tree. The wrapper
> now uses the active chip bases and documented `ENABLE`, `INTENCLR` and
> `PK.STATUS` offsets; adjacent `CracenIkg` APIs were hardened to use bounded
> chip-relative CryptoRAM and fail closed. See the evidence and explicit
> runtime limits at the top of this report. The original text below is retained
> only as audit history.

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: All; LM20 addresses are additionally invalid  
Affected files: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_cracen_pke.h`  
Affected functions: PKE enable/status/interrupt control  
Peripheral/system block: CRACEN/PKE

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_cracen_pke.h`
* Line(s): 21-45, 140-149
* Code summary: ENABLE is addressed at `+0x500` instead of `+0x400`, status is fabricated, and interrupt disable writes `INTENSET +0x304` rather than `INTENCLR +0x308`. Hardcoded L15 CRACEN/core/PKE bases are not the LM20 bases.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 CRACEN §7.8.1.1/.6 pp. 134-140; LM20 instantiation Table 11 and CRACEN chapter
* Requirement: ENABLE is `+0x400`, interrupt clearing uses `INTENCLR +0x308`, and chip-specific secure bases must be used.

Why this is a problem:

PKE may never enable, completion/status is meaningless and interrupts cannot be disabled, undermining cryptographic operations and interrupt safety.

How to reproduce or test:

Run `CracenPke` under a secure debugger and log accesses/IRQ state on each chip.

Suggested fix:

Replace the raw wrapper with chip-specific generated definitions and follow the documented CRACEN/PKE sequence or supported local SDK abstraction.

Patch sketch, if safe:

No safe offset-only patch; audit the entire operation/operand-memory protocol.

Risk of fix:

High security and compatibility risk.

Related findings: FINDING-035, FINDING-063, FINDING-065

### FINDING-035: LM20 TAMPC HAL uses the L15 peripheral base

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: XIAO nRF54LM20A/B  
Affected files: `cores/nrf54lm20b/nrf54lm20b.h`; shared `nrf54l15_regs.h`; HAL security/tamper users  
Affected functions: TAMPC configuration/status and security setup  
Peripheral/system block: TAMPC

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/nrf54lm20b.h`; `libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_regs.h`
* Line(s): header 160; regs 125; consumers around HAL line 648/security line 791 onward
* Code summary: Shared code uses `0x500DC000`, the L15 base. LM20 startup elsewhere uses the correct `0x500EFxxx` address, making the internal inconsistency visible.

Datasheet / errata evidence:

* Document: LM20 PS
* Section / table / page: Instantiation Table 11 p. 19 and TAMPC §7.8.6 p. 206
* Requirement: LM20 TAMPC is ID239 at secure base `0x500EF000`.

Why this is a problem:

Security/tamper calls access an unrelated/reserved block and do not configure TAMPC.

How to reproduce or test:

Set a bus watchpoint, invoke the API and inspect TAMPC state at the documented base.

Suggested fix:

Select the base from a chip-correct generated header and eliminate shared L15 raw constants.

Patch sketch, if safe:

Use `NRF_TAMPC` from the active device header plus a compile-time base assertion.

Risk of fix:

Medium because previously dormant tamper/reset behavior may activate.

Related findings: FINDING-034, FINDING-065

### FINDING-036: PWM sequence-1 REFRESH and ENDDELAY use a four-byte rather than 0x20-byte stride

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: All HAL PWM users using sequence 1  
Affected files: `nrf54l15_regs.h`; `nrf54l15_hal_peripherals.inc`  
Affected functions: PWM sequence configuration  
Peripheral/system block: PWM EasyDMA sequencer

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_peripherals.inc`
* Line(s): 1085-1087, 1223-1226; offsets declared in `nrf54l15_regs.h:434-435`
* Code summary: Sequence selects `baseOffset + sequence*4`, so sequence 1 writes inside the sequence-0 block rather than the sequence-1 registers.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: PWM §8.15.5.35-.36; L15 p. 444; LM20 pp. 441-442
* Requirement: Sequence register blocks are separated by `0x20` bytes.

Why this is a problem:

Sequence-1 timing is not configured and unrelated sequence-0 fields may be corrupted, breaking double-buffered playback.

How to reproduce or test:

Configure distinct refresh/end-delay values for both sequences and inspect the hardware register block/waveform.

Suggested fix:

Use a typed `SEQ[2]` structure or a `0x20` stride with static offset assertions.

Patch sketch, if safe:

`seqBase = base + SEQ0_OFFSET + index * 0x20` after validating `index < 2`.

Risk of fix:

Low.

Related findings: FINDING-039, FINDING-070

### FINDING-037: FICR helpers corrupt/omit identification and trim fields

Severity: Medium  
Confidence: High  
Status: Definite  
Affected chips/boards: All; one part-ID defect is LM20B-specific  
Affected files: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_ficr.h`  
Affected functions: variant string, device address, part detection, oscillator trim helpers  
Peripheral/system block: FICR

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_ficr.h`
* Line(s): 67-120, 169-187, 233-238
* Code summary: Device-address packing drops one byte and includes the wrong byte; variant construction shifts a 32-bit value by 32+ bits and can write a terminator out of bounds; LM20B is compared to the A part code; trim slope/offset fields use wrong widths/positions.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 FICR §4.2.4.1.5/.12/.13 pp. 40-44; LM20 §4.2.4.1.1.4/.5 pp. 41-43
* Requirement: Device ID/address and variant bytes use the stated word/byte order; LM20B PART is `0x054BC20B`; slope is signed 9-bit [8:0] and offset is 10-bit [25:16].

Why this is a problem:

Identity can collide or be reported incorrectly, buffer bounds can be violated and oscillator calibration values are wrong.

How to reproduce or test:

Compare helper output byte-for-byte with raw FICR words on A/B silicon and exercise variant buffers of lengths 0..9 under sanitizers in a host unit.

Suggested fix:

Use explicit byte extraction, length-first writes and signed-field decoding; add fixtures from raw FICR dumps.

Patch sketch, if safe:

Avoid shifts equal to the type width and write a terminator only when `outLen > bytesWritten`.

Risk of fix:

Medium if device addresses are used as persistent identifiers.

Related findings: FINDING-046, FINDING-065

### FINDING-038: SystemInit clears every RESETREAS cause except RESETPIN

Severity: Medium  
Confidence: High  
Status: Definite  
Affected chips/boards: All  
Affected files: both `system_*.c`  
Affected functions: `SystemInit` reset-cause handling  
Peripheral/system block: RESET

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/system_nrf54l15.c`; LM20 peer
* Line(s): L15 169-171; LM20 88-90
* Code summary: Code writes `~RESETPIN_Msk` to a write-one-to-clear register, clearing all currently set reasons except RESETPIN rather than clearing RESETPIN.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 RESETREAS §5.8.11.1 p. 104; LM20 p. 112
* Requirement: RESETREAS bits are cleared by writing one to the specific bit(s) to clear.

Why this is a problem:

Watchdog, System OFF, lockup and other diagnostic causes are erased before application code can inspect them, while the intended cause remains set.

How to reproduce or test:

Induce watchdog/System OFF reset, stop after SystemInit and inspect RESETREAS.

Suggested fix:

Snapshot reset causes early, clear only explicitly consumed bits with their masks, and expose the snapshot to Arduino code.

Patch sketch, if safe:

Replace complement write with `RESETREAS = RESETPIN_Msk` only if that is the intended consumed cause.

Risk of fix:

Low; applications may start seeing previously erased causes.

Related findings: FINDING-020, FINDING-021

### FINDING-039: Arduino PWM idle polarity can disagree with the GPIO output latch

Severity: Medium  
Confidence: High  
Status: Definite  
Affected chips/boards: All PWM-capable Arduino pins  
Affected files: both `wiring_analog.c`  
Affected functions: PWM initialization/`analogWrite`  
Peripheral/system block: PWM/GPIO

Repository evidence:

* File: both `hardware/nrf54l15clean/nrf54l15clean/cores/*/wiring_analog.c`
* Line(s): around 1745-1784
* Code summary: Code programs PWM `IDLEOUT` but enables output without first forcing the corresponding GPIO `OUT` latch to the matching level; existing latch state is preserved.

Datasheet / errata evidence:

* Document: Both Nordic product specifications
* Section / table / page: L15 PWM §8.15.4 p. 425; LM20 PWM §8.15.4 p. 423, including Table 48
* Requirement: PWM output polarity/idle transition interacts with GPIO output configuration/latch and must be initialized coherently.

Why this is a problem:

Starting/stopping PWM can glitch or settle at the opposite level, which is unsafe for active-low loads and power controls.

How to reproduce or test:

Preload the opposite GPIO latch, start/stop PWM, and scope the pin around ownership transitions.

Suggested fix:

Set the GPIO latch to the intended idle level before connecting/enabling PWM and restore a documented state on detach.

Patch sketch, if safe:

Use OUTSET/OUTCLR before PSEL/ENABLE with a transition-order regression test.

Risk of fix:

Low to medium because visible startup polarity changes.

Related findings: FINDING-036, FINDING-070

### FINDING-040: `tone` is blocking and mishandles duration zero and `noTone`

Severity: Medium  
Confidence: High  
Status: Definite  
Affected chips/boards: All  
Affected files: both `wiring_analog.c`  
Affected functions: `tone`, `noTone`  
Peripheral/system block: Arduino tone API/GPIO timing

Repository evidence:

* File: both `hardware/nrf54l15clean/nrf54l15clean/cores/*/wiring_analog.c`
* Line(s): 2260-2285
* Code summary: `tone` performs a foreground busy loop, does not ensure OUTPUT mode, treats duration zero as a long finite loop rather than indefinite playback, and `noTone` cannot interrupt the already-blocking call.

Datasheet / errata evidence:

* Document: Repository Arduino API declarations/examples; GPIO/TIMER chapters
* Section / table / page: Public API contract in local core headers; no external Arduino document used
* Requirement: The exposed asynchronous stop API and zero-duration semantics require a persistent nonblocking generator.

Why this is a problem:

Sketch execution stalls, `noTone` is ineffective, and the pin may not drive at all if it was not previously configured.

How to reproduce or test:

Call `tone(pin, 1000, 0)`, attempt foreground work/`noTone`, and observe that control does not behave as the API surface implies.

Suggested fix:

Implement tone with an owned TIMER/GPIOTE/PWM resource and ISR/state machine, or clearly remove/mark the unsupported API.

Patch sketch, if safe:

Add a nonblocking single-channel service with atomic replacement/stop and explicit resource arbitration.

Risk of fix:

Medium due to timer/resource conflicts.

Related findings: FINDING-033, FINDING-041

### FINDING-041: Arduino interrupt nesting can re-enable interrupts disabled by other code

Severity: Medium  
Confidence: High  
Status: Definite  
Affected chips/boards: All  
Affected files: both `wiring_digital.c`  
Affected functions: `noInterrupts`, `interrupts`, `attachInterrupt`, `detachInterrupt`  
Peripheral/system block: CPU PRIMASK/critical sections

Repository evidence:

* File: both `hardware/nrf54l15clean/nrf54l15clean/cores/*/wiring_digital.c`
* Line(s): 333-449, especially 435-449
* Code summary: A global software nest counter is incremented without capturing prior PRIMASK. When it returns to zero, `interrupts()` unconditionally enables IRQs, even if IRQs were already disabled by an ISR, RTOS/library critical section, or direct CMSIS call. Internal attach/detach calls participate in the same counter.

Datasheet / errata evidence:

* Document: Arm Cortex-M33 architectural behavior represented by local CMSIS intrinsics; Nordic NVIC integration chapter
* Section / table / page: Local `__disable_irq`/`__enable_irq` semantics and PS interrupt model
* Requirement: A nested critical section must restore the entry interrupt state, not force-enable globally.

Why this is a problem:

Code can unexpectedly permit interrupts inside another owner's critical region, creating races and invariant violations.

How to reproduce or test:

Disable IRQs directly, call `noInterrupts(); interrupts();`, then read PRIMASK; it becomes enabled although it was disabled on entry.

Suggested fix:

Use save/restore helpers that capture PRIMASK per critical scope, and avoid hiding critical sections inside attach/detach APIs.

Patch sketch, if safe:

Expose `uint32_t irqSave()`/`irqRestore(state)` internally; preserve Arduino's simple public functions without claiming cross-owner nesting.

Risk of fix:

Medium because existing code may rely on `interrupts()` as a force-enable call.

Related findings: FINDING-008, FINDING-043

### FINDING-042: String self/aliased concatenation can use freed or overlapping memory

Severity: Medium  
Confidence: High  
Status: Definite  
Affected chips/boards: All  
Affected files: both `WString.h`  
Affected functions: `String::concat(const String&)`, `String::concat(const char*)`  
Peripheral/system block: C++ core library/heap

Repository evidence:

* File: both `hardware/nrf54l15clean/nrf54l15clean/cores/*/WString.h`
* Line(s): 255-293
* Code summary: Code calls `realloc(_data, ...)` before copying from `other.c_str()`/`value`. If the source aliases the same allocation, realloc may free/move it; `memcpy` is also invalid for overlap. Size addition can wrap before allocation.

Datasheet / errata evidence:

* Document: Local C/C++ implementation contract; hardware datasheet not applicable
* Section / table / page: `realloc`/`memcpy` language semantics embodied by the toolchain libc
* Requirement: Source pointers must remain valid after reallocation, overlap requires `memmove`, and allocation sizes must be overflow-checked.

Why this is a problem:

`s += s` or `s.concat(s.c_str()+n)` can read freed memory, corrupt content or overflow the allocation calculation.

How to reproduce or test:

Force realloc movement and exercise self/substring concatenation under a host sanitizer and on-target heap guards.

Suggested fix:

Detect aliasing, preserve source offset or copy through temporary storage, use `memmove` where appropriate, and reject `SIZE_MAX` overflow.

Patch sketch, if safe:

Check `rhs_len > SIZE_MAX - _length - 1`; capture alias offset before realloc and rebuild the pointer afterward.

Risk of fix:

Low to medium due to constrained-memory temporary allocation behavior.

Related findings: FINDING-028, FINDING-044

### FINDING-043: SoftwareTimer callback can delete the iterator and cause use-after-free

Severity: Medium  
Confidence: High  
Status: Definite  
Affected chips/boards: All  
Affected files: both `SoftwareTimer.cpp`  
Affected functions: destructor, `serviceAll`, `serviceOne`  
Peripheral/system block: Cooperative timer/global object list

Repository evidence:

* File: both `hardware/nrf54l15clean/nrf54l15clean/cores/*/SoftwareTimer.cpp`
* Line(s): 24-33, 83-103
* Code summary: `serviceAll` advances with `timer = timer->next_` after invoking a callback. The callback may delete that timer, and its destructor unlinks/frees it, so the loop dereferences freed storage. List access is also unsynchronized against interrupt/foreground mutation.

Datasheet / errata evidence:

* Document: Local C++ lifetime rules; hardware datasheet not applicable
* Section / table / page: Object lifetime and callback contract in local implementation
* Requirement: Iterators must not dereference an object after a callback may destroy it; shared list mutation must be serialized.

Why this is a problem:

Valid callback behavior can cause heap corruption or arbitrary control flow.

How to reproduce or test:

Create a heap timer whose callback deletes itself while another timer follows it; run `serviceAll` with heap guards.

Suggested fix:

Capture a safe successor before callback with a mutation protocol, or use stable intrusive ownership/deferred deletion; protect list updates with state-preserving critical sections.

Patch sketch, if safe:

Mark callbacks in progress and defer unlink/free until the outer traversal completes.

Risk of fix:

Medium due to callback/reentrancy semantics.

Related findings: FINDING-041

### FINDING-044: Core numeric helpers contain signed-overflow undefined behavior

Severity: Low  
Confidence: High  
Status: Definite  
Affected chips/boards: All  
Affected files: both `Print.cpp`; both `wiring_math.c`  
Affected functions: `Print::printSigned`, `map`  
Peripheral/system block: Arduino utility APIs

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/Print.cpp`; `wiring_math.c`; LM20 copies
* Line(s): `Print.cpp:299-305`; `wiring_math.c:11-19`
* Code summary: Printing negates `LONG_MIN`, which is not representable as positive `long`. `map` performs subtraction and multiplication in signed `long` before division and can overflow for valid endpoint arguments.

Datasheet / errata evidence:

* Document: GCC/C language model selected by `platform.txt`; hardware datasheet not applicable
* Section / table / page: C/C++ signed-integer semantics
* Requirement: Signed overflow is undefined; edge-valued public APIs need widened/unsigned arithmetic.

Why this is a problem:

Extreme values can print incorrectly or produce compiler-dependent mapping results.

How to reproduce or test:

Print `LONG_MIN` and map across `LONG_MIN..LONG_MAX` under UBSan in a host build.

Suggested fix:

Convert magnitude without signed negation and use checked 64-bit/intermediate arithmetic.

Patch sketch, if safe:

For negative magnitude use `0UL - (unsigned long)value`; widen `map` operands before subtraction/multiplication.

Risk of fix:

Low.

Related findings: FINDING-042

### FINDING-045: Analog aliases and reverse mapping do not match physical SAADC channels

Severity: Medium  
Confidence: High  
Status: Definite  
Affected chips/boards: XIAO LM20A; XIAO L15; HOLYIOT-25007/25008; generic 36-pad  
Affected files: affected `variants/*/pins_arduino.h`; analog core helpers  
Affected functions: `pinToSaadcChannel`, `analogInputToDigitalPin`, `analogRead`  
Peripheral/system block: SAADC/board pins

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/variants/xiao_nrf54lm20b/pins_arduino.h` and L15/module variants
* Line(s): LM20 19-20, 69-82, 242-250, 321-329; XIAO L15 78-94 and reverse map around 241-249; analogous module/25008 lines
* Code summary: LM20 declares five analog inputs indexed A0..A4 but `analogInputToDigitalPin(4)` fails because it switches on physical channel 7. Several L15 variants expose A4 on P1.10, which has no matching SAADC AIN route; `analogRead` silently returns zero. DK A0-A7 mapping is valid.

Datasheet / errata evidence:

* Document: Both product specifications and XIAO schematics
* Section / table / page: Chapter 10 pin tables; L15 P1.10/P1.11-.14 rows and LM20 P1.03 AIN7 row
* Requirement: Arduino analog-index conversion must be distinct from the physical AIN number, and aliases must name pins carrying the stated AIN function.

Why this is a problem:

Valid analog-index APIs fail on LM20, while advertised L15 A4 returns a fabricated zero instead of sampling or reporting invalid input.

How to reproduce or test:

Apply known voltages to every A alias and test both `analogRead(Ax)` and `analogInputToDigitalPin(index)`.

Suggested fix:

Correct aliases from the chip/package/board pin table, implement index-to-pin independently of pin-to-channel, and return an explicit invalid/error path where possible.

Patch sketch, if safe:

LM20 reverse map should use `case 4`; remove/reassign L15 A4 only after board schematic/pad confirmation.

Risk of fix:

Medium because changing aliases breaks sketches wired to current labels.

Related findings: FINDING-064, FINDING-069

### FINDING-046: LM20A board profile defines both LM20A and LM20B silicon macros

Severity: Medium  
Confidence: High  
Status: Definite  
Affected chips/boards: XIAO nRF54LM20A  
Affected files: `hardware/nrf54l15clean/nrf54l15clean/boards.txt`; conditional code throughout core/HAL  
Affected functions: All `#if` branches selected by part macro  
Peripheral/system block: Build/chip selection

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/boards.txt`
* Line(s): 219
* Code summary: The profile simultaneously defines `ARDUINO_NRF54LM20A`, `ARDUINO_NRF54LM20B`, `NRF54LM20A_XXAA` and `NRF54LM20B_XXAA`.

Datasheet / errata evidence:

* Document: LM20 PS
* Section / table / page: Device variants/part identification table; FICR PART distinction
* Requirement: A compilation must select the actual silicon variant; LM20B-only features (including Axon-related capability) must not be assumed on A.

Why this is a problem:

Conditional code can include incompatible B-only paths and makes it impossible to reason about target-specific register/feature coverage.

How to reproduce or test:

Preprocess representative source and list both macro branches; compare runtime FICR PART on the XIAO board.

Suggested fix:

Define only the actual board MCU macro and use a separate family macro for genuinely shared code.

Patch sketch, if safe:

Keep `NRF54LM20_FAMILY` plus one of A/B, never both.

Risk of fix:

Medium because copied code may incorrectly use the B macro as a family selector.

Related findings: FINDING-037, FINDING-061, FINDING-065

### FINDING-047: LM20 linker exposes 64,832 fewer RAM bytes than the documented usable range

Severity: Low  
Confidence: High  
Status: Definite  
Affected chips/boards: XIAO nRF54LM20A/B  
Affected files: both LM20 linker-script copies; `boards.txt` maximum data size  
Affected functions: Link layout/build size reporting  
Peripheral/system block: RAM map

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/nrf54lm20b_linker_script.ld`; duplicate in L15 core
* Line(s): MEMORY definition and symbols through 169; board maximum-size metadata
* Code summary: RAM length is `0x70000` (448 KiB), while the local PS exposes usable application RAM through `0x2007FD40`; 64,832 usable bytes are excluded without a linker reservation explaining ownership.

Datasheet / errata evidence:

* Document: LM20 PS
* Section / table / page: LM20 §3.5.1 p. 13 and §3.5.3 p. 14; VPR configuration §8.28 p. 1105 identifies the top context area at `0x2007FD40`
* Requirement: The application linker map should reflect usable memory or explicitly reserve documented regions for a named owner.

Why this is a problem:

Large applications fail to link substantially earlier than necessary and board-manager capacity reporting understates the device.

How to reproduce or test:

Link a `.bss` fill approaching 448 KiB, then compare failure point with the PS usable endpoint and any produced map reservations.

Suggested fix:

Document the owner if the space is intentionally reserved; otherwise extend RAM to the documented usable endpoint while retaining stack/heap guards.

Patch sketch, if safe:

Use explicit named MEMORY regions and a link assertion rather than a rounded-down length.

Risk of fix:

Medium until hidden ROM/security/VPR reservations are excluded conclusively.

Related findings: FINDING-028

### FINDING-048: LM20 linker and forced include paths are hardcoded through the L15 core directory

Severity: Low  
Confidence: Medium  
Status: Likely  
Affected chips/boards: XIAO LM20A/B, especially future divergence  
Affected files: `hardware/nrf54l15clean/nrf54l15clean/platform.txt`; duplicate linker/header files  
Affected functions: Compile/link recipes  
Peripheral/system block: Build selection

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/platform.txt`
* Line(s): 31, 33-35
* Code summary: All boards load linker scripts and `CoreVersionGenerated.h` through `cores/nrf54l15`, even though LM20 has its own core directory. The current duplicate LM20 script happens to match, so immediate behavior is not independently wrong.

Datasheet / errata evidence:

* Document: LM20 versus L15 product specifications
* Section / table / page: Distinct memory maps and peripheral implementations throughout
* Requirement: Chip-selected build artifacts must track the selected device rather than an unrelated duplicate path.

Why this is a problem:

An LM20-specific linker/header fix can be silently ignored, causing future memory/startup regressions. Status is Likely because current duplicate content masks it.

How to reproduce or test:

Make a harmless symbol-only change in the LM20 script and inspect which script appears in the link command/map.

Suggested fix:

Add a `{build.core.path}`/board-selected linker directory property and remove duplicate authoritative files.

Patch sketch, if safe:

Set an LM20 board property for the linker-script directory and assert a chip marker in the final ELF.

Risk of fix:

Low.

Related findings: FINDING-028, FINDING-047

### FINDING-049: Default release archives omit sources required by advertised OpenThread and Matter menus

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: All board-manager installations using Thread/Matter stage menus  
Affected files: `scripts/build_release.py`; staged mbedTLS/OpenThread wrapper sources; `boards.txt`  
Affected functions: Release archive construction and feature builds  
Peripheral/system block: Packaging/OpenThread/Matter

Repository evidence:

* File: `/home/lolren/Desktop/eport_nrf54/nrf54-arduino-core/scripts/build_release.py`
* Line(s): 42-46, 447-450, 614-632
* Code summary: Default exclusions remove `third_party/openthread-core` and bridge sources while every board exposes feature menus. A default-excluded staged archive reproducibly fails at `src/mbedtls_stage/mbedtls_aes.c:5` because the referenced OpenThread AES source is absent; a full source-tree build passes.

Datasheet / errata evidence:

* Document: Hardware datasheet not applicable; local preprocessor include and release contract
* Section / table / page: `boards.txt` feature menus and compiler missing-file output
* Requirement: A published feature selection must include every source/header it compiles.

Why this is a problem:

Features pass developer source-tree CI but fail for actual Board Manager users.

How to reproduce or test:

Run `build_release.py` without `--include-openthread-core`, install/extract the archive and compile the OpenThread/Matter stage probes.

Suggested fix:

Include the dependency by default, or remove/disable menus in packages that omit it; validate the exact staged archive before index publication.

Patch sketch, if safe:

Invert the option to an explicit size-reduced package and add archive-install feature-matrix CI.

Risk of fix:

Medium due to archive size/licensing review.

Related findings: FINDING-052, FINDING-053, FINDING-057

### FINDING-050: Windows `.uf2` recipe only renames an Intel HEX file

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: All Windows users relying on UF2 export/upload  
Affected files: `hardware/nrf54l15clean/nrf54l15clean/platform.txt`  
Affected functions: Windows UF2 recipe  
Peripheral/system block: Build artifact/upload

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/platform.txt`
* Line(s): 65-68
* Code summary: Unix invokes `uf2_emit.py`; Windows executes `copy ...hex ...uf2`. The resulting file retains Intel HEX syntax and is not UF2.

Datasheet / errata evidence:

* Document: Hardware datasheet not applicable; repository UF2 emitter and upload helper define expected binary format
* Section / table / page: Local `tools/uf2/uf2_emit.py` and platform recipes
* Requirement: A UF2 artifact must be encoded as UF2 blocks, not named by extension alone.

Why this is a problem:

Drag-and-drop or upload helpers receive an invalid image and fail, potentially with an opaque bootloader error.

How to reproduce or test:

Run the Windows recipe and inspect the first bytes: `:` denotes Intel HEX rather than UF2 magic.

Suggested fix:

Invoke the same Python UF2 emitter on Windows with correctly quoted paths.

Patch sketch, if safe:

Remove the Windows copy override and make the portable recipe path-safe.

Risk of fix:

Low.

Related findings: FINDING-051

### FINDING-051: Programmer definitions do not connect to valid upload tool recipes

Severity: Medium  
Confidence: High  
Status: Definite  
Affected chips/boards: All users selecting pyOCD/OpenOCD programmers  
Affected files: `programmers.txt`; `platform.txt`; missing OpenOCD configs  
Affected functions: Arduino `upload_using_programmer` resolution  
Peripheral/system block: Upload/debug tooling

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/programmers.txt`; `platform.txt`
* Line(s): programmers 1-7; platform 98-100
* Code summary: Programmer stanzas supply name/communication/protocol but no valid `.program.tool` integration. Platform defines malformed anonymous keys `.cmd.path`/`.upload.pattern`; referenced OpenOCD configuration is absent. Local `-P` reproduction reports undefined programmer tool.

Datasheet / errata evidence:

* Document: Hardware datasheet not applicable; Arduino recipe metadata in the repository
* Section / table / page: `boards.txt`, `platform.txt`, `programmers.txt` key relationships
* Requirement: Each advertised programmer must resolve to a named tool and complete pattern/configuration.

Why this is a problem:

IDE/CLI programmer upload fails before contacting hardware.

How to reproduce or test:

Invoke `arduino-cli upload -P pyocd` or OpenOCD equivalent against a built sketch and inspect property-expansion failure.

Suggested fix:

Define named `tools.pyocd.program.*`/`tools.openocd.program.*` recipes, ship referenced configs and add CLI upload dry-run tests.

Patch sketch, if safe:

Remove anonymous dot-prefixed properties and point `program.tool` to one verified tool per programmer.

Risk of fix:

Medium across host OSes.

Related findings: FINDING-050

### FINDING-052: Two divergent release implementations can publish different packages

Severity: Medium  
Confidence: High  
Status: Definite  
Affected chips/boards: Package consumers  
Affected files: `docs/release-script.md`; `tools/release.sh`; `scripts/build_release.py`  
Affected functions: Release metadata/archive/index generation  
Peripheral/system block: Release engineering

Repository evidence:

* File: `/home/lolren/Desktop/eport_nrf54/nrf54-arduino-core/docs/release-script.md`; `tools/release.sh`
* Line(s): docs 1-20; shell 151-180; Python release builder throughout
* Code summary: Documentation calls `tools/release.sh` canonical, but the newer Python builder has content-addressed archives, exclusions and full metadata. The shell path emits an unhashed filename, lists only three coarse board entries and rewrites one index differently.

Datasheet / errata evidence:

* Document: Hardware datasheet not applicable; local package-index/release contract
* Section / table / page: Three package indexes and both release scripts
* Requirement: One reproducible release path must generate consistent archive identity, board list, checksums and indexes.

Why this is a problem:

Following repository documentation can publish artifacts different from those validated by the newer tooling, including the missing-source defect in FINDING-049.

How to reproduce or test:

Run both builders in isolated copies for the same version and diff archive manifests/index entries.

Suggested fix:

Choose one canonical release implementation, make documentation/CI call it, and retire the other.

Patch sketch, if safe:

Turn `tools/release.sh` into a thin checked wrapper around `scripts/build_release.py`.

Risk of fix:

Low to medium due to release automation changes.

Related findings: FINDING-049, FINDING-054

### FINDING-053: “All examples” scripts omit coverage and can report success after failures

Severity: Medium  
Confidence: High  
Status: Definite  
Affected chips/boards: CI/release confidence for all boards  
Affected files: `scripts/test_all.sh`; `scripts/test_all_examples.py`; `scripts/build_all_examples.py`  
Affected functions: Example discovery, build result handling, process exit status  
Peripheral/system block: Test automation

Repository evidence:

* File: `/home/lolren/Desktop/eport_nrf54/nrf54-arduino-core/scripts/test_all.sh` and peer scripts
* Line(s): shell 31-47, 59-83, 88-101, 129-130; Python hardcoded path 6-12 and no final failing exit around 64-68; builder board list 9-12
* Code summary: Shell masks command status with `|| true`, limits L15/feature sets with `head`, and always exits zero. `test_all_examples.py` hardcodes installed version 0.9.161 and prints a summary without returning failure. `build_all_examples.py` omits XIAO L15 and intentionally skips Thread/Matter on L15.

Datasheet / errata evidence:

* Document: Hardware datasheet not applicable; script comments and release test claims
* Section / table / page: Script headers claim “ALL examples”/boards/options
* Requirement: A gating test must execute the stated matrix against the current source and propagate any failure.

Why this is a problem:

CI/release can be green with compile failures or entirely untested profiles; the two shipped link failures demonstrate the gap.

How to reproduce or test:

Introduce a syntax error outside the `head` subset or invoke the script with an invalid compiler; observe exit zero/omission.

Suggested fix:

Build a manifest-driven matrix from local `boards.txt`, current source and example metadata, preserve command return codes, and fail on any unexpected skip/failure.

Patch sketch, if safe:

Emit machine-readable results and use `sys.exit(1 if failed else 0)` in one canonical runner.

Risk of fix:

Low; CI duration will increase.

Related findings: FINDING-049, FINDING-058

### FINDING-054: Package indexes contain duplicate version 0.9.54 entries

Severity: Low  
Confidence: High  
Status: Definite  
Affected chips/boards: Board Manager metadata consumers  
Affected files: `package_nrf54l15clean_index.json`; stable index  
Affected functions: Package version resolution  
Peripheral/system block: Package index

Repository evidence:

* File: `/home/lolren/Desktop/eport_nrf54/nrf54-arduino-core/package_nrf54l15clean_index.json`; stable peer
* Line(s): main 4961 and 4999; stable 1845 and 1883
* Code summary: Two entries share version `0.9.54` but point at different archive filenames/URLs.

Datasheet / errata evidence:

* Document: Hardware datasheet not applicable; local JSON package schema/consumer behavior
* Section / table / page: Duplicate platform arrays at cited lines
* Requirement: A package architecture/version tuple must identify one immutable archive.

Why this is a problem:

Resolution/cache behavior is ambiguous and reproducibility depends on which duplicate a client chooses.

How to reproduce or test:

Parse each index, group by `(architecture, version)` and report nonunique entries.

Suggested fix:

Keep the intended immutable entry, archive/remove the duplicate and add uniqueness validation to release CI.

Patch sketch, if safe:

Extend `verify_package_index.py` to reject duplicate versions per platform.

Risk of fix:

Low; old cached users may reference the removed URL.

Related findings: FINDING-052

### FINDING-055: Board reference documents a Serial routing menu that the XIAO profiles do not provide

Severity: Low  
Confidence: High  
Status: Definite  
Affected chips/boards: XIAO nRF54L15 / Sense; potentially XIAO LM20A readers  
Affected files: `docs/board-reference.md`; `boards.txt`  
Affected functions: Board configuration/user-visible menu  
Peripheral/system block: Documentation/Serial routing

Repository evidence:

* File: `/home/lolren/Desktop/eport_nrf54/nrf54-arduino-core/docs/board-reference.md`; platform `boards.txt`
* Line(s): docs 18 and 155; XIAO board stanzas lack the claimed Serial routing menu
* Code summary: Documentation says Serial can be switched between bridge and header UART through Tools, but no corresponding XIAO menu/property exists.

Datasheet / errata evidence:

* Document: XIAO schematic and local board metadata
* Section / table / page: UART/bridge nets in schematic; board menu stanzas
* Requirement: Documented routing choices must map to an implemented board selection or fixed wiring must be stated.

Why this is a problem:

Users cannot select the promised route and may wire to a header on the assumption that a nonexistent setting controls it.

How to reproduce or test:

List FQBN options with Arduino CLI and compare them to the board-reference table.

Suggested fix:

Implement and validate a route menu or correct the documentation to the fixed object/pin behavior.

Patch sketch, if safe:

Documentation-only correction is lowest risk unless two hardware-valid routes are fully supported.

Risk of fix:

Low.

Related findings: FINDING-009, FINDING-010

### FINDING-056: TinyUSB compatibility stubs report success while discarding all USB behavior

Severity: Low  
Confidence: High  
Status: Definite  
Affected chips/boards: All sketches including the compatibility header  
Affected files: both `Adafruit_TinyUSB.h/.cpp` core copies  
Affected functions: `TinyUSBDevice` lifecycle/status and `tud_cdc_*` I/O  
Peripheral/system block: USB compatibility API

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/Adafruit_TinyUSB.h`; LM20 copy
* Line(s): 4-6, 24-49
* Code summary: Lifecycle/configuration calls are no-ops, `ready()`/`mounted()`/`tud_cdc_connected()` always return true, reads always fail and writes are discarded. The header comments call these stubs, but the runtime return values mimic successful hardware.

Datasheet / errata evidence:

* Document: XIAO schematics and local header contract
* Section / table / page: Board USB bridge connections; no native TinyUSB device implementation is present
* Requirement: A compatibility API must not report mounted/connected success when it cannot transfer data.

Why this is a problem:

Ported sketches compile and proceed as if USB is active while silently losing data and configuration.

How to reproduce or test:

Call `TinyUSBDevice.mounted()` and `tud_cdc_write_char()` without a USB device stack; success is reported and no bytes reach the bridge.

Suggested fix:

Return false/unsupported, provide compile-time diagnostics, or implement an explicit bridge-backed adapter with honest capabilities.

Patch sketch, if safe:

Make status methods false and writes return an error unless a real backend is registered.

Risk of fix:

Low; sketches incorrectly relying on fake success will expose their incompatibility.

Related findings: FINDING-055

### FINDING-057: Matter ECC falls back to a predictable software generator when hardware entropy fails

Severity: High  
Confidence: High  
Status: Needs human verification  
Affected chips/boards: Thread/Matter stage builds on L15 and LM20  
Affected files: `matter_secp256r1.cpp`; `matter_rng.h`  
Affected functions: `Secp256r1::randomBytes`, `randomWord`, scalar/key/signature nonce generation  
Peripheral/system block: Matter cryptography/entropy

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_secp256r1.cpp`
* Line(s): 20-50, 889-940; `matter_rng.h:8-23`
* Code summary: On CRACEN RNG failure, output comes from a xorshift generator seeded with device identifiers, times, length and an address. The local Matter RNG header says operations fail rather than use weak pseudo-random data, but this ECC path does the opposite.

Datasheet / errata evidence:

* Document: Local `matter_rng.h`; CRACEN RNG chapters in both product specifications
* Section / table / page: Header lines 8-11; hardware RNG health/error behavior
* Requirement: The repository's stated policy is fail-closed hardware entropy. No Matter/cryptographic standard was supplied locally to confirm all protocol-specific requirements.

Why this is a problem:

Private keys and ECDSA nonces can become predictable after a hardware RNG fault, potentially disclosing credentials. Final standards-compliance classification needs a security owner because the relevant Matter specification is absent.

How to reproduce or test:

Force `CracenRng::fill` failure at deterministic boot timing and compare generated scalars/signatures across resets/devices with controlled IDs.

Suggested fix:

Return failure through every key/nonce call and abort the cryptographic operation when healthy hardware entropy is unavailable.

Patch sketch, if safe:

Change random APIs to `bool`, zeroize outputs/state on failure and propagate failure to keygen/signing.

Risk of fix:

High API/security-path change, but fail-open randomness should not remain.

Related findings: FINDING-049, FINDING-063, FINDING-065

### FINDING-058: Two shipped examples fail to link because they redefine a strong BLE IRQ service symbol

Severity: Medium  
Confidence: High  
Status: Definite  
Affected chips/boards: XIAO nRF54L15; any board compiling the copies with the HAL library  
Affected files: both `examples/*/SenseDelayRailRetentionProbe.ino`; `nrf54l15_hal.cpp`  
Affected functions: `nrf54l15_ble_grtc_irq_service`  
Peripheral/system block: Example/build/GRTC BLE service

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/examples/Power/SenseDelayRailRetentionProbe/SenseDelayRailRetentionProbe.ino`; `libraries/nRF54-Board-Examples/examples/XIAO-nRF54L15-Sense/SenseDelayRailRetentionProbe/...`; HAL implementation
* Line(s): both sketches 93-108; `libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.cpp:138`
* Code summary: Each sketch defines the same strong C symbol already strongly defined by the linked library. GCC 7-2017q4 and GCC 9.2.1 both report multiple definition. In the top-level sweep these were the two failures out of 83 examples.

Datasheet / errata evidence:

* Document: Hardware datasheet not applicable; local linker output and ELF symbol rules
* Section / table / page: Reproduced linker diagnostic names both cited definitions
* Requirement: A linked program can contain only one strong external definition.

Why this is a problem:

Advertised examples cannot produce firmware. Making either definition weak without design review can instead suppress essential BLE GRTC service.

How to reproduce or test:

Compile either directory with the local XIAO L15 FQBN and package GCC 7; linker exits nonzero.

Suggested fix:

Add a registered diagnostic callback or separate weak probe hook serviced by the library; do not override the core BLE service symbol.

Patch sketch, if safe:

Rename the sketch callback and register it through a new explicit observer API.

Risk of fix:

Medium because BLE timing service must remain intact.

Related findings: FINDING-053

### FINDING-059: L15 I2S wrappers pass word counts to a byte-count MAXCNT register

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: L15 I2S users  
Affected files: `nrf54l15_hal_i2s.inc` and public I2S declarations/examples  
Affected functions: `I2sTx::setBuffers/begin`, `I2sRx::*`, `I2sDuplex::*`  
Peripheral/system block: I2S EasyDMA

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_i2s.inc`
* Line(s): 17-33, 102-103, 267-283, 352-353, 518-558, 631-632
* Code summary: APIs accept `uint32_t*` plus `wordCount`, but write `wordCount` directly to RXTXD.MAXCNT and validate it as if the field were words.

Datasheet / errata evidence:

* Document: L15 PS
* Section / table / page: I2S EasyDMA §8.11.7 pp. 329-330
* Requirement: RXTXD.MAXCNT and buffer size are expressed in bytes.

Why this is a problem:

Only one quarter of the advertised 32-bit words transfers, so audio block sizes/callback expectations are wrong.

How to reproduce or test:

Provide 64 known words, start one block and inspect AMOUNT/waveform; only 64 bytes rather than 256 transfer.

Suggested fix:

Checked-multiply word count by four before range validation/programming, or make the public unit explicitly bytes.

Patch sketch, if safe:

Reject `wordCount > fieldMax/sizeof(uint32_t)` then program `wordCount*sizeof(uint32_t)`.

Risk of fix:

Medium because corrected transfers are four times longer and expose buffer-rotation bugs.

Related findings: FINDING-060, FINDING-061

### FINDING-060: I2S PTRUPD callbacks operate on the buffer hardware just activated

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: L15 I2S TX/RX/duplex users  
Affected files: `nrf54l15_hal_i2s.inc`  
Affected functions: `I2sTx::onIrq`, `I2sRx::onIrq`, `I2sDuplex::onIrq`  
Peripheral/system block: I2S double-buffered EasyDMA

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_i2s.inc`
* Line(s): TX 185-198; RX 431-444; duplex 718-744
* Code summary: On PTRUPD code queues `nextBufferIndex_`, toggles the index, then calls the refill/receive callback on the toggled buffer. At the first event TX refills buffer 0 while DMA is transmitting it; RX delivers buffer 0 before DMA filled it. Pattern repeats.

Datasheet / errata evidence:

* Document: L15 PS
* Section / table / page: I2S §8.11.7 p. 329
* Requirement: Pointer registers are hardware double-buffered; PTRUPD occurs when the programmed pointer transfers into the internal active buffer.

Why this is a problem:

TX output can be torn/corrupted and RX callbacks receive stale/partial samples. Callback code races active DMA memory.

How to reproduce or test:

Stamp each block in its callback and capture output/input plus watchpoints; observe accesses while the same buffer is active.

Suggested fix:

Track queued, active and released buffers explicitly; treat the first PTRUPD as priming and callback only a buffer proven released/completed.

Patch sketch, if safe:

Implement a three-state two-buffer machine and test first-event/restart/stop ordering.

Risk of fix:

High because exact stream sequencing and underrun policy change.

Related findings: FINDING-059

### FINDING-061: LM20 headers and vectors expose an I2S peripheral that does not exist

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: XIAO nRF54LM20A/B  
Affected files: `nrf54lm20b.h`, `cmsis.h`, startup, copied types and shared I2S wrappers  
Affected functions: Any I2S object/IRQ use  
Peripheral/system block: Reserved address / LM20 TDM

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/nrf54lm20b.h`; `cmsis.h`; `startup_nrf54lm20b.S`
* Line(s): header 137, 148, 217, 279, 349, 360-361; cmsis 126; startup 118-119
* Code summary: Copied L15 definitions create I2S20 at `0x400DD000/0x500DD000`, IRQ 221. The LM20 type file is byte-identical to L15, so unsupported code compiles.

Datasheet / errata evidence:

* Document: LM20 PS
* Section / table / page: Instantiation Table 11 pp. 18-20; TDM §8.21 p. 636 onward
* Requirement: LM20 has no I2S/ID221. Its audio peripheral is TDM ID232 at `0x400E8000`.

Why this is a problem:

LM20 I2S calls access reserved space and enable a reserved IRQ; they cannot produce audio and may fault.

How to reproduce or test:

Instantiate/start I2S on LM20 and watch the reserved address/IRQ; no documented peripheral responds.

Suggested fix:

Conditionally exclude I2S from LM20 and implement a separate TDM driver/API only from the LM20 chapter.

Patch sketch, if safe:

Remove copied I2S base/IRQ/type exposure under LM20 and add compile-time unsupported diagnostics.

Risk of fix:

Low for correctness; source compatibility breaks for code that never could work.

Related findings: FINDING-001, FINDING-046, FINDING-059

### FINDING-062: SAADC timeout returns with EasyDMA still pointing at a dead stack object

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: All HAL SAADC users  
Affected files: `nrf54l15_hal_crypto_analog.inc`  
Affected functions: `Saadc::sampleRaw`; callers including `sampleMilliVolts`  
Peripheral/system block: SAADC EasyDMA

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_crypto_analog.inc`
* Line(s): 1515-1552
* Code summary: RESULT.PTR points to local `volatile int16_t sample`. On STARTED, END or STOPPED timeout the function returns immediately without abort/disable, clearing PTR or proving DMA quiescence.

Datasheet / errata evidence:

* Document: Both product specifications
* Section / table / page: L15 SAADC EasyDMA §8.18.6 pp. 559-560; LM20 §8.18.6 pp. 568-569
* Requirement: RESULT.PTR is captured for asynchronous DMA; invalid/inaccessible RAM can HardFault or corrupt memory, and the buffer must remain valid until transfer stops.

Why this is a problem:

A late SAADC transfer writes into a reused caller stack frame after failure is returned.

How to reproduce or test:

Force a tiny spin limit, return into a stack guard frame and later trigger/complete SAADC; watch the old address.

Suggested fix:

On every failure issue bounded STOP, wait STOPPED, disable if required and clear pointer/count before returning, or use a persistent aligned member buffer.

Patch sketch, if safe:

Use one cleanup label that proves DMA inactive before any return.

Risk of fix:

Medium; fault cleanup can block to its timeout.

Related findings: FINDING-003, FINDING-013, FINDING-020

### FINDING-063: KMU provisioning preserves buffered RRAM mode despite requiring unbuffered writes

Severity: High  
Confidence: High  
Status: Likely  
Affected chips/boards: All secure KMU users after another RRAM writer configured buffering  
Affected files: `nrf54l15_hal_security.cpp`; EEPROM/Preferences/other RRAM writers  
Affected functions: `Kmu::enableRramWrite`, `Kmu::provision`  
Peripheral/system block: KMU/RRAMC

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_security.cpp`
* Line(s): 164-173, 237-275
* Code summary: Code saves CONFIG and sets `previousConfig | WEN`, preserving any nonzero WRITEBUFSIZE. It does not first commit/empty a prior buffer.

Datasheet / errata evidence:

* Document: Both product specifications
* Section / table / page: KMU provisioning L15 §7.8.3.2.1 p. 167 step 4; LM20 p. 181; RRAMC §4.2.6.2/CONFIG (L15 pp. 47-48, 57; LM20 pp. 48-49, 59)
* Requirement: Provisioning uses an unbuffered RRAM write; WRITEBUFSIZE=0 selects unbuffered mode.

Why this is a problem:

Provisioning violates the mandatory sequence and may fail or lose atomic/power-fail behavior when shared RRAMC state was left buffered. Manifestation is state-dependent, hence Likely.

How to reproduce or test:

Set a nonzero write buffer using EEPROM/Preferences, provision a test slot, power-cycle at controlled points and verify events/key metadata.

Suggested fix:

Wait ready, commit/empty the existing buffer, save CONFIG, set WEN while clearing WRITEBUFSIZE, provision, then restore safely.

Patch sketch, if safe:

Mask both fields explicitly rather than ORing WEN into inherited state.

Risk of fix:

High security/nonvolatile-state risk; validate on sacrificial slots.

Related findings: FINDING-034, FINDING-057, FINDING-066

### FINDING-064: QDEC accepts unsupported ports and shared LM20 HAL cannot represent valid P3 routes

Severity: Medium  
Confidence: High  
Status: Definite  
Affected chips/boards: All L15 HAL users; LM20 P3 users  
Affected files: `nrf54l15_hal_crypto_analog.inc`; `nrf54l15_regs.h`; `nrf54l15_hal_support.cpp`  
Affected functions: `Qdec::begin`, generic `Gpio::configure`, pin helpers  
Peripheral/system block: QDEC/GPIO routing

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_crypto_analog.inc`
* Line(s): 1141-1205; `nrf54l15_regs.h:13-15`; `nrf54l15_hal_support.cpp:39-49`
* Code summary: QDEC validates only generic connected pins and accepts L15 P0/P2 although its instance cannot route them. Conversely the shared base/helper table omits GPIO P3, so valid LM20 P3 QDEC and other HAL routes fail.

Datasheet / errata evidence:

* Document: Both product specifications
* Section / table / page: L15 QDEC §8.16.7 p. 450; LM20 §8.16.7 p. 447
* Requirement: L15 QDEC20/21 route only P1; LM20 supports P1/P3.

Why this is a problem:

`begin()` can return true for impossible L15 routing and false for valid LM20 routing, breaking encoders and broader P3 peripherals.

How to reproduce or test:

Try L15 P0 and LM20 P3 A/B pairs and compare return value/waveform/events.

Suggested fix:

Validate port against peripheral instance and add chip-conditional P3 base/decode support throughout the shared HAL.

Patch sketch, if safe:

Add a central `instanceSupportsPort` table and P3 unit tests across every HAL class.

Risk of fix:

Medium because P3 support touches shared GPIO abstractions.

Related findings: FINDING-008, FINDING-010, FINDING-045

### FINDING-065: LM20 CRACEN RNG health checks use copied L15 bit definitions

> **2026-07-12 revalidation correction:** The status-position premise is
> false. Nordic's nRF54LM20A header places `STARTUPFAIL` at bit 10, the
> per-share fields at bits 12-19, and `CONDITIONINGISTOOSLOW` at bit 20, as the
> current tree does. See the correction at the top of this report. The raw
> LM20 header still merits full generated-header reconciliation, but that
> maintenance task is not this claimed security defect. The original text
> below is retained only as audit history.

Severity: High  
Confidence: High  
Status: Definite  
Affected chips/boards: XIAO nRF54LM20A/B; cryptographic/BLE/Matter entropy consumers  
Affected files: `nrf54lm20b_types.h`; `nrf54l15_hal_crypto_analog.inc`  
Affected functions: `CracenRng::beginNonBlocking`, `end`, `healthy`  
Peripheral/system block: CRACEN RNG

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/nrf54lm20b_types.h`; HAL implementation
* Line(s): types 4245-4253; HAL 41-45, 107-149, 241-249
* Code summary: Copied types define STARTUPFAIL bit 10 and FIFOACCFAIL bit 11. `healthy()` tests those L15 masks. It also treats CONTROL.ENABLE as a persistent level rather than LM20's start pulse.

Datasheet / errata evidence:

* Document: LM20 PS
* Section / table / page: RNG STATUS §7.8.1.9.33 p. 167; CONTROL §7.8.1.9.24 p. 164
* Requirement: LM20 has ANYHEALTHTESTFAIL bit 6, STARTUPFAIL bit 8, per-share failures bits 9-16, conditioning-too-slow bit 17 and no FIFOACCFAIL; ENABLE is self-clearing.

Why this is a problem:

The HAL can report entropy healthy despite hardware health-test failure, propagating unsafe random data into security protocols.

How to reproduce or test:

Inject/emulate each documented status bit and compare `healthy()`; inspect CONTROL after begin/end.

Suggested fix:

Regenerate LM20 device types and implement an LM20-specific aggregate failure mask/state sequence.

Patch sketch, if safe:

Key the RNG implementation on active chip type; test every failure bit, not just STARTUPFAIL.

Risk of fix:

High security path; correct behavior should fail closed.

Related findings: FINDING-034, FINDING-046, FINDING-057, FINDING-063

### FINDING-066: Headers invent non-secure RRAMC aliases used by EEPROM and Preferences

Severity: Medium  
Confidence: Medium  
Status: Needs human verification  
Affected chips/boards: Latent `NRF_TRUSTZONE_NONSECURE` builds; no current advertised profile defines it  
Affected files: both device headers; `EEPROM.cpp`; `Preferences.cpp`  
Affected functions: RRAM access in non-secure builds  
Peripheral/system block: TrustZone/RRAMC

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/nrf54l15.h`; LM20 header; EEPROM/Preferences
* Line(s): L15 header 52,167; LM20 52,177; EEPROM 45-63,121 onward; Preferences 146-163,240 onward
* Code summary: Headers define non-secure RRAMC bases `0x4004B000`/`0x4004E000` and libraries use `NRF_RRAMC` without an NS build guard.

Datasheet / errata evidence:

* Document: Both product specifications
* Section / table / page: L15 RRAMC §4.2.6.7 p. 49; LM20 §4.2.6.7 p. 51; TrustZone instantiation maps
* Requirement: Supplied tables list only secure RRAMC bases. It is unclear from supplied documents whether an SPU-mediated non-secure alias can be provisioned externally.

Why this is a problem:

A future non-secure profile can fault or access no controller while EEPROM/Preferences assume successful flash control. Final disposition needs security-partition documentation absent from the reference set.

How to reproduce or test:

Build/run a genuine non-secure image under the intended secure firmware and test controller access plus SPU fault status.

Suggested fix:

Remove unsupported aliases or gate EEPROM/Preferences behind an explicit secure service/verified SPU configuration.

Patch sketch, if safe:

Fail compilation for direct RRAMC use in NS builds until a documented backend is selected.

Risk of fix:

Medium for future TrustZone integrations; none for current profiles.

Related findings: FINDING-002, FINDING-063

### FINDING-067: NFCT interrupt API enables an IRQ whose vector remains Default_Handler

Severity: Medium  
Confidence: Medium  
Status: Likely  
Affected chips/boards: All NFCT interrupt users  
Affected files: `nrf54l15_hal_nfct.h`; both startup tables; CMSIS headers  
Affected functions: `Nfct::enableInterrupts`, user NFCT handlers  
Peripheral/system block: NFCT/NVIC

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_nfct.h`; both startup files
* Line(s): HAL 405-443; startup slots around 109-112
* Code summary: HAL exposes interrupt masks and NFCT_IRQn is 214, but startup leaves ID214 in the repeated `Default_Handler` region and supplies no weak `NFCT_IRQHandler` symbol.

Datasheet / errata evidence:

* Document: Both product specifications
* Section / table / page: NFCT instantiation/interrupt ID and §8.13.14 register tables
* Requirement: Enabled NFCT events assert IRQ214; a normal linked handler needs a vector entry unless raw VTOR patching is an intentional documented contract.

Why this is a problem:

A sketch enabling NVIC after the HAL mask enters `Default_Handler`. Status is Likely because the API might have intended polling/raw vector patching, but that restriction is undocumented.

How to reproduce or test:

Define a conventional `NFCT_IRQHandler`, enable a field event and observe that the symbol is never referenced by the vector table.

Suggested fix:

Add a weak NFCT vector/handler and clear/dispatch documented events, or explicitly make the API polling-only and prevent NVIC enable.

Patch sketch, if safe:

Insert `NFCT_IRQHandler` at vector 214 with a weak default alias.

Risk of fix:

Low to medium due to vector-table regeneration.

Related findings: FINDING-001

### FINDING-068: `attachInterrupt(..., LOW)` configures falling edge rather than low level

Severity: Medium  
Confidence: High  
Status: Definite  
Affected chips/boards: All interrupt-capable Arduino pins  
Affected files: both `wiring_digital.c`  
Affected functions: `attachInterrupt` mode conversion  
Peripheral/system block: GPIOTE/GPIO SENSE API

Repository evidence:

* File: both `hardware/nrf54l15clean/nrf54l15clean/cores/*/wiring_digital.c`
* Line(s): 95-106 and channel setup
* Code summary: Mode `LOW` is mapped to falling-edge polarity. The GPIO level-sense mechanism is not used, so an already-low pin and a continuously low pin do not have level semantics.

Datasheet / errata evidence:

* Document: Both product specifications; local Arduino mode constants
* Section / table / page: GPIOTE polarity/event configuration and GPIO PIN_CNF.SENSE descriptions
* Requirement: Edge polarity detects transitions; low-level detection requires GPIO SENSE/PORT behavior and appropriate latch handling.

Why this is a problem:

Wake/interrupt code expecting LOW level can miss an asserted line or behave differently after event clear.

How to reproduce or test:

Hold the pin low before attach and compare callback behavior with a later high-to-low transition.

Suggested fix:

Implement LOW/HIGH with the documented level-sense/PORT path and keep FALLING/RISING as edge modes, or reject unsupported level modes.

Patch sketch, if safe:

Split level and edge backends and add already-asserted/latch-clear tests.

Risk of fix:

Medium because level interrupts require anti-storm policy.

Related findings: FINDING-008, FINDING-069

### FINDING-069: HOLYIOT-25008 accelerometer interrupt aliases are on a port without GPIOTE/SENSE

Severity: Info  
Confidence: High  
Status: Definite  
Affected chips/boards: HOLYIOT-25008 nRF54L15  
Affected files: `variants/holyiot_25008_nrf54l15/pins_arduino.h`; sensor examples/docs  
Affected functions: `attachInterrupt`/wake use on `PIN_ACCEL_INT1/2`  
Peripheral/system block: Board pin routing/GPIOTE

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/variants/holyiot_25008_nrf54l15/pins_arduino.h`
* Line(s): 63-65, 75-80
* Code summary: INT1 is mapped to P2.00 and INT2 to P2.03. The aliases are valid digital inputs but cannot use the core's interrupt/wakeup facility.

Datasheet / errata evidence:

* Document: L15 PS
* Section / table / page: GPIOTE §8.9.3-.5 pp. 286-287 and GPIO port capability/pin tables
* Requirement: L15 GPIOTE covers P0/P1 as documented; P2 has no GPIOTE/SENSE path.

Why this is a problem:

Motion/data-ready polling can work, but interrupt-driven or wake-on-motion examples cannot work on those aliases. This is primarily a documented hardware limitation unless the unsupplied schematic reveals alternative routing.

How to reproduce or test:

Attach callbacks to both aliases and toggle the physical sensor outputs; no GPIOTE event is routable.

Suggested fix:

Document the polling-only limitation, have `digitalPinToInterrupt` return `NOT_AN_INTERRUPT`, and avoid wake examples for these nets.

Patch sketch, if safe:

Add board capability constants such as `ACCEL_INTERRUPTS_POLL_ONLY` and explicit example fallback.

Risk of fix:

Low.

Related findings: FINDING-008, FINDING-045, FINDING-068

### FINDING-070: `Pwm::setActiveHigh` writes IDLEOUT while PWM is enabled, so hardware ignores it

Severity: Medium  
Confidence: High  
Status: Definite  
Affected chips/boards: All HAL PWM users changing polarity after begin  
Affected files: `nrf54l15_hal_peripherals.inc`  
Affected functions: `Pwm::setActiveHigh`  
Peripheral/system block: PWM

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_peripherals.inc`
* Line(s): 1186-1209
* Code summary: The function writes IDLEOUT while the PWM wrapper remains configured/enabled, then returns true and updates waveform words.

Datasheet / errata evidence:

* Document: Both product specifications
* Section / table / page: PWM §8.15.5.34; L15 p. 444; LM20 p. 441
* Requirement: IDLEOUT writes are ignored while PWM is enabled.

Why this is a problem:

Active waveform polarity can change while idle polarity remains stale, producing an unexpected level/glitch when a sequence stops.

How to reproduce or test:

Start PWM, call `setActiveHigh` with the opposite value, read IDLEOUT and observe stop idle level.

Suggested fix:

Stop/synchronize DMA, disable PWM, update IDLEOUT and sequence polarity, then safely re-enable/restart; or defer until the next begin.

Patch sketch, if safe:

Return false while running unless a complete stop/reconfigure/restart path succeeds.

Risk of fix:

Medium because stopping PWM can visibly glitch outputs.

Related findings: FINDING-036, FINDING-039

### FINDING-071: Watchdog timeout is one tick long and stop leaves TSEN armed

Severity: Low  
Confidence: High  
Status: Definite  
Affected chips/boards: All HAL watchdog users  
Affected files: `nrf54l15_hal_crypto_analog.inc`  
Affected functions: `Watchdog::configure`, `Watchdog::stop`  
Peripheral/system block: WDT

Repository evidence:

* File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_crypto_analog.inc`
* Line(s): 1762-1810
* Code summary: CRV is programmed as `ceil(ms*32768/1000)`, but the timeout contains CRV+1 ticks, making every request one tick longer. After successful STOP, TSEN remains enabled.

Datasheet / errata evidence:

* Document: Both product specifications
* Section / table / page: L15 WDT §8.27 pp. 806-807; LM20 §8.29 pp. 1164-1165
* Requirement: Timeout is `(CRV+1)/32768`; Nordic explicitly recommends writing TSEN zero after stop to prevent runaway STOP behavior.

Why this is a problem:

Timeout accuracy is systematically off and an armed stop mechanism remains available longer than intended.

How to reproduce or test:

Measure timeout in LFCLK ticks for small values and inspect TSEN after `stop()` returns true.

Suggested fix:

Program `ceil(requestedTicks)-1` within field limits and clear TSEN after confirmed STOPPED.

Patch sketch, if safe:

Use a checked helper for CRV conversion and set `wdt_->TSEN = 0` after the event.

Risk of fix:

Low.

Related findings: FINDING-020

## Datasheet Cross-Check Matrix

| Peripheral / system block | Datasheet requirement | Repository implementation | Match / mismatch / unclear | Evidence | Finding ID |
|---|---|---|---|---|---|
| Peripheral/NVIC identity | Single IRQ number equals peripheral ID | Many serial/SAADC and LM20 IDs are copied from other blocks/chip | Mismatch | Both PS §8.1.10 and instantiation tables; CMSIS/startup | FINDING-001 |
| CPU ICACHE | Controller at `0xE0082000` with documented control/tasks | Raw helper uses `0x4004B000` and invented layout | Mismatch | Both PS §4.2.3; L15 RRAMC §4.2.6 | FINDING-002 |
| GPIO register access | Port-specific PIN_CNF/OUT/DIR fields | Core digital GPIO access is generally typed/correct for P0-P2; LM20 variant helpers know P3 | Match with integration caveat | GPIO chapters and variant pin decode | FINDING-008, FINDING-064 |
| GPIOTE routing | GPIOTE20=P1 (LM20 P1/P3), GPIOTE30=P0 | Arduino interrupts always use GPIOTE20 | Mismatch | L15 §8.9.3-.5; LM20 §8.10 | FINDING-008 |
| GPIO level sense | LOW/HIGH require SENSE/PORT level path | LOW is converted to falling edge | Mismatch | GPIO PIN_CNF.SENSE and GPIOTE polarity tables | FINDING-068 |
| DPPI/PPI | Channels/endpoints must use documented publish/subscribe model | Reviewed HAL endpoint helpers did not yield a confirmed field mismatch | Match in reviewed paths | DPPI chapters; typed endpoint uses | None |
| TIMER00 count/compare | Capture latches count; indexed event/ONESHOT registers | Reads CC without capture; wrong masks/array semantics | Mismatch | L15 §8.22.5 pp. 646-652; LM20 §8.23 | FINDING-033 |
| GRTC low-power timing | Strict TIMEOUT/WAKETIME guard, synchronized compare and early restore | Invalid inequality; delay-based sync; no confirmed early bootstrap caller | Mismatch / likely timing failure | L15 §8.10.2; LM20 §8.11.2 | FINDING-024, FINDING-025 |
| GRTC compare programming | CCH/CCADD enable channel; CCADD ignored in counter sleep | API writes even when disabled and does not hold counter active | Mismatch | L15 pp. 296-297; LM20 pp. 314-315 | FINDING-026 |
| WDT timeout/stop | Timeout `(CRV+1)/32768`; clear TSEN after stop recommended | Programs cycles directly and leaves TSEN | Mismatch | L15 §8.27; LM20 §8.29 | FINDING-071 |
| CLOCK/PLL | Frequency selected at startup; runtime change undefined | Public/internal runtime switching and SPI boosting | Mismatch | L15 §5.5.3; LM20 §5.5.3 | FINDING-006, FINDING-007 |
| CLOCK versus OSCILLATORS | CLOCK owns tasks/events; OSCILLATORS owns trim/config | Oscillator helper sends CLOCK tasks to OSCILLATORS base | Mismatch | L15 §§5.4-5.5; LM20 §§5.4-5.5 | FINDING-030 |
| RESETREAS | W1C only the causes being consumed | Writes complement of RESETPIN mask | Mismatch | L15 §5.8.11.1; LM20 p. 112 | FINDING-038 |
| System OFF | Finish DMA, stop HFXO, prepare reset/wake, then write SYSTEMOFF | No global DMA/HFXO quiescence; several APIs only WFI | Mismatch | Both PS §5.2 | FINDING-020, FINDING-021, FINDING-023, FINDING-027 |
| SPIM L15 | SPIM00 fixed 128 MHz and limited divisor range | Uses CPU rate and advertises sub-minimum SCK | Mismatch | L15 §8.19 p. 588 | FINDING-011 |
| SPIM LM20 | PRESCALER at `+0x52C`; shared-ID ownership required | Writes reserved `+0x524`; SPI and Serial1 share ID21 | Mismatch | LM20 §8.19 pp. 587-590 | FINDING-004, FINDING-012 |
| TWIM transaction | Program actual EasyDMA byte count/error bits | Empty call fabricates `0x00`; ANACK/DNACK shifted | Mismatch | L15 §8.23; LM20 §8.24 | FINDING-018, FINDING-019 |
| TWIS TX DMA | PTR/MAXCNT/AMOUNT at documented offsets | All three shifted to later offsets | Mismatch | L15 §8.24.10; LM20 §8.25.10 | FINDING-032 |
| UARTE routing | Instance-specific port capability | Several board defaults and all setters lack correct validation | Mismatch | L15 §8.25 plus ch. 10; LM20 §8.26 | FINDING-009, FINDING-010 |
| UARTE DMA/lifecycle | RAM-resident live buffer; wait TXSTOPPED before disable | Caller pointers used directly; early disable | Mismatch | L15 §8.25.2/.11; LM20 §8.26 | FINDING-013, FINDING-014 |
| UARTE baud/config | BAUDRATE based on instance PCLK; frame size matches API | UARTE00 uses 16 MHz presets; all modes force 8-bit | Mismatch | L15 §8.25.1; LM20 §8.26.1; CONFIG tables | FINDING-015, FINDING-016 |
| UARTE register offsets | DMA.TX.READY at `+0x16C`; reserved writes zero | Uses `+0x164`; LM20 writes reserved `+0x51C/+0x520` | Mismatch | UARTE register overviews | FINDING-017 |
| SAADC map/count | RESULT count in bytes; valid AIN pins | Main register/count map matches; timeout lifetime and aliases do not | Partial mismatch | L15 §8.18; LM20 §8.18; pin tables | FINDING-045, FINDING-062 |
| PWM sequences | SEQ blocks have `0x20` stride | Sequence-1 timing uses 4-byte stride | Mismatch | Both §8.15.5.35-.36 | FINDING-036 |
| PWM idle/polarity | GPIO/IDLEOUT coherent; IDLEOUT writes disabled-only | Arduino latch can disagree; HAL writes while enabled | Mismatch | L15 §8.15.4/.5.34; LM20 equivalents | FINDING-039, FINDING-070 |
| I2S L15 DMA | MAXCNT is bytes; PTRUPD transfers pointer to active buffer | Uses 32-bit-word count and callbacks active buffer | Mismatch | L15 §8.11.7 | FINDING-059, FINDING-060 |
| LM20 audio | No I2S; TDM is ID232/base `0x400E8000` | Copied I2S ID221/base `0x400DD000` | Mismatch | LM20 Table 11 and §8.21 | FINDING-061 |
| PDM DMA | Allocated bytes=`MAXCNT*2` | API multiplies samples by 2 before MAXCNT | Mismatch | L15 §8.14.4; LM20 §8.14.4 | FINDING-003 |
| QDEC | L15 P1 only; LM20 P1/P3 | Accepts invalid L15 ports; shared HAL rejects LM20 P3 | Mismatch | L15 §8.16.7; LM20 §8.16.7 | FINDING-064 |
| NFCT register map | Task/event/config offsets per §8.13.14 | Raw offsets/fields checked match | Match | Both NFCT §8.13.14 | None |
| NFCT interrupt integration | IRQ214 must reach a handler | Exposed INTEN API but vector is Default_Handler | Unclear / likely mismatch | Instantiation table; startup vector | FINDING-067 |
| RADIO transaction | nRF54 task/event offsets and READY/START flow | Periodic helper uses obsolete map and omits START | Mismatch | L15 §8.17.6/.14; LM20 §8.17 | FINDING-031 |
| CRACEN PKE | ENABLE `+0x400`, INTENCLR `+0x308`, chip bases | Wrong offsets/status/disable and L15 bases on LM20 | Mismatch | L15 §7.8.1; LM20 Table 11 | FINDING-034 |
| CRACEN RNG L15 | L15 health/status map | Reviewed L15 masks/primary offsets agree | Match in L15 | L15 §7.8.1.7 | None |
| CRACEN RNG LM20 | LM20-specific health bits and start-pulse control | Byte-copied L15 layout/masks | Mismatch | LM20 §7.8.1.9.24/.33 | FINDING-065 |
| KMU provisioning | RRAM writes unbuffered | Inherits nonzero WRITEBUFSIZE | Likely mismatch | Both KMU provisioning step 4; RRAMC CONFIG | FINDING-063 |
| TAMPC LM20 | ID239 at `0x500EF000` | HAL uses L15 `0x500DC000` | Mismatch | LM20 Table 11/§7.8.6 | FINDING-035 |
| FICR | Exact part/address/trim bit fields | Multiple packing, bounds, part-code and field errors | Mismatch | L15 §4.2.4; LM20 §4.2.4 | FINDING-037 |
| RRAMC TrustZone | Supplied maps show secure controller base | Headers invent/use NS alias | Unclear | L15 §4.2.6.7; LM20 §4.2.6.7 | FINDING-066 |
| MEMCONF | POWER array with CONTROL/RET/RET2 only | Invented task/event/status/protection model | Mismatch | L15 §4.2.5; LM20 §4.2.5 | FINDING-029 |
| nPM1300 rails | Voltage code 0..23 maps 1.0..3.3 V | Writes four-entry array index as code | Mismatch | nPM1300 §6.3.8/.4.3 | FINDING-005 |
| VPR/FLPR memory | Respect chip/core reservations | L15 VPR on/off scripts are internally bounded; no hardware execution | Unclear | L15 memory map and linker scripts | None |
| External QSPI/flash | Board pins/instance and shutdown must match schematic | XIAO LM pins match schematic; SPI speed/shared-owner and OFF hooks fail | Partial mismatch | XIAO schematic; LM20 SPIM | FINDING-004, FINDING-012, FINDING-022 |

## Board Variant Matrix

| Board | MCU | Memory configuration | Linker script | Pin map status | Peripheral conflicts | Sleep/power concerns | Findings |
|---|---|---|---|---|---|---|---|
| XIAO nRF54L15 / Sense | nRF54L15 | `0x17C000` code + 4 KiB persistence; RAM `0x26000` with VPR or `0x3FE00` without | `nrf54l15_linker_script.ld` / `_no_vpr.ld` | Supplied schematic agrees with bridge, IMU/mic, battery and RF nets; P2.08/.07 UARTE21 is valid; A4=P1.10 is not a valid AIN | P0 interrupts use GPIOTE20; setter APIs can choose impossible routes | Generic OFF sequence incomplete; LFXO present at P1.00/P1.01; first-start timeout needs bench timing | 001, 006, 008, 010, 011, 020, 024-027, 045, 055, 058 |
| XIAO nRF54LM20A | nRF54LM20A (profile also incorrectly defines B) | `0x1FC000` code + 4 KiB persistence; RAM `0x70000` exposed | `nrf54lm20b_linker_script.ld` (linked via L15 directory) | Supplied schematic agrees with D0-D27, PMIC, RGB, button, QSPI, IMU/mic; analog reverse map and P3 HAL coverage fail | Global SPI and Serial1 both ID21; P0/P3 interrupt defects; nonexistent I2S exposed | Heap reaches stack; hooks skipped; LFXO order prevents normal true OFF; PMIC rail misprogrammed | 001, 004-008, 010, 012, 020-028, 035, 045-048, 057, 061, 063-066 |
| HOLYIOT-25007 nRF54L15 | nRF54L15 | Same L15 VPR on/off layout | Same L15 scripts | Shared 36-pad variant; schematic absent; A4=P1.10 invalid | Default P2.08/.07 pair is wrongly attached to UARTE20 | External crystal/rail/reserved-pad assumptions cannot be confirmed | 001, 008-011, 020, 024-027, 045 |
| HOLYIOT-25008 nRF54L15 | nRF54L15 | Same L15 VPR on/off layout | Same L15 scripts | Dedicated variant; schematic absent; both P1 UART routes are valid; A4 invalid | LIS2DH12 INT aliases on P2 cannot use GPIOTE/SENSE | External crystal/rail assumptions unverified; sensor interrupts cannot wake | 001, 008, 010-011, 020, 024-027, 045, 069 |
| Generic nRF54L15 module (36-pad) | nRF54L15 | Same L15 VPR on/off layout | Same L15 scripts | Carrier-dependent; shares 25007 variant; A4 invalid | Same UARTE20/P2 default conflict as 25007 | LFXO and external-device power state carrier-dependent | 001, 008-011, 020, 024-027, 045 |
| Nordic PCA10156 nRF54L15 DK | nRF54L15 | Same L15 VPR on/off layout; no CPU-frequency menu | Same L15 scripts | Default VCOM P1.04/.05 and A0-A7 map are valid; schematic absent | Alternate P0 UART route selects UARTE20/21 rather than UARTE30; P0 interrupts broken | Crystal population and board peripheral shutdown not verified | 001, 008-011, 020, 024-027 |
| nRF54L10 / nRF54L05 | Runtime FICR recognition only | No selected memory layout | None | No variant/FQBN | No drivers selected specifically for these parts | Not auditable as runnable targets | Coverage gap, not claimed working support |

The HOLYIOT-25007 and generic profiles intentionally share `variants/nrf54l15_module_36pin`. No separate L10/L05 board, core, startup path, linker script or variant exists; the only part-specific references are FICR constants/runtime identification.

## Build And Compile Results

The checkout was exposed under a temporary Arduino vendor namespace so the installed `nrf54l15clean` 0.9.212 package could not shadow it. The Board Manager-declared compiler was pinned explicitly:

```bash
arduino-cli compile \
  --config-file /tmp/nrf-audit-cli.yaml \
  --fqbn auditnrf54:auditnrf54:<board> \
  --build-property compiler.path=/home/lolren/.arduino15/packages/arduino/tools/arm-none-eabi-gcc/7-2017q4/bin/ \
  --build-path /tmp/nrf-gcc7-<board> \
  hardware/nrf54l15clean/nrf54l15clean/examples/Basics/CoreVersionProbe
```

| Profile | GCC 7 result | Program | RAM | Notes |
|---|---|---:|---:|---|
| XIAO L15 default | Pass | 10,808 B | 6,644 B | Default 64 MHz, VPR on |
| XIAO LM20A default | Pass | 14,508 B | 7,148 B | Menu says 64 MHz and hardware remains 64 MHz |
| HOLYIOT-25007 default | Pass | 9,888 B | 6,644 B | Compile cannot detect invalid UARTE route |
| HOLYIOT-25008 default | Pass | 9,888 B | 6,644 B | Compile cannot detect P2 interrupt limitation |
| Generic 36-pad default | Pass | 9,888 B | 6,644 B | Carrier not present for runtime test |
| PCA10156 DK default | Pass | 9,460 B | 6,644 B | DK has no 128 MHz menu |
| XIAO L15 combined nondefault | Pass | 10,832 B | 6,644 B | 128 MHz, VPR off, BLE off, trace on, Zigbee off |
| XIAO LM20 combined nondefault | Pass | 14,508 B | 7,148 B | 128 MHz macro selected, BLE/Zigbee off, trace on; hardware-clock bug remains |
| OpenThread stage L15 / LM20 | Pass / Pass | 326,924 / 330,484 B | 37,836 / 38,508 B | Full source tree, not default-excluded archive |
| Matter+Thread stage L15 / LM20 | Pass / Pass | 129,456 / 134,340 B | 16,164 / 16,860 B | Compile-only foundation probes |

Additional results:

* 83 top-level examples were compiled once on a selected/applicable XIAO target using the temporary namespace's resolved GCC 9.2.1: 81 passed and the two `SenseDelayRailRetentionProbe` copies failed. The two failures were independently reproduced with pinned GCC 7. The other 81 were not all rerun with GCC 7.
* Thirteen targeted HAL examples (`CacheDmaCoherence`, `CracenPke`, `FicrDump`, `MemconfPower`, `OscillatorsState`, PDM, two PWM, `Timer00`, two TWIS and others) all compiled. This confirms that compiler success does not detect the documented register faults.
* The GCC 9 example sweep emitted 10,356 repeated warning instances at 136 unique source locations, primarily in the large BLE implementation. The targeted HAL sweep emitted 3,129 instances at 128 unique locations. These counts include the same library warnings recompiled for many sketches and are warning-debt indicators, not 10,356 distinct defects.
* The six pinned-GCC7 smoke builds completed without reported warnings. Current package JSON parses and identifies platform version 0.9.215. The exact remotely published archive was not present locally, so its checksum/content could not be verified.
* Reproduced build logs were kept outside the repository under `/tmp/eport_local_core_examples_build.log`, `/tmp/eport-sense-gcc7.log` and related temporary build directories; they are not durable project artifacts.

## Function-Level Audit Appendix

For compactness, `P` below means `hardware/nrf54l15clean/nrf54l15clean`, and `HAL` means `P/libraries/Nrf54L15-Clean-Implementation/src`. Identical L15/LM20 copies and closely related overloads are grouped in one row; constructors, destructors, global objects and interrupt entry points are named explicitly. “OK” means no issue was established from the supplied documents in that reviewed function, not a formal proof. Imported third-party algorithms are not individually relisted; their integration boundaries are included near the end.

### Core, Startup And Arduino APIs

| File | Function | Purpose | Hardware touched | Boards/chips affected | Verdict | Finding ID if any |
|---|---|---|---|---|---|---|
| `P/cores/*/startup_*.S` | Vector table | Bind exceptions/peripheral IRQs | VTOR/NVIC entry addresses | All | Issue found | 001, 061, 067 |
| `P/cores/*/startup_*.S` | `Reset_Handler` | Copy data, zero BSS, call SystemInit/constructors/main | RRAM/RAM/CPU startup | All | OK in basic data/BSS/constructor order; early GRTC restore unclear | 025 |
| `P/cores/*/startup_*.S` | `Default_Handler` | Trap unhandled IRQs | CPU exception loop | All | Issue through wrongly/unmapped vectors | 001, 067 |
| `P/cores/*/system_*.c` | `SystemInit` | Errata, security, clock, reset startup | CLOCK/OSCILLATORS/RESET/security | All | Issues found | 007, 038 |
| `P/cores/nrf54l15/system_nrf54l15.c` | `zephyrErrata31/32/37/40` and raw writes | Apply anomaly workarounds | Undocumented registers | L15 | Unclear: errata document absent | Unverified item U-01 |
| `P/cores/nrf54l15/SPI.cpp` | anomaly-8 selection/write block | SPIM workaround | Undocumented SPIM offset `0xC84` | L15 | Unclear: errata document absent | U-01 |
| `P/cores/*/system_*.c` | `*_core_set/get_cpu_frequency_hz` | Runtime CPU clock control/report | PLL/CLOCK | All | Issue found | 006, 007 |
| `P/cores/*/system_*.c` | idle-scaling setters/enter/exit | Lower/restore CPU clock around WFI | PLL/CLOCK | All | Issue found | 006 |
| `P/cores/*/main.cpp` | `main`, `init` | Arduino setup/loop and idle service | CPU WFI, clocks, timers | All | Runtime scaling issue; early GRTC bootstrap missing | 006, 025 |
| `P/variants/*/variant.cpp` | `initVariant` and board rail helpers | Initialize onboard rails/RF switch/pins | GPIO, PMIC/external devices | Per board | XIAO mappings mostly match; non-XIAO hardware unclear | 005, U-03 |
| `P/variants/*/pins_arduino.h` | `pinToPortPin` | Convert Arduino number to GPIO | GPIO port identity | All | Issues for analog/P3/board limitations | 045, 064, 069 |
| `P/variants/*/pins_arduino.h` | `pinToSaadcChannel` | Map public analog pin to AIN | SAADC | All | Issue found on several variants | 045 |
| `P/variants/*/pins_arduino.h` | `digitalPinToPort/BitMask`, register accessors | Arduino fast-pin metadata | GPIO | All | OK in mapped P0-P3 paths | None |
| `P/variants/*/pins_arduino.h` | `digitalPinToInterrupt` | Validate interrupt-capable alias | GPIOTE/GPIO SENSE | All | Incomplete/wrong capability model | 008, 064, 069 |
| `P/variants/*/pins_arduino.h` | `analogInputToDigitalPin` | Analog index to Arduino pin | Variant metadata | All | LM20 A4 reverse-map issue | 045 |
| `P/cores/*/wiring_digital.c` | `pinMode` | Configure direction/pull | GPIO PIN_CNF | All | OK for decoded supported ports | None |
| `P/cores/*/wiring_digital.c` | `digitalWrite` | Set/clear output latch | GPIO OUTSET/OUTCLR | All | OK | None |
| `P/cores/*/wiring_digital.c` | `digitalRead` | Read input | GPIO IN | All | OK | None |
| `P/cores/*/wiring_digital.c` | `attachInterrupt` | Allocate/configure callback channel | GPIOTE/NVIC/GPIO | All | Issues found | 008, 041, 068, 069 |
| `P/cores/*/wiring_digital.c` | `detachInterrupt` | Disable/unlink callback | GPIOTE/NVIC | All | Wrong-instance and critical-state issues | 008, 041 |
| `P/cores/*/wiring_digital.c` | GPIOTE IRQ handlers | Clear events and invoke callbacks | GPIOTE20 | All | Wrong hardware instance/vector coverage | 001, 008 |
| `P/cores/*/wiring_digital.c` | `noInterrupts`, `interrupts` | Global interrupt control | PRIMASK | All | Issue found | 041 |
| `P/cores/*/wiring_digital.c` | `shiftOut`, `shiftIn` | Bit-banged serial helpers | GPIO | All | OK; blocking by design | None |
| `P/cores/*/wiring_time.c` | GRTC raw read/ready helpers | Stable monotonic counter read | GRTC SYSCOUNTER | All | Main stable-read logic OK | None |
| `P/cores/*/wiring_time.c` | LF clock source/start helpers | Select/start LFRC/LFXO | CLOCK/OSCILLATORS | All | LM20 ordering issue; non-XIAO crystal unclear | 021, U-03 |
| `P/cores/*/wiring_time.c` | GRTC initialization/bootstrap | Initialize millis/micros timebase | GRTC | All | Early wake bootstrap likely missing | 025 |
| `P/cores/*/wiring_time.c` | GRTC IRQ handlers/service fan-out | Maintain time and BLE wake | GRTC/NVIC | All | Depends on wrong vector table/service symbol integration | 001, 058 |
| `P/cores/*/wiring_time.c` | `millis`, `micros` | Arduino monotonic time | GRTC | All | No standalone arithmetic defect established | None |
| `P/cores/*/wiring_time.c` | `delay`, `delayMicroseconds` | Blocking delay/yield | GRTC/CPU cycles | All | Clock-changing path violates runtime rule | 006 |
| `P/cores/*/wiring_time.c` | compare arm/sync helpers | Program timed wake | GRTC | All | Issues found | 024, 025 |
| `P/cores/*/wiring_time.c` | `delaySystemOff*` | Returning low-power delay | GRTC/WFI | All | API behavior issue | 027 |
| `P/cores/*/wiring_time.c` | `systemOffWakeReset` | Nonreturning timed OFF | GRTC/REGULATORS/CLOCK | All | Issues found | 020, 021, 024, 025 |
| `P/cores/*/wiring_time.c` | core System OFF prepare/retention hooks | Quiesce board/memory before OFF | DMA peripherals, RRAM, GPIO | All | Incomplete and LM20 hooks disconnected | 020, 022 |
| `P/cores/*/wiring_analog.c` | `analogReadResolution`, `analogReference` | Store SAADC settings | SAADC config state | All | OK within implemented options | None |
| `P/cores/*/wiring_analog.c` | `analogRead` | Single ADC conversion | SAADC/GPIO | All | Variant alias issue; core polling path otherwise plausible | 045 |
| `P/cores/*/wiring_analog.c` | `analogWriteResolution/Frequency` | Configure PWM policy | PWM state | All | No separate defect established | None |
| `P/cores/*/wiring_analog.c` | `analogWrite`/PWM attach | Produce duty cycle | PWM/GPIO | All | Idle/latch issue | 039 |
| `P/cores/*/wiring_analog.c` | `tone`, `noTone` | Square-wave API | GPIO/software delay | All | Issue found | 040 |
| `P/cores/*/wiring_analog.c` | `pulseIn`, `pulseInLong` | Measure pulse width | GPIO/GRTC | All | Busy-wait behavior noted; no confirmed register defect | None |
| `P/cores/*/HardwareSerial.cpp` | constructor/global `Serial`, `Serial1` | Bind UARTE instances/pins | Shared SERIAL/UARTE | All | Board routing and LM20 resource conflict | 004, 009 |
| `P/cores/*/HardwareSerial.cpp` | baud conversion | Program BAUDRATE | UARTE clock | All/custom UARTE00 | UARTE00 issue | 015 |
| `P/cores/*/HardwareSerial.cpp` | config conversion | Program frame/parity/stop | UARTE CONFIG | All | Non-8-bit modes wrong | 016 |
| `P/cores/*/HardwareSerial.cpp` | `begin` | Configure pins, DMA, IRQ and enable | UARTE/NVIC/GPIO | All | IRQ, route, reserved-register issues | 001, 009, 010, 017 |
| `P/cores/*/HardwareSerial.cpp` | `end` | Stop/disable serial | UARTE EasyDMA | All | Issue found | 014 |
| `P/cores/*/HardwareSerial.cpp` | `available`, `peek`, `read` | Consume RX ring | UARTE DMA/shared ring | All | No independent defect established | None |
| `P/cores/*/HardwareSerial.cpp` | `flush` | Wait pending TX | UARTE/events | All | Event-offset dependency | 017 |
| `P/cores/*/HardwareSerial.cpp` | single-byte and buffer `write` | Queue/transmit data | UARTE EasyDMA | All | Caller-lifetime issue | 013 |
| `P/cores/*/HardwareSerial.cpp` | `setPins`/route helpers | Change UART pins | UARTE/GPIO | All | Missing instance capability validation | 010 |
| `P/cores/*/HardwareSerial.cpp` | UARTE IRQ handlers | Move DMA/ring state | UARTE/NVIC | All | Vector/event mapping issue | 001, 017 |
| `P/cores/*/SPI.cpp` | constructor/global SPI objects | Bind SPIM/pins | Shared SERIAL/SPIM | All | LM conflict; L15 globals both SPIM00 | 004, 011 |
| `P/cores/*/SPI.cpp` | `begin`, `end` | Configure/release pins and SPIM | SPIM/GPIO | All | Route/instance caveats | 004, 010 |
| `P/cores/*/SPI.cpp` | `beginTransaction`, settings conversion | Apply mode/order/frequency | SPIM/CLOCK | All | Frequency/runtime-clock issues | 006, 011, 012 |
| `P/cores/*/SPI.cpp` | `endTransaction` | Restore transaction state | SPIM/CLOCK | All | Runtime clock restore prohibited | 006 |
| `P/cores/*/SPI.cpp` | `transfer`, `transfer16`, buffer transfers | Synchronous DMA transaction | SPIM EasyDMA | All | No separate buffer-lifetime defect established in blocking path | None |
| `P/cores/*/SPI.cpp` | `setPins` | Reassign bus pins | SPIM/GPIO | All | Missing instance route validation | 010 |
| `P/cores/*/Wire.cpp` | constructor/global `Wire`, `Wire1` | Bind TWIM/TWIS instances/pins | Shared SERIAL/TWIM/TWIS | All | IRQ/route issues | 001, 010 |
| `P/cores/*/Wire.cpp` | controller/target `begin`, `end` | Configure mode/IRQ/pins | TWIM/TWIS/NVIC | All | Target IRQ mapping issue | 001 |
| `P/cores/*/Wire.cpp` | `setClock`, `setPins` | Configure timing/routing | TWIM/GPIO | All | Pin capability validation missing | 010 |
| `P/cores/*/Wire.cpp` | `beginTransmission`, `write` | Stage controller TX | RAM buffer | All | OK in nonempty staging path | None |
| `P/cores/*/Wire.cpp` | `endTransmission` | Execute controller write/probe | TWIM EasyDMA | All | Empty-probe/error-mask issues | 018, 019 |
| `P/cores/*/Wire.cpp` | `requestFrom` | Execute controller read | TWIM EasyDMA | All | Error mask issue; no other confirmed defect | 019 |
| `P/cores/*/Wire.cpp` | `available`, `read`, `peek`, `flush` | Manage receive buffer | RAM | All | OK | None |
| `P/cores/*/Wire.cpp` | `onReceive`, `onRequest`, target IRQ service | User target callbacks | TWIS/NVIC | All | Depends on wrong vector identity | 001 |
| `P/cores/*/Print.cpp` | `write` overloads, print/println/number/float | Format Stream output | Serial/backend only | All | `printSigned` edge UB | 044 |
| `P/cores/*/Stream.cpp` | timed read/find/parse/readString helpers | Buffered stream utilities | millis/backend | All | No confirmed issue in reviewed functions | None |
| `P/cores/*/WString.*` | constructors, destructor, copy/move/assignment | Own dynamic string | Heap | All | General ownership paths reviewed; concat issue | 042 |
| `P/cores/*/WString.h` | `concat` overloads/operators | Append text/numbers | Heap/libc | All | Issue found | 042 |
| `P/cores/*/SoftwareTimer.cpp` | constructor/destructor | Maintain intrusive global timer list | RAM | All | Deletion/traversal issue | 043 |
| `P/cores/*/SoftwareTimer.cpp` | `begin/start/stop/reset/setPeriod` | Configure timer state | millis/RAM | All | OK absent concurrent mutation | None |
| `P/cores/*/SoftwareTimer.cpp` | `serviceAll`, `serviceOne`, service hook | Dispatch callbacks | Timer list/millis | All | Use-after-free/reentrancy issue | 043 |
| `P/cores/*/wiring_random.c` | `randomSeed`, `random` overloads | Arduino PRNG | Software state | All | No confirmed arithmetic/hardware issue | None |
| `P/cores/*/wiring_math.c` | `map` | Range conversion | CPU arithmetic | All | Signed-overflow issue | 044 |
| `P/cores/*/nrf52_compat.cpp` | `sd_power_system_off` | nRF52 power compatibility | WFI/REGULATORS | All | Issue found | 023 |
| `P/cores/*/nrf52_compat.cpp` | other SoftDevice-style shims | Compatibility status/no-op surface | Mixed | All | Semantics vary; no additional datasheet finding isolated | None |
| `P/cores/*/Adafruit_TinyUSB.*` | global object and all stub methods | Source compatibility | None/SAMD11 bridge externally | All | Misleading success/no-op issue | 056 |
| `P/cores/*/syscalls.c` | `_sbrk` | Heap growth | Linker RAM symbols | All | LM20 stack collision | 028 |
| `P/cores/*/syscalls.c` | `_write`, `_read`, `_close`, `_fstat`, `_isatty`, `_lseek`, `_kill`, `_getpid` | newlib syscall shims | Serial/none | All | No additional confirmed hardware defect | None |
| `P/cores/*/abi.cpp` and runtime hooks | pure virtual/delete/guard/runtime support | C++ ABI | CPU/heap | All | Reviewed; no additional issue established | None |

### Register-Level HAL And First-Party Libraries

| File | Function | Purpose | Hardware touched | Boards/chips affected | Verdict | Finding ID if any |
|---|---|---|---|---|---|---|
| `HAL/nrf54l15_hal_support.cpp` | `Gpio::configure` | Configure generic HAL pin | GPIO PIN_CNF/DIR | All | LM20 P3 omitted | 064 |
| `HAL/nrf54l15_hal_support.cpp` | `Gpio::write/read/toggle` | Generic digital I/O | GPIO OUT/IN | All | OK on representable ports | None |
| `HAL/nrf54l15_hal_support.cpp` | PSEL/build/base helpers | Encode peripheral pins/bases | GPIO/peripheral routing | All | Shared port capability incomplete | 010, 064 |
| `HAL/nrf54l15_hal.cpp` | `ClockControl::*` | Clock-domain requests/releases | CLOCK | All | No separate confirmed defect; affected by wrong clock wrapper/runtime policy elsewhere | 006, 030 |
| `HAL/nrf54l15_hal.cpp` | `Spim::begin/end` | Configure raw SPIM | SPIM/GPIO | All | No additional map defect isolated; route ownership remains caller responsibility | 010 |
| `HAL/nrf54l15_hal.cpp` | `Spim::transfer` | Blocking EasyDMA transfer | SPIM EasyDMA | All | Reviewed; no separate confirmed defect | None |
| `HAL/nrf54l15_hal.cpp` | `Spis::*` | SPI target setup/transfer | SPIS EasyDMA | All | Register surface screened; runtime hardware not available | U-06 |
| `HAL/nrf54l15_hal.cpp` | `Twim::begin/end/transfer` | Raw I2C controller | TWIM EasyDMA | All | No additional issue beyond Arduino Wire paths | None |
| `HAL/nrf54l15_hal.cpp` | `Uarte::begin/end/read/write` | Raw UART API | UARTE EasyDMA | All | Instance/vector/shared-layout risks apply | 001, 010, 017 |
| `HAL/nrf54l15_hal.cpp` | `Timer::*` | Generic timer setup/capture/compare | TIMER | All | Typed generic implementation not separately contradicted; Timer00 raw helper is defective | 033 |
| `HAL/nrf54l15_hal_parts/nrf54l15_hal_peripherals.inc` | `Pwm::begin/end/start/stop` | Configure PWM and sequences | PWM EasyDMA/GPIO | All | Sequence/idle issues in related methods | 036, 039 |
| `HAL/nrf54l15_hal_parts/nrf54l15_hal_peripherals.inc` | PWM sequence setters | Program PTR/CNT/REFRESH/ENDDELAY | PWM EasyDMA | All | Sequence-1 stride issue | 036 |
| `HAL/nrf54l15_hal_parts/nrf54l15_hal_peripherals.inc` | `Pwm::setActiveHigh` | Change active/idle polarity | PWM/GPIO | All | Issue found | 070 |
| `HAL/nrf54l15_hal.cpp` | `Gpiote::*` | Raw channel/event configuration | GPIOTE | All | Generic class screened; Arduino instance selection wrong | 008 |
| `HAL/nrf54l15_hal.cpp` | `Dppic::*` | Enable/configure DPPI channels | DPPIC | All | No confirmed register mismatch | None |
| `HAL/nrf54l15_hal.cpp` | `Egu::*` | Trigger/clear EGU events | EGU/NVIC | All | No confirmed register mismatch | None |
| `HAL/nrf54l15_hal_crypto_analog.inc` | `CracenRng::begin/end/fill/randomWord` | Hardware entropy | CRACEN/RNG FIFO | All | LM20 layout/health issue | 065 |
| `HAL/nrf54l15_hal_security.cpp` | `Kmu` constructor/status/wait helpers | Access key slots/tasks | KMU | All secure builds | Basic task/event map reviewed | None |
| `HAL/nrf54l15_hal_security.cpp` | `Kmu::provision`/RRAM helpers | Provision key material | KMU/RRAMC | All secure builds | Buffered-write sequencing likely wrong | 063 |
| `HAL/nrf54l15_hal_security.cpp` | `Kmu::push/revoke/readMetadata` | Operate key slots | KMU | All secure builds | No additional confirmed defect | None |
| `HAL/nrf54l15_hal_security.cpp` | `CracenIkg::*` | Internal key generation | CRACEN/IKG/KMU | All secure builds | Integration screened; no independent issue established | None |
| `HAL/nrf54l15_hal_security.cpp` | `Tampc::*` | Configure tamper/security state | TAMPC | All | LM20 base wrong | 035 |
| `HAL/nrf54l15_hal_crypto_analog.inc` | `Aar::*`, `Ecb::*`, `Ccm::*` | BLE address resolution/AES/CCM | CRACEN/AAR/ECB/CCM/VDMA | All | IRQ identities wrong on LM20; no other confirmed field mismatch | 001 |
| `HAL/nrf54l15_hal_crypto_analog.inc` | `Comp::*`, `Lpcomp::*` | Analog compare/wakeup | COMP/LPCOMP/GPIO | All | Register map screened; no confirmed issue | None |
| `HAL/nrf54l15_hal_crypto_analog.inc` | `Qdec::begin/end/read` | Quadrature decoder | QDEC/GPIO | All | Port validation/P3 issue | 064 |
| `HAL/nrf54l15_hal_crypto_analog.inc` | `Saadc::configure*` | Configure channels/gain/reference | SAADC/GPIO | All | Register fields/stride checked OK | None |
| `HAL/nrf54l15_hal_crypto_analog.inc` | `Saadc::sampleRaw/sampleMilliVolts` | Blocking one-shot conversion | SAADC EasyDMA | All | Timeout DMA lifetime issue | 062 |
| `HAL/nrf54l15_hal_board_policy.cpp` | `BoardControl::*` | Board rails/RF/battery policy | GPIO/PMIC | XIAO-focused | XIAO schematic reviewed; non-XIAO board state unclear | 005, U-03 |
| `HAL/nrf54l15_hal_timebase.cpp` | `PowerManager` clock/frequency APIs | Expose frequency/idle controls | CLOCK/PLL | All | Prohibited runtime switching | 006 |
| `HAL/nrf54l15_hal_timebase.cpp` | `PowerManager::systemOff*` and hooks | Enter/prep System OFF | GRTC/REGULATORS/RRAM | All | LM20 hooks disconnected/incomplete | 020, 022 |
| `HAL/nrf54l15_hal_timebase.cpp` | `Grtc::begin/read` | Initialize/read counter | GRTC | All | Stable-read portions OK | None |
| `HAL/nrf54l15_hal_timebase.cpp` | `Grtc` compare methods | Arm relative/absolute compare | GRTC | All | Enable/sleep semantics wrong | 026 |
| `HAL/nrf54l15_hal_timebase.cpp` | `GrtcPwm::*` | GRTC-driven low-frequency PWM | GRTC/DPPI/GPIOTE | All | Integration screened; no separate confirmed mismatch | None |
| `HAL/nrf54l15_hal_crypto_analog.inc` | `TempSensor::*` | Temperature sampling | TEMP | All | Register path screened; no confirmed issue | None |
| `HAL/nrf54l15_hal_crypto_analog.inc` | `Watchdog::configure/start/stop/feed` | WDT lifecycle | WDT | All | Timeout/TSEN issue | 071 |
| `HAL/nrf54l15_hal_crypto_analog.inc` | `Pdm::begin/start/stop/end` | Microphone capture | PDM EasyDMA/GPIO | All | DMA count overrun | 003 |
| `HAL/nrf54l15_hal_i2s.inc` | `I2sTx` constructor/config/start/stop/end | I2S transmit stream | I2S EasyDMA | L15; wrongly LM20 | Byte-count issue; nonexistent LM20 block | 059, 061 |
| `HAL/nrf54l15_hal_i2s.inc` | `I2sTx::onIrq` | Rotate/refill TX buffers | I2S/NVIC | L15 | Active-buffer callback issue | 060 |
| `HAL/nrf54l15_hal_i2s.inc` | `I2sRx` constructor/config/start/stop/end | I2S receive stream | I2S EasyDMA | L15; wrongly LM20 | Byte-count issue; nonexistent LM20 block | 059, 061 |
| `HAL/nrf54l15_hal_i2s.inc` | `I2sRx::onIrq` | Rotate/deliver RX buffers | I2S/NVIC | L15 | Premature callback issue | 060 |
| `HAL/nrf54l15_hal_i2s.inc` | `I2sDuplex` lifecycle | Full-duplex audio | I2S EasyDMA | L15; wrongly LM20 | Byte-count/nonexistent block | 059, 061 |
| `HAL/nrf54l15_hal_i2s.inc` | `I2sDuplex::onIrq` | Rotate TX/RX buffers | I2S/NVIC | L15 | Both callback directions wrong | 060 |
| `HAL/nrf54l15_hal_ficr.h` | identity/part/variant helpers | Read factory identity | FICR | All | Multiple issues | 037 |
| `HAL/nrf54l15_hal_ficr.h` | trim helpers | Decode oscillator trims | FICR/OSCILLATORS | All | Field extraction issue | 037 |
| `HAL/nrf54l15_hal_cache.h` | constructor/all cache operations | Cache control/maintenance | Purported ICACHE | All | Entire map targets other blocks | 002 |
| `HAL/nrf54l15_hal_memconf.h` | constructor/all methods | RAM power/retention/protection | MEMCONF | All | Entire task/status model invalid | 029 |
| `HAL/nrf54l15_hal_oscillators.h` | start/stop/status/calibrate methods | Clock source control | OSCILLATORS/CLOCK | All | Wrong base/block model | 030 |
| `HAL/nrf54l15_hal_timer00.h` | constructor/config/start/stop/count/compare | Timer00 convenience API | TIMER00 | All | Multiple register/semantic issues | 033 |
| `HAL/nrf54l15_hal_twis.h` | begin/end/address/event helpers | I2C target setup | TWIS | All | General setup screened | None |
| `HAL/nrf54l15_hal_twis.h` | TX buffer/amount methods | Target response DMA | TWIS EasyDMA | All | Shifted offsets | 032 |
| `HAL/nrf54l15_hal_nfct.h` | sense/activate/disable/task/event/config helpers | NFC Type A peripheral | NFCT | All | Register map checked OK | None |
| `HAL/nrf54l15_hal_nfct.h` | interrupt enable/disable helpers | NFCT event IRQ masks | NFCT/NVIC | All | Vector integration likely broken | 067 |
| `HAL/nrf54l15_hal_cracen_pke.h` | enable/status/interrupt/operation helpers | Public-key engine | CRACEN/PKE | All | Wrong offsets/bases/disable | 034 |
| `HAL/nrf54l15_hal_ble_periodic.h` | constructor/config/start/stop/event service | Raw periodic advertising | RADIO | All | Obsolete register map/state flow | 031 |
| `HAL/nrf54l15_hal.cpp` and BLE `.inc` parts | `BleRadio` constructor/begin/end | Own/configure BLE radio state | RADIO, GRTC, CRACEN, GPIO | All | IRQ dependency; full protocol validation unavailable | 001, U-05 |
| Same BLE units | advertising/scanning methods | Build/send/receive legacy/extended ads | RADIO/GRTC/DPPI | All | Large surface static-screened; raw periodic helper separately wrong | 031, U-05 |
| Same BLE units | connection/encryption/SMP methods | Link layer and security | RADIO/GRTC/CRACEN/RRAM | All | Integration build-reviewed; no protocol proof | 057, 065, U-05 |
| `HAL/nrf54l15_hal.cpp` | `nrf54l15_ble_grtc_irq_service` | Service BLE compare/timeouts | GRTC/BLE state | All | Strong symbol conflicts with two examples | 058 |
| `HAL/zigbee_*.cpp`, radio parts | `ZigbeeRadio::*`, commissioning/security/persistence APIs | 802.15.4/Zigbee stack | RADIO, CRACEN, RRAM | All build targets | Inventory/build/integration review only | U-05 |
| `HAL/nrf54l15_hal.cpp` | `RawRadioLink::*` | Raw 802.15.4-like radio transactions | RADIO | All | Static-screened; RF runtime not available | U-05 |
| `HAL/npm1300.cpp` | bus/read/write/update helpers | PMIC I2C access | TWIM/nPM1300 | XIAO LM20 | Bus flow screened; no separate defect | None |
| `HAL/npm1300.cpp` | voltage conversion, BUCK/LDO setters | Configure regulator rails | nPM1300 regulators | XIAO LM20 | Voltage encoding issue | 005 |
| `HAL/npm1300.cpp` | charger current/termination/input-limit methods | Configure charger | nPM1300 charger | XIAO LM20 | Register/units screened; no separate confirmed issue | None |
| `HAL/npm1300.cpp` | ADC read/convert methods | Battery/current/temp telemetry | nPM1300 ADC | XIAO LM20 | Screened against supplied PS; no separate confirmed issue | None |
| `HAL/npm1300.cpp` | hibernate/ship/LED/GPIO methods | PMIC low power and auxiliaries | nPM1300 | XIAO LM20 | Hardware execution unavailable; no additional definite mismatch | U-04 |
| `P/libraries/EEPROM/src/EEPROM.cpp` | blob CRC/read/write/commit helpers | Persistent EEPROM emulation | RRAM/RRAMC | All | Non-secure alias concern | 066 |
| Same | `EEPROMClass::begin/end/read/write/update/commit` | Arduino EEPROM API | RRAM/RAM | All | Secure default path screened; NS latent issue | 066 |
| `P/libraries/Preferences/src/Preferences.cpp` | blob validation/read/write/commit/index helpers | Key/value persistence | RRAM/RRAMC | All | Non-secure alias/shared buffering concern | 063, 066 |
| Same | `Preferences::begin/end/clear/remove/put*/get*` | Public Preferences API | RRAM/RAM | All | Core serialization screened; no additional issue established | None |
| `P/libraries/Adafruit_SPIFlash/src/*` | flash transport/device APIs | External SPI flash | SPI/QSPI pins | Board-dependent | Integration affected by SPI instance/speed; imported library not re-proven | 004, 012, U-04 |

### Protocol, Coprocessor, Build And Tooling Boundaries

| File | Function | Purpose | Hardware touched | Boards/chips affected | Verdict | Finding ID if any |
|---|---|---|---|---|---|---|
| `HAL/openthread_platform_nrf54l15.cpp` | platform init/process/state helpers | Bind staged OpenThread core | RADIO/time/settings | L15/LM20 stage builds | Source-local builds pass; exact archive misses dependency | 049 |
| Same | `otPlatRadioEnable/Disable/Sleep/Receive/Transmit` and service helpers | OpenThread radio operations | RADIO/GRTC | L15/LM20 | Integration/static review only; no RF interoperability test | U-05 |
| Same | energy scan/source match/receive-at methods | 802.15.4 timing/filter support | RADIO/timers | L15/LM20 | Build paths pass; protocol timing not externally validated | U-05 |
| Same | `otPlatEntropyGet`, pseudo-entropy helpers | Platform entropy | CRACEN/software | L15/LM20 | No separate confirmed finding; complete security review requires OpenThread spec | U-05 |
| Same | settings get/set/add/delete/wipe helpers | OpenThread persistence | Preferences/RRAM | L15/LM20 | Inherits RRAM concerns | 063, 066 |
| Same | AES/SHA/HMAC/HKDF/key-reference helpers | OpenThread crypto platform | CPU/CRACEN/KMU | L15/LM20 | Integration screened; imported/spec behavior not re-proven | U-05 |
| `HAL/openthread_core_stage_bridge.cpp` | stage bridge/export hooks | Link staged upstream core | None directly | L15/LM20 | Full-source compile passes; omitted from default archive | 049 |
| `HAL/matter_rng.cpp` | `MatterRng::begin/end/getRandom*/healthy` | Fail-closed Matter entropy facade | CRACEN RNG | L15/LM20 | Stated policy sound; LM20 health backend is wrong | 065 |
| `HAL/matter_secp256r1.cpp` | bignum/point/scalar methods | P-256 arithmetic | CPU | L15/LM20 | Algorithm implementation not cryptographically re-proven | U-05 |
| Same | `generateRandomScalar/randomBytes/randomWord/generateKeyPair/ecdsaSign` | ECC secrets/nonces | CRACEN RNG/software | L15/LM20 | Predictable fallback needs security disposition | 057 |
| `HAL/matter_pbkdf2.cpp` | PBKDF2/HMAC derivation APIs | Matter password derivation | CPU/crypto | L15/LM20 | Compile/integration reviewed; standard vectors not run in this audit | U-05 |
| `HAL/matter_pase_commissioning.cpp` | PASE session/state handlers | Matter commissioning | Crypto/network | L15/LM20 | Build-only coverage; Matter spec absent | U-05 |
| `HAL/matter_case_session.cpp` | CASE session/state handlers | Operational secure session | Crypto/network | L15/LM20 | Build-only coverage; entropy finding applies | 057, U-05 |
| `HAL/matter_fabric_table.cpp` | fabric add/remove/load/store | Matter fabric persistence | Preferences/RRAM | L15/LM20 | Inherits persistent-storage concerns; no separate finding | 063, 066 |
| `HAL/matter_credentials.cpp`, `matter_device_attestation.cpp` | credential/attestation APIs | Device identity and signatures | RRAM/crypto | L15/LM20 | Requires real credential provisioning/spec validation | U-05 |
| `HAL/matter_access_control.cpp`, scenes/endpoint/light files | data-model/application handlers | Matter endpoint behavior | Software/network | L15/LM20 | Compile/integration reviewed | U-05 |
| `HAL/matter_platform_stage.cpp`, foundation bridge | platform/event/time/random glue | Matter platform | GRTC/crypto/network | L15/LM20 | Source build passes; packaging defect | 049 |
| `HAL/nrf54l15_vpr.cpp` | VPR load/start/stop/status methods | Manage coprocessor image | VPR/RAM/IPC | L15 | Link/build reviewed; no hardware execution | U-07 |
| `HAL/vpr_softperipheral_manager.cpp` | allocate/start/stop RPC services | VPR soft peripherals | IPC/VPR | L15 | Build/static review only | U-07 |
| `HAL/vpr_sqspi.cpp` | SQSPI RPC/transfer methods | VPR-driven SPI | IPC/SPIM/GPIO | L15 | Hardware/firmware pair not exercised | U-07 |
| `HAL/ble_nus.cpp`, `P/libraries/Bluefruit52Lib/src/*` | NUS/Bluefruit begin, advertising, GATT, connection APIs | Arduino BLE compatibility | BLE HAL/RADIO/GRTC | All | Integration depends on IRQ/clock defects; protocol surface not fully interoperated | 001, 006, U-05 |
| `HAL/ble_channel_sounding.cpp` and profile helpers | channel-sounding procedures/calibration | RADIO/BLE/crypto/VPR | L15/LM20 | Static/build review and local docs only; no peer/radio validation | U-05 |
| `HAL/zigbee_stack.cpp`, commissioning/security/persistence | Zigbee public stack surface | RADIO/CRACEN/RRAM | All enabled profiles | Build/integration review only | 063, 065, U-05 |
| `scripts/build_release.py` | argument parsing/archive/index/tool builders | Release packaging | Host filesystem | Package consumers | Default dependency exclusion issue | 049 |
| `tools/release.sh` | monolithic release flow | Release packaging/git/index | Host tools | Package consumers | Diverges from Python release path | 052 |
| `scripts/build_all_examples.py` | discovery/menu selection/build/main | Example matrix | Compiler/linker | Listed boards | Omits important profiles/features | 053 |
| `scripts/test_all.sh` | `compile` and scenarios | Example smoke matrix | Compiler/linker | XIAO profiles mainly | Masks/omits failures and exits zero | 053 |
| `scripts/test_all_examples.py` | `run` and main loop | Fixed example subset | Compiler/linker | XIAO profiles | Hardcoded installed version/no failing exit | 053 |
| `scripts/verify_package_index.py`, `verify_public_release.py` | index/archive checks | Release validation | Host JSON/archive | Package consumers | Current JSON parses; duplicate-version validation insufficient | 054 |
| `P/tools/uf2/uf2_emit.py` | UF2 conversion | Firmware image encoding | Host filesystem | All | Portable converter exists and is used on Unix | None |
| `P/platform.txt` Windows objcopy recipe | Windows UF2 postprocess | Firmware artifact | Host shell | Windows/all boards | Does not call converter | 050 |
| `P/tools/upload.py` | runner selection/recovery upload | SWD/UF2/pyOCD | Debug probes/bootloader | All | Main recipe present; programmer-selection path separately broken | 051 |
| `P/programmers.txt` plus platform programmer recipes | programmer metadata | IDE/CLI programmer upload | SWD | All | Incomplete/malformed | 051 |
| `package_nrf54l15clean*_index.json` | platform/tool entries | Board Manager installation | Host package manager | All | Parse/current version OK; duplicate historical version | 054 |
| 83 top-level `examples/**/*.ino` | `setup`, `loop`, sketch hooks | Varies | Applicable XIAO profile | 81 GCC9 passes; two confirmed link failures | 058 |
| 13 targeted HAL examples | `setup`, `loop` | Cache/crypto/FICR/MEMCONF/clock/PDM/PWM/TIMER/TWIS | L15/LM20 as selected | Compile passes despite hardware mismatches | 002, 003, 029-036 |

## Unverified / Ambiguous Items

* **U-01: Errata/anomaly writes.** No Nordic errata document was supplied. `system_nrf54l15.c:21-27,112-166` applies “Zephyr errata” 31/32/37/40 with raw addresses and magic values, and `SPI.cpp:45-46,553-559` applies anomaly 8 through raw offset `0xC84`. Their applicability, revision tests, values and ordering cannot be confirmed from the product specification alone.
* **U-02: Exact published package.** Local indexes identify version 0.9.215, but the referenced remote archive was not supplied. The audit verified a locally staged archive and current source; it cannot assert that an already-uploaded archive is byte-identical or symlink-free.
* **U-03: Missing board schematics.** No HOLYIOT-25007, HOLYIOT-25008, generic-carrier or PCA10156 schematic was present. LED/button polarity, reserved pins, power rails, antenna switch, voltage domains and LFXO population on those boards remain unconfirmed. The XIAO L15 and LM20 schematics were supplied and checked.
* **U-04: External hardware execution.** nPM1300 rail voltages, QSPI flash, charger/hibernate, microphone, IMU and RF switch behavior were not exercised on physical boards. The voltage-code defect is definite from the local PMIC PS; other analog/current/timing behavior still needs bench validation.
* **U-05: Protocol conformance/security.** No Bluetooth Core, IEEE 802.15.4, Zigbee, OpenThread or Matter standard/test suite was supplied. First-party integration was static/build-audited and obvious hardware/security defects were reported, but BLE/Thread/Zigbee/Matter conformance, RF timing, channel sounding and cryptographic algorithm correctness are not certified by this audit.
* **U-06: Less-used peripheral runtime behavior.** SPIS, EGU, DPPI, COMP/LPCOMP, TEMP and raw-radio paths were register-screened but not exercised on hardware. No issue was invented where the local PS/code did not establish one.
* **U-07: VPR/FLPR.** L15 VPR-enabled/disabled link configurations compile and their explicit reservations are internally bounded, but VPR image loading, IPC transport, SQSPI firmware and low-power restoration were not run on silicon. LM20 has no selected VPR support in its board profile.
* **LFXO timeout/calibration.** L15 `ensureSystemOffLfxoRunning()` uses a 120,000,000-spin timeout described as 128 MHz calibrated even though boards default to 64 MHz. On an unschematized board without LFXO this may impose a long delay; exact duration/current requires a board schematic and bench trace. XIAO L15 does have LFXO on P1.00/P1.01.
* **nRF54L10/L05 target support.** The combined L15 PS covers these parts and FICR helpers recognize their PART values, but the repository supplies no FQBN, variant, startup, chip-selected header/core or linker layout. There is therefore no runnable L10/L05 configuration to build or audit.
* **TrustZone partitioning.** FINDING-066 remains needs-human-verification because no secure boot/SPU partition source or security architecture document was supplied for a prospective non-secure build.
* **Matter entropy requirement.** FINDING-057 contradicts the repository's own fail-closed comment and uses predictably seeded xorshift output, but final Matter certification/security disposition needs the absent Matter requirements and threat model.
* **Wake-current/timing figures.** System OFF sequence errors are established from the PS, but actual current, RTCOMPARESYNC timing, reset cause and external-rail state require power-analyzer/debugger testing on each physical board.
* **Examples matrix limit.** The audit did not compile all 445 `.ino` files against all six profiles (2,670 combinations). It compiled all 83 top-level core examples once on an applicable XIAO target, six-board smoke/menu probes and 13 targeted HAL examples. Imported library example compatibility remains broader than the executed matrix.

## Recommended Priority Fix List

### 1. Must fix before users rely on the core

* Rebuild device definitions and vectors per chip: FINDING-001, FINDING-008, FINDING-035, FINDING-061, FINDING-065 and FINDING-067.
* Remove or quarantine drivers with demonstrably false register maps: FINDING-002, FINDING-029, FINDING-030, FINDING-031, FINDING-032, FINDING-033 and FINDING-034.
* Eliminate memory/DMA corruption: FINDING-003, FINDING-013, FINDING-014, FINDING-036, FINDING-059, FINDING-060 and FINDING-062.
* Resolve default-board resource/routing faults: FINDING-004, FINDING-005, FINDING-009, FINDING-011 and FINDING-012.
* Remove runtime PLL switching and make boot frequency truthful: FINDING-006 and FINDING-007.
* Replace System OFF with a documented, verified shutdown/wake sequence: FINDING-020, FINDING-021, FINDING-022, FINDING-023, FINDING-024 and FINDING-025.
* Correct security-critical entropy/provisioning behavior before enabling Matter/security features: FINDING-057, FINDING-063 and FINDING-065.
* Validate the exact installable archive and include all advertised feature sources: FINDING-049 and FINDING-050.

### 2. Should fix before release

* Enforce peripheral route/resource ownership and accurate public configurations: FINDING-010, FINDING-015, FINDING-016, FINDING-017, FINDING-018 and FINDING-019.
* Correct remaining GRTC/low-power API semantics: FINDING-026 and FINDING-027.
* Fix linker, identity, reset and analog/PWM mappings: FINDING-028, FINDING-037, FINDING-038, FINDING-039, FINDING-045, FINDING-046 and FINDING-070.
* Make core utility/callback code interrupt- and lifetime-safe: FINDING-040, FINDING-041, FINDING-042 and FINDING-043.
* Repair release/upload/test gates so failures block publication: FINDING-051, FINDING-052, FINDING-053 and FINDING-058.
* Decide and enforce TrustZone/NFCT/QDEC capability contracts: FINDING-064, FINDING-066, FINDING-067 and FINDING-068.

### 3. Nice to fix

* Remove arithmetic edge UB and reclaim/document unused LM20 RAM: FINDING-044 and FINDING-047.
* Select the LM20 linker/header directory explicitly: FINDING-048.
* Remove duplicate package history, make compatibility stubs honest and tighten WDT behavior: FINDING-054, FINDING-056 and FINDING-071.

### 4. Documentation-only fixes

* Correct the nonexistent Serial routing menu claim: FINDING-055.
* Document HOLYIOT-25008 accelerometer interrupt pins as polling-only: FINDING-069.
* State explicitly that nRF54L10/L05 are recognized by PART ID but are not build targets.
* Add the missing HOLYIOT/PCA10156 schematics, Nordic errata and security/protocol requirements to the local audit reference set.

## BLE Completion Follow-up - 2026-07-12

The current source tree closes another set of concrete BLE implementation and
compatibility gaps. This is an implementation and regression-test statement,
not a Bluetooth SIG qualification claim.

* Bond storage now supports eight peers with A/B power-loss-safe replicas,
  legacy-record migration, LRU replacement, indexed inspection/deletion, and
  per-peer privacy, signing, CCCD, and Service Changed state.
* Pairing policy is explicit per transaction: bonding, MITM, Secure
  Connections allowed/required, and minimum/maximum encryption-key sizes are
  snapshotted when pairing starts. Request-scoped passkey input rejects stale
  replies and preserves leading zeroes.
* Legacy SMP now performs association-model selection and role-correct local
  and peer encryption-, identity-, and signing-key distribution. Exact
  retransmissions are idempotent; conflicting or out-of-order key PDUs fail
  closed.
* Directed advertising, deferred dynamic GATT authorization, peripheral
  indications, central indication confirmation, Service Changed persistence,
  and bounded central service/characteristic discovery are implemented with
  new examples and contract tests.
* Central client object lifetime and deferred-event handling now use connection
  generations and deregistration, preventing stale callbacks and slot reuse
  from targeting destroyed client objects.
* Periodic advertising remains unsupported and now reports that fact
  fail-closed. The compatibility API no longer stores data or reports success
  for a radio procedure that the controller does not transmit.

Two connected boards were used for runtime verification: a XIAO nRF54L15 and a
XIAO nRF54LM20A. A fresh Just Works pairing completed key distribution, saved
the bond, and carried encrypted notifications and writes. After reset, both
boards selected the saved peer and restored encryption without a new pairing.
A separate Bluefruit fixed-PIN run completed with encryption and authenticated
MITM state on both boards, discovered the secure UART service, exchanged GATT
traffic, and then repeated the encrypted bonded reconnect without another PIN
prompt.

Remaining BLE work is tracked honestly in `docs/BLE_COMPLIANCE_RESUME.md`.
Important gaps include GATT Robust Caching, LE Credit Based L2CAP Channels,
extended/periodic advertising, persisted per-peer repeated-attempt throttling,
and broader phone/controller interoperability and conformance testing.

## Machine-Readable Summary

The following is the valid JSON snapshot of the original 2026-07-09 audit. It
is retained for provenance and is not a current remediation-status manifest;
in particular, consult the 2026-07-12 corrections for FINDING-003,
FINDING-034 and FINDING-065. File paths are repository-relative and
intentionally abbreviated to the smallest ownership-relevant set.

```json
{
  "audit_date": "2026-07-09",
  "repository": "/home/lolren/Desktop/eport_nrf54/nrf54-arduino-core",
  "counts": {"definite": 65, "likely": 4, "needs_human_verification": 2, "total": 71},
  "findings": [
    {"id":"FINDING-001","severity":"Critical","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/cmsis.h","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/cmsis.h","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/startup_nrf54l15.S","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/startup_nrf54lm20b.S"],"affected_boards":["All"]},
    {"id":"FINDING-002","severity":"Critical","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_cache.h"],"affected_boards":["All"]},
    {"id":"FINDING-003","severity":"Critical","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_crypto_analog.inc"],"affected_boards":["All PDM users","XIAO Sense boards"]},
    {"id":"FINDING-004","severity":"Critical","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/SPI.cpp","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/HardwareSerial.cpp"],"affected_boards":["XIAO nRF54LM20A/B"]},
    {"id":"FINDING-005","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/npm1300.cpp"],"affected_boards":["XIAO nRF54LM20A/B"]},
    {"id":"FINDING-006","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/system_nrf54l15.c","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/system_nrf54lm20b.c","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/SPI.cpp","hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_timebase.cpp"],"affected_boards":["All"]},
    {"id":"FINDING-007","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/boards.txt","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/system_nrf54lm20b.c"],"affected_boards":["XIAO nRF54LM20A/B"]},
    {"id":"FINDING-008","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/wiring_digital.c","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/wiring_digital.c","hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_regs.h"],"affected_boards":["All"]},
    {"id":"FINDING-009","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/boards.txt","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/HardwareSerial.cpp"],"affected_boards":["HOLYIOT-25007","Generic 36-pad","PCA10156 DK"]},
    {"id":"FINDING-010","severity":"Medium","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/SPI.cpp","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/Wire.cpp","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/HardwareSerial.cpp","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/SPI.cpp","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/Wire.cpp","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/HardwareSerial.cpp"],"affected_boards":["All"]},
    {"id":"FINDING-011","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/SPI.cpp","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/SPI.h"],"affected_boards":["All nRF54L15 boards"]},
    {"id":"FINDING-012","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/SPI.cpp"],"affected_boards":["XIAO nRF54LM20A/B"]},
    {"id":"FINDING-013","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/HardwareSerial.cpp","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/HardwareSerial.cpp"],"affected_boards":["All"]},
    {"id":"FINDING-014","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/HardwareSerial.cpp","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/HardwareSerial.cpp"],"affected_boards":["All"]},
    {"id":"FINDING-015","severity":"Medium","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/HardwareSerial.cpp","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/HardwareSerial.cpp"],"affected_boards":["Custom UARTE00 users"]},
    {"id":"FINDING-016","severity":"Medium","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/Arduino.h","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/HardwareSerial.cpp","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/Arduino.h","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/HardwareSerial.cpp"],"affected_boards":["All"]},
    {"id":"FINDING-017","severity":"Medium","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/HardwareSerial.cpp","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/HardwareSerial.cpp"],"affected_boards":["All"]},
    {"id":"FINDING-018","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/Wire.cpp","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/Wire.cpp"],"affected_boards":["All"]},
    {"id":"FINDING-019","severity":"Medium","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/Wire.cpp","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/Wire.cpp"],"affected_boards":["All"]},
    {"id":"FINDING-020","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/wiring_time.c","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/wiring_time.c"],"affected_boards":["All"]},
    {"id":"FINDING-021","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/wiring_time.c"],"affected_boards":["XIAO nRF54LM20A/B"]},
    {"id":"FINDING-022","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_timebase.cpp","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/wiring_time.c"],"affected_boards":["XIAO nRF54LM20A/B"]},
    {"id":"FINDING-023","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/nrf52_compat.cpp","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/nrf52_compat.cpp"],"affected_boards":["All"]},
    {"id":"FINDING-024","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/wiring_time.c","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/wiring_time.c"],"affected_boards":["All"]},
    {"id":"FINDING-025","severity":"High","confidence":"Medium","status":"Likely","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/wiring_time.c","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/wiring_time.c","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/main.cpp"],"affected_boards":["All"]},
    {"id":"FINDING-026","severity":"Medium","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_timebase.cpp"],"affected_boards":["All"]},
    {"id":"FINDING-027","severity":"Medium","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/wiring_time.c","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/wiring_time.c"],"affected_boards":["All"]},
    {"id":"FINDING-028","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/nrf54lm20b_linker_script.ld","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/nrf54lm20b_linker_script.ld"],"affected_boards":["XIAO nRF54LM20A/B"]},
    {"id":"FINDING-029","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_memconf.h"],"affected_boards":["All"]},
    {"id":"FINDING-030","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_oscillators.h"],"affected_boards":["All"]},
    {"id":"FINDING-031","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_ble_periodic.h"],"affected_boards":["All raw periodic-advertising users"]},
    {"id":"FINDING-032","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_twis.h"],"affected_boards":["All TWIS users"]},
    {"id":"FINDING-033","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_timer00.h"],"affected_boards":["All HAL Timer00 users"]},
    {"id":"FINDING-034","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_cracen_pke.h"],"affected_boards":["All"]},
    {"id":"FINDING-035","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/nrf54lm20b.h","hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_regs.h"],"affected_boards":["XIAO nRF54LM20A/B"]},
    {"id":"FINDING-036","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_regs.h","hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_peripherals.inc"],"affected_boards":["All HAL PWM users"]},
    {"id":"FINDING-037","severity":"Medium","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_ficr.h"],"affected_boards":["All"]},
    {"id":"FINDING-038","severity":"Medium","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/system_nrf54l15.c","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/system_nrf54lm20b.c"],"affected_boards":["All"]},
    {"id":"FINDING-039","severity":"Medium","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/wiring_analog.c","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/wiring_analog.c"],"affected_boards":["All"]},
    {"id":"FINDING-040","severity":"Medium","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/wiring_analog.c","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/wiring_analog.c"],"affected_boards":["All"]},
    {"id":"FINDING-041","severity":"Medium","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/wiring_digital.c","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/wiring_digital.c"],"affected_boards":["All"]},
    {"id":"FINDING-042","severity":"Medium","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/WString.h","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/WString.h"],"affected_boards":["All"]},
    {"id":"FINDING-043","severity":"Medium","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/SoftwareTimer.cpp","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/SoftwareTimer.cpp"],"affected_boards":["All"]},
    {"id":"FINDING-044","severity":"Low","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/Print.cpp","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/wiring_math.c","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/Print.cpp","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/wiring_math.c"],"affected_boards":["All"]},
    {"id":"FINDING-045","severity":"Medium","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/variants/xiao_nrf54l15/pins_arduino.h","hardware/nrf54l15clean/nrf54l15clean/variants/xiao_nrf54lm20b/pins_arduino.h","hardware/nrf54l15clean/nrf54l15clean/variants/nrf54l15_module_36pin/pins_arduino.h","hardware/nrf54l15clean/nrf54l15clean/variants/holyiot_25008_nrf54l15/pins_arduino.h"],"affected_boards":["XIAO L15","XIAO LM20A","HOLYIOT-25007","HOLYIOT-25008","Generic 36-pad"]},
    {"id":"FINDING-046","severity":"Medium","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/boards.txt"],"affected_boards":["XIAO nRF54LM20A"]},
    {"id":"FINDING-047","severity":"Low","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/nrf54lm20b_linker_script.ld","hardware/nrf54l15clean/nrf54l15clean/boards.txt"],"affected_boards":["XIAO nRF54LM20A/B"]},
    {"id":"FINDING-048","severity":"Low","confidence":"Medium","status":"Likely","affected_files":["hardware/nrf54l15clean/nrf54l15clean/platform.txt"],"affected_boards":["XIAO nRF54LM20A/B"]},
    {"id":"FINDING-049","severity":"High","confidence":"High","status":"Definite","affected_files":["scripts/build_release.py","hardware/nrf54l15clean/nrf54l15clean/boards.txt"],"affected_boards":["All package installations using Thread/Matter"]},
    {"id":"FINDING-050","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/platform.txt"],"affected_boards":["All Windows users"]},
    {"id":"FINDING-051","severity":"Medium","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/programmers.txt","hardware/nrf54l15clean/nrf54l15clean/platform.txt"],"affected_boards":["All"]},
    {"id":"FINDING-052","severity":"Medium","confidence":"High","status":"Definite","affected_files":["docs/release-script.md","tools/release.sh","scripts/build_release.py"],"affected_boards":["All package consumers"]},
    {"id":"FINDING-053","severity":"Medium","confidence":"High","status":"Definite","affected_files":["scripts/test_all.sh","scripts/test_all_examples.py","scripts/build_all_examples.py"],"affected_boards":["All"]},
    {"id":"FINDING-054","severity":"Low","confidence":"High","status":"Definite","affected_files":["package_nrf54l15clean_index.json","package_nrf54l15clean_stable_index.json"],"affected_boards":["All package consumers"]},
    {"id":"FINDING-055","severity":"Low","confidence":"High","status":"Definite","affected_files":["docs/board-reference.md","hardware/nrf54l15clean/nrf54l15clean/boards.txt"],"affected_boards":["XIAO nRF54L15 / Sense"]},
    {"id":"FINDING-056","severity":"Low","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/Adafruit_TinyUSB.h","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/Adafruit_TinyUSB.h"],"affected_boards":["All"]},
    {"id":"FINDING-057","severity":"High","confidence":"High","status":"Needs human verification","affected_files":["hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_secp256r1.cpp","hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_rng.h"],"affected_boards":["L15/LM20 Matter stage builds"]},
    {"id":"FINDING-058","severity":"Medium","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/examples/Power/SenseDelayRailRetentionProbe/SenseDelayRailRetentionProbe.ino","hardware/nrf54l15clean/nrf54l15clean/libraries/nRF54-Board-Examples/examples/XIAO-nRF54L15-Sense/SenseDelayRailRetentionProbe/SenseDelayRailRetentionProbe.ino","hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.cpp"],"affected_boards":["XIAO nRF54L15"]},
    {"id":"FINDING-059","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_i2s.inc"],"affected_boards":["nRF54L15 I2S users"]},
    {"id":"FINDING-060","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_i2s.inc"],"affected_boards":["nRF54L15 I2S users"]},
    {"id":"FINDING-061","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/nrf54lm20b.h","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/cmsis.h","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/startup_nrf54lm20b.S"],"affected_boards":["XIAO nRF54LM20A/B"]},
    {"id":"FINDING-062","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_crypto_analog.inc"],"affected_boards":["All HAL SAADC users"]},
    {"id":"FINDING-063","severity":"High","confidence":"High","status":"Likely","affected_files":["hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_security.cpp"],"affected_boards":["All secure KMU users"]},
    {"id":"FINDING-064","severity":"Medium","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_crypto_analog.inc","hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_regs.h","hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_support.cpp"],"affected_boards":["All","XIAO nRF54LM20A/B P3 users"]},
    {"id":"FINDING-065","severity":"High","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/nrf54lm20b_types.h","hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_crypto_analog.inc"],"affected_boards":["XIAO nRF54LM20A/B"]},
    {"id":"FINDING-066","severity":"Medium","confidence":"Medium","status":"Needs human verification","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/nrf54l15.h","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/nrf54lm20b.h","hardware/nrf54l15clean/nrf54l15clean/libraries/EEPROM/src/EEPROM.cpp","hardware/nrf54l15clean/nrf54l15clean/libraries/Preferences/src/Preferences.cpp"],"affected_boards":["Future non-secure builds"]},
    {"id":"FINDING-067","severity":"Medium","confidence":"Medium","status":"Likely","affected_files":["hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_nfct.h","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/startup_nrf54l15.S","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/startup_nrf54lm20b.S"],"affected_boards":["All NFCT interrupt users"]},
    {"id":"FINDING-068","severity":"Medium","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/wiring_digital.c","hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/wiring_digital.c"],"affected_boards":["All"]},
    {"id":"FINDING-069","severity":"Info","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/variants/holyiot_25008_nrf54l15/pins_arduino.h"],"affected_boards":["HOLYIOT-25008"]},
    {"id":"FINDING-070","severity":"Medium","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_peripherals.inc"],"affected_boards":["All HAL PWM users"]},
    {"id":"FINDING-071","severity":"Low","confidence":"High","status":"Definite","affected_files":["hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_crypto_analog.inc"],"affected_boards":["All HAL watchdog users"]}
  ]
}
```
