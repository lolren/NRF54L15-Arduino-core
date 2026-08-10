---
status: awaiting_human_verify
trigger: "GitHub issue #108 reports that on the nRF54L15 with core v1.0.13, delaySystemOffNoRetention() wakes immediately on both USB and battery, though behavior varies by sketch. The reporter later observed current stayed high unless an active I2C device was put to sleep and wrote that this aspect was resolved. Determine whether the core still has an independent defect; if confirmed, fix it."
created: 2026-08-10T06:59:23+01:00
updated: 2026-08-10T08:22:00+01:00
---

## Current Focus

hypothesis: Confirmed and self-verified: idle Wire's missing STOPPED event caused a false stage-2 abort; the idle-aware predicate fixes it while preserving strict non-idle handling.
test: User reruns the original I2C sketch from this checkout on the intended USB and/or battery setup, with the external I2C device itself put to sleep when measuring whole-board current.
expecting: The MCU remains in no-retention System OFF until its configured wake source; any intentionally asserted GPIO wake remains immediate, and external-device current is independently controlled.
next_action: Publish the hardware-verified fix, request tester confirmation, and keep issue #108 open until that confirmation arrives.

## Symptoms

expected: delaySystemOffNoRetention() should enter no-retention System OFF and remain off until a configured wake source occurs.
actual: It sometimes wakes immediately; behavior is sketch-dependent. Current consumption also stayed high with an active I2C device until that device was put to sleep.
errors: No textual error was reported.
reproduction: On Seeed Studio XIAO nRF54L15, core v1.0.13, call delaySystemOffNoRetention() from sketches under USB or battery power. A XIAO nRF54L15 is attached via CMSIS-DAP at /dev/ttyACM0, USB VID:PID 2886:0066, serial E91217E8.
started: Reported 2026-08-10 against v1.0.13; repository main is now v1.0.14 at commit aa7b1a0e.

## Eliminated

- hypothesis: The report is only external I2C-device current and there is no independent MCU/core defect.
  evidence: With no external I2C transaction required, merely calling Wire.begin() changed the retained-marker result from a timed GRTC wake to RESETREAS=0x40 and System OFF abort stage 2.
  timestamp: 2026-08-10T07:42:00+01:00

- hypothesis: The baseline GRTC/no-retention timing path itself wakes immediately on current main.
  evidence: The controlled baseline produced RESETREAS=0x840, GRTC=yes, and abort stage 0; after the fix the same baseline signature remained unchanged.
  timestamp: 2026-08-10T08:14:00+01:00

- hypothesis: The unrelated v1.0.14 RADIO EasyDMA fix had already removed every independent immediate-reset mechanism relevant to issue #108.
  evidence: Main's pre-fix Wire.cpp was byte-identical to the hardware-tested failing blob, direct L15 quiesce returned false with ENABLE successfully disabled, and a new Wire-specific edit was required to change abort stage 2 into the expected GRTC wake.
  timestamp: 2026-08-10T08:14:00+01:00

## Evidence

- timestamp: 2026-08-10T07:01:00+01:00
  checked: Persistent debug knowledge base.
  found: .planning/debug/knowledge-base.md does not exist, so there is no prior resolved-session match to test.
  implication: Form hypotheses from repository, issue, runtime, and hardware evidence.

- timestamp: 2026-08-10T07:01:00+01:00
  checked: Repository state and recent history.
  found: HEAD is clean apart from this untracked debug file, at aa7b1a0e (main/tag v1.0.14); v1.0.13 is 7ec90ae9. The v1.0.14 commit message is "release: fix USB timed hibernate in 1.0.14".
  implication: USB System OFF behavior changed after the reported version and that diff is a high-value evidence source, while unrelated user work is not present in tracked files.

- timestamp: 2026-08-10T07:03:00+01:00
  checked: Complete public text of GitHub issue #108.
  found: The issue contains no sketch, wake-source configuration, reset-reason output, or measured timing. It says USB and battery both appeared to wake immediately, behavior varied by sketch, and later says the high-current aspect was resolved by sleeping the active I2C device.
  implication: The report alone does not demonstrate an MCU wake/reset defect; hardware execution must distinguish actual reboot from peripheral current and test controlled sketches.

