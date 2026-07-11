# <picture><source media="(prefers-color-scheme: dark)"><img align="right" width="80" src="https://raw.githubusercontent.com/lolren/nrf54-arduino-core/main/docs/xiao_nrf54l15_default_pin_routes.png"></picture> nRF54L Arduino Core

<div align="center">

**Bare-metal Cortex-M33 + RISC-V. No Zephyr runtime or external nRF Connect SDK installation.**

[![Release](https://img.shields.io/github/v/release/lolren/nrf54-arduino-core?color=00d4ff&label=latest)](https://github.com/lolren/nrf54-arduino-core/releases)
[![Boards](https://img.shields.io/badge/boards-5-00d4ff)](#-supported-boards)
[![License](https://img.shields.io/badge/license-MIT%20%2B%20third--party-00d4ff)](hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/third_party/nordic_sdc/LICENSE)

*The most advanced bare-metal Arduino core for Nordic's latest-generation SoC — BLE, 802.15.4, Thread, Matter, Zigbee, and a RISC‑V coprocessor, all without a vendor RTOS.*

</div>

---

## ⚡ Quick Install

```
https://raw.githubusercontent.com/lolren/nrf54-arduino-core/main/package_nrf54l15clean_index.json
```

Add this URL in **Arduino IDE → Preferences → Additional Boards Manager URLs**, then install **nRF54L15 Boards** from the Boards Manager.


Normal uploads use the bundled native [**nRF OCD**](https://github.com/lolren/open-nrf-ocd) tool on Linux and Windows, so Windows does not need a separate Python install just to upload. If native upload fails, switch **Tools -> Upload Method** to **pyOCD Recovery**; that recovery path still uses the packaged Python host tools.

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

## 🖥️ Supported Boards

<div align="center">

| | |
|---|---|
| <img src="docs/xiao_nrf54l15_default_pin_routes.png" width="280"><br>**[XIAO nRF54L15 / Sense](https://wiki.seeedstudio.com/xiao_nrf54l15_sense_getting_started/)**<br>`xiao_nrf54l15` | <img src="docs/nrf54lm20a_front_pinout.png" width="280"><br>**[XIAO nRF54LM20A / Sense](https://wiki.seeedstudio.com/xiao_nrf54lm20a_getting_started/)**<br>`xiao_nrf54lm20b` |
| <img src="docs/boards/holyiot_25007_product.png" width="280"><br>**[HOLYIOT-25007](docs/holyiot-25007-module-reference.md)**<br>`holyiot_25007_nrf54l15` | <img src="docs/boards/holyiot_25008_product.jpg" width="280"><br>**[HOLYIOT-25008](docs/holyiot-25008-module-reference.md)**<br>`holyiot_25008_nrf54l15` |

</div>

| Product | Arduino board selection | Specs / notes |
|---|---|---|
| **[Seeed Studio XIAO nRF54L15](https://wiki.seeedstudio.com/xiao_nrf54l15_sense_getting_started/)** | `XIAO nRF54L15 / Sense` (`xiao_nrf54l15`) | 128 MHz M33 + 128 MHz RISC-V coprocessor · 1.5 MB NVM · 256 KB RAM |
| **[Seeed Studio XIAO nRF54L15 Sense](https://wiki.seeedstudio.com/xiao_nrf54l15_sense_getting_started/)** | `XIAO nRF54L15 / Sense` (`xiao_nrf54l15`) | Same core board support as XIAO nRF54L15 · onboard LSM6DS3TR-C IMU + MSM261DGT006 PDM mic |
| **[Seeed Studio XIAO nRF54LM20A](https://wiki.seeedstudio.com/xiao_nrf54lm20a_getting_started/)** | `XIAO nRF54LM20A` (`xiao_nrf54lm20b`) | 128 MHz M33 + 128 MHz RISC-V coprocessor · 2 MB NVM · 512 KB RAM · nPM1300 PMIC · onboard 8 MB PY25Q64 QSPI flash · [back pinout](docs/nrf54lm20a_back_pinout.png) |
| **[Seeed Studio XIAO nRF54LM20A Sense](https://wiki.seeedstudio.com/xiao_nrf54lm20a_getting_started/)** | `XIAO nRF54LM20A` (`xiao_nrf54lm20b`) | Same core board support as XIAO nRF54LM20A · onboard LSM6DS3TR-C IMU + MSM261DGT006 PDM mic · nPM1300 PMIC · onboard 8 MB PY25Q64 QSPI flash · [back pinout](docs/nrf54lm20a_back_pinout.png) |
| **[HOLYIOT-25007](docs/holyiot-25007-module-reference.md)** | `HOLYIOT-25007 nRF54L15 Module` (`holyiot_25007_nrf54l15`) | 18.0 x 14.8 mm · PCB antenna |
| **[HOLYIOT-25008](docs/holyiot-25008-module-reference.md)** | `HOLYIOT-25008 nRF54L15 Module` (`holyiot_25008_nrf54l15`) | 23.2 x 17.5 mm · PCB antenna |

> See [board reference](docs/board-reference.md) for detailed pin assignments and schematics.

---

## ⚡ Why Bare Metal?

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

## ✨ Feature Matrix

### 📡 Wireless

| | BLE | 802.15.4 | Thread | Zigbee | Matter | CS |
|---|---|---|---|---|---|---|
| **Advertising** | ✅ | — | — | — | — | — |
| **Scanning** | ✅ | — | — | — | — | — |
| **1M / 2M / Coded PHY** | ✅ | — | — | — | — | — |
| **GATT Server + Client** | ✅ | — | — | — | — | — |
| **Bluefruit API** | ✅ | — | — | — | — | — |
| **LE Secure Connections** | ✅ | — | — | — | — | — |
| **OOB + Numeric Comparison** | ⚠️ | — | — | — | — | — |
| **Privacy / RPA** | ⚠️ | — | — | — | — | — |
| **Channel Sounding Mode 2** | ⚠️ | — | — | — | — | ⚠️ |
| **MAC / NWK / APS** | — | ✅ | ⚠️ | ⚠️ | — | — |
| **Coordinator / Router** | — | — | ⚠️ | ⚠️ | — | — |
| **End Device** | — | — | ⚠️ | ⚠️ | — | — |
| **UDP Transport** | — | — | ✅ | — | ⚠️ | — |
| **ZCL (OnOff / Level / Temp)** | — | — | — | ⚠️ | — | — |
| **On/Off Light + commissioning** | — | — | — | — | ⚠️ | — |

### 🔐 Crypto

| | Hardware | Status |
|---|---|---|
| **CRACEN RNG** | ✅ | Production |
| **CRACEN IKG** | ✅ | 0 ms key generation |
| **AES‑CCM / AES‑ECB** | ✅ | Hardware‑accelerated |
| **PBKDF2‑HMAC‑SHA256** | ✅ | Hardware‑accelerated |
| **ECDSA sign** | ✅ | ~0.84 s |
| **ECDSA verify** | ✅ | ~1.76 s |
| **secp256r1 ECC** | ⚠️ | Software‑only (CRACEN PK engine needs Nordic microcode) |

### 🎛️ Peripherals

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

### 🧠 System

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
- Examples: `File > Examples > SPI > HighSpeedSpi32MHzProbe`, `File > Examples > XiaoLM20A > QspiFlashInfo`, and `File > Examples > Adafruit SPIFlash > FlashInfo`.

## XIAO nRF54LM20A Onboard QSPI Flash

XIAO nRF54LM20A includes an onboard PY25Q64-class 8 MB flash on the dedicated QSPI/HS-SPI pads. Nordic DK-style MX25R6435F QSPI flash is also recognized by the bundled SPIFlash compatibility layer. The core exposes this in two layers:

- `XiaoQspiFlash` for board-specific low-level control, including JEDEC read, read/write/erase, and `prepareForSleep()`.
- `Adafruit_SPIFlash` compatibility with `Adafruit_FlashTransport_QSPI_NRF54`, so sketches can use common `begin()`, `readBuffer()`, `writeBuffer()`, `eraseSector()`, `readJEDECID()`, and `runCommand(0xB9)` style calls.

For low-current sleep on LM20A, put the external flash into deep power-down before sleeping. Use `XiaoQspiFlash.prepareForSleep()` or `flash.runCommand(0xB9); flash.end();` from the SPIFlash-compatible API.

Examples:

- `File > Examples > XiaoLM20A > QspiFlashInfo`
- `File > Examples > XiaoLM20A > QspiFlashReadWrite`
- `File > Examples > Adafruit SPIFlash > FlashInfo`
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

## 📊 Stack Maturity

| Stack | Lines | Maturity | Production Ready? |
|---|---|---|---|
| **Arduino Core** | ~150K | ✅ Mature | Yes — GPIO, PWM, ADC, I2C, SPI, UART, I2S, PDM, NFC |
| **BLE** | ~80K | ✅ Mature | Yes — advertising, scanning, connections, GATT, Bluefruit |
| **Zigbee** | ~40K | ⚠️ Good | Partial — HA/Zigbee2MQTT device demos, ZDO descriptors/binding/sketch-configurable management tables, no OTA |
| **Thread** | ~30K | ⚠️ Staged | Partial — OpenThread FTD/MeshCoP/SRP/UDP examples compile and have two-board validation paths |
| **Matter** | ~25K | ⚠️ Staged | Partial — custom on-network On/Off/PASE/CASE demos compile; local two-board SRP readiness works, HA/OTBR commissioning still needs full validation |
| **Channel Sounding** | Nordic controller + Arduino glue | ⚠️ Experimental | Two-board LE CS Test through bundled Nordic SDC/MPSL; no Bluetooth qualification or connected-ACL interoperability claim |
| **PMIC Driver** | ~3K | ✅ Mature | Yes — all nPM1300 features, GPIO bit‑bang I²C |

---

## ⚠️ Known Limitations

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

## 🔗 Links

- **[Board Reference & Pinouts](docs/board-reference.md)**
- **[Development Guide](docs/development.md)**
- **[BLE Status & Resume Checklist](docs/BLE_COMPLIANCE_RESUME.md)**
- **[Zigbee2MQTT Integration](docs/ZIGBEE2MQTT_INTEGRATION.md)**
- **[Zigbee Full-Support Handoff](docs/ZIGBEE_FULL_SUPPORT_HANDOFF.md)**
- **[Channel Sounding Status](docs/CHANNEL_SOUNDING_CURRENT_STATUS.md)**
- **[Thread & Matter Implementation Plan](docs/THREAD_MATTER_IMPLEMENTATION_PLAN.md)**
- **[Thread & Matter Hardening Status](docs/THREAD_MATTER_FINISH_PLAN.md)**
- **[Power Profile Measurements](POWER_PROFILE_MEASUREMENTS.md)**

---

<div align="center">

**Bare-metal Arduino core with clearly identified, separately licensed third-party components.**

---

## ⚡ PWM (analogWrite)

### Pin Allocation (LM20A)

| Instance | Channels | Pins |
|---|---|---|
| **PWM20** | 0–3 | D0, D1, D2, D3 |
| **PWM21** | 0–3 | D4, D5, D6, D7 |
| **PWM22** | 0–3 | D8, LED_R (28), LED_B (29), LED_G (30) |
| Software | — | D9–D15 |

All 10 digital pins (D0–D9) and all 3 onboard RGB LEDs have hardware PWM support.

### Frequency Control

- **`analogWriteFrequency(hz)`** — Sets the shared base frequency for all hardware PWM instances (~980 Hz default).
- **`analogWritePinFrequency(pin, hz)`** — Per-pin independent frequency using a dedicated timer + DPPI + GPIOTE path. Supports up to **6 pins** simultaneously.
- Pins without a custom frequency inherit their instance's shared frequency.

### Examples

```cpp
analogWriteResolution(8);

// Same frequency on shared instance (PWM20):
analogWrite(D0, 128);   // 50% duty, ~980 Hz
analogWrite(D1, 64);    // 25% duty, ~980 Hz

// Different frequencies per pin (timer-backed):
analogWritePinFrequency(D0, 500);   // 500 Hz
analogWritePinFrequency(D1, 2000);  // 2 kHz
analogWrite(D0, 128);
analogWrite(D1, 128);
```

### Limitations

- Pins sharing the same PWM instance run at the same base frequency unless `analogWritePinFrequency()` is used.
- Per-pin timer PWM is limited to **6 pins** (hardware TIMER + DPPI slot count).
- **D9** uses software PWM fallback (CPU-driven). Use `analogWritePinFrequency(D9, 1000)` for timer-backed PWM.
- D6–D8 share pins with SPI (P1.04–P1.06). When SPI is active, those PWM channels are unavailable.

## 🔧 Troubleshooting

### Linux: Upload fails with "hidraw access denied"

```
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

</div>

## 🙏 Special Thanks

This core would not have been possible without the invaluable help of:

- **[msfujino](https://github.com/msfujino)** — Extensive testing, debugging, and feedback across PWM, analogWrite, and countless other areas. Every oscilloscope trace, every edge case report, every patient retest helped shape this core into what it is today.
- **[lyusupov](https://github.com/lyusupov)** — Deep technical contributions, critical bug fixes, and ongoing collaboration that pushed this project forward.

Thank you both for your time, expertise, and dedication. ❤️

## Adafruit Attribution

[![Adafruit](https://img.shields.io/badge/Adafruit-Open%20Source%20Examples-000000?logo=adafruit&logoColor=white)](https://www.adafruit.com/)

Some bundled Bluefruit52Lib examples, HID sketches, TinyUSB compatibility code, and SPIFlash-compatible APIs preserve Adafruit open-source example text, naming, and API compatibility. The Adafruit name and logo are shown here to acknowledge that origin and to make clear why Adafruit attribution appears in redistributed example sketches.
