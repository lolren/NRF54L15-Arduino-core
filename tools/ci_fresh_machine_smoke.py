#!/usr/bin/env python3
from __future__ import annotations

import contextlib
import fnmatch
import http.server
import os
import socket
import subprocess
import sys
import tempfile
import threading
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
PLATFORM_TXT = REPO_ROOT / "hardware" / "nrf54l15" / "nrf54l15" / "platform.txt"
RELEASE_SCRIPT = REPO_ROOT / "tools" / "release_boards_manager.py"

FORBIDDEN_PATTERNS = [
    "*/tools/ncs/*",
    "*/tools/ncs.partial/*",
    "*/tools/zephyr-sdk/*",
    "*/tools/.arduino-zephyr-build/*",
    "*/variants/*/zephyr_lib/*",
]


def run(cmd: list[str], *, cwd: Path | None = None, env: dict[str, str] | None = None) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=str(cwd) if cwd else None, env=env, check=True)


def read_core_version(platform_txt: Path) -> str:
    for line in platform_txt.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith("version="):
            return line.split("=", 1)[1].strip()
    raise RuntimeError(f"Unable to find version= in {platform_txt}")


def choose_free_port() -> int:
    with contextlib.closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def quote_yaml_path(path: Path) -> str:
    return str(path).replace("\\", "\\\\")


def assert_archive_excludes(archive_path: Path) -> None:
    import tarfile

    with tarfile.open(archive_path, mode="r:bz2") as tar:
        names = [m.name for m in tar.getmembers() if m.isfile()]

    for name in names:
        normalized = name.replace("\\", "/")
        for pattern in FORBIDDEN_PATTERNS:
            if fnmatch.fnmatch(normalized, pattern):
                raise RuntimeError(f"Forbidden packaged path found: {normalized} (pattern: {pattern})")


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, _format: str, *_args: object) -> None:
        return


@contextlib.contextmanager
def http_server(root: Path, port: int):
    handler = lambda *args, **kwargs: QuietHandler(*args, directory=str(root), **kwargs)
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", port), handler)
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    try:
        yield
    finally:
        httpd.shutdown()
        httpd.server_close()
        thread.join(timeout=5)


