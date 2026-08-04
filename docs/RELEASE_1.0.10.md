# nRF54 Arduino Core 1.0.10

`1.0.10` fixes the two current open issues: Arduino IDE 1.8 upload failures
([#102](https://github.com/lolren/nrf54-arduino-core/issues/102)) and battery-only
BLE startup on the XIAO nRF54L15 ([#105](https://github.com/lolren/nrf54-arduino-core/issues/105)).

## Arduino IDE 1.8 uploads

- Use direct `python3` launchers in the non-Windows build, UF2, upload, and
  programmer recipes.
- Keep the legacy empty verbosity properties for IDE 1.8.x compatibility.
- Add regression coverage for recipes that accidentally retain unresolved
  `{tools.python3.*}` placeholders.

## Battery-safe XIAO serial bridge

- Add the **USB bridge when powered** default Serial Routing option.
- Defer the XIAO SAMD11 bridge UART until its host-side serial bridge is active.
- Leave P1.08/P1.09 high-impedance while the USB-only bridge is absent, avoiding
  an nRF output driving an unpowered SAMD11 during battery boot.
- Preserve normal USB serial activation and provide explicit Header UART and
  Serial-disabled routes.

## Validation

- Core I/O, upload-recipe, and release-version contract suites pass.
- The XIAO nRF54L15 beacon example compiles and uploads through the attached
  Seeed CMSIS-DAP probe.
- A host BLE scan sees the beacon after the initial flash and after two further
  reset/reflash cycles.
- Battery-only power removal was not available during this validation; please
  test the release on a LiPo-powered board.

## Install or upgrade

```bash
arduino-cli core update-index
arduino-cli core install "nrf54l15clean:nrf54l15clean@1.0.10"
```

[Full changes since v1.0.9](https://github.com/lolren/nrf54-arduino-core/compare/v1.0.9...v1.0.10)
