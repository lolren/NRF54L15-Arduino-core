---
status: resolved
trigger: "Investigate and fix GitHub issue #107: nPM1300 timed hibernate still fails on USB after v1.0.13."
created: 2026-08-10T06:02:26+01:00
updated: 2026-08-10T05:33:36.401Z
---

## Current Focus

hypothesis: Confirmed hardware mechanism: LM20A canonicalizes a zero EasyDMA pointer write to SRAM base 0x20000000. scrubRadioDmaPointersIfDisabled() incorrectly requires literal-zero pointer readback, so it reports failure after the DMA engines are disabled and their counts are cleared.
test: Owner-run three-cycle verification of the public npm1300_enter_timed_hibernate_ms(2000) API on the attached USB-powered XIAO nRF54LM20A.
expecting: Each cycle rejects invalid ranges, quiesces successfully, cold-boots after the delay with RESETREAS=0x840, reports GRTC System OFF wake, and retains abort stage 0.
next_action: None; investigation and verification are complete. Report without release, push, issue mutation, commit, archive move, or changes outside the owned files.

## Symptoms

expected: npm1300_enter_timed_hibernate_ms(duration) should enter timed hibernation when the XIAO nRF54LM20A is powered by USB and wake after the requested duration.
actual: On v1.0.10 it either failed to wake or woke immediately. v1.0.13 added corrected timer registers plus a VBUS guard, so the API now returns false on USB; tester says the issue remains unresolved.
errors: No exception. v1.0.13 returns false whenever nPM1300 VBUS_PRESENT is set.
reproduction: Plug XIAO nRF54LM20A into USB and call npm1300_enter_timed_hibernate_ms(). Attached board is currently visible as /dev/ttyACM0 and CMSIS-DAP.
started: First reported against v1.0.10 on 2026-08-09; tester reconfirmed unresolved on v1.0.13 on 2026-08-10.

## Eliminated

- hypothesis: Delegating the VBUS-present public API to the existing systemOffWakeReset() implementation is sufficient for the attached XIAO nRF54LM20A.
  evidence: The hardware checkpoint executes the fallback but resets with RESETREAS=0x40 and nrf54SystemOffAbortStage()=2. A focused probe reads RADIO STATE=Disabled before and after, while nrf54_hal_quiesce_for_system_off(2000000) still returns false.
  timestamp: 2026-08-10T05:25:38.637Z

- hypothesis: Removing the VBUS guard and issuing TASKENTERHIBERNATE directly will fix USB behavior.
  evidence: Nordic requires EVENTSVBUSIN0SET before TASKENTERHIBERNATE, applying VBUS is a wake source, and the XIAO has no software-controlled VBUS disconnect.
  timestamp: 2026-08-10T05:09:22.752Z

- hypothesis: The timer target register sequence alone explains the remaining v1.0.13 USB failure.
  evidence: On USB, v1.0.13 exits before any timer register is written; its corrected PMIC sequence is only reachable with VBUS absent.
  timestamp: 2026-08-10T05:09:22.752Z

- hypothesis: The USB fallback should publish a smaller 4,294,967 ms limit matching the current uint32_t-microsecond core implementation.
  evidence: The owner will widen the disjoint core System OFF delay path to uint64_t and explicitly requires one global nPM1300 API range.
  timestamp: 2026-08-10T05:09:22.752Z

## Evidence

- timestamp: 2026-08-10T05:33:36.401Z
  checked: Source-registered compile of the unchanged issue #107 hardware probe
  found: arduino-cli compiled the probe for localnrf54:nrf54l15clean:xiao_nrf54lm20b with warnings enabled; it uses 136,696 bytes of flash and 8,796 bytes of RAM.
  implication: The production HAL change compiles and links cleanly for the affected LM20A target.

- timestamp: 2026-08-10T05:33:36.401Z
  checked: Owner hardware verification on the attached USB-powered XIAO nRF54LM20A
  found: Three consecutive public-API cycles passed. Every start reported VBUS status 0x21 and hal_quiesce=OK; zero and over-maximum durations were rejected; npm1300_enter_timed_hibernate_ms(2000) then cold-booted with RESETREAS=0x840, retained the marker, reported system_off_abort_stage=0, woke_from_system_off=yes, woke_from_grtc=yes, and RESULT=USB_TIMED_HIBERNATE_PASS.
  implication: The original USB failure is resolved end-to-end, the prior stage-2 false abort is gone, input validation remains intact, and the result is stable across three repeated cycles.

