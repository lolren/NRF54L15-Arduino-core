#!/usr/bin/env python3
"""Run the reproducible two-board hardware gate used for release candidates.

The gate deliberately drives the source tree through the same Arduino compile
and pyOCD paths used by customers. It does not infer board roles from
``/dev/ttyACM*`` ordering: the CMSIS-DAP UID identifies a board and the serial
port is resolved from its stable ``/dev/serial/by-id`` link.

The default ``smoke`` profile checks programming, boot, a complete 2M -> 1M
-> 2M BLE cycle, and the controller-backed Channel Sounding pair. ``full``
adds pairing/bonding, Numeric Comparison, mutual OOB, RPA rotation, a
privacy-aware bonded reconnect, and a reset-recovery repetition of the link.

This is a hardware regression gate, not Bluetooth qualification. It produces
the evidence needed to make a release decision while preserving all serial and
tool output necessary to diagnose a failure.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable, Iterable, Sequence


ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / "hardware" / "nrf54l15clean" / "nrf54l15clean"
LIBRARY_EXAMPLES = PLATFORM / "libraries" / "Nrf54L15-Clean-Implementation" / "examples"
CORE_VERSION_PROBE = PLATFORM / "examples" / "CoreVersionProbe"
CS_SCRIPT = ROOT / "scripts" / "test_cs_controller_pair.sh"
LOCAL_PACKAGER = "localnrf54"
DECLARED_COMPILER_VERSION = "7-2017q4"

FQBN_L15 = f"{LOCAL_PACKAGER}:nrf54l15clean:xiao_nrf54l15"
FQBN_LM20 = f"{LOCAL_PACKAGER}:nrf54l15clean:xiao_nrf54lm20b"
TARGET_L15 = "nrf54l"
TARGET_LM20 = "nrf54lm20a"

FATAL_MARKERS = ("FATAL:", "fatal_stage=", "pairing-failed", "ASSERT_FAIL")
PHASE_NAMES = (
    "build_environment",
    "boot",
    "ble_phy_mtu_dle",
    "ble_pair_bond",
    "ble_numeric_comparison",
    "ble_numeric_comparison_reject",
    "ble_oob_pairing",
    "ble_oob_peripheral_to_central",
    "ble_oob_central_to_peripheral",
    "ble_rpa_rotation",
    "ble_privacy_bond",
    "ble_reset_recovery",
    "system_off_wake",
    "channel_sounding",
)


def source_platform_version() -> str:
    for line in (PLATFORM / "platform.txt").read_text(encoding="utf-8").splitlines():
        if line.startswith("version="):
            return line.split("=", 1)[1].strip()
    raise GateFailure("platform.txt has no version field")


class GateFailure(RuntimeError):
    """A test phase failed after preserving all of its artifacts."""


@dataclass(frozen=True)
class Board:
    role: str
    uid: str
    target: str
    fqbn: str
    port: str
    core: str


@dataclass
class PhaseResult:
    name: str
    status: str
    duration_s: float
    detail: str


class SerialCapture:
    def __init__(self, boards: Iterable[Board], baud: int = 115200) -> None:
        try:
            import serial  # type: ignore
        except ImportError as exc:  # pragma: no cover - hardware dependency
            raise GateFailure("pyserial is required: python3 -m pip install pyserial") from exc

        self._serial_module = serial
        self._handles: dict[str, object] = {}
        self._chunks: dict[str, list[bytes]] = {}
        self._threads: list[threading.Thread] = []
        self._stop = threading.Event()
        self._lock = threading.Lock()
        for board in boards:
            handle = serial.Serial(board.port, baudrate=baud, timeout=0.05)
            handle.reset_input_buffer()
            self._handles[board.role] = handle
            self._chunks[board.role] = []

    def start(self) -> None:
        for role, handle in self._handles.items():
            thread = threading.Thread(target=self._read, args=(role, handle), daemon=True)
            thread.start()
            self._threads.append(thread)

    def _read(self, role: str, handle: object) -> None:
        while not self._stop.is_set():
            data = handle.read(4096)  # type: ignore[attr-defined]
            if data:
                with self._lock:
                    self._chunks[role].append(data)

    def text(self, role: str) -> str:
        with self._lock:
            chunks = list(self._chunks.get(role, []))
        return b"".join(chunks).decode("utf-8", errors="replace")

    def stop(self) -> dict[str, str]:
        self._stop.set()
        for thread in self._threads:
            thread.join(timeout=1.0)
        for handle in self._handles.values():
            handle.close()  # type: ignore[attr-defined]
        return {role: self.text(role) for role in self._chunks}

    def send(self, role: str, command: str) -> None:
        handle = self._handles.get(role)
        if handle is None:
            raise GateFailure(f"serial capture has no handle for {role}")
        handle.write(command.encode("ascii"))  # type: ignore[attr-defined]
        handle.flush()  # type: ignore[attr-defined]

    def clear(self, role: str) -> None:
        handle = self._handles.get(role)
        if handle is None:
            raise GateFailure(f"serial capture has no handle for {role}")
        handle.reset_input_buffer()  # type: ignore[attr-defined]
        with self._lock:
            self._chunks[role].clear()


class ReleaseGate:
    def __init__(self, args: argparse.Namespace, boards: tuple[Board, Board]) -> None:
        self.args = args
        self.l15, self.lm20 = boards
        self.started = time.monotonic()
        self.started_utc = datetime.now(timezone.utc)
        timestamp = self.started_utc.strftime("%Y%m%dT%H%M%SZ")
        self.outdir = args.outdir or ROOT / "measurements" / f"two_board_release_gate_{timestamp}"
        self.outdir.mkdir(parents=True, exist_ok=False)
        self.build_dir = self.outdir / "build"
        self.logs_dir = self.outdir / "logs"
        self.command_dir = self.outdir / "commands"
        self.build_dir.mkdir()
        self.logs_dir.mkdir()
        self.command_dir.mkdir()
        self.results: list[PhaseResult] = []
        self._command_index = 0
        self.compiler_path = find_compiler_path(args.compiler_path)
        self.source_version = source_platform_version()
        self.git_revision = run_checked(["git", "rev-parse", "HEAD"]).strip()
        self.git_dirty = bool(run_checked(["git", "status", "--porcelain"]).strip())
        self.cli_root = self.outdir / "arduino-cli"
        self.cli_config = self._register_source_core()

    def _register_source_core(self) -> Path:
        user = self.cli_root / "user"
        data = Path(
            os.environ.get("ARDUINO_DATA_DIR", str(Path.home() / ".arduino15"))
        ).expanduser().resolve()
        if not data.is_dir():
            raise GateFailure(f"Arduino data directory does not exist: {data}")
        downloads = self.cli_root / "downloads"
        hardware = user / "hardware" / LOCAL_PACKAGER
        hardware.mkdir(parents=True)
        downloads.mkdir()
        (hardware / "nrf54l15clean").symlink_to(PLATFORM, target_is_directory=True)
        config = self.cli_root / "arduino-cli.yaml"
        config.write_text(
            "directories:\n"
            f"  data: {data}\n"
            f"  downloads: {downloads}\n"
            f"  user: {user}\n",
            encoding="utf-8",
        )
        return config

    def command(self, name: str, cmd: Sequence[str], *, check: bool = True,
                timeout: float | None = None, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
        self._command_index += 1
        prefix = f"{self._command_index:03d}_{name}"
        log_path = self.command_dir / f"{prefix}.log"
        try:
            completed = subprocess.run(
                list(cmd), cwd=ROOT, text=True, capture_output=True,
                timeout=timeout, env=env,
            )
        except subprocess.TimeoutExpired as exc:
            stdout = exc.stdout.decode(errors="replace") if isinstance(exc.stdout, bytes) else (exc.stdout or "")
            stderr = exc.stderr.decode(errors="replace") if isinstance(exc.stderr, bytes) else (exc.stderr or "")
            log_path.write_text(
                "$ " + " ".join(cmd) + "\n\n[stdout]\n" + stdout +
                "\n[stderr]\n" + stderr + "\n[timeout]\n",
                encoding="utf-8",
            )
            raise GateFailure(f"{name} timed out; see {log_path}") from exc
        log_path.write_text(
            "$ " + " ".join(cmd) + "\n\n[stdout]\n" + completed.stdout +
            "\n[stderr]\n" + completed.stderr +
            f"\n[exit]={completed.returncode}\n",
            encoding="utf-8",
        )
        if check and completed.returncode != 0:
            raise GateFailure(f"{name} failed (exit {completed.returncode}); see {log_path}")
        return completed

    def compile(self, phase: str, board: Board, sketch: Path,
                *, cpp_defines: Sequence[str] = ()) -> Path:
        output = self.build_dir / phase / board.role / "artifacts"
        build_path = self.build_dir / phase / board.role / "work"
        output.mkdir(parents=True, exist_ok=True)
        source_sidecars_before = set(sketch.glob("*.ino.*.uf2"))
        core_version = PLATFORM / "cores" / board.core / "CoreVersionGenerated.h"
        cpp_extra = f'-include "{core_version}"'
        if cpp_defines:
            cpp_extra += " " + " ".join(f"-D{value}" for value in cpp_defines)
        cmd = [
            "arduino-cli", "compile", "--config-file", str(self.cli_config),
            "--warnings", "all", "--clean", "--build-path", str(build_path),
            "--output-dir", str(output), "--fqbn", board.fqbn,
            "--build-property", f"compiler.path={self.compiler_path}{os.sep}",
            "--build-property", f"compiler.cpp.extra_flags={cpp_extra}",
            "--build-property",
            "recipe.hooks.savehex.postsavehex.1.pattern=/usr/bin/true",
            str(sketch),
        ]
        try:
            self.command(
                f"compile_{phase}_{board.role}", cmd,
                timeout=self.args.compile_timeout,
            )
        finally:
            # The platform's Save Hex hook normally copies a UF2 beside the
            # source sketch. The hook is overridden above; remove only a new
            # exact sidecar as a defensive fallback for older Arduino CLI.
            for sidecar in set(sketch.glob("*.ino.*.uf2")) - source_sidecars_before:
                sidecar.unlink()
        images = sorted(output.glob("*.hex"))
        if len(images) != 1:
            raise GateFailure(f"{phase}/{board.role}: expected one HEX, found {images}")
        return images[0]

    def flash(self, phase: str, board: Board, image: Path, *, erase: bool = True) -> None:
        cmd = [self.args.pyocd, "load", "--no-config", "-W"]
        if erase:
            cmd.extend(["-e", "chip"])
        cmd.extend(["-t", board.target, "-u", board.uid, "--format", "hex", str(image)])
        self.command(f"flash_{phase}_{board.role}", cmd, timeout=self.args.flash_timeout)

    def reset(self, phase: str, board: Board) -> None:
        self.command(
            f"reset_{phase}_{board.role}",
            [self.args.pyocd, "commander", "--no-config", "-W", "-t", board.target,
             "-u", board.uid, "-c", "reset"],
            timeout=self.args.flash_timeout,
        )

    def halt(self, phase: str, board: Board) -> None:
        self.command(
            f"halt_{phase}_{board.role}",
            [self.args.pyocd, "commander", "--no-config", "-W", "-t", board.target,
             "-u", board.uid, "-c", "halt"],
            timeout=self.args.flash_timeout,
        )

    def capture(self, phase: str, boards: Sequence[Board], seconds: float,
                reset_order: Sequence[Board]) -> dict[str, str]:
        capture = SerialCapture(boards)
        capture.start()
        try:
            for board in reset_order:
                capture.clear(board.role)
                self.reset(phase, board)
                time.sleep(0.35)
            time.sleep(seconds)
        finally:
            logs = capture.stop()
            self.save_logs(phase, logs)
        return logs

    def run_phase(self, name: str, action: Callable[[], str]) -> None:
        started = time.monotonic()
        try:
            detail = action()
        except GateFailure as exc:
            self.results.append(PhaseResult(name, "FAIL", time.monotonic() - started, str(exc)))
            self.write_summary()
            raise
        self.results.append(PhaseResult(name, "PASS", time.monotonic() - started, detail))
        self.write_summary()

    def save_logs(self, phase: str, logs: dict[str, str]) -> None:
        for role, output in logs.items():
            (self.logs_dir / f"{phase}_{role}.log").write_text(output, encoding="utf-8")

    def build_environment_phase(self) -> str:
        compiler = self.compiler_path / "arm-none-eabi-g++"
        self.command("compiler_version", [str(compiler), "--version"])
        source_link = (
            self.cli_root / "user" / "hardware" / LOCAL_PACKAGER / "nrf54l15clean"
        )
        if source_link.resolve() != PLATFORM.resolve():
            raise GateFailure(f"isolated source registration points at {source_link.resolve()}")
        for board in (self.l15, self.lm20):
            self.command(
                f"board_details_{board.role}",
                ["arduino-cli", "board", "details", "--config-file",
                 str(self.cli_config), "--fqbn", board.fqbn],
            )
        return (
            f"source {self.source_version} registered with GCC "
            f"{DECLARED_COMPILER_VERSION}"
        )

    @staticmethod
    def require(log: str, marker: str, description: str) -> None:
        if marker not in log:
            raise GateFailure(f"{description}: missing serial marker {marker!r}")

    @staticmethod
    def require_count(log: str, marker: str, minimum: int, description: str) -> None:
        count = log.count(marker)
        if count < minimum:
            raise GateFailure(f"{description}: found {count} {marker!r} markers, need {minimum}")

    @staticmethod
    def require_regex(log: str, pattern: str, description: str) -> re.Match[str]:
        match = re.search(pattern, log, flags=re.MULTILINE)
        if match is None:
            raise GateFailure(f"{description}: missing serial pattern {pattern!r}")
        return match

    @staticmethod
    def reject_fatal(logs: dict[str, str], description: str) -> None:
        for role, text in logs.items():
            for marker in FATAL_MARKERS:
                if marker in text:
                    raise GateFailure(f"{description}: {role} reported {marker!r}")

    @staticmethod
    def since_last(log: str, marker: str, description: str) -> str:
        offset = log.rfind(marker)
        if offset < 0:
            raise GateFailure(f"{description}: missing boot marker {marker!r}")
        return log[offset:]

    def pair_logs_since_boot(self, logs: dict[str, str],
                             description: str) -> dict[str, str]:
        return {
            role: self.since_last(text, "ble_pair ===", f"{description}/{role}")
            for role, text in logs.items()
        }

    def clear_pair_bonds(self, phase: str) -> None:
        self.halt(phase, self.l15)
        self.halt(phase, self.lm20)
        capture = SerialCapture([self.l15, self.lm20])
        capture.start()
        try:
            capture.clear(self.l15.role)
            self.reset(phase, self.l15)
            time.sleep(1.5)
            capture.send(self.l15.role, "clear\n")
            time.sleep(self.args.pair_clear_capture_s)
            self.halt(phase, self.l15)

            capture.clear(self.lm20.role)
            self.reset(phase, self.lm20)
            time.sleep(1.5)
            capture.send(self.lm20.role, "clear\n")
            time.sleep(self.args.pair_clear_capture_s)
            self.halt(phase, self.lm20)
        finally:
            logs = capture.stop()
            self.save_logs(phase, logs)
        for board in (self.l15, self.lm20):
            self.require(logs[board.role], "ble_pair bond-cleared-storage",
                         f"{phase}/{board.role}")
            self.require(logs[board.role], "ble_pair bond-cleared",
                         f"{phase}/{board.role}")

    def request_pair_evidence(self, phase: str, command: str) -> dict[str, str]:
        capture = SerialCapture([self.l15, self.lm20])
        capture.start()
        try:
            capture.send(self.l15.role, command + "\n")
            capture.send(self.lm20.role, command + "\n")
            time.sleep(1.0)
        finally:
            logs = capture.stop()
            self.save_logs(phase, logs)
        return logs

    def boot_phase(self) -> str:
        images = {
            self.l15.role: self.compile("boot", self.l15, CORE_VERSION_PROBE),
            self.lm20.role: self.compile("boot", self.lm20, CORE_VERSION_PROBE),
        }
        self.flash("boot", self.l15, images[self.l15.role])
        self.flash("boot", self.lm20, images[self.lm20.role])
        logs = self.capture("boot", [self.l15, self.lm20], self.args.boot_capture_s,
                            [self.l15, self.lm20])
        for board in (self.l15, self.lm20):
            self.require(logs[board.role], f"Core version: {self.source_version}",
                         f"boot/{board.role}")
            self.require(logs[board.role],
                         f"Core version heartbeat: {self.source_version}",
                         f"boot/{board.role}")
        return f"both boards programmed and reported {self.source_version}"

    def phy_pair_phase(self, *, recovery: bool) -> str:
        phase = "ble_reset_recovery" if recovery else "ble_phy_mtu_dle"
        peripheral = LIBRARY_EXAMPLES / "BLE" / "Connections" / "Ble2MPhyProbe"
        central = LIBRARY_EXAMPLES / "BLE" / "Connections" / "Ble2MPhyCentralProbe"
        peripheral_image = self.compile(phase, self.l15, peripheral)
        central_image = self.compile(phase, self.lm20, central)
        self.flash(phase, self.l15, peripheral_image)
        self.flash(phase, self.lm20, central_image)

        if recovery:
            capture = SerialCapture([self.l15, self.lm20])
            capture.start()
            try:
                self.reset(phase, self.l15)
                time.sleep(0.35)
                self.reset(phase, self.lm20)
                time.sleep(self.args.phy_cycle_s)
                self.reset(phase, self.l15)
                time.sleep(self.args.recovery_capture_s)
            finally:
                logs = capture.stop()
            for role, text in logs.items():
                (self.logs_dir / f"{phase}_{role}.log").write_text(text, encoding="utf-8")
            self.reject_fatal(logs, phase)
            self.require_count(logs[self.lm20.role], "cycle phase: 2M long notify reconfirmed", 2,
                               phase)
            self.require(logs[self.lm20.role], "disconnected", phase)
            return "two complete 2M/1M/2M cycles with a peripheral reset and automatic reconnect"

        logs = self.capture(phase, [self.l15, self.lm20], self.args.phy_cycle_s,
                            [self.l15, self.lm20])
        self.reject_fatal(logs, phase)
        central_log = logs[self.lm20.role]
        self.require(central_log, "request data length 251: queued", phase)
        self.require(central_log, "request mtu 247: queued", phase)
        self.require(central_log, "notifications enabled", phase)
        self.require(central_log, "request 2M phy: queued", phase)
        self.require(central_log, "cycle phase: 1M long notify confirmed", phase)
        self.require(central_log, "cycle phase: 2M long notify reconfirmed", phase)
        self.require(logs[self.l15.role], "cycle phase: 2M return complete", phase)
        return "ATT discovery/CCCD, MTU 247, DLE 251, and 2M/1M/2M long-notify cycle"

    def pair_bond_phase(self) -> str:
        phase = "ble_pair_bond"
        peripheral = LIBRARY_EXAMPLES / "BLE" / "Security" / "BlePairPeripheral"
        central = LIBRARY_EXAMPLES / "BLE" / "Security" / "BlePairCentral"
        peripheral_image = self.compile(phase, self.l15, peripheral)
        central_image = self.compile(phase, self.lm20, central)
        self.flash(phase, self.l15, peripheral_image)
        self.flash(phase, self.lm20, central_image)

        # Preferences persists independently of the application image. Clear
        # both role-specific records through the sketch API before measuring.
        self.clear_pair_bonds(f"{phase}_clear")

        logs = self.pair_logs_since_boot(
            self.capture(phase, [self.l15, self.lm20], self.args.pair_capture_s,
                         [self.l15, self.lm20]),
            phase,
        )
        self.reject_fatal(logs, phase)
        for role, text in logs.items():
            self.require(text, "ble_pair bond-saved", f"{phase}/{role}")
            self.require(text, "encryption=ON", f"{phase}/{role}")
        self.require(logs[self.lm20.role], "ble_pair central: subscribed", phase)
        self.require(logs[self.lm20.role], "ble_pair central: wrote", phase)
        self.require(logs[self.lm20.role], "ble_pair central: notify=", phase)
        self.require(logs[self.l15.role], "ble_pair gatt-write val=", phase)

        reconnect_phase = "ble_pair_bond_reconnect"
        reconnect_logs = self.pair_logs_since_boot(
            self.capture(reconnect_phase, [self.l15, self.lm20],
                         self.args.pair_reconnect_capture_s,
                         [self.l15, self.lm20]),
            reconnect_phase,
        )
        self.reject_fatal(reconnect_logs, reconnect_phase)
        for role, text in reconnect_logs.items():
            # The core can restore a valid retained/flash record before the
            # sketch callback is installed, so require the stable status state
            # rather than the optional callback's serial side effect.
            self.require(text, "conn=0 enc=0 auth=0 bond=1",
                         f"{reconnect_phase}/{role}")
            self.require(text, "encryption=ON", f"{reconnect_phase}/{role}")
            self.require_regex(
                text, r"conn=1 enc=1 auth=0 bond=1",
                f"{reconnect_phase}/{role}",
            )
            if "ble_pair bond-saved" in text:
                raise GateFailure(f"{reconnect_phase}/{role}: peer paired again")
        return "Just Works pairing, retained bond reload, encrypted reconnect, subscription, and write"

    def numeric_comparison_phase(self) -> str:
        phase = "ble_numeric_comparison"
        peripheral = LIBRARY_EXAMPLES / "BLE" / "Security" / "BlePairPeripheral"
        central = LIBRARY_EXAMPLES / "BLE" / "Security" / "BlePairCentral"
        defines = ("BLE_PAIR_USE_NUMERIC_COMPARISON=1",
                   "BLE_PAIR_AUTO_ACCEPT_PROMPTS=1")
        peripheral_image = self.compile(phase, self.l15, peripheral,
                                        cpp_defines=defines)
        central_image = self.compile(phase, self.lm20, central,
                                     cpp_defines=defines)
        self.flash(phase, self.l15, peripheral_image)
        self.flash(phase, self.lm20, central_image)
        self.clear_pair_bonds(f"{phase}_clear")

        logs = self.pair_logs_since_boot(
            self.capture(phase, [self.l15, self.lm20], self.args.pair_capture_s,
                         [self.l15, self.lm20]),
            phase,
        )
        self.reject_fatal(logs, phase)
        values: dict[str, str] = {}
        for board in (self.l15, self.lm20):
            output = logs[board.role]
            match = self.require_regex(
                output,
                r"ble_pair prompt type=numcmp value=([0-9]{6}) auto=1",
                f"{phase}/{board.role}",
            )
            values[board.role] = match.group(1)
            self.require(output, "ble_pair prompt-accepted", f"{phase}/{board.role}")
            self.require(output, "ble_pair bond-saved", f"{phase}/{board.role}")
            self.require(output, "encryption=ON", f"{phase}/{board.role}")
            self.require_regex(
                output, r"conn=1 enc=1 auth=1 bond=1",
                f"{phase}/{board.role}",
            )
        if values[self.l15.role] != values[self.lm20.role]:
            raise GateFailure(f"{phase}: displayed values differ: {values}")
        self.require(logs[self.lm20.role], "ble_pair central: subscribed", phase)
        self.require(logs[self.lm20.role], "ble_pair central: wrote", phase)
        self.require(logs[self.lm20.role], "ble_pair central: notify=", phase)
        self.require(logs[self.l15.role], "ble_pair gatt-write val=", phase)

        reconnect_phase = f"{phase}_reconnect"
        reconnect_logs = self.pair_logs_since_boot(
            self.capture(
                reconnect_phase, [self.l15, self.lm20],
                self.args.pair_reconnect_capture_s, [self.l15, self.lm20],
            ),
            reconnect_phase,
        )
        self.reject_fatal(reconnect_logs, reconnect_phase)
        for board in (self.l15, self.lm20):
            output = reconnect_logs[board.role]
            self.require(output, "conn=0 enc=0 auth=0 bond=1",
                         f"{reconnect_phase}/{board.role}")
            self.require(output, "encryption=ON", f"{reconnect_phase}/{board.role}")
            self.require_regex(
                output, r"conn=1 enc=1 auth=1 bond=1",
                f"{reconnect_phase}/{board.role}",
            )
            if "prompt type=numcmp" in output:
                raise GateFailure(f"{reconnect_phase}/{board.role}: bonded reconnect prompted again")
            if "ble_pair bond-saved" in output:
                raise GateFailure(f"{reconnect_phase}/{board.role}: peer paired again")
        return (
            f"matching six-digit Numeric Comparison value {values[self.l15.role]}, "
            "authenticated traffic, bond save, and prompt-free encrypted reconnect"
        )

    def numeric_comparison_reject_phase(self) -> str:
        phase = "ble_numeric_comparison_reject"
        peripheral = LIBRARY_EXAMPLES / "BLE" / "Security" / "BlePairPeripheral"
        central = LIBRARY_EXAMPLES / "BLE" / "Security" / "BlePairCentral"
        peripheral_image = self.compile(
            phase, self.l15, peripheral,
            cpp_defines=("BLE_PAIR_USE_NUMERIC_COMPARISON=1",
                         "BLE_PAIR_AUTO_ACCEPT_PROMPTS=0",
                         "BLE_PAIR_PROMPT_REPLY_DELAY_MS=1500UL"),
        )
        central_image = self.compile(
            phase, self.lm20, central,
            cpp_defines=("BLE_PAIR_USE_NUMERIC_COMPARISON=1",
                         "BLE_PAIR_AUTO_ACCEPT_PROMPTS=1"),
        )
        self.flash(phase, self.l15, peripheral_image)
        self.flash(phase, self.lm20, central_image)
        self.clear_pair_bonds(f"{phase}_clear")

        logs = self.pair_logs_since_boot(
            self.capture(
                phase, [self.l15, self.lm20],
                self.args.numeric_reject_capture_s, [self.l15, self.lm20],
            ),
            phase,
        )
        peripheral_log = logs[self.l15.role]
        central_log = logs[self.lm20.role]
        peripheral_match = self.require_regex(
            peripheral_log,
            r"ble_pair prompt type=numcmp value=([0-9]{6}) auto=0",
            f"{phase}/{self.l15.role}",
        )
        self.require(peripheral_log, "ble_pair prompt-rejected",
                     f"{phase}/{self.l15.role}")
        central_match = self.require_regex(
            central_log,
            r"ble_pair prompt type=numcmp value=([0-9]{6}) auto=1",
            f"{phase}/{self.lm20.role}",
        )
        self.require(central_log, "ble_pair prompt-accepted",
                     f"{phase}/{self.lm20.role}")
        if peripheral_match.group(1) != central_match.group(1):
            raise GateFailure(
                f"{phase}: displayed values differ: "
                f"{peripheral_match.group(1)} != {central_match.group(1)}"
            )
        for role, output in logs.items():
            for marker in ("FATAL:", "fatal_stage=", "ASSERT_FAIL"):
                if marker in output:
                    raise GateFailure(f"{phase}/{role}: reported {marker!r}")
        if not any("ble_pair pairing-failed" in output for output in logs.values()):
            raise GateFailure(f"{phase}: rejection did not surface a pairing failure")
        for board in (self.l15, self.lm20):
            output = logs[board.role]
            if "encryption=ON" in output or "ble_pair bond-saved" in output:
                raise GateFailure(
                    f"{phase}/{board.role}: rejected comparison encrypted or saved a bond"
                )
            self.require_regex(
                output, r"conn=[01] enc=0 auth=0 bond=0",
                f"{phase}/{board.role}",
            )
        return "responder rejection prevents DHKey completion, encryption, and bond save"

    def oob_pairing_phase(self, mode: int = 0) -> str:
        if mode not in (0, 1, 2):
            raise GateFailure(f"unsupported OOB mode {mode}")
        phase = {
            0: "ble_oob_pairing",
            1: "ble_oob_peripheral_to_central",
            2: "ble_oob_central_to_peripheral",
        }[mode]
        peripheral = LIBRARY_EXAMPLES / "BLE" / "Security" / "BleOobPairPeripheral"
        central = LIBRARY_EXAMPLES / "BLE" / "Security" / "BleOobPairCentral"
        defines = (f"BLE_OOB_MODE={mode}",)
        if mode == 0:
            defines += ("BLE_OOB_REQUEST_DLE=0",)
        if self.args.oob_trace:
            defines += ("NRF54L15_CLEAN_BLE_TRACE=1",)
        peripheral_image = self.compile(
            phase, self.l15, peripheral, cpp_defines=defines,
        )
        central_image = self.compile(
            phase, self.lm20, central, cpp_defines=defines,
        )
        self.flash(phase, self.l15, peripheral_image)
        self.flash(phase, self.lm20, central_image)

        line_pattern = re.compile(
            r"paste_on_peer: peer ([0-9A-Fa-f]{32}) ([0-9A-Fa-f]{32})"
        )
        capture = SerialCapture([self.l15, self.lm20])
        capture.start()
        try:
            capture.clear(self.l15.role)
            self.reset(phase, self.l15)
            time.sleep(0.35)
            capture.clear(self.lm20.role)
            self.reset(phase, self.lm20)
            deadline = time.monotonic() + self.args.oob_exchange_s
            publishers = {
                0: (self.l15, self.lm20),
                1: (self.l15,),
                2: (self.lm20,),
            }[mode]
            oob_records: dict[str, re.Match[str]] = {}
            while time.monotonic() < deadline:
                for board in publishers:
                    match = line_pattern.search(capture.text(board.role))
                    if match is not None:
                        oob_records[board.role] = match
                if len(oob_records) == len(publishers):
                    break
                time.sleep(0.05)
            if len(oob_records) != len(publishers):
                raise GateFailure(f"{phase}: timed out waiting for local OOB record(s)")
            if self.lm20.role in oob_records:
                record = oob_records[self.lm20.role]
                capture.send(
                    self.l15.role,
                    f"peer {record.group(1)} {record.group(2)}\n",
                )
            if self.l15.role in oob_records:
                record = oob_records[self.l15.role]
                capture.send(
                    self.lm20.role,
                    f"peer {record.group(1)} {record.group(2)}\n",
                )
            time.sleep(self.args.oob_capture_s)
        finally:
            logs = capture.stop()
            self.save_logs(phase, logs)
        logs = {
            self.l15.role: self.since_last(
                logs[self.l15.role], "BLE OOB peripheral", f"{phase}/{self.l15.role}"
            ),
            self.lm20.role: self.since_last(
                logs[self.lm20.role], "BLE OOB central", f"{phase}/{self.lm20.role}"
            ),
        }
        self.reject_fatal(logs, phase)
        peer_data_roles = {
            0: (self.l15, self.lm20),
            1: (self.lm20,),
            2: (self.l15,),
        }[mode]
        for board in peer_data_roles:
            self.require(logs[board.role], "Peer OOB data stored",
                         f"{phase}/{board.role}")
        expected_authenticated = "yes" if mode == 0 else "no"
        for board in (self.l15, self.lm20):
            output = logs[board.role]
            self.require(output, "Pair complete status=0x0", f"{phase}/{board.role}")
            self.require(output, "Connection encrypted with OOB pairing",
                         f"{phase}/{board.role}")
            self.require(output,
                         f"OOB mutually authenticated={expected_authenticated}",
                         f"{phase}/{board.role}")
        central_log = logs[self.lm20.role]
        self.require(central_log, "Requesting OOB pairing", phase)
        if central_log.find("Connection encrypted with OOB pairing") < central_log.find(
            "Requesting OOB pairing"
        ):
            raise GateFailure(f"{phase}: encryption reused a bond before the OOB request")
        self.require(logs[self.lm20.role], "BLE UART discovered", phase)
        self.require(logs[self.l15.role], "oob central ", phase)
        self.require(logs[self.lm20.role], "oob peripheral ", phase)
        direction = {
            0: "mutual",
            1: "peripheral-to-central one-way",
            2: "central-to-peripheral one-way",
        }[mode]
        return f"{direction} LE Secure Connections OOB exchange, encryption, and bidirectional UART"

    def rpa_rotation_phase(self) -> str:
        phase = "ble_rpa_rotation"
        advertiser = LIBRARY_EXAMPLES / "BLE" / "Privacy" / "BleResolvablePrivateAddress"
        scanner = LIBRARY_EXAMPLES / "BLE" / "Scanning" / "BleActiveScanner"
        advertiser_image = self.compile(phase, self.l15, advertiser)
        scanner_image = self.compile(phase, self.lm20, scanner)
        self.flash(phase, self.l15, advertiser_image)
        self.flash(phase, self.lm20, scanner_image)
        logs = self.capture(phase, [self.l15, self.lm20], self.args.rpa_capture_s,
                            [self.l15, self.lm20])
        self.reject_fatal(logs, phase)
        advertiser_log = self.since_last(
            logs[self.l15.role], "BleResolvablePrivateAddress", phase,
        )
        for marker in (
            "local_identity=", "local_irk=", "preview_rpa=",
            "preview_resolved=yes", "resolving_list_match=yes",
            "active_rpa=", "privacy_enabled=yes", "result=PASS",
        ):
            self.require(advertiser_log, marker, phase)

        scanner_log = logs[self.lm20.role]
        scanner_start = scanner_log.rfind("BleActiveScanner start")
        if scanner_start < 0:
            raise GateFailure(f"{phase}: scanner did not boot")
        observed: set[str] = set()
        for line in scanner_log[scanner_start:].splitlines():
            if "X54-RPA" not in line:
                continue
            if "adv_addr_type=random" not in line:
                raise GateFailure(f"{phase}: X54-RPA was not reported as random: {line}")
            match = re.search(r"\badvA=([0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5})", line)
            if match is not None:
                observed.add(match.group(1).upper())
        if len(observed) < self.args.rpa_min_addresses:
            raise GateFailure(
                f"{phase}: scanner saw {len(observed)} X54-RPA address(es), "
                f"need {self.args.rpa_min_addresses}: {sorted(observed)}"
            )
        for address in observed:
            if (int(address[:2], 16) & 0xC0) != 0x40:
                raise GateFailure(f"{phase}: observed address is not an RPA: {address}")
        return f"RPA self-test plus over-air rotation across {len(observed)} addresses"

    def privacy_bond_reconnect_phase(self) -> str:
        phase = "ble_privacy_bond"
        peripheral = LIBRARY_EXAMPLES / "BLE" / "Security" / "BlePairPeripheral"
        central = LIBRARY_EXAMPLES / "BLE" / "Security" / "BlePairCentral"
        defines = (
            "BLE_PAIR_USE_PRIVACY=1",
            f"BLE_PAIR_PRIVACY_ROTATION_MS={self.args.privacy_rotation_ms}UL",
            "NRF54L15_CLEAN_BLE_TRACE=1",
        )
        peripheral_image = self.compile(phase, self.l15, peripheral,
                                        cpp_defines=defines)
        central_image = self.compile(phase, self.lm20, central,
                                     cpp_defines=defines)
        self.flash(phase, self.l15, peripheral_image)
        self.flash(phase, self.lm20, central_image)
        self.clear_pair_bonds(f"{phase}_clear")

        logs = self.pair_logs_since_boot(
            self.capture(phase, [self.l15, self.lm20], self.args.pair_capture_s,
                         [self.l15, self.lm20]),
            phase,
        )
        self.reject_fatal(logs, phase)
        initial_identity: dict[str, str] = {}
        initial_rpa: dict[str, str] = {}
        address_pattern = (
            r"ble_pair PRIVACY_ENABLED local_identity="
            r"([0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5}) active_rpa="
            r"([0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5})"
        )
        for board in (self.l15, self.lm20):
            output = logs[board.role]
            match = self.require_regex(output, address_pattern, f"{phase}/{board.role}")
            initial_identity[board.role] = match.group(1).upper()
            initial_rpa[board.role] = match.group(2).upper()
            if initial_rpa[board.role] == initial_identity[board.role]:
                raise GateFailure(f"{phase}/{board.role}: active RPA equals identity")
            if (int(initial_rpa[board.role].rsplit(":", 1)[1], 16) & 0xC0) != 0x40:
                raise GateFailure(
                    f"{phase}/{board.role}: active address is not an RPA: "
                    f"{initial_rpa[board.role]}"
                )
            self.require(output, "ble_pair local_irk=", f"{phase}/{board.role}")
            self.require(output, "ble_pair bond-saved", f"{phase}/{board.role}")
            self.require(output, "encryption=ON", f"{phase}/{board.role}")
        evidence = self.request_pair_evidence(f"{phase}_evidence", "privacy")
        for board in (self.l15, self.lm20):
            self.require_regex(
                evidence[board.role],
                r"ble_pair privacy_evidence id_info=1 id_addr=1 bond_aar=[01] bond_primed=[01]",
                f"{phase}_evidence/{board.role}",
            )

        reconnect_phase = f"{phase}_reconnect"
        reconnect_logs = self.pair_logs_since_boot(
            self.capture(
                reconnect_phase, [self.l15, self.lm20],
                self.args.pair_reconnect_capture_s, [self.l15, self.lm20],
            ),
            reconnect_phase,
        )
        self.reject_fatal(reconnect_logs, reconnect_phase)
        for board in (self.l15, self.lm20):
            output = reconnect_logs[board.role]
            match = self.require_regex(output, address_pattern,
                                       f"{reconnect_phase}/{board.role}")
            identity = match.group(1).upper()
            rpa = match.group(2).upper()
            if identity != initial_identity[board.role]:
                raise GateFailure(
                    f"{reconnect_phase}/{board.role}: identity changed "
                    f"{initial_identity[board.role]} -> {identity}"
                )
            if rpa == initial_rpa[board.role]:
                raise GateFailure(f"{reconnect_phase}/{board.role}: RPA did not rotate")
            if rpa == identity or (int(rpa.rsplit(":", 1)[1], 16) & 0xC0) != 0x40:
                raise GateFailure(
                    f"{reconnect_phase}/{board.role}: active address is not an RPA: {rpa}"
                )
            self.require(output, "conn=0 enc=0 auth=0 bond=1",
                         f"{reconnect_phase}/{board.role}")
            self.require(output, "encryption=ON", f"{reconnect_phase}/{board.role}")
            if "ble_pair bond-saved" in output:
                raise GateFailure(f"{reconnect_phase}/{board.role}: peer paired again")
        reconnect_evidence = self.request_pair_evidence(
            f"{reconnect_phase}_evidence", "privacy"
        )
        for board in (self.l15, self.lm20):
            self.require_regex(
                reconnect_evidence[board.role],
                r"ble_pair privacy_evidence id_info=[01] id_addr=[01] bond_aar=1 bond_primed=1",
                f"{reconnect_phase}_evidence/{board.role}",
            )
        return "stable identities, rotated RPAs, IRK distribution, AAR bond resolution, and encrypted reconnect"

    def system_off_wake_phase(self) -> str:
        phase = "system_off_wake"
        sketch = LIBRARY_EXAMPLES / "LowPower" / "LowPowerGrtcPwmSystemOff"
        l15_image = self.compile(phase, self.l15, sketch)
        lm20_image = self.compile(phase, self.lm20, sketch)
        self.flash(phase, self.l15, l15_image)
        self.flash(phase, self.lm20, lm20_image)
        logs = self.capture(phase, [self.l15, self.lm20], self.args.system_off_capture_s,
                            [self.l15, self.lm20])
        for board in (self.l15, self.lm20):
            text = logs[board.role]
            self.require(text, "LowPowerGrtcPwmSystemOff", f"{phase}/{board.role}")
            self.require(text, "Entering SYSTEM OFF for", f"{phase}/{board.role}")
            self.require(text, "wake_from_grtc_or_off=1", f"{phase}/{board.role}")
        return "both boards entered timed System OFF and rebooted from the GRTC wake source"

    def cs_phase(self) -> str:
        phase = "channel_sounding"
        env = os.environ.copy()
        env.update({
            "CS_INITIATOR_UID": self.l15.uid,
            "CS_INITIATOR_PORT": self.l15.port,
            "CS_INITIATOR_TARGET": self.l15.target,
            "CS_INITIATOR_FQBN": f"{self.l15.fqbn}:clean_ble=on,cpu_freq=128m",
            "CS_REFLECTOR_UID": self.lm20.uid,
            "CS_REFLECTOR_PORT": self.lm20.port,
            "CS_REFLECTOR_TARGET": self.lm20.target,
            "CS_REFLECTOR_FQBN": f"{self.lm20.fqbn}:clean_ble=on,cpu_freq=128m",
            "CS_CAPTURE_SECONDS": str(int(self.args.cs_capture_s)),
            "CS_NEGATIVE_CAPTURE_SECONDS": str(int(self.args.cs_negative_s)),
            "CS_RECOVERY_CAPTURE_SECONDS": str(int(self.args.cs_recovery_s)),
            "CS_MIN_RESULTS": str(self.args.cs_min_results),
            "CS_LOG_DIR": str(self.logs_dir / phase),
            "CS_BUILD_ROOT": str(self.build_dir / phase),
            "CS_PYOCD": self.args.pyocd,
            "CS_ARDUINO_CLI_CONFIG": str(self.cli_config),
            "CS_COMPILER_PATH": str(self.compiler_path),
        })
        self.command(phase, ["bash", str(CS_SCRIPT)], timeout=self.args.cs_timeout, env=env)
        return "positive, silent-reflector rejection, and recovery controller-backed CS checks"

    def write_summary(self) -> None:
        completed_phases = [result.name for result in self.results]
        failed = any(result.status == "FAIL" for result in self.results)
        complete = completed_phases == self.args.expected_phases
        if failed:
            outcome = "FAIL"
        elif complete:
            outcome = "SUBSET_PASS" if self.args.gate_scope == "subset" else "PASS"
        else:
            outcome = "IN_PROGRESS"
        summary = {
            "schema": 2,
            "started_utc": self.started_utc.isoformat(),
            "elapsed_s": round(time.monotonic() - self.started, 3),
            "profile": self.args.profile,
            "scope": self.args.gate_scope,
            "outcome": outcome,
            "expected_phases": self.args.expected_phases,
            "completed_phases": completed_phases,
            "source_version": self.source_version,
            "git_revision": self.git_revision,
            "git_dirty": self.git_dirty,
            "source_platform": str(PLATFORM),
            "arduino_cli_config": str(self.cli_config),
            "compiler_path": str(self.compiler_path),
            "boards": [asdict(self.l15), asdict(self.lm20)],
            "results": [asdict(result) for result in self.results],
            "limitations": [
                "No Bluetooth PTS/BQB qualification is performed by this gate.",
                "No phone/desktop interoperability or current measurement is performed by this gate.",
                "The CS phase validates the released standalone LE CS Test pair, not connected-ACL CS.",
            ],
        }
        (self.outdir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n",
                                                   encoding="utf-8")


def run_checked(cmd: Sequence[str]) -> str:
    completed = subprocess.run(list(cmd), cwd=ROOT, text=True, capture_output=True)
    if completed.returncode != 0:
        raise GateFailure(
            f"{' '.join(cmd)} failed (exit {completed.returncode}): {completed.stderr.strip()}"
        )
    return completed.stdout


def find_compiler_path(override: Path | None) -> Path:
    if override is not None:
        candidate = override.expanduser().resolve()
        if (candidate / "arm-none-eabi-g++").is_file():
            return candidate
        raise GateFailure(f"ARM compiler bin directory is invalid: {candidate}")
    env_override = os.environ.get("NRF54_ARM_GCC_PATH")
    if env_override:
        return find_compiler_path(Path(env_override))
    roots = (
        Path.home() / ".arduino15" / "packages" / "arduino" / "tools" /
        "arm-none-eabi-gcc",
        Path.home() / "Library" / "Arduino15" / "packages" / "arduino" /
        "tools" / "arm-none-eabi-gcc",
        Path(os.environ.get("LOCALAPPDATA", "")) / "Arduino15" / "packages" /
        "arduino" / "tools" / "arm-none-eabi-gcc",
    )
    for root in roots:
        candidate = root / DECLARED_COMPILER_VERSION / "bin"
        if (candidate / "arm-none-eabi-g++").is_file():
            return candidate
    raise GateFailure(
        f"ARM compiler {DECLARED_COMPILER_VERSION} not found; pass --compiler-path"
    )


def port_for_uid(uid: str) -> str:
    candidates = sorted(Path("/dev/serial/by-id").glob(f"*{uid}*if02"))
    if len(candidates) != 1:
        raise GateFailure(f"expected one CDC serial port for {uid}, found {candidates}")
    path = candidates[0]
    if not path.exists():
        raise GateFailure(f"serial link for {uid} is unavailable: {path}")
    return str(path)


def discover_boards(args: argparse.Namespace) -> tuple[Board, Board]:
    if args.l15_uid and args.lm20_uid:
        l15_uid, lm20_uid = args.l15_uid.upper(), args.lm20_uid.upper()
    else:
        output = run_checked([args.pyocd, "list"])
        l15_uid = ""
        lm20_uid = ""
        for line in output.splitlines():
            match = re.search(r"\b([0-9A-F]{8,})\b", line, flags=re.IGNORECASE)
            if not match:
                continue
            uid = match.group(1).upper()
            normalized = line.upper()
            if "LM20" in normalized:
                lm20_uid = uid
            elif "NRF54" in normalized:
                l15_uid = uid
        if args.l15_uid:
            l15_uid = args.l15_uid.upper()
        if args.lm20_uid:
            lm20_uid = args.lm20_uid.upper()
        if not l15_uid or not lm20_uid:
            raise GateFailure(
                "could not identify one nRF54L15 and one nRF54LM20 probe; "
                "pass --l15-uid and --lm20-uid explicitly"
            )
    if l15_uid == lm20_uid:
        raise GateFailure("the two board UIDs must differ")
    return (
        Board("l15", l15_uid, TARGET_L15, FQBN_L15,
              args.l15_port or port_for_uid(l15_uid), "nrf54l15"),
        Board("lm20", lm20_uid, TARGET_LM20, FQBN_LM20,
              args.lm20_port or port_for_uid(lm20_uid), "nrf54lm20b"),
    )


def require_tools(pyocd: str) -> None:
    for tool in ("arduino-cli", pyocd):
        if shutil.which(tool) is None:
            raise GateFailure(f"required tool not found on PATH: {tool}")
    if not CS_SCRIPT.is_file():
        raise GateFailure(f"missing CS gate script: {CS_SCRIPT}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=("smoke", "full"), default="smoke")
    parser.add_argument(
        "--only-phase", action="append", choices=PHASE_NAMES,
        help="run only this phase; repeat to select multiple phases",
    )
    parser.add_argument("--outdir", type=Path, help="artifact directory; must not already exist")
    parser.add_argument("--pyocd", default="pyocd")
    parser.add_argument("--compiler-path", type=Path,
                        help=f"ARM GCC {DECLARED_COMPILER_VERSION} bin directory")
    parser.add_argument("--l15-uid", help="override detected nRF54L15 CMSIS-DAP UID")
    parser.add_argument("--lm20-uid", help="override detected nRF54LM20 CMSIS-DAP UID")
    parser.add_argument("--l15-port", help="override detected nRF54L15 CDC port")
    parser.add_argument("--lm20-port", help="override detected nRF54LM20 CDC port")
    parser.add_argument("--boot-capture-s", type=float, default=3.0)
    parser.add_argument("--phy-cycle-s", type=float, default=28.0)
    parser.add_argument("--recovery-capture-s", type=float, default=34.0)
    parser.add_argument("--pair-capture-s", type=float, default=35.0)
    parser.add_argument("--pair-clear-capture-s", type=float, default=2.0)
    parser.add_argument("--pair-reconnect-capture-s", type=float, default=25.0)
    parser.add_argument("--numeric-reject-capture-s", type=float, default=18.0)
    parser.add_argument("--oob-exchange-s", type=float, default=10.0)
    parser.add_argument("--oob-capture-s", type=float, default=35.0)
    parser.add_argument("--oob-trace", action="store_true",
                        help="compile OOB phases with deferred core BLE tracing")
    parser.add_argument("--rpa-capture-s", type=float, default=72.0)
    parser.add_argument("--rpa-min-addresses", type=int, default=2)
    parser.add_argument("--privacy-rotation-ms", type=int, default=5000)
    parser.add_argument("--system-off-capture-s", type=float, default=13.0)
    parser.add_argument("--cs-capture-s", type=float, default=35.0)
    parser.add_argument("--cs-negative-s", type=float, default=12.0)
    parser.add_argument("--cs-recovery-s", type=float, default=35.0)
    parser.add_argument("--cs-min-results", type=int, default=3)
    parser.add_argument("--compile-timeout", type=float, default=300.0)
    parser.add_argument("--flash-timeout", type=float, default=120.0)
    parser.add_argument("--cs-timeout", type=float, default=480.0)
    args = parser.parse_args()
    for name in ("boot_capture_s", "phy_cycle_s", "recovery_capture_s", "pair_capture_s",
                 "pair_clear_capture_s",
                 "pair_reconnect_capture_s", "numeric_reject_capture_s",
                 "oob_exchange_s", "oob_capture_s",
                 "rpa_capture_s", "system_off_capture_s", "cs_capture_s",
                 "cs_negative_s", "cs_recovery_s"):
        if getattr(args, name) <= 0:
            parser.error(f"--{name.replace('_', '-')} must be positive")
    if args.cs_min_results < 1:
        parser.error("--cs-min-results must be positive")
    if args.rpa_min_addresses < 2:
        parser.error("--rpa-min-addresses must be at least 2")
    if args.privacy_rotation_ms < 1000:
        parser.error("--privacy-rotation-ms must be at least 1000")
    return args


def main() -> int:
    args = parse_args()
    try:
        require_tools(args.pyocd)
        boards = discover_boards(args)
        selected = set(args.only_phase or ())
        if selected:
            args.gate_scope = "subset"
            args.expected_phases = [name for name in PHASE_NAMES if name in selected]
        elif args.profile == "full":
            args.gate_scope = "full"
            args.expected_phases = list(PHASE_NAMES)
        else:
            args.gate_scope = "smoke"
            args.expected_phases = [
                "build_environment",
                "boot",
                "ble_phy_mtu_dle",
                "channel_sounding",
            ]
        gate = ReleaseGate(args, boards)
        print(f"two-board gate artifacts: {gate.outdir}")
        print(f"l15 uid={gate.l15.uid} port={gate.l15.port}")
        print(f"lm20 uid={gate.lm20.uid} port={gate.lm20.port}")

        def should_run(name: str, *, full_only: bool = False) -> bool:
            if selected:
                return name in selected
            return not full_only or args.profile == "full"

        if should_run("build_environment"):
            gate.run_phase("build_environment", gate.build_environment_phase)
        if should_run("boot"):
            gate.run_phase("boot", gate.boot_phase)
        if should_run("ble_phy_mtu_dle"):
            gate.run_phase("ble_phy_mtu_dle", lambda: gate.phy_pair_phase(recovery=False))
        if should_run("ble_pair_bond", full_only=True):
            gate.run_phase("ble_pair_bond", gate.pair_bond_phase)
        if should_run("ble_numeric_comparison", full_only=True):
            gate.run_phase("ble_numeric_comparison", gate.numeric_comparison_phase)
        if should_run("ble_numeric_comparison_reject", full_only=True):
            gate.run_phase("ble_numeric_comparison_reject",
                           gate.numeric_comparison_reject_phase)
        if should_run("ble_oob_pairing", full_only=True):
            gate.run_phase("ble_oob_pairing", gate.oob_pairing_phase)
        if should_run("ble_oob_peripheral_to_central", full_only=True):
            gate.run_phase(
                "ble_oob_peripheral_to_central",
                lambda: gate.oob_pairing_phase(1),
            )
        if should_run("ble_oob_central_to_peripheral", full_only=True):
            gate.run_phase(
                "ble_oob_central_to_peripheral",
                lambda: gate.oob_pairing_phase(2),
            )
        if should_run("ble_rpa_rotation", full_only=True):
            gate.run_phase("ble_rpa_rotation", gate.rpa_rotation_phase)
        if should_run("ble_privacy_bond", full_only=True):
            gate.run_phase("ble_privacy_bond", gate.privacy_bond_reconnect_phase)
        if should_run("ble_reset_recovery", full_only=True):
            gate.run_phase("ble_reset_recovery", lambda: gate.phy_pair_phase(recovery=True))
        if should_run("system_off_wake", full_only=True):
            gate.run_phase("system_off_wake", gate.system_off_wake_phase)
        if should_run("channel_sounding"):
            gate.run_phase("channel_sounding", gate.cs_phase)
        completed_phases = [result.name for result in gate.results]
        if completed_phases != args.expected_phases:
            raise GateFailure(
                f"phase coverage mismatch: expected {args.expected_phases}, "
                f"completed {completed_phases}"
            )
        gate.write_summary()
        outcome = "SUBSET_PASS" if args.gate_scope == "subset" else "PASS"
        print(f"two_board_release_gate={outcome}")
        return 0
    except (GateFailure, subprocess.TimeoutExpired) as exc:
        print(f"two_board_release_gate=FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
