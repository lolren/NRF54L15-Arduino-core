<h1><font color="red">IMPORTANT: first-time installation + first compile can be slow and may look stalled. Let it finish.</font></h1>
<h2><font color="red">Disk usage warning: after first setup/compilation, required disk space is about 9 GB (SDK/toolchain/workspace + build cache).</font></h2>
<h2><font color="red">Windows can be much slower because real-time protection scans large SDK/toolchain files. Recommended: use Linux. On Windows, add this core/tools path as an antivirus exception, or temporarily disable real-time protection only for first setup/compile.</font></h2>
<h2><font color="red">Windows recommendation: use the script install method only (`install_windows.bat`). Sometimes first compilation on Windows fails with `ECONNRESET` due to a bug in the Arduino IDE Windows client (Electron-based). After one successful first compilation, switching back to Arduino IDE usually works.</font></h2>

# Arduino Core for nRF54L15 (Zephyr-Based)

A clean, lightweight Arduino core for Nordic Semiconductor nRF54L15 microcontrollers.
This repository intentionally keeps heavyweight SDK/toolchain content out of source control and release archives.

Build/runtime basis:
- Arduino-compatible core APIs and libraries in this repository.
- Zephyr + NCS components are used by the build pipeline and are bootstrapped when needed.
- Release/source archives remain publishable and lightweight by excluding bundled SDK payloads.

## Features

- ✅ **No bundled SDK payloads** - NCS/Zephyr SDK are bootstrapped at build time when needed
- ✅ **Lean distribution** - Boards Manager archive excludes generated SDK/workspace payloads
- ✅ **Core Arduino API** - Standard digital, timing, serial, Wire, and SPI APIs
- ✅ **Board helper library** - Sleep/system-off, watchdog, reset-cause, antenna, radio-profile, and peripheral power-gating helpers
- ✅ **64MHz Cortex-M33** - Fast ARM core with DSP instructions
- ✅ **512KB Flash, 64KB RAM** - Plenty of memory for your projects
- ✅ **CMSIS-DAP debug** - Works with built-in debugger on XIAO boards
- ✅ **Cross-platform uploader** - `Auto` runner selects `pyOCD` then `OpenOCD`
- ✅ **Board Tools menu** - Upload method, CPU frequency, antenna, radio profile
- ✅ **Expanded example set** - Includes core examples plus legacy Seeed example folders in Arduino IDE

## Supported Boards

### Seeed Studio XIAO nRF54L15
- **18 GPIO pins** with multi-function capabilities
- **Built-in RGB LED** (NeoPixel)
- **Built-in user button**
- **SPI, I2C, UART** peripherals
- **Bluetooth API library** (BLE advertise/scan/peripheral examples included)
- **BLE central API support** (`BLE.connect(...)`, `BLE.disconnect()`, `BLE.connectLastScanResult(...)`)
- **Bluetooth scan filter APIs** (`BLE.setScanFilterName(...)`, `BLE.setScanFilterAddress(...)`) for deterministic range probing
- **Bluetooth scan callback API** (`BLE.scanForEach(...)`) for streaming all advertisers in dense RF environments
- **IEEE 802.15.4 library** (`IEEE802154`) for channel/PAN/address/tx-power config + scan callbacks
- **Low-power helpers + watchdog API** (`XiaoNrf54L15`)
- **CPU frequency + power profile helpers** (`cpuFrequencyHz`, `cpuFrequencyFromToolsHz`, `setCpuFrequencyHz`, `applyPowerProfile`)
- **Watchdog compatibility library** (`Watchdog.begin(...)`, `Watchdog.feed()`)
- **12-bit ADC** with 8 channels
- **4x PWM outputs**

## Installation

### Method 1: Manual Installation (Linux/macOS recommended)

1. Download or clone this repository
2. Copy the `nrf54l15` folder to your Arduino hardware directory:
   - **Linux**: `~/Arduino/hardware/`
   - **macOS**: `~/Documents/Arduino/hardware/`
   - **Windows**: not recommended (use Method 3 script install instead)

The folder structure should look like:
```
Arduino/
└── hardware/
    └── nrf54l15/
        └── nrf54l15/
            ├── boards.txt
            ├── platform.txt
            ├── cores/
            ├── variants/
            ├── libraries/
            └── tools/
```

3. Restart Arduino IDE
4. Select **Tools > Board > XIAO nRF54L15 (Zephyr-Based - NO BLE)**