- timestamp: 2026-08-10T05:32:26.543Z
  checked: Arduino CLI installation, board discovery, and source-registered core
  found: arduino-cli 1.3.0 sees the attached XIAO nRF54LM20A at /dev/ttyACM0 and resolves the checkout-backed FQBN localnrf54:nrf54l15clean:xiao_nrf54lm20b.
  implication: The existing parent-owned probe can be compiled and exercised directly against the edited source without modifying it or using the installed release core.

- timestamp: 2026-08-10T05:31:54.560Z
  checked: Owned-file diff and git diff --check
  found: The HAL diff contains only the exact cleared-pointer predicate and four postcondition substitutions. The new test assertions are additive beside the parent's existing nPM1300 assertions; whitespace/error checking passed.
  implication: The fix is narrow and does not overwrite or revert concurrent work.

- timestamp: 2026-08-10T05:31:54.560Z
  checked: Complete python3 scripts/test_core_io_regressions.py suite
  found: Exit code 0; every source contract and all ASan/UBSan-backed host tests passed, including XIAO low-power, timed System OFF, serial, SPI, BLE, Matter, and utility regressions.
  implication: The HAL change preserves adjacent functionality in host-verifiable scope; target compile and attached-hardware behavior remain to verify.

- timestamp: 2026-08-10T05:31:11.714Z
  checked: Focused validate_xiao_low_power_board_contracts() run after the HAL change
  found: The targeted contract exited 0 and printed both XIAO low-power and LM20A PMIC contract PASS lines.
  implication: The previously red regression is green, including exact fail-closed predicate semantics, all four pointer call sites, and the existing USB fallback source contract.

- timestamp: 2026-08-10T05:30:44.885Z
  checked: Minimal production edit in nrf54l15_hal.cpp
  found: Added radioDmaPointerIsCleared(), whose only accepted values are 0U and kCanonicalClearedEasyDmaPointer (0x20000000UL), and replaced only the four RADIO DMA pointer equality checks. Existing RADIO Disabled, AUX completion, ENABLE, and MAXCNT requirements are unchanged.
  implication: LM20A canonical readback can pass without accepting arbitrary pointers or weakening the rest of the quiescence proof.

- timestamp: 2026-08-10T05:30:03.125Z
  checked: Focused validate_xiao_low_power_board_contracts() run before the HAL change
  found: The test exited 1 at the new kCanonicalClearedEasyDmaPointer assertion; earlier low-power assertions passed.
  implication: The regression is red for the intended reason and directly distinguishes the current strict-zero implementation from the required fail-closed dual representation.

- timestamp: 2026-08-10T05:29:29.112Z
  checked: Generated nRF54L15/nRF54LM20B register headers and existing EasyDMA address contracts
  found: The generated headers use 0x20000000 as the PTR reset value for I2S, SPIM, SPIS, TWIM, TWIS, and UARTE, while RADIO PACKETPTR documents zero. The local memory map confirms 0x20000000 is SRAM base.
  implication: Supporting both literal zero and canonical SRAM-base readback is consistent with this platform's EasyDMA register model; accepting any wider SRAM range would weaken the fail-closed postcondition.

- timestamp: 2026-08-10T05:29:29.112Z
  checked: Focused regression contract added to validate_xiao_low_power_board_contracts()
  found: The new assertions require an exact predicate accepting only 0U or kCanonicalClearedEasyDmaPointer=0x20000000UL and require PACKETPTR, DFEPACKET.PTR, and both AUX PTR checks to use it instead of direct literal-zero comparisons.
  implication: The regression will reject both the current false-negative behavior and an over-broad arbitrary-pointer acceptance.

- timestamp: 2026-08-10T05:28:07.922Z
  checked: Attached LM20A register snapshot before and after strong HAL quiescence
  found: Before quiescence owner=0, RADIO STATE=Disabled, AUX ENABLE=0/0, PTR=0x20000000/0x20000000, MAXCNT=64/64, AMOUNT=0/0, PACKETPTR=0x20000000, DFEPACKET.PTR=0x20000000, and DFEPACKET.MAXCNT=16384. After zero writes, ENABLE and all MAXCNT fields read zero, but all four PTR registers read back as canonical 0x20000000. The HAL therefore returns false only because it requires each PTR readback to equal literal zero.
  implication: The DMA engines are quiesced; the false negative is the postcondition's failure to recognize LM20A's canonical cleared EasyDMA pointer representation. A safe fix must accept 0x20000000 only as an alternate cleared encoding and continue rejecting arbitrary nonzero pointers.

