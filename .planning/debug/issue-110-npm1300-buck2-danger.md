---
status: resolved
trigger: "GitHub issue #110 asks whether calling npm1300_buck2_enable(false) on XIAO nRF54LM20A cuts power to the SAMD11 USB/COM bridge, preventing sketch upload and possibly leaving the board unrecoverable. Determine whether the electrical and software claims are true, the available recovery paths, and whether the public function should be removed, guarded, renamed, or documented. Do not change files until root cause/risk is established."
created: 2026-08-12T09:32:51+01:00
updated: 2026-08-12T09:48:48+01:00
---

## Current Focus

hypothesis: Resolved: normal builds now fail closed for unsafe BUCK2 mutations and use a board-aware mode-only helper for the populated system converter.
test: Core I/O contracts and every changed XIAO nRF54LM20A example compile passed; source, Nordic documentation, and Seeed topology agree.
expecting: npm1300_buck2_enable(false) and BUCK2 voltage writes cannot reach the PMIC unless the complete core is built with the explicit unsafe opt-in.
next_action: Publish 1.0.16 and ask the reporter to test recovery and the guarded API without closing issue #110.

## Symptoms

expected: A low-level PMIC rail-control API should have clear, recoverable behavior and should not unexpectedly strand normal upload access.
actual: The reporter suspects disabling BUCK2 removes SAMD11 power and may already have a board in that state.
errors: COM/upload access may disappear; no exact uploader error is given.
reproduction: Run a sketch on XIAO nRF54LM20A that calls npm1300_buck2_enable(false), then attempt upload via the SAMD11-backed COM/CMSIS-DAP interface.
started: Reported 2026-08-12 against current core; repository main is v1.0.15.

## Eliminated

- hypothesis: Disabling BUCK2 only removes power from the SAMD11 while leaving the nRF54 sketch running.
  evidence: Seeed's official block and power sheets show nPM1300 VOUT2 through L2 creates VSYS_3V3, which directly powers nRF54LM20A; SAMD11_3V3 is a VBUS-enabled load-switched derivative of the same VSYS_3V3 rail.
  timestamp: 2026-08-12T08:40:03.844Z

- hypothesis: The board is permanently bricked once BUCK2 is disabled.
  evidence: Nordic documents that a greater-than-10-second SHPHLD low causes a whole-system power cycle and is enabled by default after power-up; a PMIC power cycle resets supplies and re-enables BUCK. Seeed straps VSET2 to the 3.3 V enabled state and exposes SHPHLD.
  timestamp: 2026-08-12T08:40:55.917Z

## Evidence

- timestamp: 2026-08-12T08:33:38.808Z
  checked: .planning/debug/knowledge-base.md
  found: No knowledge base file exists.
  implication: No prior resolved pattern is available; investigate independently.

- timestamp: 2026-08-12T08:34:16.761Z
  checked: Initial git worktree state
  found: Branch main tracks origin/main; the only untracked path is the newly requested issue-110 debug session.
  implication: No pre-existing product edits need disentangling at this point, and product files remain untouched.

- timestamp: 2026-08-12T08:34:16.761Z
  checked: Repository-wide search for BUCK2, SAMD11, and XIAO nRF54LM20A references
  found: Board/core comments explicitly describe the SAMD11 bridge as USB-only and potentially unpowered, while release notes identify it as the board's CMSIS-DAP/upload path. The broad result was too large to resolve the exact PMIC API safely.
  implication: The electrical-domain claim is plausible but unconfirmed; narrow the search to the API and register implementation before forming a causal conclusion.

- timestamp: 2026-08-12T08:35:19.538Z
  checked: Narrow search for npm1300_buck2_enable and related PMIC symbols
  found: The public declaration is in npm1300.h and the sole implementation is npm1300.cpp line 566: npm1300_buck2_enable(bool e) returns buck_enable(1, e). No repository call site was found outside the declaration/definition.
  implication: Risk is concentrated in the generic buck_enable helper and board electrical mapping; there is no visible call-site guard or in-tree use that changes its semantics.

