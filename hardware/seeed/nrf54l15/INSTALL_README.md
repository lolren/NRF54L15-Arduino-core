# Install Guide - Seeed XIAO nRF54L15 (Zephyr Core)

This guide installs this standalone core into Arduino IDE/CLI.

Core source folder:
- `/home/lolren/Desktop/Nrf54L15/hardware/seeed/nrf54l15`

Target Arduino sketchbook hardware folder:
- Linux: `~/Arduino/hardware/seeed/nrf54l15`
- macOS: `~/Documents/Arduino/hardware/seeed/nrf54l15`
- Windows: `%USERPROFILE%\\Documents\\Arduino\\hardware\\seeed\\nrf54l15`

## 1) Copy the core

Copy `hardware/seeed/nrf54l15` into your sketchbook `hardware/seeed/` folder.

Linux example:

```bash
mkdir -p ~/Arduino/hardware/seeed
rsync -a --delete /home/lolren/Desktop/Nrf54L15/hardware/seeed/nrf54l15/ ~/Arduino/hardware/seeed/nrf54l15/
```

## 2) Restart Arduino IDE

Close all Arduino IDE windows and reopen.  
Then select:
- `Tools -> Board -> Seeed Studio XIAO nRF54L15 (Zephyr)`

## 3) Select port and upload method

- Plug in the board USB.
- Select the board port in `Tools -> Port`.
- `Tools -> Upload Method` can be left at `Auto (Recommended)`.

Upload is handled by the Zephyr flash tool (`flash_zephyr.py`), not `avrdude`.

## 4) First test

Open:
- `File -> Examples -> 00.XIAO-BoardTests -> Blink500ms`

Compile and upload.

## 5) Recommended board options

- `Tools -> Antenna`: `On-board Ceramic (Recommended)` unless using external U.FL.
- `Tools -> Radio Profile`: `BLE Only (Recommended)` unless you need 802.15.4.
- `Tools -> BLE TX Power`: `0 dBm (Recommended)` to start.
- `Tools -> HPF MSPI`: `Disabled (Recommended)` unless explicitly testing HPF MSPI.

For Zigbee/802.15.4 examples, set:
- `Tools -> Radio Profile` to `BLE + 802.15.4` or `802.15.4 Only`.

## 6) Update workflow after edits

When you change files in the source folder, sync again:

```bash
rsync -a --delete --exclude '.arduino-zephyr-build/' /home/lolren/Desktop/Nrf54L15/hardware/seeed/nrf54l15/ ~/Arduino/hardware/seeed/nrf54l15/
```

Then restart Arduino IDE before retesting.

## 7) Troubleshooting quick checks

1. Board options missing in `Tools`:
   - Confirm selected board is `Seeed Studio XIAO nRF54L15 (Zephyr)`.
   - Confirm path is exactly `.../Arduino/hardware/seeed/nrf54l15`.
   - Fully restart IDE.

2. Upload fails with SWD/OpenOCD probe errors:
   - Keep `Upload Method = Auto` first.
   - Try reconnecting USB and power-cycling board.
   - If needed, use a recover/erase flow then upload again.

3. Serial monitor shows nothing:
   - Match sketch baud rate and Serial Monitor baud rate.
   - For `AnalogReadSerial`, use `9600`.
