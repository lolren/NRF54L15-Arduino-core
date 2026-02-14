# Seeed XIAO nRF54L15 Arduino Core (Zephyr-based)

Standalone core folder for Arduino `hardware/seeed/nrf54l15`.

This package is already pruned (no full 6.5GB workspace dump).

## Install

Copy this folder to:

```text
<Sketchbook>/hardware/seeed/nrf54l15
```

Example:

```text
~/Arduino/hardware/seeed/nrf54l15
```

## Local Dev Paths (This Machine)

Source core (editable):

```text
/home/lolren/Desktop/Nrf54L15/hardware/seeed/nrf54l15
```

Arduino IDE core path:

```text
/home/lolren/Arduino/hardware/seeed/nrf54l15
```

Current setup on this machine is a real copied folder (not symlink).

To sync changes from source into Arduino IDE core path:

```bash
rsync -a --delete --exclude '.arduino-zephyr-build/' /home/lolren/Desktop/Nrf54L15/hardware/seeed/nrf54l15/ /home/lolren/Arduino/hardware/seeed/nrf54l15/
```

`tools/.arduino-zephyr-build` is a generated cache and can be deleted any time (it will be rebuilt automatically).

## Upload (No avrdude)

Upload uses `tools/flash_zephyr.py` (no `avrdude`).

- All runners (`openocd`, `jlink`, `nrfutil`, `nrfjprog`) are flashed through Zephyr `west flash`.

- Default is `Auto`, which picks the best available runner.
- Manual override is available in Arduino: `Tools -> Upload Method`.
- Supported runners: `openocd`, `jlink`, `nrfutil`, `nrfjprog`.

If OpenOCD hits SWD access errors (`DP initialisation failed`, target unknown, etc.), run a manual recover:

```bash
pyocd erase -t nrf54l --mass -v
pyocd erase -t nrf54l --mass -v
```

Install pyOCD if missing:

```bash
python3 -m pip install --user pyocd
```

If multiple probes are connected, set:

```text
ARDUINO_ZEPHYR_DEV_ID=<probe_id>
```

## Cross-Platform Status

This core now uses Python-based build/link/upload scripts and is intended to run on:
- Linux
- macOS (Intel + Apple Silicon)
- Windows

Host requirements:
- Python 3
- Git
- Ninja/CMake (normally provided by Zephyr/NCS setup)

Toolchain bootstrap:
- `tools/get_toolchain.py` auto-detects host OS/arch and downloads a matching Zephyr SDK release asset.
- By default it also prunes SDK multilibs to the nRF54L15 target slice (`cortex-m33`, `thumb/v8-m.main/nofp`).
- On Windows, Zephyr SDK archives are typically `.7z`; install `7z` (7-Zip CLI) so extraction works.

SDK size notes (this core):
- Full arm Zephyr SDK bundle is typically around ~900MB.
- After automatic prune for this board target, SDK footprint is around ~280-300MB.
- Most remaining core size is from the bundled NCS workspace (`tools/ncs`), not the compiler alone.

## Serial Notes

- `Serial` maps to Zephyr console UART (`uart20`), which is exposed on USB through the on-board SAMD11 CMSIS-DAP bridge.
- `Serial1` maps to `xiao_serial` (`uart21`) on XIAO pins `D6` (TX) and `D7` (RX).
- `Serial.begin(<baud>)` is runtime-configurable (`CONFIG_UART_USE_RUNTIME_CONFIGURE=y`), so examples like `AnalogReadSerial` at `9600` work when Serial Monitor is also set to `9600`.
- This is UART-backed Arduino `HardwareSerial` (Zephyr UART APIs), not the nRF52840 native USB core path.

## LED Notes

- `LED_BUILTIN` is the user LED (`USER_LED`) on `P2.00` and is active-low.
- The board also has a separate charge/status LED (`CHARGE_LED`) that can stay on; it is not `LED_BUILTIN`.

## RF Switch / Antenna Notes

- XIAO nRF54L15 RF switch pins are now forced at startup in `variants/xiao_nrf54l15/variant.cpp`:
  - `P2.03` (`RF_SW_POW`) = HIGH (RF switch powered)
  - `P2.05` (`RF_SW_CTRL`) = LOW (ceramic/on-board antenna selected) by default
