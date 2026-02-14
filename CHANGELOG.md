# Changelog

All notable changes to this project are documented in this file.

## 1.1.8 - 2026-02-14

- Fixed clean-install dependency-discovery failures before `zephyr_lib` is generated:
  Zephyr header includes in Bluetooth/XiaoNrf54L15/IEEE802154 library sources are now
  guarded with `__has_include(...)` so Arduino preprocessor discovery can run before
  first Zephyr bootstrap/build.
- This allows BLE sketches to compile first on a fresh install without requiring a
  prior Blink/default build.

## 1.1.7 - 2026-02-14

- Fixed fresh-install BLE compile regression:
  `libraries/Bluetooth/src/Bluetooth.h` now includes `<zephyr/types.h>`
  only when available, so BLE sketches can compile on first run before
  generated Zephyr headers are populated.
- Kept 1.1.6 workspace/bootstrap lock hardening intact.

## 1.1.6 - 2026-02-14

- Added cross-process build lock in `build_zephyr_lib.py` to avoid concurrent `west build`
  collisions on the same Zephyr build directory.
- Added automatic one-shot workspace integrity recovery retry when `west build` fails and
  the shared NCS workspace is detected as incomplete mid-run.

## 1.1.5 - 2026-02-14

- Fixed recovery from partially initialized shared NCS workspaces:
  `build_zephyr_lib.py` now validates workspace integrity (`.west/config`,
  `nrf/west.yml`, and Zephyr west command directory) and re-runs bootstrap when incomplete.
  This prevents `west: unknown command "build"` errors after interrupted first-time setup.
- Added official Arm GNU Toolchain downloads link in docs as an optional manual fallback reference.

## 1.1.4 - 2026-02-14

- Added shared bootstrap cache handling for Boards Manager installs:
  NCS/Zephyr SDK payloads now live under the package vendor `tools/` directory,
  so changing board options or updating core versions does not trigger full re-downloads.
- Added cross-platform toolchain launcher wrappers (`toolchain-*` + `.cmd`) and
  `toolchain_exec.py` so first compile on a clean machine can bootstrap the Zephyr SDK
  before compiler invocation instead of failing with missing `arm-none-eabi-*` tools.
- Updated installed-core discovery to include `packages/nrf54l15/...` layout in
  addition to legacy vendor roots.
- Expanded CI fresh-machine matrix to include `macos-latest`.
- Updated documentation with cache behavior and `ARDUINO_NRF54L15_SHARED_TOOLS_DIR`.

## 1.1.3 - 2026-02-14

- Fixed `Tools > Radio Profile > BLE Only` build failure on nRF54L15:
  BLE profile now enforces 128 MHz CPU and applies the 128 MHz overlay, matching MPSL requirements.
- Updated docs to clarify that BLE radio profiles use a 128 MHz radio-safe clock configuration.

## 1.1.2 - 2026-02-14

- Added Linux host-tools fallback in `tools/get_toolchain.py`:
  when `dtc` is missing, the script now fetches `hosttools_linux-<arch>.tar.xz`,
  extracts the standalone installer into the SDK directory, and retries setup automatically.
- Kept host-tools standalone installer files during prune to preserve recovery path for subsequent runs.

## 1.1.1 - 2026-02-14

- Hardened `tools/get_toolchain.py` download flow for unstable networks:
  automatic retry on truncated/corrupt archives, size validation, and resume-capable downloads via `curl`/`wget`.
- Added host-tool (`dtc`) checks in SDK bootstrap path and clearer fallback messaging when host tools are not bundled.
- Increased default SDK download retry count (`ZEPHYR_SDK_DOWNLOAD_RETRIES`, default now `10`).

## 1.1.0 - 2026-02-14

- Added `examples/03.Board/HardwareValidationMatrix`:
  a single serial-menu bring-up sketch for ADC, I2C, SPI, UART1 loopback,
  watchdog, sleep, CPU frequency control, power profiles, BLE tests, and
  IEEE 802.15.4 tests.
- Added deterministic release archive mtimes in
  `tools/release_boards_manager.py` via `--source-date-epoch`.
- Added `tools/check_release_reproducible.py` to generate release artifacts
  twice and verify matching archive SHA-256 and package index JSON.
- Extended `tools/ci_fresh_machine_smoke.py` to validate
  `HardwareValidationMatrix` presence and compile coverage on default, BLE,
  and 802.15.4 profiles.
- Updated GitHub Actions workflow to run reproducibility checks before
  fresh-machine smoke.
