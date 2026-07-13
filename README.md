# <picture><source media="(prefers-color-scheme: dark)"><img align="right" width="80" src="https://raw.githubusercontent.com/lolren/nrf54-arduino-core/main/docs/xiao_nrf54l15_default_pin_routes.png"></picture> nRF54L Arduino Core

<div align="center">

**Bare-metal Cortex-M33 + RISC-V development with the Arduino API. No Zephyr
runtime or external nRF Connect SDK installation.**

[![Release](https://img.shields.io/github/v/release/lolren/nrf54-arduino-core?color=00d4ff&label=latest)](https://github.com/lolren/nrf54-arduino-core/releases)
[![Boards](https://img.shields.io/badge/board_targets-6-00d4ff)](#supported-boards)
[![License](https://img.shields.io/badge/license-MIT%20%2B%20third--party-00d4ff)](LICENSE)

*A register-level Arduino core for Nordic's nRF54L family, with a mature
peripheral surface, a practical Bluetooth LE stack, low-power board support,
and direct access to the VPR RISC-V coprocessor.*

</div>

---

## Project Scope

The `1.0.0` line focuses on a dependable Arduino, peripheral, power-management,
and Bluetooth LE experience on supported nRF54L boards. The core is suitable
for real BLE prototyping and embedded applications within the documented
single-link security/privacy scope. Release-critical feature probes are compiled
from the exact packaged archive, the source checkout receives broader example
coverage, and the release gate exercises both a XIAO nRF54L15 and a XIAO
nRF54LM20A.

The following protocol work is included for evaluation, but is **not finished
or production-ready**:

| Area | Current boundary |
|---|---|
| **Zigbee** | Experimental partial stack and device demonstrations; incomplete Zigbee PRO coverage, routing, clusters, OTA, and ecosystem interoperability |
| **Thread** | Experimental staged OpenThread FTD/MeshCoP/SRP/UDP paths; production commissioning, sleepy-device coverage, interoperability, and soak testing remain |
| **Matter** | Experimental on-network examples and protocol/crypto bring-up; not a complete certifiable Matter device implementation |
| **Channel Sounding** | Experimental two-board controller-backed LE CS Test; not connected-ACL Channel Sounding, calibrated ranging, cross-vendor interoperability, or a qualification claim |

These boundaries keep the stable claims precise: unfinished protocol examples
are useful engineering work, but they are not presented as complete standards
implementations.

## Why Use This Core?

| Capability | Practical benefit |
|---|---|
| **Arduino workflow** | Install from Boards Manager, select a board, compile normal `.ino` sketches, and upload over a connected CMSIS-DAP probe |
| **No Zephyr runtime** | Shorter build cycles, smaller applications, and fewer framework layers between a sketch and the hardware |
| **Direct peripheral access** | GPIO, ADC, PWM, serial buses, audio, NFC, DPPI, GRTC, watchdog, comparators, and power controls are implemented against nRF54L hardware |
| **Useful BLE depth** | Peripheral, central, dual-role, GATT server/client, HID, BLE UART, PHY/DLE/MTU control, LE Secure Connections, bonding, OOB, Numeric Comparison, privacy/RPA, and CSRK signed writes |
| **Low-power board integration** | System ON idle, timed System OFF, reset-cause APIs, XIAO RF-switch control, LM20A external-flash power-down, and nPM1300 hibernate support |
| **Dual-core access** | Use the 128 MHz Cortex-M33 application CPU and the VPR RISC-V coprocessor without adopting a vendor RTOS |
| **Observable validation** | Public diagnostics, regression scripts, release-archive compilation, and a repeatable two-board hardware gate document what was actually tested |

This core is a good fit when Arduino productivity and close control over timing,
memory, radio behavior, or power matter more than adopting the full nRF Connect
SDK application model. For a product that requires a Bluetooth, Matter, Thread,
or Zigbee qualification program, treat this core as engineering source and
validate the complete product against the relevant conformance suite.

## Contents

- [Quick install](#quick-install)
- [Getting started](#getting-started)
- [Supported boards](#supported-boards)
- [Feature matrix](#feature-matrix)
- [Bluetooth LE](#bluetooth-le)
- [Power consumption](#power-consumption)
- [SPI and QSPI](#spi-speed-and-routing)
- [System OFF and PMIC APIs](#timed-system-off-apis)
- [Maturity and limitations](#stack-maturity)
- [Troubleshooting](#troubleshooting)
- [License](#license)

## Quick Install

```text
https://raw.githubusercontent.com/lolren/nrf54-arduino-core/main/package_nrf54l15clean_index.json
```

Add this URL in **Arduino IDE → Preferences → Additional Boards Manager URLs**, then install **nRF54L15 Boards** from the Boards Manager.

Normal uploads use the bundled native [**nRF OCD**](https://github.com/lolren/open-nrf-ocd) tool on Linux and Windows, so Windows does not need a separate Python install just to upload. If native upload fails, switch **Tools -> Upload Method** to **pyOCD Recovery**; its first setup installs pinned dependencies into the packaged tool-local runtime and requires access to the configured Python package index.

### Arduino CLI Install And Updates

Arduino IDE users can update through **Tools -> Board -> Boards Manager** by searching for **nRF54L15 Boards** and clicking **Update** when a newer release is available.

Linux/macOS:

```bash
BOARD_URL="https://raw.githubusercontent.com/lolren/nrf54-arduino-core/main/package_nrf54l15clean_index.json"

arduino-cli config add board_manager.additional_urls "$BOARD_URL"
arduino-cli core update-index
arduino-cli core install nrf54l15clean:nrf54l15clean

# Check whether this core has an available update.
arduino-cli core list --updatable

# Update this core to the latest Board Manager release.
arduino-cli core upgrade nrf54l15clean:nrf54l15clean
```

Windows PowerShell:

```powershell
$BoardUrl = "https://raw.githubusercontent.com/lolren/nrf54-arduino-core/main/package_nrf54l15clean_index.json"

arduino-cli config add board_manager.additional_urls $BoardUrl
arduino-cli core update-index
arduino-cli core install nrf54l15clean:nrf54l15clean

# Check whether this core has an available update.
arduino-cli core list --updatable

# Update this core to the latest Board Manager release.
arduino-cli core upgrade nrf54l15clean:nrf54l15clean
```

`arduino-cli outdated` is also useful when you want to see all updatable cores and libraries. If `core list --updatable` prints nothing for `nrf54l15clean:nrf54l15clean`, the installed core is already current.

---

## Getting Started

### Arduino IDE

1. Install the package with the Boards Manager URL above.
2. Connect the board and select **Tools -> Board -> nRF54L15 Boards**, then
   choose **XIAO nRF54L15 / Sense** or **XIAO nRF54LM20A**.
3. Keep **CPU Frequency -> 64 MHz** for normal sketches. Use 128 MHz only when
   an example explicitly requires it, including the Channel Sounding pair.
4. Start with one of the examples below and click **Upload**. The default native
   nRF OCD uploader selects the connected CMSIS-DAP probe; the recovery uploader
   is available from **Tools -> Upload Method**.
5. Open the Serial Monitor at the baud rate selected by the sketch. If serial
   output is missing, verify **Tools -> Serial Routing** and close other programs
   that own the port.

Recommended first examples:

| Goal | Arduino example |
|---|---|
| Verify the board and core | `Nrf54L15-Clean-Implementation > Diagnostics > CleanBringUp` |
| Create a phone-visible BLE UART peripheral | `Bluefruit52Lib > Peripheral > bleuart` |
| Scan and connect as a BLE central | `Bluefruit52Lib > Central > central_bleuart` |
| Exercise authenticated pairing | `Bluefruit52Lib > Security > pairing_numeric_comparison` |
| Enter and recover from timed System OFF | `Nrf54L15-Clean-Implementation > LowPower > LowPowerGrtcPwmSystemOff` |
| Inspect LM20A external flash | `nRF54 Board Examples > XIAO-nRF54LM20A > QspiFlashInfo` |
| Read the LM20A Sense microphone | `nRF54 Board Examples > XIAO-nRF54LM20A-Sense > XiaoLM20A_MicLevel` |

Hardware-specific sketches are grouped under `File > Examples > nRF54 Board Examples`.
The `XIAO-nRF54L15-Sense` and `XIAO-nRF54LM20A-Sense` submenus
contain the matching onboard IMU and microphone sketches; base-board flash,
RGB, and control examples remain in their XIAO submenu, while HOLYIOT-25008
and the Nordic nRF54L15 DK have separate submenus.

### Arduino CLI

The board identifiers deliberately retain the original internal LM20B name for
package compatibility:

```bash
# XIAO nRF54L15 / Sense
arduino-cli compile \
  --fqbn "nrf54l15clean:nrf54l15clean:xiao_nrf54l15" \
  "$HOME/Arduino/MySketch"

# XIAO nRF54LM20A / Sense
arduino-cli compile \
  --fqbn "nrf54l15clean:nrf54l15clean:xiao_nrf54lm20b" \
  "$HOME/Arduino/MySketch"

# The core can identify a single attached probe automatically.
arduino-cli upload \
  --fqbn "nrf54l15clean:nrf54l15clean:xiao_nrf54l15" \
  "$HOME/Arduino/MySketch"
```

When two boards are attached, select the target explicitly with the port or UID
reported by `arduino-cli board list` or `pyocd list`. Disable experimental
Thread, Matter, or Zigbee build options unless the sketch actually uses them;
this reduces compile/link surface and makes the intended runtime clear.

### Sketch Compatibility

Normal Arduino APIs such as `pinMode`, `digitalWrite`, `analogRead`,
`analogWrite`, `Wire`, `SPI`, and `Serial` are available. BLE sketches should
use the bundled `Bluefruit52Lib` compatibility API or the lower-level clean-core
BLE interfaces shown in the packaged examples. `ArduinoBLE` is not the native
BLE library for this core.

Board-specific hardware still matters. The Sense IMU is on `Wire1`; the LM20A
external header SPI and onboard QSPI flash use different controllers; and the
L15 antenna selection controls an external RF switch. Consult the
[board reference](docs/board-reference.md) before using fixed-function pins or
direct register access.

---

## Current BLE Highlights

- LE Secure Connections security now includes asynchronous Numeric Comparison
  accept/reject handling, request-scoped keyboard/passkey input, explicit
  bonding/MITM/SC/key-size policy, and OOB records supplied mutually or in
  either one-way direction.
- SMP security now reduces encryption keys to the negotiated 7-16 octet size,
  enforces the 30-second transaction timeout with single-peer repeated-attempt
  throttling, and aborts without deterministic fallback if CRACEN entropy is
  unavailable.
- BLE privacy now keeps the stable local identity separate from the active RPA,
  derives the local IRK from the identity root, distributes identity keys during
  bonding, resolves bonded peers through hardware AAR, and reuses the retained
  bond after an RPA change.
- SMP can exchange and retain CSRKs for both roles. ATT Signed Write Commands
  use AES-CMAC signatures, monotonically persisted counters, replay rejection,
  and a Bluefruit client `writeSigned()` entry point.
- Bond storage now retains up to eight peers with power-loss-safe replicated
  records, per-peer CCCDs and Service Changed state, role-correct legacy key
  tuples, LRU replacement, indexed inspection/deletion, and RPA resolution.
- GATT Robust Caching includes a spec-generated 128-bit Database Hash,
  persistent per-bond Client Supported Features, Database Out Of Sync handling,
  change-aware transitions, and notification/indication suppression while a
  client cache is stale.
- Bluefruit central support now includes safe service/characteristic object
  lifetimes, handle-range discovery, bulk characteristic discovery,
  `readCharByUuid()`, central indications, and bonded reconnect encryption.
- Custom GATT characteristics now enforce fixed and maximum lengths for local
  updates, remote writes, and prepare/execute writes. Service permissions are
  inherited by their characteristics. Dynamic read/write authorization runs
  callbacks outside the radio ISR, supports approve, reject, and replacement
  values, and fails closed on timeout or disconnect. Battery Service database
  writes and explicit notifications now have distinct behavior.
- `BLEUart::bufferTXD(true)` now coalesces small writes into the current ATT
  notification payload, sends a full packet automatically, and lets sketches
  explicitly flush a partial packet without discarding it on backpressure.
- Bluefruit central reads continue with Read Blob requests across ATT fragments,
  characteristic discovery determines complete handle ranges, and the HID
  client selects generic reports through their Report Reference descriptors.
- ATT responses and callbacks now act only on fresh link-layer packets, and
  service, characteristic, descriptor, and Read Blob pagination rejects
  malformed lengths, out-of-range handles, and non-advancing peer responses.
- Connection setup now rejects malformed access addresses, timing, channel-map,
  window, and hop fields before changing live state. Accepted central-side LL
  and L2CAP connection-parameter requests proceed to a scheduled connection
  update, and reserved SMP fields are rejected.
- Locally initiated connections now derive their access address and CRC seed
  from fail-closed hardware entropy before radio ownership changes, including
  the additional transition constraints required by LE Coded PHY.
- The ANCS client now sends complete notification/app attribute commands,
  reassembles fragmented Data Source responses, and implements title, subtitle,
  message, app-name, message-size, date, action-label, and action APIs. Fragmented
  response parsing is covered by host-side regression tests; live iOS behavior
  still depends on the phone's ANCS permissions and interoperability.
- The full [two-board release gate](docs/TWO_BOARD_RELEASE_GATE.md) exercises
  positive and rejected Numeric Comparison, all three OOB directions, RPA
  rotation, identity-key distribution, privacy-aware bonded reconnects, and
  signed-write counter persistence/replay rejection on XIAO nRF54L15 and XIAO
  nRF54LM20A.
- Timed System OFF now verifies reset-reason clearing before entry and exposes
  an abort-stage diagnostic; the gate requires two consecutive GRTC wake-reset
  cycles after the deliberate reset boundary on each connected board.
- XIAO nRF54LM20A Sense PDM uses the product-specific EDGE/RATIO encodings and
  a 1.28 MHz clock for 16 kHz PCM. The driver uses byte-addressed EasyDMA,
  re-arms LM20 clock/filter configuration after STOP, fences DMA ownership at
  `STOPPED`, and invalidates stale cache lines before samples are read. A final
  simultaneous 70-second hardware soak completed **115/115** captures on each
  Sense board with no timeout, underfill, guard, or DMA error: L15 captures took
  503-505 ms and LM20A captures 504-505 ms. Live peak-to-peak response reached
  4,626 counts on L15 and 3,994 counts on LM20A.
- Arduino IDE examples are grouped by board and Sense hardware. LM20A flash and
  RGB sketches, both XIAO Sense IMU/microphone sets, HOLYIOT-25008, and the
  Nordic DK now have distinct menus, with duplicate platform sketches removed.

Install the stable release from the normal Boards Manager feed shown above, or
request the exact version with Arduino CLI:

```bash
arduino-cli core install "nrf54l15clean:nrf54l15clean@1.0.1"
```

- Controller-backed Bluetooth LE Channel Sounding Test is now available through
  the two public Arduino examples: `BleChannelSoundingInitiator` and
  `BleChannelSoundingReflector`.
- Nordic SDC/MPSL runs the timing-critical LE CS Test. The examples establish a
  per-cycle session token before the test and verify that same token when the
  reflector returns its controller result, so delayed results are not paired
  with a new sounding cycle.
- The public pair is hardware-validated with XIAO nRF54L15 and XIAO nRF54LM20A
  in both initiator and reflector roles. It requires the 128 MHz CPU profile.

This is an experimental two-board LE CS Test path, not a connected-ACL Channel
Sounding implementation or a Bluetooth qualification claim. See the
[Channel Sounding status](docs/CHANNEL_SOUNDING_CURRENT_STATUS.md) for the
protocol boundary, measurements, and validation evidence.

---

## Supported Boards

<div align="center">

| | |
|---|---|
| <img src="docs/xiao_nrf54l15_default_pin_routes.png" width="280"><br>**[XIAO nRF54L15 / Sense](https://wiki.seeedstudio.com/xiao_nrf54l15_sense_getting_started/)**<br>`xiao_nrf54l15` | <img src="docs/nrf54lm20a_front_pinout.png" width="280"><br>**[XIAO nRF54LM20A / Sense](https://wiki.seeedstudio.com/xiao_nrf54lm20a_with_onboard/)**<br>`xiao_nrf54lm20b` |
| <img src="docs/boards/holyiot_25007_product.png" width="280"><br>**[HOLYIOT-25007](docs/holyiot-25007-module-reference.md)**<br>`holyiot_25007_nrf54l15` | <img src="docs/boards/holyiot_25008_product.jpg" width="280"><br>**[HOLYIOT-25008](docs/holyiot-25008-module-reference.md)**<br>`holyiot_25008_nrf54l15` |

</div>

| Product | Arduino board selection | Specs / notes |
|---|---|---|
| **[Seeed Studio XIAO nRF54L15](https://wiki.seeedstudio.com/xiao_nrf54l15_sense_getting_started/)** | `XIAO nRF54L15 / Sense` (`xiao_nrf54l15`) | 128 MHz M33 + 128 MHz RISC-V coprocessor · 1.5 MB NVM · 256 KB RAM |
| **[Seeed Studio XIAO nRF54L15 Sense](https://wiki.seeedstudio.com/xiao_nrf54l15_sense_getting_started/)** | `XIAO nRF54L15 / Sense` (`xiao_nrf54l15`) | Same core board support as XIAO nRF54L15 · onboard LSM6DS3TR-C IMU + MSM261DGT006 PDM mic |
| **[Seeed Studio XIAO nRF54LM20A](https://wiki.seeedstudio.com/xiao_nrf54lm20a_getting_started/)** | `XIAO nRF54LM20A` (`xiao_nrf54lm20b`) | 128 MHz M33 + 128 MHz RISC-V coprocessor · 2 MB NVM · 512 KB RAM · nPM1300 PMIC · onboard 8 MB PY25Q64 QSPI flash · [back pinout](docs/nrf54lm20a_back_pinout.png) |
| **[Seeed Studio XIAO nRF54LM20A Sense](https://wiki.seeedstudio.com/xiao_nrf54lm20a_with_onboard/)** | `XIAO nRF54LM20A` (`xiao_nrf54lm20b`) | Same core board support as XIAO nRF54LM20A · onboard LSM6DS3TR-C IMU + MSM261DGT006 PDM mic · nPM1300 PMIC · onboard 8 MB PY25Q64 QSPI flash · [back pinout](docs/nrf54lm20a_back_pinout.png) |
| **[HOLYIOT-25007](docs/holyiot-25007-module-reference.md)** | `HOLYIOT-25007 nRF54L15 Module` (`holyiot_25007_nrf54l15`) | 18.0 x 14.8 mm · PCB antenna |
| **[HOLYIOT-25008](docs/holyiot-25008-module-reference.md)** | `HOLYIOT-25008 nRF54L15 Module` (`holyiot_25008_nrf54l15`) | 23.2 x 17.5 mm · PCB antenna |
| **Generic nRF54L15 36-pad module** | `Generic nRF54L15 Module (36-pad)` (`generic_nrf54l15_module_36pin`) | Reference module target; verify the power, clock, antenna, and pin design of the carrier board |
| **Nordic nRF54L15 DK** | `Nordic PCA10156 nRF54L15 DK` (`nrf54l15dk_pca10156`) | Development-kit target for bring-up and peripheral testing |

> See [board reference](docs/board-reference.md) for detailed pin assignments and schematics.

---

## Why Bare Metal?

| | This Core | nRF Connect SDK |
|---|---|---|
| **RTOS** | None (opt-in Thread/Matter) | Zephyr RTOS mandatory |
| **Binary size** | ~12 KB blink | ~100 KB+ blink |
| **Compiler** | GCC | GCC + Zephyr build system |
| **Peripheral access** | Direct register writes | Vendor HAL + DTS |
| **BLE stack** | Custom register-level; bundled Nordic SDC/MPSL for Channel Sounding | SoftDevice / Zephyr BLE |
| **RISC‑V coprocessor** | Fully usable | Limited Arduino access |
| **Learning curve** | Datasheet + Arduino API | Zephyr + DTS + Kconfig |

**This core is for developers who want full hardware control with Arduino convenience.**

---

## Feature Matrix

### Wireless

| | BLE | 802.15.4 | Thread | Zigbee | Matter | CS |
|---|---|---|---|---|---|---|
| **Advertising** | ✅ | — | — | — | — | — |
| **Scanning** | ✅ | — | — | — | — | — |
| **1M / 2M / Coded PHY** | ✅ | — | — | — | — | — |
| **GATT Server + Client** | ✅ | — | — | — | — | — |
| **Bluefruit API** | ✅ | — | — | — | — | — |
| **LE Secure Connections** | ✅ | — | — | — | — | — |
| **OOB + Numeric Comparison** | ✅ | — | — | — | — | — |
| **Privacy / RPA** | ✅ | — | — | — | — | — |
| **CSRK / authenticated signed writes** | ✅ | — | — | — | — | — |
| **Controller LE CS Test (Mode 2 / PBR)** | — | — | — | — | — | ⚠️ |
| **MAC / NWK / APS** | — | ✅ | ⚠️ | ⚠️ | — | — |
| **Coordinator / Router** | — | — | ⚠️ | ⚠️ | — | — |
| **End Device** | — | — | ⚠️ | ⚠️ | — | — |
| **UDP Transport** | — | — | ✅ | — | ⚠️ | — |
| **ZCL (OnOff / Level / Temp)** | — | — | — | ⚠️ | — | — |
| **On/Off Light + commissioning** | — | — | — | — | ⚠️ | — |

`CS` is limited to the controller-backed, single-antenna two-board test path
described below. It is not a general connected BLE Channel Sounding API.

The BLE security and privacy check marks describe the implemented clean-core
scope: single-link LE Secure Connections, Numeric Comparison user consent,
mutual and one-way OOB, a stable identity with rotating local RPAs, SMP identity
key/address distribution, hardware AAR resolution, retained bonded reconnects,
and CSRK-signed ATT Signed Write Commands with persisted anti-replay
counters. They do not assert complete Bluetooth Core conformance or Bluetooth
SIG qualification. A multi-bond privacy policy, locally generated legacy
bond-key distribution, controller-enforced allow-list policy, broader host
interoperability, and PTS/BQB qualification remain outside this claim.

### Crypto

| | Hardware | Status |
|---|---|---|
| **CRACEN RNG** | ✅ | Production |
| **CRACEN IKG** | ⚠️ | Key derivation requires a trusted pre-provisioned KMU seed; direct seed validation and unfinished high-level PKE wrappers fail closed |
| **AES‑CCM / AES‑ECB** | ✅ | Hardware‑accelerated |
| **PBKDF2‑HMAC‑SHA256** | ✅ | Hardware‑accelerated |
| **ECDSA sign** | ✅ | ~0.84 s |
| **ECDSA verify** | ✅ | ~1.76 s |
| **secp256r1 ECC** | ⚠️ | Software‑only (CRACEN PK engine needs Nordic microcode) |

### Peripherals

| | Status |
|---|---|
| **GPIO, PWM, ADC, I2C, SPI, UART** | ✅ |
| **HS-SPI 32 MHz** (`SPI_HS`) | ✅ |
| **I2S, PDM Microphone** | ✅ |
| **QDEC** (rotary encoder) | ✅ |
| **NFC‑A Tag** | ✅ |
| **Temperature Sensor** | ✅ |
| **Comparator, LPCOMP** | ✅ |
| **Watchdog Timer** | ✅ |
| **Deep Sleep / System OFF** | ✅ |
| **DPPI** (hardware event system) | ✅ |
| **Tamper Detection** | ✅ |
| **KMU** (key management) | ✅ |

### System

| | Status |
|---|---|
| **VPR RISC‑V Coprocessor** | ✅ |
| **SoftPeripheral SDK + sQSPI** | ✅ |
| **nPM1300 PMIC Driver** | ✅ |
| **LM20A QSPI Flash + Sleep** | ✅ |
| **GPIO Bit‑Bang I²C** (zero residual) | ✅ |
| **Buck Hysteretic Mode** (µA sleep) | ✅ |
| **LM20A IMU** (LSM6DS3TR‑C) | ✅ |
| **LM20A PDM Mic** (MSM261DGT006) | ✅ |

---

> **Legend:** ✅ Production &nbsp; ⚠️ Experimental / Partial &nbsp; 🚧 In Development

---

## Bluetooth LE

Bluetooth LE is the core's primary wireless surface. It uses a clean,
register-level host/controller implementation for normal BLE operation and
ships a `Bluefruit52Lib` compatibility layer so familiar Adafruit-style
sketches can be moved to nRF54L with limited changes.

### Implemented Surface

| Layer | Available functionality |
|---|---|
| **GAP** | Advertising, active/passive scanning, central and peripheral roles, dual-role examples, connection parameter updates, RSSI, 1M/2M/Coded PHY, and reconnect behavior |
| **ATT/GATT** | Server and client discovery, 16-bit and 128-bit UUIDs, services, characteristics, descriptors, CCCDs, long reads/writes, fixed/maximum value lengths, notifications, indications, MTU 247, and DLE 251 paths |
| **Compatibility services** | BLE UART/NUS, Device Information, Battery, HID keyboard/mouse/gamepad, Current Time, ANCS notification and fragmented attribute handling, beacons, and custom services |
| **Security** | LE Secure Connections, Just Works, passkey/PIN flows, asynchronous Numeric Comparison, mutual and one-way OOB, bonding, negotiated 7-16-byte key sizes, CSRK signed writes, and authenticated access permissions |
| **Privacy** | Stable identity, local IRK derivation, rotating RPAs, identity-key distribution, eight-peer bond storage, direct/identity/RPA selection, and privacy-aware directed reconnect |
| **Reliability** | Encrypted retransmission/counter handling, signed-write anti-replay counters, SMP timeout and repeated-attempt controls, fail-closed CRACEN entropy, and release-gate positive/negative pairing tests |

The two-board release gate checks advertising, discovery, CCCDs, PHY changes,
MTU/DLE, long notifications, pairing, bond reload, encrypted reconnect,
Numeric Comparison acceptance and rejection, all three OOB directions, RPA
rotation, identity-key distribution, AAR-based bond resolution, and signed
writes with persisted monotonic counters plus replay rejection. See
[Two-Board Release Gate](docs/TWO_BOARD_RELEASE_GATE.md) for the exact scope and
test procedure.

### Choosing An API

- Use `#include <bluefruit.h>` for the largest example set and compatibility
  with common Bluefruit sketches.
- Start with `Bluefruit52Lib > Peripheral > bleuart` for a phone or computer
  peripheral, or `Bluefruit52Lib > Central > central_bleuart` for a central.
- Use `BLEService` and `BLECharacteristic` for custom GATT services. Configure
  properties, permissions, fixed or variable length, and callbacks before
  calling `begin()`.
- Use `Bluefruit.Security` and characteristic permissions when a value must be
  encrypted or authenticated. Numeric Comparison requires an explicit
  asynchronous accept/reject response; the bundled example shows the complete
  flow.
- Use `Bluefruit.Security.enumerateBonds()`, `getBondInfo()`, `deleteBond()`,
  and `clearBonds()` to manage up to eight built-in peer records. The legacy
  custom persistence callback interface remains intentionally capacity-one.
- Use the lower-level examples under `Nrf54L15-Clean-Implementation` for radio,
  privacy, crypto, and hardware diagnostics where compatibility wrappers would
  hide the behavior being tested.
A normal peripheral sketch follows this order:

1. Create service and characteristic objects globally.
2. Call `Bluefruit.begin()`, set the device name and TX power, and register
   connection callbacks.
3. Configure security before starting protected services.
4. Configure and start each GATT service.
5. Add flags, advertised services, and the device name to the advertising and
   scan-response payloads.
6. Enable restart-on-disconnect, select advertising intervals, and call
   `Bluefruit.Advertising.start()`.

### BLE Scope Boundary

The core's BLE claim is intentionally narrower than "all Bluetooth LE." It does
not claim Bluetooth SIG qualification or complete Core conformance. Locally
generated legacy bond-key distribution and the built-in multi-bond privacy
store are implemented; automatic controller-enforced allow-list policy, broad
cross-vendor negative testing, and PTS/BQB remain outside the validated scope.
The built-in store retains up
to eight peers with isolated signing counters, CCCDs, privacy identities, and
Service Changed state. Nordic secure DFU is not
implemented, and `BLEDfu::begin()` returns `ERROR_NOT_SUPPORTED`. Legacy
directed advertising is supported with explicit target selection and compliant
high-duty scheduling. Service Changed tracking persists a structural database
fingerprint and pending handle range per bond, retries on encrypted
reconnects, and clears the range only after ATT confirmation. Channel Sounding
is separate from the normal connected BLE stack and remains experimental, as
documented below.

---

## Power Consumption

The core has board-specific low-power behavior rather than treating sleep as a
CPU-only operation. On XIAO nRF54L15 it controls the external RF switch around
radio activity. On XIAO nRF54LM20A it coordinates the nPM1300, oscillator state,
RAM retention, and the onboard QSPI flash's deep-power-down mode.

### Community PPK2 Measurements

These are **measured board-level snapshots**, not datasheet limits or guaranteed
production figures. They depend on the core version, sketch, radio settings,
power path, board revision, instrument wiring, temperature, attached probes,
and peripherals. USB/debug connections can dominate microamp measurements.

#### Delay and System OFF, side by side

[![PPK2 current traces comparing XIAO nRF54LM20A and XIAO nRF54L15 during returning delay and no-retention System OFF](https://github.com/user-attachments/assets/b76a3790-2665-4bff-b382-a28630701ab9)](https://github.com/lolren/nrf54-arduino-core/discussions/76#discussioncomment-17293706)

*Community PPK2 comparison posted 14 June 2026: the LM20A trace (left)
labels returning `delay()` at about 7 uA and no-retention System OFF at about
3 uA; the L15 trace (right) labels the same paths at about 4 uA and 2.5 uA.
This is a point-in-time comparison, not a guaranteed `1.0.0` current limit.
[Measurement and image by @msfujino in Discussion #76](https://github.com/lolren/nrf54-arduino-core/discussions/76#discussioncomment-17293706).*

#### XIAO nRF54L15

| Test snapshot | Measured result | Source |
|---|---:|---|
| Returning `delay()` / System ON sleep | **about 4 uA** | [Discussion #76 comparison](https://github.com/lolren/nrf54-arduino-core/discussions/76#discussioncomment-17293706) |
| `delaySystemOffNoRetention()` | **about 2.5 uA** | [Discussion #76 comparison](https://github.com/lolren/nrf54-arduino-core/discussions/76#discussioncomment-17293706) |

The post does not identify the exact core version used for this comparison.

#### XIAO nRF54LM20A

The latest posted trace used core `v0.9.222` after the LFXO, external-flash DPD,
nPM1300 cleanup, and System OFF fixes. It repeated the `v0.9.219` result and
reported:

| Test snapshot | Measured result | Source |
|---|---:|---|
| Returning `delay()` / System ON sleep | **8.7 uA** | [Discussion #94 v0.9.222 trace](https://github.com/lolren/nrf54-arduino-core/discussions/94#discussioncomment-17608738) |
| `delaySystemOffNoRetention()` | **3.1 uA** | [Discussion #94 v0.9.222 trace](https://github.com/lolren/nrf54-arduino-core/discussions/94#discussioncomment-17608738) |
| nPM1300 hibernate in the earlier `v0.9.59` test | **0.5 uA** | [Discussion #94 hibernate trace](https://github.com/lolren/nrf54-arduino-core/discussions/94) |

[![PPK2 trace of XIAO nRF54LM20A v0.9.222 returning delay at 8.7 uA and no-retention System OFF at 3.1 uA](https://github.com/user-attachments/assets/f66e453b-03ba-4de2-87d8-1384dbe4b260)](https://github.com/lolren/nrf54-arduino-core/discussions/94#discussioncomment-17608738)

*Latest posted LM20A core trace, measured with `v0.9.222`: returning
`delay()` is labelled 8.7 uA and `delaySystemOffNoRetention()` is labelled
3.1 uA. The graph's 2.75 uA board-component total is an estimate rather than
a separately measured rail. [Discussion #94 source](https://github.com/lolren/nrf54-arduino-core/discussions/94#discussioncomment-17608738).*

[![PPK2 trace of XIAO nRF54LM20A v0.9.59 entering nPM1300 hibernate at 0.5 uA and waking](https://github.com/user-attachments/assets/2966f283-3c47-486f-b934-4e5a0bb681a3)](https://github.com/lolren/nrf54-arduino-core/discussions/94)

*Earlier LM20A `v0.9.59` hibernate capture: the test sequence labels
returning `delay(2000)` at 7.2 uA and nPM1300 hibernate at 0.5 uA before the
board wakes. It documents that test, not a guarantee for every board and
measurement setup. Both LM20A images and measurements are by
[@msfujino in Discussion #94](https://github.com/lolren/nrf54-arduino-core/discussions/94).*

The `v0.9.222` graph estimates the always-present LM20A board component floor at
about **2.75 uA** before measurement and environmental effects. Hibernate is a
PMIC-controlled cold-boot path; it is not interchangeable with a returning
`delay()`.

### Selecting A Sleep Path

| Requirement | API / pattern | Wake behavior |
|---|---|---|
| Continue after a timed idle | `delayLowPowerIdle(ms)` or normal low-power `delay(ms)` | Returns to the next statement |
| Lowest CPU System OFF with retained RAM banks | `delaySystemOff(ms)` | Cold reset; explicitly retained `.noinit` state may survive |
| System OFF without RAM retention | `delaySystemOffNoRetention(ms)` | Cold reset |
| Lowest timed LM20A board sleep | `npm1300_enter_timed_hibernate_ms(ms)` | PMIC restores power and the board cold-boots |

For reproducible measurements, power from VBAT through a PPK2/Joulescope/Otii,
disconnect USB, close serial/debug sessions, keep voltage and temperature
constant, wait for the board to settle, and record both average and peak current
over multiple runs. The full repeatable workflow and blank measurement matrix
are in [Power Profile Measurements](POWER_PROFILE_MEASUREMENTS.md).

---

## SPI Speed And Routing

| Board family | External Arduino `SPI` pins | Implemented max on exposed pins | `SPI_HS` / 32 MHz status |
|---|---|---|---|
| **XIAO nRF54L15 / Sense** | `D8=SCK`, `D9=MISO`, `D10=MOSI`, `D2=SS` on the dedicated P2 `SPIM00` route | Up to 32 MHz from SPIM00's fixed 128 MHz source | `SPI_HS` uses the same physical pins and does not change the CPU clock profile |
| **HOLYIOT-25007 / 25008 / nRF54L15 module boards** | Board/module `D8/D9/D10/D2` route on dedicated P2 `SPIM00` | Up to 32 MHz from SPIM00's fixed 128 MHz source | `SPI_HS` uses the same physical P2.01/P2.04/P2.02 route without changing the CPU clock profile |
| **XIAO nRF54LM20A / Sense** | `D8=SCK`, `D9=MISO`, `D10=MOSI`, `D2=SS` on a serial-fabric SPIM | 8 MHz on the exposed XIAO header pins | `SPI_HS` uses `SPIM00`, but those pins are the onboard PY25Q64 QSPI flash bus, not the XIAO header |

Notes:

- On **nRF54L15**, the exposed Arduino SPI pins can only use `SPIM00`. `SPI` and `SPI_HS` are separate logical objects that retain separate settings, but share that one physical controller and must be used sequentially.
- On **nRF54L15**, SPIM00 has a fixed 128 MHz peripheral source, so `SPI_HS` can request 32 MHz without changing the CPU clock profile.
- L15 SPIM00 cannot clock below about 1.016 MHz; lower requests clamp to that documented minimum rather than using nonexistent divider values.
- On **LM20A**, external BMP388/SD/MCP2515-style devices should use normal `SPI` on `D8/D9/D10`; that path is working but is limited to 8 MHz by the board/peripheral route.
- On **L15**, `SPI_HS` is plain 1-bit SPI on the P2 high-speed route, not Quad SPI.
- On **XIAO L15**, `SPI_HS` defaults to `D2` for software-controlled chip select. P2.05 remains reserved for the RF switch.
- On **LM20A**, the 32 MHz `SPI_HS` path is useful for the onboard QSPI flash and deliberate advanced probing of the flash pads. The schematic does not expose that HS bus on the normal XIAO header.
- The L15 implementation follows the documented P2 high-speed pad requirements, including E0/E1 output drive, maximum `HSBIAS` slew above 8 MHz, and the nRF54L SPIM anomaly 8 workaround.
- Examples: `File > Examples > SPI > HighSpeedSpi32MHzProbe` and `File > Examples > nRF54 Board Examples > XIAO-nRF54LM20A`, where the onboard-flash sketches are grouped with the board that provides the hardware.

## XIAO nRF54LM20A Onboard QSPI Flash

XIAO nRF54LM20A includes an onboard PY25Q64-class 8 MB flash on the dedicated QSPI/HS-SPI pads. Nordic DK-style MX25R6435F QSPI flash is also recognized by the bundled SPIFlash compatibility layer. The core exposes this in two layers:

- `XiaoQspiFlash` for board-specific low-level control, including JEDEC read, read/write/erase, and `prepareForSleep()`.
- `Adafruit_SPIFlash` compatibility with `Adafruit_FlashTransport_QSPI_NRF54`, so sketches can use common `begin()`, `readBuffer()`, `writeBuffer()`, `eraseSector()`, `readJEDECID()`, and `runCommand(0xB9)` style calls.

For low-current sleep on LM20A, put the external flash into deep power-down before sleeping. Use `XiaoQspiFlash.prepareForSleep()` or `flash.runCommand(0xB9); flash.end();` from the SPIFlash-compatible API.

Examples:

- `File > Examples > nRF54 Board Examples > XIAO-nRF54LM20A > QspiFlashInfo`
- `File > Examples > nRF54 Board Examples > XIAO-nRF54LM20A > QspiFlashReadWrite`
- `File > Examples > nRF54 Board Examples > XIAO-nRF54LM20A > FlashInfo`
- `File > Examples > Bluefruit52Lib > Diagnostics > lm20a_spiflash_sleep_adv`

---

## Timed System Off APIs

- `delayLowPowerIdle(ms)` is the returning System ON sleep API; the sketch continues at the next statement.
- `delaySystemOff(ms)` enters real timed `SYSTEMOFF` with RAM retention enabled. Wake is a cold reset, so only explicitly retained data such as `.noinit` can survive and execution starts again from `setup()`.
- `delaySystemOffNoRetention(ms)` enters real timed `SYSTEMOFF` after disabling RAM retention. It also cold-resets and never returns.
- `systemOffWakeReset(ms)` is the compatibility spelling for timed no-retention System OFF.
- `nrf54ResetReason()`, `nrf54ClearResetReason(mask)`, `wasSystemOffWakeReset()`, `wasSystemOffWakeFromGrtc()`, and `clearSystemOffWakeResetReason()` expose the reset-cause snapshot captured before constructors. See `File > Examples > Power > SystemOffWakeReset`.

## XIAO nRF54LM20A nPM1300 Charging Notes

- `npm1300_charger_set_current(ma)` sets the battery charge-current target and now also updates the nPM1300 VBUS input-current limiter so the default 100 mA input limit does not throttle higher charge currents.
- `npm1300_vbus_set_input_current_limit_ma(ma)` and `npm1300_vbus_get_input_current_limit_ma()` expose the VBUS limiter directly. This limit is the allowed USB/VBUS input draw, not the measured battery charge or discharge current.
- `npm1300_enter_timed_hibernate_ms(ms)` programs the nPM1300 hibernate wake timer and enters PMIC hibernate mode. On XIAO nRF54LM20A this is the lowest-current timed sleep path; wake is a cold boot after the PMIC restores power. Measure from the battery/VBAT pads because USB/debug wiring can dominate the current.
- Use `File > Examples > Nrf54L15-Clean-Implementation > PMIC > nPM1300_ChargerControl` or `File > Examples > Power > nPM1300_BatteryCurrent` to compare `IBAT` with the configured `VBUS_ILIM`.

---

## Stack Maturity

| Stack | Lines | Maturity | Production Ready? |
|---|---|---|---|
| **Arduino Core** | ~150K | ✅ Mature | Yes — GPIO, PWM, ADC, I2C, SPI, UART, I2S, PDM, NFC |
| **BLE** | ~80K | ✅ Validated scope | Yes within the documented single-link scope: advertising, scanning, connections, GATT, Bluefruit, LE SC security, and privacy/RPA |
| **Zigbee** | ~40K | ⚠️ Experimental / unfinished | No — HA/Zigbee2MQTT device demos and selected ZDO/ZCL paths work, but the implementation is incomplete and has no OTA |
| **Thread** | ~30K | ⚠️ Experimental / unfinished | No — staged OpenThread FTD/MeshCoP/SRP/UDP examples compile and have selected two-board validation paths |
| **Matter** | ~25K | ⚠️ Experimental / unfinished | No — custom on-network On/Off/PASE/CASE demos compile, but HA/OTBR commissioning and complete Matter behavior are not validated |
| **Channel Sounding** | Nordic controller + Arduino glue | ⚠️ Experimental / unfinished | No — hardware-validated two-board LE CS Test only; no connected-ACL, cross-vendor, calibrated-ranging, or qualification claim |
| **PMIC Driver** | ~3K | ✅ Mature | Yes for the documented nPM1300 charger, rail, telemetry, low-power, and hibernate APIs |

---

## Known Limitations

- **ECC secp256r1 is software‑only.** The CRACEN PK engine needs proprietary Nordic microcode. Thread/Matter pairing takes 2‑5 seconds of CPU‑bound crypto.
- **Thread and Matter are staged protocol stacks.** OpenThread FTD/MeshCoP/SRP/UDP and custom Matter command-surface demos compile on all staged boards; local two-board SRP readiness is working, but production-grade HA/OTBR commissioning and long soak validation are still pending.
- **Zigbee is functional but incomplete** — many ZCL clusters, OTA, and automatic route maintenance / production multi‑hop routing are still missing. ZDO neighbor/routing management responses are available and can expose sketch-configured table entries. A Zigbee2MQTT external converter for the bundled CleanCore HA examples is in `extras/zigbee2mqtt/`.
- **LM20A has two SPI paths:** `SPI` stays on the XIAO header pins; `SPI_HS` is the onboard QSPI flash bus and is only for deliberate HS-SPI/QSPI-pad use.
- **P2 GPIO port has no interrupt/wake capability** (hardware limitation).
- **Channel Sounding has one supported experimental path:** the
  `BleChannelSoundingInitiator` / `BleChannelSoundingReflector` pair. Nordic
  SDC/MPSL executes LE CS Test and emits HCI CS result events. A separate
  CRC-protected Arduino protocol establishes a per-cycle session token before
  the test and returns a session-correlated reflector step buffer after SDC
  releases RADIO. That transport is not a connected ACL Channel Sounding
  procedure. Bluetooth SIG
  qualification, cross-vendor interoperability, calibrated accuracy, and
  production power figures are not claimed.

### Channel Sounding Setup

Select **Tools -> CPU Frequency -> 128 MHz** before building either Channel
Sounding example. The equivalent Arduino CLI option is `cpu_freq=128m`; the
runtime fails explicitly instead of starting SDC/MPSL with the default 64 MHz
profile.

The only public Channel Sounding examples are:

- `BleChannelSoundingInitiator`
- `BleChannelSoundingReflector`

The supported profile is single-antenna Mode 2/Submode 1 with AA-only RTT.
The test uses a proprietary CRC-protected session/result exchange before and
after the controller-owned sounding operation. It is intentionally separate
from the core's normal connected BLE stack.

The controller binaries are Nordic components, revision
`7a07f89ee8c32658ebfd2034b4cae92fde63e122`
(`v3.4.0-rc1-12-g7a07f89ee`). They are covered by the included
[Nordic 5-Clause license](hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/third_party/nordic_sdc/LICENSE),
not the core's MIT license. The license restricts use to Nordic Semiconductor
integrated circuits and prohibits reverse engineering, decompiling, modifying,
or disassembling the binary software. See the
[attribution notice](hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/third_party/nordic_sdc/LICENSE-ATTRIBUTION.txt)
and [version record](hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/third_party/nordic_sdc/VERSION).

---

## PWM (`analogWrite`)

### Pin Allocation (LM20A)

| Instance | Channels | Pins |
|---|---|---|
| **PWM20** | 0–3 | D0, D1, D2, D3 |
| **PWM21** | 0–3 | D4, D5, D6, D7 |
| **PWM22** | 0–3 | D8, LED_R (28), LED_B (29), LED_G (30) |
| Software | — | D9–D15 |

D0-D8 and all three onboard RGB LEDs use dedicated PWM-peripheral channels by
default. D9-D15 use the software fallback.

### Frequency Control

- **`analogWriteFrequency(hz)`** — Sets the shared base frequency for all
  hardware PWM instances (1 kHz default).
- **`analogWritePinFrequency(pin, hz)`** — Per-pin frequency selection. D0-D5
  can use the dedicated timer + DPPI + GPIOTE path. Five timer slots provide up
  to five distinct hardware-timed frequencies; pins using the same frequency
  can share a slot. Later pins use the software fallback.
- Pins without a custom frequency inherit their instance's shared frequency.

### Examples

```cpp
analogWriteResolution(8);

// Same frequency on shared instance (PWM20):
analogWrite(D0, 128);   // 50% duty, 1 kHz
analogWrite(D1, 64);    // 25% duty, 1 kHz

// Different frequencies per pin (timer-backed):
analogWritePinFrequency(D0, 500);   // 500 Hz
analogWritePinFrequency(D1, 2000);  // 2 kHz
analogWrite(D0, 128);
analogWrite(D1, 128);
```

### Limitations

- Pins sharing the same PWM instance run at the same base frequency unless `analogWritePinFrequency()` is used.
- Per-pin timer-backed PWM is limited to D0-D5 and five timer slots. D6 and
  later use the software path for a custom per-pin frequency.
- **D9-D15** use the CPU-driven software PWM fallback.
- D8-D10 share pins with SPI (P1.04-P1.06). When SPI is active, those pin
  functions are unavailable for PWM output.

## Troubleshooting

### Linux: Upload fails with "hidraw access denied"

```text
ERROR: CMSIS-DAP probe 2886:0068 is present but hidraw access is denied.
```

**Fix:** Copy the udev rules and replug the board:

```bash
sudo cp ~/.arduino15/packages/nrf54l15clean/tools/nrf54l15hosttools/*/setup/60-seeed-xiao-nrf54-cmsis-dap.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
# Unplug and replug the board
```

> This is a one-time setup. The udev rule grants plugdev group access to `/dev/hidraw*` and the USB device node. Covers both L15 (0066) and LM20A (0068).

### Linux Mint: No probe detected even after udev rules

If `pyocd list` shows no probes but `lsusb` shows the board, check:

```bash
ls -la /dev/bus/usb/*/???   # USB device should be rw for plugdev
ls -la /dev/hidraw*          # hidraw should be rw for plugdev
groups                       # verify you're in plugdev group
```

If USB device is root-only, the udev rule didn't trigger. Replug the board or run `sudo udevadm trigger`.

---

## Documentation

- **[Board Reference & Pinouts](docs/board-reference.md)**
- **[Development Guide](docs/development.md)**
- **[BLE Status & Resume Checklist](docs/BLE_COMPLIANCE_RESUME.md)**
- **[Two-Board Release Gate](docs/TWO_BOARD_RELEASE_GATE.md)**
- **[Zigbee2MQTT Integration](docs/ZIGBEE2MQTT_INTEGRATION.md)**
- **[Zigbee Full-Support Handoff](docs/ZIGBEE_FULL_SUPPORT_HANDOFF.md)**
- **[Channel Sounding Status](docs/CHANNEL_SOUNDING_CURRENT_STATUS.md)**
- **[Thread & Matter Implementation Plan](docs/archive/THREAD_MATTER_IMPLEMENTATION_PLAN.md)**
- **[Thread & Matter Hardening Status](docs/THREAD_MATTER_FINISH_PLAN.md)**
- **[Power Profile Measurements](POWER_PROFILE_MEASUREMENTS.md)**

## License

Project-owned contributions are available under the [MIT License](LICENSE).
Imported and adapted components retain their own terms; the
[third-party notice](THIRD_PARTY_NOTICES.md) identifies the principal bundled
licenses. The Board Manager archive includes the same project license, detailed
platform notice, and component license texts so installed copies remain
self-describing.

## Special Thanks

This core would not have been possible without the invaluable help of:

- **[msfujino](https://github.com/msfujino)** — Extensive testing, debugging, and feedback across PWM, analogWrite, and countless other areas. Every oscilloscope trace, every edge case report, every patient retest helped shape this core into what it is today.
- **[lyusupov](https://github.com/lyusupov)** — Deep technical contributions, critical bug fixes, and ongoing collaboration that pushed this project forward.

Thank you both for your time, expertise, and dedication. ❤️

## Adafruit Attribution

[![Adafruit](https://img.shields.io/badge/Adafruit-Open%20Source%20Examples-000000?logo=adafruit&logoColor=white)](https://www.adafruit.com/)

Some bundled Bluefruit52Lib examples, HID sketches, TinyUSB compatibility code, and SPIFlash-compatible APIs preserve Adafruit open-source example text, naming, and API compatibility. The Adafruit name and logo are shown here to acknowledge that origin and to make clear why Adafruit attribution appears in redistributed example sketches.

---

<div align="center">

**Bare-metal Arduino for nRF54L, with clearly identified and separately licensed
third-party components.**

</div>

## Support The Project

If this core saves you development time, please consider supporting its ongoing
maintenance, hardware testing, and release work.

<a href="https://buymeacoffee.com/lolren"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me a Coffee" height="50"></a>