- This avoids BLE sketches silently using the external-antenna path when no external antenna is attached.
- Runtime antenna selection is also available from sketch code:
  - `#include <XiaoNrf54L15.h>`
  - `XiaoNrf54L15.useCeramicAntenna();`
  - `XiaoNrf54L15.useExternalAntenna();`
  - `XiaoNrf54L15.setAntenna(XiaoNrf54L15Class::CERAMIC/EXTERNAL);`
  - `XiaoNrf54L15.getAntenna()`
  - `XiaoNrf54L15.isExternalAntennaBuild()`

## Board Settings API (from sketch code)

- `XiaoNrf54L15.getRadioProfile()`
  - Returns: `RADIO_BLE_ONLY`, `RADIO_DUAL`, `RADIO_802154_ONLY`, or `RADIO_UNKNOWN`.
- `XiaoNrf54L15.getBtTxPowerDbm()`
  - Returns compile-time BLE TX power selected in `Tools -> BLE TX Power`.
- `XiaoNrf54L15.isExternalAntennaBuild()`
  - Returns true when `Tools -> Antenna -> External U.FL` was selected at compile time.

## Arduino Tools Menu Options

- `Tools -> Antenna`
  - `On-board Ceramic` (default)
  - `External U.FL` (sets `RF_SW_CTRL` high at startup)
- `Tools -> Radio Profile`
  - `BLE Only` (default, smallest RAM/flash)
  - `BLE + 802.15.4 (Thread/Zigbee Base)` (enables Zephyr networking + IEEE 802.15.4 L2)
  - `802.15.4 Only (No BLE)` (disables BT stack, keeps IEEE 802.15.4 enabled)
- `Tools -> BLE Controller`
  - `Nordic SDC` (default; uses nrfxlib SoftDevice Controller + MPSL)
  - `Zephyr LL SW (Open Source, Experimental)` (switches to Zephyr software controller)
  - The open-source option is intended for stricter OSS redistribution requirements.
- `Tools -> BLE TX Power`
  - Applies when BLE is enabled (`BLE Only` / `BLE + 802.15.4` profiles).
  - `0 dBm` (default)
  - `+8 dBm`
  - `-20 dBm`
- `Tools -> HPF MSPI`
  - `Disabled` (default)
  - `Experimental Enable` (enables HPF MSPI Kconfig + overlay for CPUFLPR/IPC path)

These settings are applied by Zephyr extra Kconfig fragments during prebuild, so changing them triggers a Zephyr base rebuild.
Compile-time macros are also provided for sketch logic:
- `ARDUINO_XIAO_NRF54L15_RADIO_BLE_ONLY`
- `ARDUINO_XIAO_NRF54L15_RADIO_DUAL`
- `ARDUINO_XIAO_NRF54L15_RADIO_802154_ONLY`
- `ARDUINO_XIAO_NRF54L15_BT_CONTROLLER_SDC`
- `ARDUINO_XIAO_NRF54L15_BT_CONTROLLER_LL_SW`
- `ARDUINO_XIAO_NRF54L15_BT_TX_PWR_DBM` (0, 8, -20)
- `ARDUINO_XIAO_NRF54L15_EXT_ANTENNA` (when external antenna menu option is selected)
- `ARDUINO_XIAO_NRF54L15_HPF_MSPI` (when `Tools -> HPF MSPI -> Experimental Enable` is selected)

If menu options do not appear in Arduino IDE:
1. Confirm selected board is `Seeed Studio XIAO nRF54L15 (Zephyr)`.
2. Confirm core path exists: `/home/lolren/Arduino/hardware/seeed/nrf54l15`.
3. Close all IDE windows and start IDE again (full restart).
4. If still stale, close IDE and clear board-option cache:

```bash
mv "/home/lolren/.config/arduino-ide/Local Storage/leveldb" "/home/lolren/.config/arduino-ide/Local Storage/leveldb.backup.$(date +%Y%m%d-%H%M%S)"
mkdir -p "/home/lolren/.config/arduino-ide/Local Storage/leveldb"
```

## Build/Toolchain behavior