- timestamp: 2026-08-12T08:35:19.538Z
  checked: Relevant repository history
  found: PMIC support began in commits fa073769/4ddd0f5a; later releases added timed hibernate and USB-VBUS guards, indicating that USB power-domain interactions have required explicit policy elsewhere.
  implication: Inspect current code and history rather than assuming later hibernate safeguards also protect direct BUCK2 control.

- timestamp: 2026-08-12T08:36:02.748Z
  checked: Complete npm1300.h and LM20A rail-control example
  found: The public header describes BUCK1/BUCK2 only as generic buck regulators and exposes enable, voltage, status, and mode calls without board ownership warnings. The board-specific example controls only LDO1, explicitly documenting it as IMU&MIC_3V3; it neither calls nor explains BUCK2.
  implication: A user can reasonably treat BUCK2 as another optional controllable rail; current public documentation does not disclose any upload/debug dependency.

- timestamp: 2026-08-12T08:36:02.748Z
  checked: Related resolved timed-hibernate debug record
  found: Prior investigation reports that USB VBUS is hard-routed into nPM1300, SAMD11 supplies the visible COM/CMSIS-DAP path, and USB-specific PMIC behavior required explicit software handling. That session did not investigate BUCK2 disable.
  implication: These are useful leads, not proof for issue #110; validate the exact BUCK2 net and PMIC reset behavior independently against official sources.

- timestamp: 2026-08-12T08:36:33.617Z
  checked: Complete npm1300.cpp control path through line 756
  found: buck_enable(1,false) writes value 1 to nPM1300 base 0x04 at offset 0x03 (ENACLR offset 0x01 plus channel*2). The wrapper has no board check, VBUS check, readback, timeout, or re-enable. npm1300_begin/init only probes and enables ADC measurements; it does not restore buck rails.
  implication: A successful return means the I2C write was acknowledged, not that disabling is safe or recoverable. An nRF software reset alone has no driver path that restores BUCK2.

- timestamp: 2026-08-12T08:36:55.418Z
  checked: Remaining npm1300.cpp lines and API introduction history
  found: The complete file contains no later cleanup or recovery path. BUCK APIs were introduced as generic public functions in commit 4ddd0f5a and their current raw helper implementation dates to ecbe0329; no subsequent board-safety annotation or guard was added.
  implication: This is longstanding API design behavior rather than a recent regression, and source-level mitigation is absent in v1.0.15.

- timestamp: 2026-08-12T08:36:55.418Z
  checked: Repository identity
  found: Origin is https://github.com/lolren/nrf54-arduino-core.git, so the reported artifact is https://github.com/lolren/nrf54-arduino-core/issues/110.
  implication: The exact report can be checked directly without mutating GitHub.

- timestamp: 2026-08-12T08:37:56.683Z
  checked: Nordic nPM1300 BUCK regulator product specification
  found: Official register documentation identifies BUCK2ENACLR at base 0x400 offset 0x03; writing TASKBUCK2ENACLR=1 disables BUCK2. It also states BUCK enable tasks override the VSET pin-defined power-on state, while VSET2 selects BUCK2 enable/voltage at power-on reset.
  implication: The repository's address/value are correct and the call changes live PMIC rail state; the reset behavior depends on an actual nPM1300 power-on reset, not on the write-only task register's displayed reset value.

- timestamp: 2026-08-12T08:37:56.683Z
  checked: Initial Seeed official schematic and board wiki retrieval
  found: The seven-sheet V1.0 schematic is available from Seeed, and the official wiki identifies the onboard SAMD11 debug/serial interface and exposes nRF54/SAMD11 SWD/reset signals.
  implication: Primary board evidence is available, but the electrical net connection must be read from schematic topology rather than search-extracted text.