- timestamp: 2026-08-10T07:06:00+01:00
  checked: Repository-wide System OFF search and v1.0.13-to-v1.0.14 source diff.
  found: delaySystemOffNoRetention() funnels into enterSystemOffInternal() in wiring_time.c. The v1.0.14 timing diff widens millisecond-to-microsecond conversion and internal delay values from 32 to 64 bits; v1.0.13 clamps requests above 4,294,967 milliseconds (about 71.6 minutes), rather than preserving longer delays. The release also changed nPM1300 USB fallback and an independent RADIO EasyDMA postcondition.
  implication: Very long requested delays are a specific candidate for an unexpectedly early (but not normally immediate) wake, while the release diff does not by itself prove the reported no-retention path is fixed.

- timestamp: 2026-08-10T07:10:00+01:00
  checked: Complete adjacent issue #107 debug record and v1.0.14 release note.
  found: The shared System OFF engine deliberately software-resets on a failed quiesce check. On the previously attached LM20A, v1.0.13-like code repeatedly produced RESETREAS=0x40 and abort stage 2 because disabled RADIO EasyDMA pointer registers read 0x20000000 instead of literal zero; the narrow v1.0.14 predicate fix yielded three successful timed System OFF cycles with RESETREAS=0x840 and abort stage 0.
  implication: This is a known, directly observed immediate-reset mechanism in the same code path and must be tested first on L15. Competing hypotheses remain: an already-active configured GPIO/GPIOTE wake source, GRTC programming/entry failure, or mere external I2C current without MCU reboot.

- timestamp: 2026-08-10T07:17:00+01:00
  checked: Complete nRF54L15 wiring_time.c System OFF implementation, Arduino declarations, reset-reason capture, and XIAO L15 board-state preparation.
  found: Every guarded pre-entry failure records a persistent stage then triggers a software reset; successful timed entry first clears every GRTC compare/event/interrupt, arms one synchronized compare, clears RESETREAS, requests SYSTEMOFF, and cold-resets on wake. No-retention differs only by clearing MEMCONF retention before entry. XIAO preparation powers down board rails and disables SENSE on its IMU/I2C/mic/RF pins, but the timing path does not globally clear GPIO SENSE configured elsewhere by a sketch.
  implication: RESETREAS plus abort stage cleanly separates a shutdown abort from a real System OFF wake. A still-active user GPIO wake source could also explain sketch-dependent immediate wake and must be measured rather than inferred.

- timestamp: 2026-08-10T07:23:00+01:00
  checked: User-directed investigation scope checkpoint.
  found: The requested priority is a prompt controlled L15 hardware comparison of the minimal public API path, idle I2C state, and relevant GPIO state, with retained markers plus RESETREAS/abort diagnostics.
  implication: Pause broader source/history inspection and proceed directly to an instrumented target experiment.

- timestamp: 2026-08-10T07:26:00+01:00
  checked: Attached target discovery and Arduino CLI registration.
  found: /dev/ttyACM0 is identified as "XIAO nRF54L15 / Sense" with local source-backed FQBN localnrf54:nrf54l15clean:xiao_nrf54l15; CMSIS-DAP serial is E91217E8 and VID:PID is 2886:0066.
  implication: The requested L15 experiment can be compiled and uploaded directly to the attached target without target ambiguity.

- timestamp: 2026-08-10T07:29:00+01:00
  checked: First source-backed compile of the temporary hardware probe.
  found: Compilation stopped only because this core does not define the convenience alias D0; the probe had not yet linked or run.
  implication: Use the board's Arduino pin index 0 directly and retry; this is a probe-only correction, not product evidence.

- timestamp: 2026-08-10T07:32:00+01:00
  checked: Second source-backed compile of the temporary probe.
  found: The minimal three-case sketch compiled successfully for localnrf54:nrf54l15clean:xiao_nrf54l15 (19,108 bytes flash, 7,164 bytes RAM).
  implication: The diagnostic public API path and RESETREAS/abort symbols are valid for the attached target; make the retained marker unique before upload to prevent stale prior-probe state from skipping cases.