def main() -> int:
    if not PLATFORM_TXT.is_file():
        raise RuntimeError(f"Missing {PLATFORM_TXT}")
    if not RELEASE_SCRIPT.is_file():
        raise RuntimeError(f"Missing {RELEASE_SCRIPT}")

    version = read_core_version(PLATFORM_TXT)
    package_name = f"nrf54l15-baremetal-{version}.tar.bz2"

    with tempfile.TemporaryDirectory(prefix="nrf54l15-smoke-") as temp_dir_str:
        temp_dir = Path(temp_dir_str)
        dist_dir = temp_dir / "dist"
        dist_dir.mkdir(parents=True, exist_ok=True)

        port = choose_free_port()
        archive_url = f"http://127.0.0.1:{port}/dist/{package_name}"
        index_path = temp_dir / "package_nrf54l15_baremetal_test.json"

        run(
            [
                sys.executable,
                str(RELEASE_SCRIPT),
                "--version",
                version,
                "--repo",
                "example/example",
                "--dist-dir",
                str(dist_dir),
                "--index-path",
                str(index_path),
                "--archive-url",
                archive_url,
            ],
            cwd=REPO_ROOT,
        )

        archive_path = dist_dir / package_name
        if not archive_path.is_file():
            raise RuntimeError(f"Release archive missing: {archive_path}")
        assert_archive_excludes(archive_path)

        data_dir = temp_dir / "arduino-data"
        user_dir = temp_dir / "arduino-user"
        data_dir.mkdir(parents=True, exist_ok=True)
        user_dir.mkdir(parents=True, exist_ok=True)

        cfg_path = temp_dir / "arduino-cli.yaml"
        cfg_path.write_text(
            "\n".join(
                [
                    "board_manager:",
                    "  additional_urls:",
                    f"    - http://127.0.0.1:{port}/{index_path.name}",
                    "directories:",
                    f"  data: \"{quote_yaml_path(data_dir)}\"",
                    f"  user: \"{quote_yaml_path(user_dir)}\"",
                    "",
                ]
            ),
            encoding="utf-8",
        )

        env = dict(os.environ)

        with http_server(temp_dir, port):
            run(["arduino-cli", "--config-file", str(cfg_path), "core", "update-index"], env=env)
            run(
                [
                    "arduino-cli",
                    "--config-file",
                    str(cfg_path),
                    "core",
                    "install",
                    f"nrf54l15:nrf54l15@{version}",
                ],
                env=env,
            )

            result = subprocess.run(
                ["arduino-cli", "--config-file", str(cfg_path), "core", "list"],
                check=True,
                capture_output=True,
                text=True,
                env=env,
            )

        if "nrf54l15:nrf54l15" not in result.stdout:
            raise RuntimeError("Installed core not found in `arduino-cli core list` output")

        installed_core_root = (
            data_dir / "packages" / "nrf54l15" / "hardware" / "nrf54l15" / version
        )
        if not installed_core_root.is_dir():
            raise RuntimeError(f"Installed core directory missing: {installed_core_root}")

        basic_example = installed_core_root / "examples" / "01.Basics" / "Blink"
        i2c_target_example = installed_core_root / "examples" / "02.Communication" / "I2CTargetCallbacks"
        serial_config_example = installed_core_root / "examples" / "02.Communication" / "SerialConfig"
        spi_loopback_example = installed_core_root / "examples" / "02.Communication" / "SPILoopback"
        low_power_example = installed_core_root / "examples" / "03.Board" / "LowPowerFeatures"
        low_power_profiles_example = installed_core_root / "examples" / "03.Board" / "LowPowerProfiles"
        peripheral_power_gating_example = (
            installed_core_root / "examples" / "03.Board" / "PeripheralPowerGating"
        )
        cpu_freq_example = installed_core_root / "examples" / "03.Board" / "CpuFrequencyControl"
        watchdog_example = installed_core_root / "examples" / "03.Board" / "WatchdogSleepWake"
        matrix_example = installed_core_root / "examples" / "03.Board" / "HardwareValidationMatrix"
        watchdog_lib_example = installed_core_root / "libraries" / "Watchdog" / "examples" / "FeedWatchdog"
        ble6_example = installed_core_root / "examples" / "03.Board" / "BLE6FeatureProbe"
        ble6_range_probe_example = installed_core_root / "examples" / "03.Board" / "BLE6RangeProbe"
        ble_scan_monitor_example = installed_core_root / "examples" / "03.Board" / "BLEScanMonitor"
        ble_central_monitor_example = installed_core_root / "examples" / "03.Board" / "BLECentralMonitor"
        ble_scan_example = installed_core_root / "libraries" / "Bluetooth" / "examples" / "BLEScan"
        ble_scan_foreach_example = installed_core_root / "libraries" / "Bluetooth" / "examples" / "BLEScanForEach"
        ble_central_example = installed_core_root / "libraries" / "Bluetooth" / "examples" / "BLECentralConnect"
        ieee802154_example = installed_core_root / "libraries" / "IEEE802154" / "examples" / "IEEE802154Config"
        ieee802154_scan_example = (
            installed_core_root / "libraries" / "IEEE802154" / "examples" / "IEEE802154PassiveScan"
        )
        ieee802154_probe_example = installed_core_root / "examples" / "03.Board" / "IEEE802154FeatureProbe"

        if not basic_example.is_dir():
            raise RuntimeError(f"Missing installed example: {basic_example}")
        if not i2c_target_example.is_dir():
            raise RuntimeError(f"Missing installed example: {i2c_target_example}")
        if not serial_config_example.is_dir():
            raise RuntimeError(f"Missing installed example: {serial_config_example}")
        if not spi_loopback_example.is_dir():
            raise RuntimeError(f"Missing installed example: {spi_loopback_example}")
        if not low_power_example.is_dir():
            raise RuntimeError(f"Missing installed example: {low_power_example}")
        if not low_power_profiles_example.is_dir():
            raise RuntimeError(f"Missing installed example: {low_power_profiles_example}")
        if not peripheral_power_gating_example.is_dir():
            raise RuntimeError(f"Missing installed example: {peripheral_power_gating_example}")
        if not cpu_freq_example.is_dir():
            raise RuntimeError(f"Missing installed example: {cpu_freq_example}")
        if not watchdog_example.is_dir():
            raise RuntimeError(f"Missing installed example: {watchdog_example}")
        if not matrix_example.is_dir():
            raise RuntimeError(f"Missing installed example: {matrix_example}")
        if not watchdog_lib_example.is_dir():
            raise RuntimeError(f"Missing installed example: {watchdog_lib_example}")
        if not ble6_example.is_dir():
            raise RuntimeError(f"Missing installed example: {ble6_example}")
        if not ble6_range_probe_example.is_dir():
            raise RuntimeError(f"Missing installed example: {ble6_range_probe_example}")
        if not ble_scan_monitor_example.is_dir():
            raise RuntimeError(f"Missing installed example: {ble_scan_monitor_example}")
        if not ble_central_monitor_example.is_dir():
            raise RuntimeError(f"Missing installed example: {ble_central_monitor_example}")
        if not ble_scan_example.is_dir():
            raise RuntimeError(f"Missing installed example: {ble_scan_example}")
        if not ble_scan_foreach_example.is_dir():
            raise RuntimeError(f"Missing installed example: {ble_scan_foreach_example}")
        if not ble_central_example.is_dir():
            raise RuntimeError(f"Missing installed example: {ble_central_example}")
        if not ieee802154_example.is_dir():
            raise RuntimeError(f"Missing installed example: {ieee802154_example}")
        if not ieee802154_scan_example.is_dir():
            raise RuntimeError(f"Missing installed example: {ieee802154_scan_example}")
        if not ieee802154_probe_example.is_dir():
            raise RuntimeError(f"Missing installed example: {ieee802154_probe_example}")

        fqbn_default = "nrf54l15:nrf54l15:xiao_nrf54l15"
        fqbn_ble = (
            "nrf54l15:nrf54l15:xiao_nrf54l15:"
            "clean_radio_profile=ble_only,"
            "clean_bt_controller=zephyr_ll_sw"
        )
        fqbn_ble6 = (
            "nrf54l15:nrf54l15:xiao_nrf54l15:"
            "clean_radio_profile=ble_only,"
            "clean_bt_controller=zephyr_ll_sw,"
            "clean_ble6=channel_sounding"
        )
        fqbn_802154 = (
            "nrf54l15:nrf54l15:xiao_nrf54l15:"
            "clean_radio_profile=ieee802154_only"
        )
        fqbn_dual = (
            "nrf54l15:nrf54l15:xiao_nrf54l15:"
            "clean_radio_profile=dual,"
            "clean_bt_controller=zephyr_ll_sw"
        )

        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_default,
                str(basic_example),
            ],
            env=env,
        )
        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_default,
                str(i2c_target_example),
            ],
            env=env,
        )
        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_default,
                str(serial_config_example),
            ],
            env=env,
        )
        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_default,
                str(spi_loopback_example),
            ],
            env=env,
        )
        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_default,
                str(low_power_example),
            ],
            env=env,
        )
        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_default,
                str(low_power_profiles_example),
            ],
            env=env,
        )
        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_default,
                str(peripheral_power_gating_example),
            ],
            env=env,
        )
        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_default,
                str(cpu_freq_example),
            ],
            env=env,
        )
        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_default,
                str(watchdog_example),
            ],
            env=env,
        )
        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_default,
                str(matrix_example),
            ],
            env=env,
        )
        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_default,
                str(watchdog_lib_example),
            ],
            env=env,
        )
        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_ble,
                str(ble_scan_example),
            ],
            env=env,
        )
        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_ble,
                str(ble_scan_foreach_example),
            ],
            env=env,
        )
        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_ble,
                str(ble_scan_monitor_example),
            ],
            env=env,
        )
        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_ble,
                str(ble_central_example),
            ],
            env=env,
        )
        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_ble,
                str(ble_central_monitor_example),
            ],
            env=env,
        )
        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_ble,
                str(matrix_example),
            ],
            env=env,
        )
        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_ble6,
                str(ble6_example),
            ],
            env=env,
        )
        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_ble6,
                str(ble6_range_probe_example),
            ],
            env=env,
        )
        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_802154,
                str(ieee802154_example),
            ],
            env=env,
        )
        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_802154,
                str(ieee802154_scan_example),
            ],
            env=env,
        )
        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_802154,
                str(ieee802154_probe_example),
            ],
            env=env,
        )
        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_802154,
                str(matrix_example),
            ],
            env=env,
        )
        run(
            [
                "arduino-cli",
                "--config-file",
                str(cfg_path),
                "compile",
                "--clean",
                "-b",
                fqbn_dual,
                str(basic_example),
            ],
            env=env,
        )

    print("Fresh machine Boards Manager smoke test passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
