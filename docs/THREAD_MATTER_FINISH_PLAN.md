# Thread and Matter Hardening Status

Date: 2026-06-20

This document tracks the staged Thread and Matter implementation. It replaces
older optimistic notes that called the stack "complete". The current goal is
production-grade support, but the honest state is staged and actively being
hardened.

## Current Status

### Thread

- OpenThread core is integrated as a staged build option.
- FTD mode is enabled for staged Thread builds.
- MeshCoP is enabled for staged Thread builds.
- SRP Client is enabled for all staged Thread board profiles.
- OpenThread UDP examples compile and include payload/reconnect/soak probes.
- The staged board menu flags are now consistent across:
  - `xiao_nrf54l15`
  - `xiao_nrf54lm20b`
  - `holyiot_25007_nrf54l15`
  - `holyiot_25008_nrf54l15`
  - `generic_nrf54l15_module_36pin`
  - `nrf54l15dk_pca10156`

### Matter

- The custom Matter foundation is integrated as a staged build option.
- PASE/CASE demo code compiles.
- On-network On/Off Light command-surface demos compile.
- Matter Thread dataset and data-model seed headers are enabled for staged
  Matter builds.
- The staged Matter include surface is now consistent across all supported
  staged board profiles.

### Not Yet Production Complete

- Home Assistant commissioning through a real OTBR still needs repeated
  hardware validation.
- SRP/mDNS/DNS-SD behavior must be verified against an external Thread Border
  Router, not only two-board demos.
- Long-duration attach/reconnect/payload soak tests need pass/fail logs from
  two real boards.
- Matter operational certificate/fabric behavior needs validation beyond local
  demos.
- BLE commissioning for Matter is not complete.
- Thread sleepy-end-device power behavior needs a dedicated current pass after
  protocol correctness is stable.

## Build Verification

Use the local compile matrix before and after every Thread/Matter change:

```bash
scripts/test_thread_matter_compile_matrix.py
```

For a deeper pass:

```bash
scripts/test_thread_matter_compile_matrix.py --full
```

The script intentionally copies the local platform under a temporary
`localnrf54` vendor namespace. This prevents Arduino CLI from silently using an
installed Board Manager package instead of this checkout.

Default matrix currently compiles:

- `xiao_nrf54l15` + `ThreadExperimentalUdpSoak`
- `xiao_nrf54l15` + `MatterOnNetworkOnOffLightCommandSurfaceDemo`
- `xiao_nrf54lm20b` + `ThreadExperimentalPskcUdpHello`
- `xiao_nrf54lm20b` + `MatterOnNetworkOnOffLightNodeDemo`
- `holyiot_25008_nrf54l15` + `ThreadExperimentalPskcUdpHello`
- `generic_nrf54l15_module_36pin` + `MatterOnNetworkOnOffLightNodeDemo`
- `nrf54l15dk_pca10156` + `ThreadExperimentalUdpSoak`

Last local default result:

```text
PASS: compiled 7 Thread/Matter cases
```

Last local full result:

```text
PASS: compiled 12 Thread/Matter cases
```

## Runtime Test Plan

### Two-Board Thread UDP Soak

Use two XIAO nRF54L15-class boards with Thread staged mode enabled.

```bash
python3 scripts/test_thread_udp_soak.py --help
```

Expected coverage:

- Thread bring-up and role assignment.
- Dataset export/import.
- Unicast UDP payloads.
- Downlink UDP payloads.
- Multicast UDP payloads.
- Fragment-sized payload attempts up to the example matrix limit.
- Final `soak_done` and per-length result lines on serial.

### Reconnect Stress

Compile and run:

- `File > Examples > Thread > ThreadExperimentalReconnectStress`

Expected coverage:

- Attach after peer loss.
- Reattach after peer return.
- No high-current or busy-loop plateau after link loss.
- Stable role reporting after repeated cycles.

### Reference Dataset Attach

Compile and run:

- `File > Examples > Thread > ThreadExperimentalReferenceDatasetAttach`

Expected coverage:

- Known dataset import.
- Attach to a reference dataset.
- Stable role and RLOC reporting.

### Matter Command Surface

Compile and run:

- `File > Examples > Matter > MatterOnNetworkOnOffLightNodeDemo`
- `File > Examples > Matter > MatterOnNetworkOnOffLightCommandSurfaceDemo`

Expected coverage:

- Thread attach before Matter command exchange.
- On/Off/Toggle/Identify request encoding.
- Command response parsing.
- Serial output showing command result and endpoint state.

### Home Assistant / OTBR

Required external setup:

- A working Thread Border Router.
- Home Assistant Matter Server.
- Dataset matching between board and OTBR.

Expected validation:

- SRP service registration visible on the Thread network.
- Device can be discovered by the Matter server.
- Device can be commissioned or failure reason is captured at the PASE/CASE
  step.
- On/Off cluster commands from Home Assistant reach the board.

## Remaining Implementation Slices

1. Add a host-side SRP/DNS-SD validation helper that can observe expected
   service records from OTBR tooling.
2. Extend `test_matter_between_boards.py` to use dynamic ports, local checkout
   compilation, and clear pass/fail parsing like `test_thread_udp_soak.py`.
3. Add an OTBR/Home Assistant commissioning transcript parser for failed PASE,
   CASE, fabric, or DNS-SD stages.
4. Verify and document Thread channel/panid/dataset migration behavior.
5. Harden reboot recovery for Matter node state, including persisted fabric
   placeholders and dataset restore.
6. Add a Thread sleepy-device current profile after correctness is stable.
7. Add negative tests for wrong PSKd, stale dataset, wrong channel, and missing
   peer.
8. Decide whether BLE commissioning is in-scope for this bare-metal Matter
   path or documented as unsupported until the BLE/Matter bridge is complete.

## Rules For Future Changes

- Do not rely on installed Board Manager packages for validation.
- Run `scripts/test_thread_matter_compile_matrix.py` for compile safety.
- Run two-board serial tests for runtime changes.
- Do not claim Home Assistant support complete until an actual HA + OTBR
  commissioning log passes.
- Keep Thread/Matter board menu flags consistent unless a board has a documented
  silicon or pinout limitation.