- timestamp: 2026-08-10T07:35:00+01:00
  checked: Recompile and CMSIS-DAP upload of the unique-marker probe.
  found: The probe compiled at 19,092 bytes flash/7,164 bytes RAM and nrf_ocd identified target nRF4B150CAA, erased the chip, and programmed the image successfully via probe E91217E8.
  implication: Runtime serial output now directly tests repository main on the attached nRF54L15.

- timestamp: 2026-08-10T07:42:00+01:00
  checked: Controlled three-case hardware run of delaySystemOffNoRetention(3000) on attached L15.
  found: Baseline booted with marker 0xD1, RESETREAS=0x840, GRTC=yes, and abort stage 0. Idle Wire.begin() booted with marker 0xD2, RESETREAS=0x40, no OFF/GRTC flag, and abort stage 2. An intentionally asserted pin-0 LOW-sense wake booted with marker 0xD3, RESETREAS=0x140, OFF=yes, GRTC=no, and abort stage 0.
  implication: Main has an independent I2C-dependent pre-entry defect: merely initializing Wire converts System OFF into an immediate guarded software reset. The GPIO comparison separately proves that an asserted configured GPIO can legitimately wake immediately and has a distinguishable reset signature.

- timestamp: 2026-08-10T07:47:00+01:00
  checked: Direct hardware call to Wire.quiesceForSystemOff(2000000) after Wire.begin(), following XIAO board-state preparation.
  found: TWIM22 read ENABLE=6 and EVENTS_STOPPED=0 before quiesce; the call returned false; afterward ENABLE=0 while EVENTS_STOPPED remained 0.
  implication: The peripheral is successfully disabled, but L15 does not assert STOPPED for an idle STOP task. The hook's stopped && disabled return predicate is the exact false-negative that propagates to abort stage 2.

- timestamp: 2026-08-10T07:53:00+01:00
  checked: New focused Wire/System OFF regression contract before production edit.
  found: validate_system_off_wake_contracts() failed at the first controllerWasIdle assertion against both current Wire implementations.
  implication: The regression is red for the intended missing behavior and will distinguish the fix from the current faulty predicate.

- timestamp: 2026-08-10T07:57:00+01:00
  checked: Focused System OFF regression and diff whitespace after the production edit.
  found: validate_system_off_wake_contracts() passed for nrf54l15, nrf54lm20b, and HAL; git diff --check passed for all three edited files.
  implication: The source contract now preserves strict non-idle shutdown handling while accepting the observed idle-controller hardware semantics; target verification remains.

- timestamp: 2026-08-10T08:01:00+01:00
  checked: Resolved path used by localnrf54:nrf54l15clean during the first post-fix compile.
  found: The installed local FQBN resolves to /home/lolren/Desktop/eport_nrf54/nrf54-arduino-core rather than this requested checkout, so that fresh build did not include the new edit and will not be uploaded.
  implication: Verify that sibling source was an exact pre-fix aa7b1a0e baseline, then use an isolated temporary Arduino hardware registration for this checkout before post-fix hardware verification.

- timestamp: 2026-08-10T08:04:00+01:00
  checked: Existing local FQBN source commit and relevant blob identities.
  found: The sibling registration is at older commit 624320fe, not aa7b1a0e. Its nrf54l15 Wire.cpp blob is nevertheless byte-identical to aa7b1a0e's pre-fix Wire.cpp (git blob ec83491e); wiring_time.c differs because main later widened timing. The direct quiesce experiment therefore exercised the exact faulty Wire implementation, but the prior end-to-end image was not a complete main build.
  implication: The direct root-cause proof remains valid, while post-fix end-to-end verification must compile this requested checkout explicitly. Do not claim the earlier full image represented all of v1.0.14.

- timestamp: 2026-08-10T08:06:00+01:00
  checked: Isolated Arduino hardware registration.
  found: A temporary sketchbook now exposes this exact checkout as issue108:nrf54l15clean:xiao_nrf54l15 without changing the user's existing localnrf54 registration.
  implication: A distinct FQBN can prove the post-fix image uses the requested repository and avoids global Arduino configuration changes.

