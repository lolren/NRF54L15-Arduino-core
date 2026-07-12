# Two-Board Release Gate

`scripts/run_two_board_release_gate.py` is the repeatable hardware evidence
gate for stable releases and release candidates. It uses one attached nRF54L15
board and one nRF54LM20A board (abbreviated `LM20` in runner artifacts),
identifies them by CMSIS-DAP UID, resolves their stable CDC ports through
`/dev/serial/by-id`, and writes every compiler, programmer, and serial artifact
to a timestamped directory under `measurements/`.

The runner creates an isolated Arduino CLI user/download tree inside its
artifact directory, registers only this checkout as the `localnrf54` hardware
package, and uses a unique build path for every phase/board. Tool discovery
uses the existing Arduino data directory (normally `~/.arduino15`), while the
compiler itself is pinned to the package-declared GCC `7-2017q4` path. It
overrides the interactive Save Hex hook so gate compiles cannot leave generated
UF2 files beside source sketches. Images are programmed with pyOCD and every
command is retained with its output.

## Profiles

Run the short hardware smoke gate:

```bash
python3 scripts/run_two_board_release_gate.py --profile smoke
```

Run the complete two-board gate before a release decision:

```bash
python3 scripts/run_two_board_release_gate.py --profile full
```

Repeat `--only-phase NAME` to rerun selected failures without repeating the
whole profile. For example:

```bash
python3 scripts/run_two_board_release_gate.py \
  --only-phase ble_numeric_comparison_reject \
  --only-phase ble_oob_pairing
```

Selected-phase runs report `SUBSET_PASS` and record their exact expected and
completed phase lists in `summary.json`. They are diagnostic evidence only and
never satisfy the clean `--profile full` release requirement.

Add `--oob-trace` when diagnosing OOB/SMP timing; connected-state trace output
is deferred away from radio deadlines and retained in the phase serial logs.

Use explicit UIDs only when automatic probe detection is ambiguous:

```bash
python3 scripts/run_two_board_release_gate.py \
  --profile full \
  --l15-uid 761FDE87 \
  --lm20-uid 3377B9D6
```

The default profiles are bounded. Longer soak evidence is produced by raising
the capture durations, retaining the resulting artifact directory, and
recording its path in the release notes. For example:

```bash
python3 scripts/run_two_board_release_gate.py \
  --profile full \
  --phy-cycle-s 900 \
  --pair-capture-s 900 \
  --pair-reconnect-capture-s 600
```

## Coverage

Both profiles perform the following checks.

| Phase | Evidence required |
| --- | --- |
| `build_environment` | Both FQBNs resolve through the isolated source registration and the declared GCC toolchain is executable. |
| `boot` | Both board families compile, chip-erase/program, reset, and emit the installed core version plus a heartbeat. |
| `ble_phy_mtu_dle` | L15 peripheral and LM20 central complete ATT discovery/CCCD subscription, MTU 247, DLE 251, 2M traffic, a 1M fallback, and a 2M return with long notifications. |
| `channel_sounding` | The released controller-backed CS initiator/reflector pair produces correlated positive results, rejects a silent reflector, and recovers afterward. |

The `full` profile additionally verifies:

Together with the four common phases above, these eleven phases make the full
release gate a 15-phase run.

| Phase | Evidence required |
| --- | --- |
| `ble_pair_bond` | Two-board Just Works pairing saves a bond on each board, encrypts, subscribes, writes GATT data, reloads bonds after reset, and encrypts the reconnect. |
| `ble_signed_write` | Fresh encrypted pairing distributes SignKey material and saves a signing-ready bond on both boards. After an unencrypted reconnect, the central sends a production signed write, the peripheral applies it exactly once, and an injected replay is rejected. The runner then clears both `.noinit` bond/CCCD caches through pyOCD; a second reconnect must reload the bond, CSRKs, and counters from built-in RRAM before another signed write succeeds. |
| `ble_numeric_comparison` | Both boards display the same six-digit value, accept it, exchange encrypted GATT traffic, save authenticated bonds, and reconnect without prompting or saving another bond. |
| `ble_numeric_comparison_reject` | The initiator accepts while the responder rejects the same Numeric Comparison request; the gate requires pairing failure, no link encryption, and no bond save. |
| `ble_oob_pairing` | The runner captures both fresh LE SC `r/c` records, exchanges the exact `peer` records over Serial, and requires authenticated mutual-OOB pairing plus bidirectional encrypted UART. |
| `ble_oob_peripheral_to_central` | Only the peripheral publishes an OOB record. The central consumes it, encrypted UART succeeds, and both sides remain conservatively marked unauthenticated because the exchange was one-way. |
| `ble_oob_central_to_peripheral` | Only the central publishes an OOB record. The peripheral consumes it, encrypted UART succeeds, and both sides remain conservatively marked unauthenticated because the exchange was one-way. |
| `ble_rpa_rotation` | The privacy example passes its IRK/AAR/resolving-list self-tests while the peer scanner observes at least two distinct valid RPAs for `X54-RPA`. |
| `ble_privacy_bond` | A fresh privacy-mode pair distributes identity information, preserves each local identity, changes each RPA after reset, resolves the bonded peer through AAR, primes the retained bond, and reconnects encrypted without re-pairing. |
| `ble_reset_recovery` | The BLE capability cycle completes, the peripheral is reset, the central reports disconnection, reconnects automatically, and completes a second full cycle. |
| `system_off_wake` | Both boards enter timed System OFF and reboot from a GRTC wake source. |

The three OOB phases replace the former `scripts/test_oob.py`, which depended
on obsolete probe IDs, port ordering, and serial markers. The gate transports
the example records over USB Serial only as a deterministic stand-in for an
application-owned NFC, QR, or other out-of-band channel.

Each phase rejects fatal controller markers and produces a `summary.json` with
the probe IDs, timings, result status, exact paths, and declared limitations.

The GRTC PWM fixtures use the variant-provided `PIN_GRTC_PWM` alias. It always
maps to the silicon-fixed `P0.03` output; it must not be replaced with a
generic `Wire1` pin, because that route is board-specific (on LM20 it is
`P0.07`).

## Release Use

The stable `1.0.0` release requires a clean `--profile full` result from the
release source checkout/core version, separate verification and compilation
from the exact release archive, a fresh package-install compilation pass, the
existing compile CI, and the retained hardware artifact directory. Longer soak
runs should be retained with the release-validation record whenever they inform
the release decision.

This gate is intentionally not presented as proof of full Bluetooth
qualification. It does not replace phone/desktop interoperability testing,
power/current profiling, an RF attenuation test setup, multi-link stress, or
Bluetooth PTS/BQB qualification. Its CS phase covers the released standalone
LE CS Test pair, not a connected-ACL Channel Sounding application workflow.
The security/privacy phases cover the core's single retained bond and
application-managed resolving list. The signed-write phase covers SignKey
distribution, the production send/receive paths, replay rejection, and durable
counters between the two bundled peers; it does not establish phone/desktop
signed-write interoperability or a multi-peer signing policy. The gate also
does not test controller-enforced multi-peer privacy policy or a complete
negative SMP matrix. It does not inject entropy failure, negotiate reduced SMP
key sizes, wait out the 30-second SMP timer, validate repeated-attempt timing,
or exercise an in-place stored-bond security upgrade.