- timestamp: 2026-08-12T08:40:03.844Z
  checked: Visual inspection of Seeed official schematic sheets 02 Block Diagram, 03 Power, and 04 Debug & XIAO Header
  found: VOUT2/SW2 passes through L2 to VSYS_3V3. VSYS_3V3 is the nRF54LM20A supply and the VIN of U3; U3 is enabled by VBUS and outputs SAMD11_3V3 to ATSAMD11D14A VDD. VSET2 uses 470 kOhm, the schematic's table maps 250-500 kOhm to 3.3 V, and VSET1 is grounded/unused. Separate pads expose nRF54 reset, SAMD11 reset/SWD, nRF54 SWD, and SHPHLD.
  implication: npm1300_buck2_enable(false) collapses both the executing MCU and onboard COM/CMSIS-DAP bridge. BUCK1 examples cannot demonstrate a board load because VOUT1/SW1 are explicitly unconnected.

- timestamp: 2026-08-12T08:40:03.844Z
  checked: Reporter-supplied primary-source recovery findings
  found: Nordic documents that a PMIC power cycle re-enables BUCK, that SHPHLD held low longer than 10 seconds causes a default-enabled whole-system power cycle, and that BUCK2ENACLR overrides the VSET startup state; Seeed exposes SHPHLD pads.
  implication: The state is upload-stranding but not permanent. Full source removal or SHPHLD can restore VSET2's 3.3 V startup state, subject to preventing the bad sketch from immediately executing again.

- timestamp: 2026-08-12T08:40:03.844Z
  checked: Reporter-supplied adjacent API/example finding
  found: npm1300_buck2_set_voltage also transfers VOUT2 to software-selected voltage, while shipped BUCK examples target the physically unused BUCK1 rail.
  implication: Guarding only buck2_enable(false) would leave a second system-rail hazard, and current examples make the generic BUCK abstraction appear safer/more functional on this board than it is.

- timestamp: 2026-08-12T08:40:55.917Z
  checked: Nordic nPM1300 RESET, TIMER, BUCK, and Ship/Hibernate product-specification sections
  found: SHPHLD low longer than t_RESETBUT causes a whole-system power cycle, is enabled by default, and t_RESETBUT is typically 10 s. A power cycle internally disconnects VSYS, actively discharges outputs for about 100 ms, resets the PMIC, and re-enables BUCK. TASKSWRESET is another full power-cycle task but cannot be issued after the host rail has already collapsed.
  implication: PMIC-level recovery is authoritative and does not require rewriting OTP. Ordinary nRF reset is insufficient; either all PMIC input power must be removed or exposed SHPHLD must invoke the PMIC power cycle.

- timestamp: 2026-08-12T08:41:27.230Z
  checked: Repository-wide example and uploader search
  found: Every in-tree sketch call to a BUCK enable/mode API targets BUCK1; none call BUCK2. The LM20A board menu exposes pyOCD Recovery (CMSIS-DAP), and upload.py contains chip-erase logic plus an under-reset retry mode.
  implication: The examples are electrically disconnected from the board's active VOUT2 rail and cannot validate BUCK behavior. Existing uploader recovery may help only after PMIC-level rail restoration.

- timestamp: 2026-08-12T08:42:38.363Z
  checked: All shipped PMIC BUCK examples
  found: The three examples explicitly claim XIAO nRF54LM20B battery measurements but enable/mode-control BUCK1. Seeed's schematic marks VOUT1/SW1 unconnected; the system converter is BUCK2/VOUT2.
  implication: Their claimed ripple/current behavior is not produced by the selected rail. Correcting them to raw BUCK2 calls would expose the system-rail hazard; they need a board-aware safe mode-only API or removal.

- timestamp: 2026-08-12T08:42:38.363Z
  checked: LM20A board upload configuration and upload.py through its main pyOCD load loop
  found: The board exposes a pyOCD Recovery (CMSIS-DAP) menu. For nRF54LM20A, upload.py retries with normal connection, then halt, then under-reset; protected targets can be chip-erased, and load uses --no-reset.
  implication: The bundled tool can recover bad target firmware after SAMD11 and target power are restored, but it has no path to reset nPM1300 while both MCU and onboard probe rails are off.

