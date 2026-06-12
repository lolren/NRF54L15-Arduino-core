# <picture><source media="(prefers-color-scheme: dark)"><img align="right" width="80" src="https://raw.githubusercontent.com/lolren/nrf54-arduino-core/main/docs/xiao_nrf54l15_default_pin_routes.png"></picture> nRF54L Arduino Core

<div align="center">

**Bare-metal Cortex-M33 + RISC-V. No Zephyr. No nRF Connect SDK.**

[![Release](https://img.shields.io/github/v/release/lolren/nrf54-arduino-core?color=00d4ff&label=latest)](https://github.com/lolren/nrf54-arduino-core/releases)
[![Boards](https://img.shields.io/badge/boards-5-00d4ff)](#-supported-boards)
[![License](https://img.shields.io/badge/license-MIT-00d4ff)](#)

*The most advanced bare-metal Arduino core for Nordic's latest-generation SoC — BLE, 802.15.4, Thread, Matter, Zigbee, and a RISC‑V coprocessor, all without a vendor RTOS.*

</div>

---

## ⚡ Quick Install

```
https://raw.githubusercontent.com/lolren/nrf54-arduino-core/main/package_nrf54l15clean_index.json
```

Add this URL in **Arduino IDE → Preferences → Additional Boards Manager URLs**, then install **nRF54L15 Boards** from the Boards Manager.

```cpp
#include <nrf54_all.h>
void setup() { Serial.begin(115200); Serial.println("Hello nRF54!"); }
void loop() {}
```

---

## 🖥️ Supported Boards

<div align="center">

| | | |
|---|---|---|
| <img src="docs/xiao_nrf54l15_default_pin_routes.png" width="180"><br>**XIAO nRF54L15**<br>`xiao_nrf54l15`<br><sub>128 MHz M33 · 1.5 MB NVM</sub> | <a href="docs/nrf54lm20a_front_pinout.png"><img src="docs/nrf54lm20a_front_pinout.png" width="180"></a><br>**XIAO nRF54LM20A / Sense**<br>`xiao_nrf54lm20b`<br><sub>128 MHz M33 · 2 MB NVM · nPM1300</sub><br><sub>[front](docs/nrf54lm20a_front_pinout.png) · [back](docs/nrf54lm20a_back_pinout.png)</sub> | <img src="docs/boards/holyiot_25007_product.png" width="180"><br>**HOLYIOT-25007**<br>`holyiot_25007_nrf54l15` |
| <img src="docs/boards/holyiot_25008_product.jpg" width="180"><br>**HOLYIOT-25008**<br>`holyiot_25008_nrf54l15` | <img src="docs/xiao_nrf54l15_default_pin_routes.png" width="180"><br>**Generic 36-Pad**<br>`generic_nrf54l15_module_36pin` | |

</div>

> **LM20A / Sense** — [Official board images & pinout](https://wiki.seeedstudio.com/xiao_nrf54lm20a_getting_started/). Two variants: **standard** (wireless) and **Sense** (LSM6DS3TR-C IMU + MSM261DGT006 mic). nPM1300 PMIC with battery charging, fuel gauge, 0.33 µA ship mode. `xiao_nrf54lm20b` identifier for compatibility.

---

## ⚡ Why Bare Metal?

| | This Core | nRF Connect SDK |
|---|---|---|
| **RTOS** | None (opt-in Thread/Matter) | Zephyr RTOS mandatory |
| **Binary size** | ~12 KB blink | ~100 KB+ blink |
| **Compiler** | GCC | GCC + Zephyr build system |
| **Peripheral access** | Direct register writes | Vendor HAL + DTS |
| **BLE stack** | Custom register‑level | SoftDevice / Zephyr BLE |
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
| **UDP Transport** | — | — | ⚠️ | — | — | — |
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
| **GPIO Bit‑Bang I²C** (zero residual) | ✅ |
| **Buck Hysteretic Mode** (µA sleep) | ✅ |
| **LM20A IMU** (LSM6DS3TR‑C) | ✅ |
| **LM20A PDM Mic** (MSM261DGT006) | ✅ |

---

> **Legend:** ✅ Production &nbsp; ⚠️ Experimental / Partial &nbsp; 🚧 In Development

---

## 📊 Stack Maturity

| Stack | Lines | Maturity | Production Ready? |
|---|---|---|---|
| **Arduino Core** | ~150K | ✅ Mature | Yes — GPIO, PWM, ADC, I2C, SPI, UART, I2S, PDM, NFC |
| **BLE** | ~80K | ✅ Mature | Yes — advertising, scanning, connections, GATT, Bluefruit |
| **Zigbee** | ~40K | ⚠️ Good | Partial — ZCL clusters missing, no OTA |
| **Thread** | ~30K | ⚠️ Early | No — compile‑only, no commissioner validation |
| **Matter** | ~25K | ⚠️ Early | No — data models compile, no network‑layer completion |
| **Channel Sounding** | ~8K | ⚠️ Partial | Mode 2 works on 2‑board, needs VPR coprocessor |
| **PMIC Driver** | ~3K | ✅ Mature | Yes — all nPM1300 features, GPIO bit‑bang I²C |

---

## ⚠️ Known Limitations

- **ECC secp256r1 is software‑only.** The CRACEN PK engine needs proprietary Nordic microcode. Thread/Matter pairing takes 2‑5 seconds of CPU‑bound crypto.
- **Thread and Matter are compile‑targets only** — not functional protocol stacks. End‑to‑end commissioner validation is pending.
- **Zigbee is functional but incomplete** — many ZCL clusters, OTA, and multi‑hop routing are missing.
- **SPI and Wire1 (IMU) share serial‑fabric slot 30** — can't be used simultaneously on Sense boards.
- **P2 GPIO port has no interrupt/wake capability** (hardware limitation).
- **Channel Sounding Mode 2** needs the VPR RISC‑V coprocessor — M33 alone can't keep up with subevent timing.

---

## 🔗 Links

- **[Board Reference & Pinouts](docs/board-reference.md)**
- **[Development Guide](docs/development.md)**
- **[BLE Status & Resume Checklist](docs/BLE_COMPLIANCE_RESUME.md)**
- **[Thread & Matter Implementation Plan](docs/THREAD_MATTER_IMPLEMENTATION_PLAN.md)**
- **[Power Profile Measurements](POWER_PROFILE_MEASUREMENTS.md)**

---

<div align="center">

**Built from datasheets. Verified on hardware. No vendor blobs.**

</div>
