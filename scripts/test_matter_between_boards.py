#!/usr/bin/env python3
"""Two-board staged Matter/Thread validation runner.

This runner flashes a Matter example from the local checkout, captures serial
output from both boards, and summarizes the Thread/Matter readiness keys printed
by the examples.

It intentionally uses a temporary local vendor namespace, so Arduino CLI cannot
silently compile against an installed Board Manager release.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass, field
from typing import Dict, Iterable, List, Optional


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
PLATFORM_ROOT = REPO_ROOT / "hardware" / "nrf54l15clean" / "nrf54l15clean"
EXAMPLES_ROOT = (
    PLATFORM_ROOT
    / "libraries"
    / "Nrf54L15-Clean-Implementation"
    / "examples"
)
DEFAULT_SKETCH = (
    EXAMPLES_ROOT
    / "Matter"
    / "MatterOnNetworkOnOffLightNodeDemo"
)


@dataclass
class BoardCapture:
    port: str
    lines: List[str] = field(default_factory=list)
    values: Dict[str, str] = field(default_factory=dict)

    def update(self, line: str) -> None:
        self.lines.append(line)
        parsed = parse_key_value(line)
        if parsed is None:
            return
        key, value = parsed
        self.values[key] = value

    def value_is_one(self, *keys: str) -> bool:
        return any(self.values.get(key) == "1" for key in keys)

    @property
    def saw_thread_attached(self) -> bool:
        return self.value_is_one(
            "thread_attached",
            "readiness_thread_attached",
            "thread_attach_summary_attached",
        )

    @property
    def saw_matter_ready(self) -> bool:
        return self.value_is_one("ready", "discovery_ready")

    @property
    def saw_srp_enabled(self) -> bool:
        return self.value_is_one("discovery_srp_client", "discovery_publish_srp_client")

    @property
    def saw_any_matter_line(self) -> bool:
        return any(line.startswith(("matter_node_demo ", "matter_cmd_demo ")) for line in self.lines)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Flash and monitor two staged Matter/Thread boards."
    )
    parser.add_argument("--arduino-cli", default="arduino-cli")
    parser.add_argument(
        "--board",
        default="xiao_nrf54l15",
        help="Board ID from boards.txt, e.g. xiao_nrf54l15 or xiao_nrf54lm20b.",
    )
    parser.add_argument(
        "--boards",
        nargs="+",
        default=None,
        help=(
            "Optional per-port board IDs. Use one value for all ports or one "
            "value per --ports entry."
        ),
    )
    parser.add_argument(
        "--options",
        default="clean_thread=stage,clean_matter=stage",
        help="Board options appended to the temporary local FQBN.",
    )
    parser.add_argument(
        "--ports",
        nargs="+",
        default=["/dev/ttyACM0", "/dev/ttyACM1"],
        help="Upload ports. Pass one or two ports.",
    )
    parser.add_argument(
        "--monitor-ports",
        nargs="+",
        default=None,
        help="Serial monitor ports. Defaults to --ports.",
    )
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--duration-sec", type=float, default=180.0)
    parser.add_argument("--settle-sec", type=float, default=2.0)
    parser.add_argument("--upload-timeout-sec", type=float, default=240.0)
    parser.add_argument("--sketch", type=pathlib.Path, default=DEFAULT_SKETCH)
    parser.add_argument("--vendor", default="localnrf54")
    parser.add_argument("--work-dir", type=pathlib.Path, default=None)
    parser.add_argument("--skip-upload", action="store_true")
    parser.add_argument("--keep", action="store_true")
    parser.add_argument(
        "--no-require-ready",
        action="store_true",
        help="Only require Thread attach, not Matter ready/discovery-ready.",
    )
    return parser.parse_args()


def fqbn(vendor: str, board: str, options: str) -> str:
    return f"{vendor}:nrf54l15clean:{board}:{options}"


def board_for_port(args: argparse.Namespace, index: int) -> str:
    if args.boards is None:
        return args.board
    if len(args.boards) == 1:
        return args.boards[0]
    if len(args.boards) != len(args.ports):
        raise SystemExit("--boards must contain one board ID or match --ports length")
    return args.boards[index]


def parse_key_value(line: str) -> Optional[tuple[str, str]]:
    match = re.search(r"matter_(?:node|cmd)_demo\s+([A-Za-z0-9_]+)=([^\s]+)", line)
    if match:
        return match.group(1), match.group(2)
    return None


def prepare_work_dir(args: argparse.Namespace) -> pathlib.Path:
    if args.work_dir is not None:
        work_dir = args.work_dir.resolve()
        shutil.rmtree(work_dir, ignore_errors=True)
        work_dir.mkdir(parents=True)
        return work_dir
    return pathlib.Path(tempfile.mkdtemp(prefix="nrf54-matter-2board-"))


def copy_platform(work_dir: pathlib.Path, vendor: str) -> None:
    if not PLATFORM_ROOT.is_dir():
        raise SystemExit(f"Missing platform root: {PLATFORM_ROOT}")

    platform_copy = work_dir / "sketchbook" / "hardware" / vendor / "nrf54l15clean"
    platform_copy.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(
        PLATFORM_ROOT,
        platform_copy,
        ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "build", ".git"),
    )


def write_cli_config(work_dir: pathlib.Path) -> pathlib.Path:
    config_path = work_dir / "arduino-cli.yaml"
    config_path.write_text(
        "\n".join(
            [
                "directories:",
                "  data: /home/lolren/.arduino15",
                "  downloads: /home/lolren/.arduino15/staging",
                f"  user: {work_dir / 'sketchbook'}",
                "",
            ]
        ),
        encoding="utf-8",
    )
    return config_path


def safe_name(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value.strip("/")) or "unknown"


def flash_board(
    args: argparse.Namespace,
    config_path: pathlib.Path,
    board_fqbn: str,
    work_dir: pathlib.Path,
    port: str,
) -> bool:
    build_path = work_dir / "build" / safe_name(port)
    log_path = work_dir / "logs" / f"{safe_name(port)}.compile-upload.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.rmtree(build_path, ignore_errors=True)

    command = [
        args.arduino_cli,
        "--config-file",
        str(config_path),
        "compile",
        "--upload",
        "-p",
        port,
        "--fqbn",
        board_fqbn,
        str(args.sketch),
        "--build-path",
        str(build_path),
    ]

    print(f"flashing {port} with {board_fqbn}", flush=True)
    result = subprocess.run(
        command,
        cwd=str(REPO_ROOT),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=args.upload_timeout_sec,
    )
    log_path.write_text(result.stdout, encoding="utf-8", errors="replace")
    if result.returncode == 0:
        print(f"upload ok: {port}", flush=True)
        return True

    print(f"upload failed: {port}; full log at {log_path}", flush=True)
    print("\n".join(result.stdout.splitlines()[-30:]), flush=True)
    return False


def open_serial(port: str, baud: int):
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required: python3 -m pip install --user pyserial") from exc

    return serial.Serial(port, baud, timeout=0.2)


def monitor_ports(
    monitor_ports: Iterable[str],
    baud: int,
    duration_sec: float,
    work_dir: pathlib.Path,
) -> Dict[str, BoardCapture]:
    captures = {port: BoardCapture(port=port) for port in monitor_ports}
    serials = {}
    for port in monitor_ports:
        try:
            serials[port] = open_serial(port, baud)
            serials[port].reset_input_buffer()
        except Exception as exc:
            print(f"serial open failed: {port}: {exc}", flush=True)

    deadline = time.monotonic() + duration_sec
    try:
        while time.monotonic() < deadline:
            for port, ser in list(serials.items()):
                try:
                    raw = ser.readline()
                except Exception as exc:
                    print(f"serial read failed: {port}: {exc}", flush=True)
                    serials.pop(port, None)
                    continue
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                captures[port].update(line)
                print(f"{port}: {line}", flush=True)
    finally:
        for ser in serials.values():
            try:
                ser.close()
            except Exception:
                pass

    log_dir = work_dir / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    for port, capture in captures.items():
        (log_dir / f"{safe_name(port)}.serial.log").write_text(
            "\n".join(capture.lines) + ("\n" if capture.lines else ""),
            encoding="utf-8",
            errors="replace",
        )
    return captures


def summarize(captures: Dict[str, BoardCapture], require_ready: bool) -> bool:
    print("\nsummary", flush=True)
    ok = True
    for port, capture in captures.items():
        attached = capture.saw_thread_attached
        ready = capture.saw_matter_ready
        srp = capture.saw_srp_enabled
        matter_lines = capture.saw_any_matter_line
        role = capture.values.get("thread_role", "unknown")
        blocker = capture.values.get("readiness_blocker", capture.values.get("discovery_blocker", "n/a"))
        print(
            f"{port}: lines={len(capture.lines)} role={role} "
            f"attached={int(attached)} ready={int(ready)} srp={int(srp)} "
            f"matter_lines={int(matter_lines)} blocker={blocker}",
            flush=True,
        )
        if not matter_lines or not attached:
            ok = False
        if require_ready and not ready:
            ok = False
    print("PASS" if ok else "FAIL", flush=True)
    return ok


def main() -> int:
    args = parse_args()
    if not args.sketch.is_dir():
        raise SystemExit(f"Sketch directory not found: {args.sketch}")

    work_dir = prepare_work_dir(args)
    try:
        copy_platform(work_dir, args.vendor)
        config_path = write_cli_config(work_dir)
        monitor_ports_list = args.monitor_ports or args.ports

        if not args.skip_upload:
            for index, port in enumerate(args.ports):
                board_fqbn = fqbn(
                    args.vendor,
                    board_for_port(args, index),
                    args.options,
                )
                if not flash_board(args, config_path, board_fqbn, work_dir, port):
                    return 1

        if args.settle_sec > 0:
            time.sleep(args.settle_sec)

        captures = monitor_ports(
            monitor_ports_list,
            args.baud,
            args.duration_sec,
            work_dir,
        )
        ok = summarize(captures, require_ready=not args.no_require_ready)
        if args.keep:
            print(f"kept work dir: {work_dir}", flush=True)
        return 0 if ok else 1
    finally:
        if not args.keep:
            shutil.rmtree(work_dir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