### Method 2: Arduino IDE (Boards Manager, Linux/macOS recommended)

1. Open **File > Preferences > Additional boards manager URLs**
2. Add this package index URL (copy/paste as one line):
   ```text
   https://raw.githubusercontent.com/lolren/NRF54L15-Arduino-core/main/package_nrf54l15_index.json
   ```
3. Open **Tools > Board > Boards Manager**
4. Search for **Seeed nRF54L15 (Zephyr-Based)** and install
5. Select **Tools > Board > XIAO nRF54L15 (Zephyr-Based - NO BLE)**

Arduino IDE support:
- Linux
- macOS

Windows warning:
- Sometimes first compilation on Windows fails with `ECONNRESET` due to a bug in the Arduino IDE Windows client (Electron-based).
- Workaround: run first setup + first compile with Method 3 (`install_windows.bat`) or with Rewritto IDE (Qt6): `https://github.com/lolren/rewritto-ide`.
- After one successful first compilation, switching back to Arduino IDE usually works.

### Method 3: Setup Scripts (Standalone)

These scripts are independent from Boards Manager installation.
Use them when you want guided host setup plus first-time SDK/bootstrap warmup before IDE compile/upload.

Linux:

```bash
bash ./install_linux.sh
```

Windows (Command Prompt):

```bat
install_windows.bat
```

Root launcher scripts:
- `install_linux.sh` -> runs `tools/install/linux_prereqs.sh`
- `install_windows.bat` -> runs `tools/install/windows_prereqs.bat`

`tools/install/windows_prereqs.bat` has editable variables at the top for:
- Python download/version and installer args
- Git download/version and installer args
- CMake download/version and installer args
- Winget package IDs

Both scripts default behavior:
- Ensures Python, Git, CMake, DTC, and Ninja are installed (auto-installs if missing)
- Copies this core into `<sketchbook>/hardware/nrf54l15/nrf54l15`
- Preserves existing `tools/ncs` and `tools/zephyr-sdk` cache content on reruns
- Bootstraps NCS workspace + Zephyr SDK
- Installs required Zephyr Python build dependencies into `tools/pydeps` automatically
- Runs a one-time Zephyr warmup build so IDE compiles are sketch-focused
- Detects Arduino IDE (if installed)
- Detects sketchbook folder

Windows-only behavior:
- Uses a short default Zephyr build cache path on Windows (`%LOCALAPPDATA%\nrf54l15-build`) to avoid path-length build failures

Linux/Windows script options:
- `--skip-core-copy` to keep using the repo core path without copying
- `--skip-bootstrap` to skip NCS/SDK download and warmup build
- `--help` to print usage

Windows build-path overrides:
- `ARDUINO_ZEPHYR_BUILD_DIR` to set an explicit build directory
- `ARDUINO_NRF54L15_BUILD_ROOT` to change the default short Windows build-root