- The bundled NCS tree is pruned but complete enough for this core.
- Zephyr SDK bootstrap is host-aware (`tools/get_toolchain.py`).
- Default SDK pruning keeps only the nRF54L15-required ARM multilib target (`thumb/v8-m.main/nofp`) plus common toolchain binaries.
- If toolchain/workspace is missing, setup scripts in `tools/` bootstrap automatically.
- Final Arduino links run through Zephyr's normal `zephyr_pre0 -> isr_tables -> zephyr.elf` pipeline (via Ninja) so ISR/vector tables always match the final sketch image.
- BLE controller choices:
  - `Nordic SDC`: uses `CONFIG_BT_LL_SOFTDEVICE` and `CONFIG_MPSL` from nrfxlib.
  - `Zephyr LL SW`: uses `CONFIG_BT_LL_SW_SPLIT` with a DTS overlay that selects `zephyr,bt-hci = &bt_hci_controller`.
  - `Zephyr LL SW` reduces dependency on Nordic-licensed BLE controller binaries, but is marked experimental for this board.

SDK prune controls:
- Default: enabled (`PRUNE_ZEPHYR_SDK=1`, `PRUNE_ZEPHYR_SDK_MULTIARCH=1`).
- Disable all pruning: `PRUNE_ZEPHYR_SDK=0`.
- Keep base prune but keep all multilib variants: `PRUNE_ZEPHYR_SDK_MULTIARCH=0`.

Re-run prune on an already installed SDK:
```bash
python3 tools/get_toolchain.py
```

## Examples included

- Board-specific feature examples: `examples/05.XIAO-Specific`
  - `adc`
  - `battery`
  - `ble`
  - `blink`
  - `button`
  - `dmic`
  - `dmic-recorder`
  - `epaper` (SPI smoke test / transport check)
  - `gps` (Serial1 passthrough)
  - `imu` (I2C IMU probe)
  - `lowpower`
  - `pwm`
  - `pwm_rgb`
  - `HPF-mspi` (runtime HPF MSPI diagnostic / channel status)
  - `zigbee_scan` (802.15.4 active scan over channels 11-26)
  - `zigbee_radio_config` (channel/PAN/short address/TX power setup)
  - `uart`
  - `xiao_expanded/attitude_monitor-ncs`
  - `xiao_expanded/sd_card`
  - `xiao_expanded/buzzer`
  - `xiao_expanded/grove_relay`
  - `xiao_expanded/oled`
  - `xiao_expanded/rtc`
- Board-specific quick tests (recommended first): `examples/00.XIAO-BoardTests`
  - `Blink500ms`
  - `AnalogReadSerial`
  - `BLEAdvertiseTest`
  - `BLEScanTest`
  - `Serial1Loopback` (D6 TX -> D7 RX loopback)
  - `I2CScannerXiao` (D4 SDA / D5 SCL)
  - `SPILoopback` (D10 MOSI -> D9 MISO loopback)
  - `RadioProfileInfo` (shows selected radio/antenna/TX power build settings)
- RF switch example: `examples/04.Radio/AntennaControl`
- Additional generic core examples: `examples/01.Basics`, `examples/02.Analog`, `examples/03.Communication`
- Library examples:
  - Bluetooth: `libraries/Bluetooth/examples/BLEAdvertise`, `libraries/Bluetooth/examples/BLEScan`
  - Wire: `libraries/Wire/examples/I2CScanner`
  - Zigbee: `libraries/Zigbee/examples/ZigbeeScan`, `libraries/Zigbee/examples/ZigbeeRadioConfig`
  - HPFMSPI: `libraries/HPFMSPI/examples/HPFMSPIInfo`
  - SPI: `libraries/SPI/examples/SPITransfer`

Notes:
- `epaper`, `gps`, `imu`, `sd_card`, `oled`, and `rtc` examples are hardware bring-up/probe sketches intended to verify wiring and transport with external modules.
- `Zigbee` library currently wraps Zephyr IEEE 802.15.4 management APIs (scan/channel/PAN/address/TX power). Full OpenThread/Zigbee stack APIs are still outside this Arduino wrapper layer.
- `Wire` now includes Arduino-style slave callbacks (`begin(address)`, `onReceive`, `onRequest`) via Zephyr I2C target API; runtime support depends on the active low-level I2C backend.
- `HPF-mspi` workflow is experimental and controlled by `Tools -> HPF MSPI`.
