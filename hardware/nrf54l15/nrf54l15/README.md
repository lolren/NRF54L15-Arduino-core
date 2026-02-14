# XIAO nRF54L15 Bare-Metal Arduino Core

A clean Arduino core port for the Seeed Studio XIAO nRF54L15 board with a lightweight, publishable source tree.

## Overview

This project provides an Arduino-compatible core for nRF54L15 with direct Arduino APIs and a Zephyr/NCS-backed build pipeline. The goal is to keep this repository small and publishable by **not bundling** heavy SDK/toolchain payloads in source control or release archives.

## Key Features

- **Bare-metal implementation** - Direct register access to nRF54L15 peripherals
- **No bundled SDK/toolchain payloads** - dependencies are bootstrapped by scripts when required
- **Open-source licensed** - Apache 2.0 license
- **Arduino API compatible** - Standard Arduino functions work out of the box

## Current Status

### ✅ Implemented

- [x] Project structure and build system
- [x] GPIO and digital I/O (pinMode, digitalWrite, digitalRead)
- [x] GPIO interrupts (attachInterrupt/detachInterrupt)
- [x] Timing functions (millis, micros, delay, delayMicroseconds) using SysTick
- [x] UART/Serial driver (Serial, Serial1, `Serial.begin(baud, config)` framing)
- [x] I2C controller + target mode (Wire) including repeated-start register reads and `onReceive`/`onRequest`
- [x] SPI master (SPI) including transfer16, buffer transfer, transaction API, and `SPI.begin(csPin)`
- [x] Analog input (SAADC)
- [x] PWM output (analogWrite)
- [x] Upload helper with auto runner (pyOCD/OpenOCD fallback)
- [x] Arduino Tools menu options for upload method, CPU frequency, antenna, and radio profile
- [x] Runtime antenna control helper library (`XiaoNrf54L15`)
- [x] Low-power helper APIs (`sleepMs`, `sleepUs`, `systemOff`)
- [x] CPU frequency helpers (`cpuFrequencyHz`, `cpuFrequencyFromToolsHz`, `setCpuFrequencyHz`)
- [x] Watchdog helper APIs (`watchdogStart`, `watchdogFeed`, reset-cause helpers)
- [x] Watchdog compatibility library (`Watchdog.begin`, `Watchdog.feed`)
- [x] Bluetooth scan callback API (`BLE.scanForEach`) for multi-advertiser monitoring
- [x] BLE central APIs (`connect`, `disconnect`, `connectLastScanResult`)
- [x] IEEE 802.15.4 library (`IEEE802154`) with config + passive scan support
- [x] Core Arduino API (Print, Stream, String, etc.)
- [x] Math and random functions
- [x] Pin mapping for XIAO nRF54L15

### 🚧 In Progress / TODO

- [ ] Extended BLE6 feature-set validation beyond channel-sounding probe (distance accuracy/performance characterization)

## Hardware

**Board**: Seeed Studio XIAO nRF54L15
**MCU**: nRF54L15 (ARM Cortex-M33, 64 MHz, 512 KB Flash, 128 KB RAM)

### Pin Mapping

| Arduino Pin | Port Pin | Function               |
|-------------|----------|------------------------|
| D0-D5       | P1.04-P1.11 | GPIO                 |
| D6-D15      | P2/P0 pins | GPIO, PWM, Serial1   |
| D16         | P2.00    | LED_BUILTIN (active-low) |
| D17         | P1.02    | BUTTON                |
| A0-A7       | SAADC ch 0-7 | Analog Input        |
| D4/D5       | P1.10/P1.11 | I2C (SDA/SCL)       |
| D8/D9/D10   | P2.01/P2.04/P2.02 | SPI (SCK/MISO/MOSI) |
| D6/D7       | P2.08/P2.07 | Serial1 (TX/RX)     |

## Building

### Prerequisites

- ARM GCC toolchain (arm-none-eabi-gcc)
- CMake 3.20 or later
- Make or Ninja

### Build Commands

```bash
# Create build directory
mkdir build && cd build

# Configure with CMake
cmake ..

# Build core library
cmake --build .

# Build a sketch (example)
cmake --build . --target Blink
```

## Installing

To use this core with the Arduino IDE:

1. Copy this folder to your Arduino hardware directory:
   ```
   <Sketchbook>/hardware/nrf54l15/nrf54l15/
   ```

2. Restart the Arduino IDE

3. Select "XIAO nRF54L15 (Bare-Metal)" from the Tools > Board menu

## Flashing

### Using Arduino IDE

Use **Tools > Upload Method > Auto (Recommended)**.
This uses pyOCD by default and can fall back when possible.

### Using pyOCD

```bash
pyocd load -t nrf54l build/Blink.hex --format hex
pyocd reset -t nrf54l
```

### Using OpenOCD (Experimental)

OpenOCD can attach/debug with the provided config, but nRF54L15 flash support is not complete upstream.

## License

Apache License 2.0 - See LICENSE file for details

## Contributing

Contributions are welcome! Please feel free to submit pull requests.

## Acknowledgments

- Based on the nRF54L15 reference documentation from Nordic Semiconductor
- Inspired by various bare-metal Arduino projects
- CMSIS definitions adapted from ARM CMSIS-CORE specification
- Build pipeline integrates Zephyr + NCS components while keeping release archives lightweight