- timestamp: 2026-08-10T05:27:03.585Z
  checked: Worktree ownership, all quiesce/abort-stage references, and owned-file diff
  found: Abort stage 2 is named kSystemOffAbortDmaQuiesce and is recorded whenever the aggregate nrf54_hal_quiesce_for_system_off hook returns false. nrf54l15_hal.cpp has no existing tracked edit; the regression file contains the parent's prior nPM1300 fallback assertions, and all listed parent-owned edits remain present.
  implication: Stage 2 does not identify RADIO specifically. The investigation must enumerate the HAL's internal failure predicates and preserve the existing dirty worktree.

- timestamp: 2026-08-10T05:25:38.637Z
  checked: Owner's attached-XIAO hardware checkpoint and focused quiescence probe
  found: npm1300_enter_timed_hibernate_ms() reaches the System OFF fallback but resets with RESETREAS=0x40 and nrf54SystemOffAbortStage()=2. RADIO STATE is Disabled both before and after the probe, yet nrf54_hal_quiesce_for_system_off(2000000) returns false.
  implication: The source-only verification was insufficient. The remaining defect is below the nPM1300 wrapper, in or immediately around strong HAL quiescence; stage 2 is the first concrete divergence to trace.

- timestamp: 2026-08-10T05:03:05.313Z
  checked: .planning/debug/knowledge-base.md
  found: No debug knowledge base exists in this repository.
  implication: There is no known-pattern candidate to prioritize; investigate from primary evidence.

- timestamp: 2026-08-10T05:03:52.107Z
  checked: Git worktree, HEAD, remote, and commit 7ec90ae9
  found: HEAD and origin/main are exactly v1.0.13 (7ec90ae9); no tracked edits were present. The commit replaced the old timer sequence and added a shared VBUS-absent guard to Ship, Hibernate, and timed Hibernate, while changing documentation to declare USB unsupported.
  implication: The observed false return is deliberate policy introduced by v1.0.13, not evidence that the PMIC cannot perform timed Hibernate with VBUS present. That policy must be validated independently.

- timestamp: 2026-08-10T05:04:57.340Z
  checked: Complete npm1300.cpp, npm1300.h, timed-hibernate example, and v1.0.13 release note
  found: npm1300_enter_timed_hibernate_ms() checks VBUS before any timer write and npm1300_enter_hibernate() checks it again. On USB the implementation therefore never issues TIMERTARGETSTROBE or TASKENTERHIBERNATE. The documentation and regression assertions merely encode that policy.
  implication: v1.0.13's USB probe cannot support the claim that the PMIC rejects Hibernate on VBUS; it only confirms the software guard rejects the call.

- timestamp: 2026-08-10T05:06:52.538Z
  checked: Nordic nPM1300 Product Specification, issue #107 discussion, and XIAO nRF54LM20A schematic
  found: Nordic requires EVENTSVBUSIN0SET before TASKENTERHIBERNATE, and applying VBUS is itself a Hibernate wake source. The board routes USB VBUS to the nPM1300 without a host-controlled disconnect. Issue #107 specifically requests USB behavior; directing the reporter to battery-only operation did not address it.
  implication: PMIC Hibernate on an attached USB supply is physically unsupported, and forcibly issuing its task can be ignored or wake immediately. USB requires a different timed reset mechanism, not removal of the guard alone.

- timestamp: 2026-08-10T05:06:52.538Z
  checked: Orchestrator's attached-board GRTC System OFF probe and Nordic nRF54L15 Product Specification
  found: systemOffWakeReset(1000) repeatedly cold-booted the USB-powered LM20A with RESETREAS=0x840, woke_from_grtc=yes, and status=timed_system_off_wake_ok; GRTC SYSCOUNTER compare is a documented System OFF wake source.
  implication: A VBUS-present fallback can meet the observable timed cold-boot contract on USB while retaining true PMIC Hibernate on battery.

- timestamp: 2026-08-10T05:07:49.865Z
  checked: systemOffWakeReset declarations/call sites and orchestrator range analysis
  found: systemOffWakeReset(unsigned long) is a noreturn cold-reset API available in both core variants. Its current millisecond-to-uint32_t-microsecond conversion silently saturates above 4,294,967 ms, whereas nPM1300's 24-bit 16 ms wake timer accepts up to 268,435,440 ms.
  implication: The fallback mechanism is available, but full-range parity depends on the owner's separately authorized uint64_t core widening.

