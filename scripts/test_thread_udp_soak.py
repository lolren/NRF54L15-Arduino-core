#!/usr/bin/env python3
"""Two-board ThreadExperimentalUdpSoak host runner."""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime
from typing import Dict, List, Optional

try:
    import serial
except ImportError as exc:
    raise SystemExit(
        "pyserial is required: python3 -m pip install --user pyserial"
    ) from exc


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
HARDWARE_ROOT = REPO_ROOT / "hardware" / "nrf54l15clean"
THREAD_EXAMPLES = (
    HARDWARE_ROOT
    / "nrf54l15clean"
    / "libraries"
    / "Nrf54L15-Clean-Implementation"
    / "examples"
    / "Thread"
)

DEFAULT_FQBN = "nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage"
DEFAULT_EXAMPLE = THREAD_EXAMPLES / "ThreadExperimentalUdpSoak"
DEFAULT_SKETCHBOOK = pathlib.Path("/tmp/nrf54-thread-udp-soak-sketchbook")
DEFAULT_LOG_ROOT = REPO_ROOT / "build" / "thread-udp-soak-validation"
PAYLOAD_SIZES = [8, 16, 31, 63, 95, 127, 191, 255, 512]
SAFE_UNICAST_SIZES = [8, 16, 31, 63, 95]
SAFE_MULTICAST_SIZES = [8, 16, 31, 63]


@dataclass
class BoardResults:
    port: str
    role: str = "unknown"
    rloc16: str = ""
    begin_ok: Optional[bool] = None
    multicast_subscribed: Optional[bool] = None
    dataset_hex: Optional[str] = None
    unicast: Dict[int, str] = field(default_factory=dict)
    multicast: Dict[int, str] = field(default_factory=dict)
    done: bool = False
    lines: List[str] = field(default_factory=list)


def ensure_sketchbook(sketchbook: pathlib.Path) -> None:
    if not HARDWARE_ROOT.is_dir():
        raise SystemExit(f"Missing local hardware root: {HARDWARE_ROOT}")

    hardware_dir = sketchbook / "hardware"
    link = hardware_dir / "nrf54l15clean"
    hardware_dir.mkdir(parents=True, exist_ok=True)

    if link.is_symlink():
        target = pathlib.Path(os.readlink(link))
        if target != HARDWARE_ROOT:
            link.unlink()
            link.symlink_to(HARDWARE_ROOT, target_is_directory=True)
    elif link.exists():
        raise SystemExit(
            f"{link} exists and is not the repo symlink. Move it or pass "
            "--sketchbook to use a clean temp directory."
        )
    else:
        link.symlink_to(HARDWARE_ROOT, target_is_directory=True)


def command_env(sketchbook: pathlib.Path) -> Dict[str, str]:
    env = os.environ.copy()
    env["ARDUINO_DIRECTORIES_USER"] = str(sketchbook)
    return env


def safe_port_name(port: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", port.strip("/")) or "unknown"


def flash_board(
    arduino_cli: str,
    port: str,
    fqbn: str,
    example: pathlib.Path,
    env: Dict[str, str],
    log_dir: pathlib.Path,
) -> bool:
    print(f"flashing {port}")
    build_path = log_dir / "upload-build" / safe_port_name(port)
    shutil.rmtree(build_path, ignore_errors=True)
    log_file = log_dir / f"{safe_port_name(port)}.compile-upload.log"
    log_file.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        [
            arduino_cli,
            "compile",
            "--upload",
            "-p",
            port,
            "-b",
            fqbn,
            "--build-path",
            str(build_path),
            str(example),
        ],
        capture_output=True,
        cwd=str(REPO_ROOT),
        env=env,
        text=True,
        timeout=240,
    )
    log_file.write_text(
        result.stdout + result.stderr, encoding="utf-8", errors="replace"
    )
    if result.returncode == 0:
        return True
    print(result.stdout[-1000:])
    print(result.stderr[-1000:])
    print(f"full log: {log_file}")
    return False


def parse_line(line: str, result: BoardResults) -> None:
    match = re.search(r"role=([A-Za-z0-9_]+)", line)
    if match:
        result.role = match.group(1)
    match = re.search(r"rloc16=0x([0-9a-fA-F]+)", line)
    if match:
        result.rloc16 = match.group(1)
    match = re.search(r"begin_ok=(\d)", line)
    if match:
        result.begin_ok = match.group(1) == "1"
    match = re.search(r"mcast_subscribed=(\d)", line)
    if match:
        result.multicast_subscribed = match.group(1) == "1"
    match = re.search(r"dataset_hex=([0-9a-fA-F]+)", line)
    if match:
        result.dataset_hex = match.group(1)

    match = re.search(r"soak_pass\s+len=(\d+)", line)
    if match:
        result.unicast[int(match.group(1))] = "pass"
    match = re.search(r"soak_fail\s+len=(\d+)\s+mode=([A-Za-z0-9_]+)", line)
    if match:
        result.unicast[int(match.group(1))] = f"fail_{match.group(2)}"
    match = re.search(r"soak_mcast_pass\s+len=(\d+)", line)
    if match:
        result.multicast[int(match.group(1))] = "pass"
    match = re.search(r"soak_mcast_fail\s+len=(\d+)\s+mode=([A-Za-z0-9_]+)", line)
    if match:
        result.multicast[int(match.group(1))] = f"fail_{match.group(2)}"
    if line.startswith("soak_done"):
        result.done = True


