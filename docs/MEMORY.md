# Session Memory — nRF54L Arduino Core

Load this at session start after context compaction.

## Release Process
- **Script**: `./tools/release.sh [version]` — handles everything
- **Archive**: MUST have single root dir. No symlinks (Windows fails).
- **Index**: 3 toolsDeps: arm-none-eabi-gcc, openocd, nrf54l15hosttools
- **CDN**: After delete+recreate, CDN caches old file ~5min. Use new version number.
- **Host tools**: 1.1.4 with pyOCD 0.44.1. Upload for all 5 platforms.
- **Upload detach**: `pyocd commander -c resume` after flash (AppImage compatible)

## Hardware
### LM20B
- SPI: SCK=D8(P1.04), MISO=D9(P1.05), MOSI=D10(P1.06)
- I2C: SDA=D4(P1.03), SCL=D5(P1.07)
- UART: TX=D6(P1.08), RX=D7(P1.09)
- LEDs: P1.22(R), P1.23(B), P1.24(G) active-low
- Button: P0.09
- IMU: P0.07(SCL), P0.08(SDA), P3.12(CS), addr 0x6A
- PDM mic: P1.13(CLK), P1.14(DAT)
- PMIC: P1.17(SCL), P1.18(SDA), addr 0x6B, bit-bang I2C
- QSPI flash: P2.05(CS), 8MB PY25Q64 (JEDEC 85 20 17)
- Probe UID: 3377B9D6, PID: 0068
- Flash: `pyocd flash -t nrf54lm20a -u 3377B9D6 -e chip sketch.hex`

### L15
- SPI: same as LM20B on header
- Sense IMU: P0.03(SCL), P0.04(SDA), addr 0x6A, power P0.01
- LED: P2.0 active-low
- Button: P0.09
- Probe UID: E91217E8, PID: 0066
- Flash: `pyocd flash -t nrf54l -u E91217E8 -e chip sketch.hex`

### Radio events (nRF54L — different from nRF52!)
- EVENTS_READY @ 0x200, EVENTS_RXREADY @ 0x208
- EVENTS_END @ 0x218, EVENTS_DISABLED @ 0x220
- No EVENTS_RSSIEND — read RSSISAMPLE @ 0x718 immediately after RSSISTART
- RADIO base: 0x5008A000
- STOP doesn't fire END reliably — use DISABLE instead

## SPI Frequencies
- L15: SPIM00 at F_CPU (64/128 MHz). Default 4 MHz. Max 32 MHz at 128 MHz.
- LM20B: SPIM22 at 16 MHz serial fabric. Default 4 MHz. Max 8 MHz.
- LM20B HS-SPI: SPIM00 on QSPI pads, 32 MHz at 128 MHz CPU.

## udev Rules
- Must include BOTH 0066 (L15) AND 0068 (LM20B)
- Linux Mint/Ubuntu: `sudo cp rules to /etc/udev/rules.d/`

## AppImage IDE
- All Linux users use AppImage Arduino IDE
- Sandboxed Python CANNOT use system pip packages
- Must use `pyocd commander` CLI (not Python API) for AppImage compat
- Host tools wheelhouse provides pyocd for AppImage