- timestamp: 2026-08-10T05:09:22.752Z
  checked: Owner decision on delay-range compatibility
  found: The disjoint core System OFF path will be widened to uint64_t; the nPM1300 library must retain only NPM1300_HIBERNATE_TIMER_MAX_MS (268,435,440 ms) and must not add a USB-only maximum.
  implication: Library validation and documentation should expose one power-source-independent range while this session leaves core files untouched.

- timestamp: 2026-08-10T05:10:15.755Z
  checked: Updated regression contract against unmodified v1.0.13 source
  found: scripts/test_core_io_regressions.py failed at the first new assertion because npm1300_enter_timed_hibernate_ms() has no common maximum-delay validation before its VBUS guard; the existing suite passed up to that deliberate red test.
  implication: The test reproduces the missing USB fallback and will distinguish the intended implementation from the v1.0.13 blanket rejection.

- timestamp: 2026-08-10T05:11:24.433Z
  checked: Full core I/O regression suite after the minimal implementation change
  found: All contracts and sanitizer-backed tests passed, including the new common-range validation, VBUS status branch, systemOffWakeReset(delay_ms) fallback, and unchanged PMIC timer/Hibernate ordering.
  implication: The implementation is internally consistent and did not regress adjacent PMIC/System OFF contracts; public docs and hardware-level wrapper verification remain.

- timestamp: 2026-08-10T05:13:10.114Z
  checked: Full regression suite after public-contract updates and concurrent owner core changes
  found: All tests passed. The owner-modified nrf54l15 and nrf54lm20b core paths now convert milliseconds to uint64_t microseconds and carry uint64_t through GRTC wake programming; the attached LM20A remains visible at /dev/ttyACM0 with the local workspace FQBN.
  implication: The one global 268,435,440 ms library range is representable on both PMIC and USB fallback paths, and compile/hardware verification can proceed without this session editing core files.

- timestamp: 2026-08-10T05:15:16.757Z
  checked: Source-registered Arduino CLI compile for XIAO nRF54LM20A
  found: nPM1300_TimedHibernate compiled cleanly with warnings enabled against this checkout and produced a 140,652-byte image.
  implication: The library fallback, public header, example, and widened core link correctly for the target board.

- timestamp: 2026-08-10T05:16:28.758Z
  checked: Verification scope checkpoint
  found: The owner accepted the source regression and source-registered compile evidence as sufficient for this session and retained ownership of the hardware wrapper test, release, and GitHub communication.
  implication: Stop without upload, release, commit, push, issue comment, or archive; preserve the session at awaiting_human_verify.

## Resolution

root_cause: On LM20A silicon, writing zero to the disabled RADIO EasyDMA pointer registers reads back as canonical SRAM base 0x20000000. scrubRadioDmaPointersIfDisabled() required literal-zero readback for PACKETPTR, DFEPACKET.PTR, and both AUXDATADMA PTRs, so it returned false even though RADIO was Disabled and ENABLE/MAXCNT were zero. That false result propagates through nrf54_hal_quiesce_for_system_off() to System OFF abort stage 2 and a software reset (RESETREAS=0x40), defeating the USB timed-hibernate fallback.
fix: Added a fail-closed radioDmaPointerIsCleared() predicate that accepts only literal zero or LM20A's canonical cleared SRAM-base readback (0x20000000), and used it for PACKETPTR, DFEPACKET.PTR, and both AUXDATADMA PTR postconditions. All ENABLE, MAXCNT, owner, state, and completion checks remain unchanged.
verification: The focused regression failed before the HAL change and passed after it. The complete scripts/test_core_io_regressions.py suite passed, including all ASan/UBSan host tests, and git diff --check passed. The unchanged issue #107 probe compiled for the source-registered LM20A FQBN with warnings enabled (136,696-byte flash, 8,796-byte RAM). Owner hardware verification then passed three consecutive public npm1300_enter_timed_hibernate_ms(2000) cycles on USB: VBUS=0x21, hal_quiesce=OK, invalid ranges rejected, RESETREAS=0x840, abort stage 0, System OFF=yes, GRTC=yes, and USB_TIMED_HIBERNATE_PASS.
files_changed: [hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.cpp, scripts/test_core_io_regressions.py, hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/npm1300.cpp, hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/npm1300.h, hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/PMIC/nPM1300_TimedHibernate/nPM1300_TimedHibernate.ino, README.md, docs/RELEASE_1.0.13.md]