def read_available(ser: serial.Serial, result: BoardResults, seconds: float) -> List[str]:
    lines: List[str] = []
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()
        if not line:
            continue
        lines.append(line)
        result.lines.append(line)
        parse_line(line, result)
    return lines


def print_matrix(title: str, values: Dict[int, str]) -> None:
    print(f"\n{title}")
    for size in PAYLOAD_SIZES:
        print(f"  {size:>3}: {values.get(size, '---')}")


def parse_size_list(value: str, default: List[int]) -> List[int]:
    text = value.strip().lower()
    if text in ("", "default", "safe"):
        return list(default)
    if text == "all":
        return list(PAYLOAD_SIZES)
    if text == "none":
        return []

    sizes: List[int] = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        sizes.append(int(part, 0))
    return sizes


def validate_required(title: str, values: Dict[int, str], required: List[int]) -> bool:
    ok = True
    if not required:
        print(f"\n{title}: no required sizes")
        return True

    print(f"\n{title} required sizes")
    for size in required:
        state = values.get(size, "---")
        passed = state == "pass"
        ok = ok and passed
        marker = "PASS" if passed else "FAIL"
        print(f"  {size:>3}: {marker} ({state})")
    return ok


def write_serial_logs(log_dir: pathlib.Path, board1: BoardResults, board2: BoardResults) -> None:
    log_dir.mkdir(parents=True, exist_ok=True)
    for board in (board1, board2):
        path = log_dir / f"{safe_port_name(board.port)}.serial.log"
        path.write_text("\n".join(board.lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port1", default="/dev/ttyACM0")
    parser.add_argument("--port2", default="/dev/ttyACM1")
    parser.add_argument("--fqbn", default=DEFAULT_FQBN)
    parser.add_argument("--example", type=pathlib.Path, default=DEFAULT_EXAMPLE)
    parser.add_argument("--arduino-cli", default="arduino-cli")
    parser.add_argument("--sketchbook", type=pathlib.Path, default=DEFAULT_SKETCHBOOK)
    parser.add_argument("--log-root", type=pathlib.Path, default=DEFAULT_LOG_ROOT)
    parser.add_argument("--skip-flash", action="store_true")
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument(
        "--require-unicast",
        default="safe",
        help="Required unicast payload sizes: safe, all, none, or comma list.",
    )
    parser.add_argument(
        "--require-multicast",
        default="safe",
        help="Required multicast payload sizes: safe, all, none, or comma list.",
    )
    parser.add_argument(
        "--require-fragmentation",
        action="store_true",
        help="Require every payload size, including fragmented 127+ byte packets.",
    )
    parser.add_argument("--dump-lines", action="store_true")
    args = parser.parse_args()

    ensure_sketchbook(args.sketchbook)
    env = command_env(args.sketchbook)
    run_dir = args.log_root / datetime.now().strftime("%Y%m%d-%H%M%S")
    required_unicast = (
        list(PAYLOAD_SIZES)
        if args.require_fragmentation
        else parse_size_list(args.require_unicast, SAFE_UNICAST_SIZES)
    )
    required_multicast = (
        list(PAYLOAD_SIZES)
        if args.require_fragmentation
        else parse_size_list(args.require_multicast, SAFE_MULTICAST_SIZES)
    )

    if not args.skip_flash:
        if not flash_board(args.arduino_cli, args.port1, args.fqbn, args.example, env, run_dir):
            return 1
        if not flash_board(args.arduino_cli, args.port2, args.fqbn, args.example, env, run_dir):
            return 1
        time.sleep(5)

    board1 = BoardResults(args.port1)
    board2 = BoardResults(args.port2)
    ser1 = serial.Serial(args.port1, 115200, timeout=0.3)
    ser2 = serial.Serial(args.port2, 115200, timeout=0.3)
    try:
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            for line in read_available(ser1, board1, 0.5):
                if args.dump_lines:
                    print(f"{args.port1}: {line}")
            for line in read_available(ser2, board2, 0.5):
                if args.dump_lines:
                    print(f"{args.port2}: {line}")
            if board1.done or board2.done:
                break
    finally:
        ser1.close()
        ser2.close()

    write_serial_logs(run_dir, board1, board2)
    sender = max(
        (board1, board2),
        key=lambda board: len(board.unicast) + len(board.multicast),
    )
    receiver = board2 if sender is board1 else board1
    print(f"\nsender={sender.port} role={sender.role} rloc16=0x{sender.rloc16}")
    print(f"receiver={receiver.port} role={receiver.role} rloc16=0x{receiver.rloc16}")
    print(f"logs={run_dir}")
    print_matrix("Unicast", sender.unicast)
    print_matrix("Multicast", sender.multicast)
    if sender.dataset_hex:
        print(f"\ndataset_hex={sender.dataset_hex}")

    unicast_ok = validate_required("Unicast", sender.unicast, required_unicast)
    multicast_ok = validate_required("Multicast", sender.multicast, required_multicast)
    boot_ok = sender.begin_ok is not False and receiver.begin_ok is not False
    return 0 if boot_ok and unicast_ok and multicast_ok else 2


if __name__ == "__main__":
    sys.exit(main())
