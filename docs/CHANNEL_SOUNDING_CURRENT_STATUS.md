# Channel Sounding Current Status

Last updated: 2026-07-11

This is the current source of truth for Bluetooth LE Channel Sounding in this
Arduino core. Older VPR, raw RADIO, calibration, and Zephyr-parity documents are
historical bring-up records and do not describe the supported runtime.

## Supported Surface

The Arduino IDE exposes exactly two Channel Sounding examples:

- `BleChannelSoundingInitiator`
- `BleChannelSoundingReflector`

They target a two-board initiator/reflector test using the Nordic SoftDevice
Controller (SDC) and Multiprotocol Service Layer (MPSL). The public examples are
the supported entry point. HCI/VPR, raw-radio, LL-control, transport, parser,
soak, and interop diagnostics are retained under
`extras/tests/channel_sounding`; they are test fixtures, not additional Arduino
examples.

## Runtime Architecture

The controller-backed path uses this sequence:

1. The initiator creates a per-cycle session token and DRBG nonce. A
   CRC-protected, acknowledged request gives the reflector the same values.
2. Both boards release the request transport. `BleCsControllerRuntime` then
   initializes MPSL and SDC and starts an LE CS Test in the selected role.
3. SDC owns the timing-critical RADIO sequence and produces standard HCI LE Meta
   CS Subevent Result (`0x31`) and continuation (`0x32`) events.
4. The Arduino layer reassembles the controller step data and ends the CS Test.
5. After SDC releases RADIO ownership, the reflector returns an envelope with
   the session token, profile, role, controller counters, step count, and its
   completed local step buffer through the acknowledged transport.
6. The initiator accepts only an envelope correlated to its current session,
   then combines the two controller results and reports the coherent PBR range
   with RTT as supplemental diagnostic data.

The sounding operation itself is executed by the Nordic Bluetooth controller.
The pre-test session request and post-test step-buffer transfer are a separate
Arduino pair protocol. They are not a Bluetooth connected-ACL Channel Sounding
procedure or a general cross-vendor result-exchange profile.

## Required Board Setting

SDC/MPSL requires the application CPU to start at 128 MHz. Select:

```text
Tools -> CPU Frequency -> 128 MHz
```

For Arduino CLI builds, include `cpu_freq=128m`, for example:

```bash
arduino-cli compile \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:cpu_freq=128m \
  hardware/nrf54l15clean/nrf54l15clean/libraries/\
Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding/\
BleChannelSoundingInitiator
```

The runtime returns `BleCsControllerRuntime::kErrorRequires128MHz` instead of
starting at 64 MHz. Runtime clock switching is not a substitute for selecting
the 128 MHz boot profile.

The controller archives are selected per chip family. The current public pair
is intended for the XIAO nRF54L15 and XIAO nRF54LM20A board profiles. Other
board and antenna combinations require their own hardware validation.

## Standards And Accuracy Boundary

The implementation uses Nordic's LE CS Test controller command and HCI result
events. That is stronger evidence than the retired direct-radio CTE/CSTONES
experiment, but it is not a Bluetooth SIG qualification or certification of
this Arduino core or a product built from it.

In particular, this project does not currently claim:

- Bluetooth SIG qualification or RF conformance certification;
- a normal connected ACL Channel Sounding host workflow;
- cross-vendor application-level result transport;
- universal antenna-delay calibration or tape-measure accuracy;
- calibrated RTT on every board pair; or
- production power figures for active Channel Sounding.

Treat reported ranges as engineering measurements. Validate and calibrate the
specific boards, antennas, placement, and enclosure before relying on absolute
distance.

## Validation Status

The controller path has warning-clean builds for both public roles on both
board families. The two-board harness chip-erases each target, verifies the
first two vector words against the compiled image, slices fresh serial logs,
and requires repeated complete `0x31`/`0x32` reassembly, matching session token,
transfer ID and CRC, finite coherent PBR estimates, and zero controller packet
drops.

Results captured from the current source-tree examples:

- XIAO nRF54L15 initiator and XIAO nRF54LM20A reflector: 20 accepted initiator
  ranges, 23 reflector results, and 20 exact session/transfer matches.
- Silent reflector: zero accepted initiator ranges. After restoring the
  reflector, recovery produced 37 accepted ranges, 42 reflector results, and
  38 matching result sessions (the capture ended while the final accepted
  initiator line was being printed).
- XIAO nRF54LM20A initiator and XIAO nRF54L15 reflector: 12 accepted initiator
  ranges, 15 reflector results, and 12 exact session/transfer matches.
- Accepted procedures contained 42 controller steps on each side and 34 or 35
  usable PBR channels. The tested runs reported no parser rejection or
  controller packet drop.

This validates both roles on both attached chip families and demonstrates that
the initiator cannot manufacture an accepted range while the reflector is
absent. It is hardware evidence for these two boards and this exact test path,
not a universal interoperability or accuracy claim.

The official Zephyr/NCS `connected_cs` sample has separately demonstrated that
the attached nRF54L15/nRF54LM20A hardware can execute controller-backed Channel
Sounding. That reference run is useful hardware evidence, but it is not evidence
that the Arduino runtime implements Zephyr's connected application workflow.

## Nordic Binary Components

Channel Sounding links bundled Nordic binary libraries for the `nrf54l` and
`nrf54lm` families:

- SoftDevice Controller multirole;
- MPSL; and
- MPSL FEM common.

Bundled revision:

```text
Nordic nrfxlib revision: 7a07f89ee8c32658ebfd2034b4cae92fde63e122
Upstream describe: v3.4.0-rc1-12-g7a07f89ee
```

These components are not covered by the core's MIT license. They are distributed
under the included Nordic 5-Clause license and associated attribution notice:

- [`third_party/nordic_sdc/LICENSE`](../hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/third_party/nordic_sdc/LICENSE)
- [`third_party/nordic_sdc/LICENSE-ATTRIBUTION.txt`](../hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/third_party/nordic_sdc/LICENSE-ATTRIBUTION.txt)
- [`third_party/nordic_sdc/VERSION`](../hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/third_party/nordic_sdc/VERSION)

The Nordic license restricts use to Nordic Semiconductor integrated circuits
and prohibits reverse engineering, decompiling, modifying, or disassembling the
binary software. Redistributed packages must retain the applicable notices.

## Historical Paths

The previous raw RADIO/2M/CTE/CSTONES implementation did not execute a complete
Bluetooth LE Channel Sounding procedure. Its calibration profiles and distance
captures remain archived as evidence of that experiment only and must not be
applied to SDC controller results.

The synthetic VPR HCI result generator remains useful for parser and state
regressions. Its deterministic step data is not physical ranging evidence.