Troubleshooting if the core does not appear in Boards Manager:
- Ensure the URL above is added as one full line.
- Search `seeed` (3 e's) or `nrf54l15` in Boards Manager.
- If you previously installed this core manually, remove/rename
  `~/Arduino/hardware/nrf54l15` (or move it outside `hardware/`) and refresh Boards Manager.
- If Zephyr build paths are mixed between your local repo and
  `Arduino15/packages/...`, clear stale `ZEPHYR_BASE` and rerun:
  Windows CMD: `set ZEPHYR_BASE=`
  PowerShell: `Remove-Item Env:ZEPHYR_BASE -ErrorAction SilentlyContinue`

To generate release archive + checksum + updated package index before publishing:

```bash
python3 tools/release_boards_manager.py --version 0.1.1 --repo lolren/NRF54L15-Arduino-core
```

To run a local fresh-machine Boards Manager smoke test:

```bash
python3 tools/ci_fresh_machine_smoke.py
```

To verify release reproducibility (deterministic archive + index content):

```bash
python3 tools/check_release_reproducible.py
```

To create a clean sync folder (without local SDK/cache artifacts) and zip it
for manual GitHub upload:

```bash
python3 tools/export_sync_bundle.py --clean --zip
```

Default output:
- Folder: `sync_export/NRF54L15-Arduino-core-sync`
- Zip: `sync_export/NRF54L15-Arduino-core-sync.zip`

## Toolchain Requirements

This core bootstraps required NCS/Zephyr SDK components automatically on first build.
No globally installed `arm-none-eabi-*` toolchain is required.

Host prerequisites:
- Python 3
- Git
- CMake
- Internet access for first-time bootstrap downloads
- Optional manual fallback toolchain page:
  https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
- Optional helper scripts: `tools/install/linux_prereqs.sh`, `tools/install/windows_prereqs.bat`

Bootstrap cache behavior:
- Changing Tools menu options (for example BLE enabled/disabled) reuses cached SDK/NCS data.
- Updating the core version via Boards Manager reuses the same cache under
  `<Arduino data>/packages/<vendor>/tools/` when possible.
- Override cache location with `ARDUINO_NRF54L15_SHARED_TOOLS_DIR`.

## Uploading Firmware

The XIAO nRF54L15 has a built-in CMSIS-DAP debugger. You can upload firmware using:

### Via Arduino IDE
1. Select **Tools > Upload Method > Auto** (recommended)
2. Click the Upload button

### Via Command Line (pyocd)
```bash
pyocd load -t nrf54l15 firmware.hex --format hex
```

### Via Command Line (openocd)
```bash
openocd -f tools/openocd/nrf54l15.cfg -c "program firmware.hex verify reset"
```

## Tools Menu (Board-Specific)

The clean core exposes these Arduino IDE options:

- **Upload Method**: `Auto (Recommended)`, `pyOCD (CMSIS-DAP)`, `OpenOCD (Experimental)`
- **CPU Frequency**: `64 MHz (Default)`, `128 MHz (Experimental)`
- **Antenna**: `On-board Ceramic`, `External U.FL`
- **Radio Profile**: `Radio Disabled`, `BLE Only`, `802.15.4 Only`, `BLE + 802.15.4`

For lowest active current, keep **CPU Frequency = 64 MHz** and use `03.Board/LowPowerProfiles`.

When **Radio Profile** is `BLE Only`, `802.15.4 Only`, or `BLE + 802.15.4`, the build automatically enforces a 128 MHz radio-safe clock profile.

`OpenOCD` is marked experimental for nRF54L15 because upstream flash support is incomplete.
If OpenOCD upload fails, the uploader falls back to `pyOCD` automatically when available.

To sync this local core into your Arduino sketchbook hardware path:

```bash
rsync -a --delete /home/lolren/Desktop/Xiaonrf54l15/Nrf54L15_clean/hardware/nrf54l15/ /home/lolren/Arduino/hardware/nrf54l15/
```

## Pin Mapping (XIAO nRF54l15)

| Arduino Pin | Port/Pin | Functions               |
|-------------|----------|-------------------------|
| D0          | P1.04    | GPIO                    |
| D1          | P1.05    | GPIO                    |
| D2          | P1.06    | GPIO                    |
| D3          | P1.07    | GPIO                    |
| D4          | P1.10    | GPIO, I2C SDA          |
| D5          | P1.11    | GPIO, I2C SCL          |
| D6          | P2.08    | GPIO, PWM, UART TX      |
| D7          | P2.07    | GPIO, PWM, UART RX      |
| D8          | P2.01    | GPIO, PWM, SPI SCK      |
| D9          | P2.04    | GPIO, PWM, SPI MISO     |
| D10         | P2.02    | GPIO, PWM, SPI MOSI     |
| D11         | P0.03    | GPIO                    |
| D12         | P0.04    | GPIO                    |
| D13         | P2.10    | GPIO                    |
| D14         | P2.09    | GPIO                    |
| D15         | P2.06    | GPIO                    |
| D16         | P2.00    | LED_BUILTIN (active-low)|
| D17         | P1.02    | Built-in Button          |

### Analog Pins
- **A0-A7**: ADC channels 0-7 (map to various GPIO pins)

## Serial Output

The XIAO nRF54L15 uses USB CDC for serial communication. The board appears as:
- **Linux**: `/dev/ttyACM0`
- **macOS**: `/dev/cu.usbmodem...`
- **Windows**: `COMx`

```cpp
void setup() {
    Serial.begin(115200);
    Serial.println("XIAO nRF54L15 ready!");
}

void loop() {
    Serial.println("Hello!");
    delay(1000);
}
```

## Examples

The core includes several examples:

- **01.Basics/Blink** - Basic LED blinking
- **01.Basics/AnalogRead** - Analog input with serial output
- **02.Communication/I2CScanner** - I2C address scanner
- **02.Communication/I2CRegisterRead** - Repeated-start register read
- **02.Communication/I2CTargetCallbacks** - I2C target/slave callback demo (`onReceive`/`onRequest`)
- **02.Communication/SPIDigitalPot** - SPI peripheral example
- **02.Communication/SPILoopback** - SPI loopback self-test
- **02.Communication/SPIBurstTransfer** - SPI buffer transfer demo
- **02.Communication/SerialEcho** - Serial RX/TX test
- **02.Communication/SerialConfig** - UART framing config example (`SERIAL_8N1`, etc.)
- **02.Communication/SerialBridge01** - Serial (USB/console) to Serial1 (D6/D7) bridge
- **02.Digital/LEDDetect** - LED detection test
- **02.Digital/SimpleBlink** - Blink without Arduino abstractions
- **libraries/Bluetooth/examples/** - BLE API examples for the clean core
- **libraries/Bluetooth/examples/BLEScanForEach** - callback-based scan stream demo
- **libraries/Bluetooth/examples/BLECentralConnect** - central connect/disconnect cycle
- **03.Board/BLEScanMonitor** - stream all scan hits in each window (`scanForEach`)
- **03.Board/BLECentralMonitor** - board-level BLE central loop for real-device tests
- **libraries/IEEE802154/examples/** - 802.15.4 config + passive scan examples
- **03.Board/IEEE802154FeatureProbe** - board-level 802.15.4 probe/scan example
- **libraries/Watchdog/examples/FeedWatchdog** - simple `Watchdog.begin()/feed()` compatibility check
- **libraries/XiaoNrf54L15/examples/AntennaSelect** - Runtime antenna switching + radio profile info
- **03.Board/LowPowerProfiles** - Button-switchable low-power duty profiles
- **03.Board/PeripheralPowerGating** - Manual peripheral suspend/resume command shell
- **03.Board/WatchdogSleepWake** - Watchdog + sleep + reset-cause validation
- **03.Board/CpuFrequencyControl** - CPU frequency monitor/control (Tools + optional runtime set)
- **03.Board/HardwareValidationMatrix** - single serial-menu validation sketch across ADC/I2C/SPI/UART/watchdog/sleep/BLE/802.15.4
- **03.Board/BLE6RangeBeacon** - BLE transmitter for range testing
- **03.Board/BLE6RangeProbe** - RSSI trend probe for practical BLE range checks

## Memory Usage

Example program storage usage:
- **Blink**: ~52KB flash, ~19KB RAM
- **BLE menu-enabled example**: ~180KB flash, ~39KB RAM
- **Full featured sketches**: varies with selected radio/profile options

## Architecture

This core consists of:

### Core Components (`cores/nrf54l15/`)
- `Arduino.h` - Main Arduino API definitions
- `cmsis.h` - Cortex-M33 CMSIS core
- `nrf54l15.h` - nRF54L15 peripheral definitions
- `wiring_*.c` - Digital I/O, analog, timing, math, random
- `HardwareSerial.cpp` - UART (Serial)
- `SPI.cpp` - SPI
- `Wire.cpp` - I2C
- `Print.cpp` - Output stream
- `WString.h` - String class (inline)
- `main.cpp` - Entry point
- `core_install.c` - Startup and exception handlers
- `syscalls.c` - Newlib system call stubs
- `nrf54l15_linker_script.ld` - Memory layout
- `startup_nrf54l15.s` - Vector table (reference)

### Variant (`variants/xiao_nrf54l15/`)
- Pin definitions and mappings
- Board-specific initialization

## Known Limitations

- No USB stack (CDC uses built-in debugger)
- BLE capabilities are menu/profile dependent; validate with BLE examples for your selected controller/profile
- `BLE + 802.15.4` profile has high RAM usage; start from lighter sketches when enabling both radios
- Thread/Mesh protocols not implemented

## Contributing

Contributions are welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request

## License

Apache License 2.0 - See LICENSE file for details

## Credits

- Based on Nordic Semiconductor nRF54L15 reference materials
- Inspired by the Arduino nRF52 core
- CMSIS-CORE by ARM Ltd.

## Links

- [nRF54L15 Product Page](https://www.nordicsemi.com/products/nrf54l15)
- [XIAO nRF54L15 Wiki](https://wiki.seeedstudio.com/xiao_nrf54l15)
- [Data Sheet](https://infocenter.nordicsemi.com/pdf/nRF54L15_PS_v1.0.pdf)

## Support

[![Buy Me a Coffee](https://img.shields.io/badge/Buy%20Me%20a%20Coffee-FFDD00?logo=buymeacoffee&logoColor=000000)](https://buymeacoffee.com/lolren)