- timestamp: 2026-08-12T08:44:07.870Z
  checked: Final scope and worktree state
  found: Investigation stopped at user direction. No hardware reproduction was attempted, and no product source, GitHub state, release artifact, commit, or attached-board state was changed; only this requested debug record is untracked.
  implication: The conclusion is documentary/source-verified and preserves the board from an intentional strand-risk test.

## Resolution

root_cause: The public API exposed raw nPM1300 rail numbering without XIAO board-topology policy. npm1300_buck2_enable(false) unconditionally wrote 1 to BUCK2ENACLR (base 0x04, offset 0x03). On XIAO nRF54LM20A, BUCK2 VOUT2 is VSYS_3V3: it powers the nRF54LM20A directly and feeds the USB-enabled load switch that generates SAMD11_3V3. The call therefore powered down both the executing target and its onboard COM/CMSIS-DAP probe. npm1300_buck2_set_voltage was an adjacent hazard, while shipped BUCK examples targeted unused BUCK1 and obscured the real topology.
fix: Normal builds now reject BUCK2 disable and every BUCK2 voltage mutation without writing the PMIC. The legacy mutators remain source-compatible but deprecated, with raw behavior available only through the explicit build-wide NPM1300_ENABLE_UNSAFE_SYSTEM_RAIL_CONTROL opt-in. New board-aware status/mode helpers target populated BUCK2, all affected examples use those helpers, unsupported BUCK1 current claims were removed, and README/release documentation gives the PMIC-level recovery procedure.
verification: Core I/O regressions passed with fail-closed source contracts. The three corrected PMIC BUCK examples, LowPowerZephyrParityBlink, and DelayAutoLowPowerMeasure compiled for XIAO nRF54LM20A. Nordic's product specification and Seeed's official schematic confirm both the root cause and recovery path. No intentional rail shutdown was performed and no XIAO nRF54LM20A was attached for hardware validation.
files_changed: ["npm1300.cpp", "npm1300.h", "five low-power/PMIC example sources plus one mirrored example", "scripts/test_core_io_regressions.py", "README.md", "docs/RELEASE_1.0.16.md"]

## Recovery Paths

1. Restore the PMIC, not merely the MCU: either remove every nPM1300 input source (USB and battery) long enough for full power loss, then reconnect, or pull the exposed SHPHLD pad low for more than 10 seconds to request the default whole-system power cycle.
2. Prevent recurrence from the installed sketch: hold the nRF54 RESET line low while PMIC power is restored so BUCK2 remains up and the SAMD11 can enumerate without the bad sketch running.
3. Erase/program a safe image through CMSIS-DAP using connect-under-reset. The bundled pyOCD recovery path contains under-reset and chip-erase support. Release target reset only after the debugger has control or the unsafe image has been replaced.
4. If the onboard probe workflow cannot take control, use the exposed nRF54 SWD/reset pads with an external debugger after restoring BUCK2, again connecting under reset.

USB replug alone is sufficient only when it actually removes all PMIC power; with a battery attached it does not guarantee a PMIC reset. nRF54 reset, SAMD11 reset, or SWD access while VSYS_3V3 remains off cannot restore BUCK2.

## API Recommendation

- Immediate compatible release: guard both BUCK2 disable and BUCK2 voltage mutation by default on this board, with an explicit unsafe compile-time escape hatch for expert low-level use.
- Public API direction: deprecate the generic BUCK2 mutators; in the next major release remove them or rename intentional rail cutoff to an unmistakable unsafe system-power operation.
- Safe useful surface: retain read/status calls and provide a board-aware system-buck mode helper if PWM/hysteretic tuning is genuinely supported.
- Examples/docs: withdraw the BUCK1 power claims because VOUT1 is unconnected, add the recovery procedure, and clearly state that BUCK2 is the target and debug-probe parent supply.
- Documentation alone is not enough: one innocent-looking call removes both execution and the normal upload path.