- timestamp: 2026-08-10T08:08:00+01:00
  checked: Exact-checkout build properties and fresh probe compile.
  found: runtime.platform.path, build.core.path, and build.variant.path all resolve under /tmp/issue108_arduino_user's symlink to this requested checkout. The unchanged probe compiled successfully at 19,592 bytes flash and 7,164 bytes RAM.
  implication: The pending upload contains the edited main source rather than the older sibling registration.

- timestamp: 2026-08-10T08:14:00+01:00
  checked: Post-fix exact-checkout three-case hardware run on attached nRF54L15.
  found: Baseline remained RESETREAS=0x840/GRTC=yes/abort 0. Idle Wire changed from pre-fix RESETREAS=0x40/abort 2 to RESETREAS=0x840/GRTC=yes/abort 0. Asserted LOW-sense GPIO remained RESETREAS=0x140/OFF=yes/GRTC=no/abort 0.
  implication: The fix resolves the original I2C-dependent immediate reset end-to-end while preserving the timed wake path and a legitimate pre-armed GPIO wake. Only the intended Wire case changed.

- timestamp: 2026-08-10T08:17:00+01:00
  checked: Complete scripts/test_core_io_regressions.py suite after the fix.
  found: Exit code 0; all source contracts and ASan/UBSan-backed host regressions passed, including both core variants' System OFF contracts, serial/SPI, BLE, Matter, Thread, Zigbee, low-power board state, and utility tests.
  implication: No host-verifiable adjacent regression was detected; compile the mirrored LM20B source before final diff review.

- timestamp: 2026-08-10T08:19:00+01:00
  checked: Exact-checkout compile of WireRepeatedStartProbe for issue108:nrf54l15clean:xiao_nrf54lm20b.
  found: Compile succeeded at 17,696 bytes flash and 7,396 bytes RAM.
  implication: The mirrored LM20B Wire.cpp edit compiles cleanly as well as the hardware-verified L15 edit.

- timestamp: 2026-08-10T08:22:00+01:00
  checked: Final owned diff, repository status, and whitespace validation.
  found: The implementation diff is 29 insertions/2 deletions across only nrf54l15/Wire.cpp, nrf54lm20b/Wire.cpp, and scripts/test_core_io_regressions.py; git diff --check passes. The only additional worktree item is this requested untracked debug session file.
  implication: The fix is narrow, preserves unrelated files, and is ready for human verification; no commit, issue mutation, release, or publication has occurred.

## Resolution

root_cause: TwoWire::quiesceForSystemOff() always clears EVENTS_STOPPED, issues TASKS_STOP, and requires STOPPED before reporting success. On nRF54L15, an initialized but idle TWIM does not generate STOPPED for that task; end() nevertheless disables the peripheral (ENABLE changes 6 to 0). The false return propagates through quiesceSystemOffDmaOwners() to guarded abort stage 2 and an immediate software reset, so any sketch that leaves Wire initialized cannot enter delaySystemOffNoRetention().
fix: Snapshot whether Wire is an idle synchronous controller before issuing STOP. Accept successful disabled readback without STOPPED only in that known-idle state; continue requiring STOPPED for target mode and a pending repeated start. Applied identically to nrf54l15 and nrf54lm20b cores with a regression contract.
verification: Before the fix, the L15 three-case probe showed baseline RESETREAS=0x840/GRTC/abort 0, idle Wire RESETREAS=0x40/abort 2, and asserted LOW-sense GPIO RESETREAS=0x140/OFF/abort 0. A direct probe confirmed Wire quiesce=false with STOPPED=0 even though ENABLE changed 6 to 0. The new focused regression failed before the edit and passed after it. An exact-checkout post-fix run changed only idle Wire to RESETREAS=0x840/GRTC/abort 0; baseline and legitimate GPIO wake signatures were preserved. The complete core I/O regression suite and git diff --check passed, the L15 probe compiled and ran from the requested checkout, and WireRepeatedStartProbe compiled for LM20B. Battery/real-sketch confirmation remains human-only.
files_changed: [hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/Wire.cpp, hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/Wire.cpp, scripts/test_core_io_regressions.py]
