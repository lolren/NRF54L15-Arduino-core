#!/usr/bin/env python3
"""Two-board ThreadExperimentalUdpSoak host runner.

The boards may use different FQBNs.  Optional CMSIS-DAP UIDs bind each logical
board to its stable CDC port so Linux ``/dev/ttyACM*`` enumeration changes do
not cause the wrong image to be uploaded.
"""

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
from typing import Dict, List, Optional, Tuple

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    serial = None
    list_ports = None


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
HARDWARE_ROOT = REPO_ROOT / "hardware" / "nrf54l15clean"
PLATFORM_ROOT = HARDWARE_ROOT / "nrf54l15clean"
LOCAL_VENDOR = "localnrf54"
THREAD_EXAMPLES = (
    HARDWARE_ROOT
    / "nrf54l15clean"
    / "libraries"
    / "Nrf54L15-Clean-Implementation"
    / "examples"
    / "Thread"
)

DEFAULT_FQBN = "nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage"
DEFAULT_PORT1 = "/dev/ttyACM0"
DEFAULT_PORT2 = "/dev/ttyACM1"
DEFAULT_EXAMPLE = THREAD_EXAMPLES / "ThreadExperimentalUdpSoak"
DEFAULT_SKETCHBOOK = pathlib.Path("/tmp/nrf54-thread-udp-soak-sketchbook")
DEFAULT_LOG_ROOT = REPO_ROOT / "build" / "thread-udp-soak-validation"
PAYLOAD_SIZES = [8, 16, 31, 63, 95, 127, 191, 255, 512]
SAFE_UNICAST_SIZES = [8, 16, 31, 63, 95]
SAFE_DOWNLINK_SIZES = [8, 16, 31, 63, 95]
SAFE_MULTICAST_SIZES = [8, 16, 31, 63]


@dataclass
class BoardResults:
    port: str
    role: str = "unknown"
    rloc16: str = ""
    partition_id: str = ""
    begin_ok: Optional[bool] = None
    multicast_subscribed: Optional[bool] = None
    dataset_hex: Optional[str] = None
    unicast: Dict[int, str] = field(default_factory=dict)
    downlink: Dict[int, str] = field(default_factory=dict)
    multicast: Dict[int, str] = field(default_factory=dict)
    done: bool = False
    lines: List[str] = field(default_factory=list)


@dataclass(frozen=True)
class BoardTarget:
    label: str
    port: str
    fqbn: str
    uid: str = ""


def require_pyserial() -> None:
    if serial is None or list_ports is None:
        raise RuntimeError(
            "pyserial is required for hardware capture: "
            "python3 -m pip install --user pyserial"
        )


def normalize_uid(uid: Optional[str]) -> str:
    return (uid or "").strip().upper()


def canonical_port(port: str) -> str:
    return os.path.realpath(os.path.expanduser(port))


def detect_port_uid(port: str) -> str:
    """Return the CMSIS-DAP serial for a CDC port, or an empty string."""

    canonical = canonical_port(port)
    for info in list_ports.comports(include_links=True):
        if canonical_port(info.device) != canonical:
            continue
        if info.serial_number:
            return normalize_uid(info.serial_number)

    path = pathlib.Path(port)
    match = re.search(r"CMSIS-DAP_([0-9A-Fa-f]+)-if\d+$", path.name)
    if match:
        return normalize_uid(match.group(1))

    if shutil.which("udevadm"):
        result = subprocess.run(
            ["udevadm", "info", "-q", "property", "-n", port],
            capture_output=True,
            text=True,
            check=False,
        )
        for line in result.stdout.splitlines():
            if line.startswith("ID_SERIAL_SHORT="):
                return normalize_uid(line.split("=", 1)[1])
    return ""


def port_for_uid(uid: str) -> str:
    expected = normalize_uid(uid)
    if not expected:
        raise ValueError("CMSIS-DAP UID must not be empty")

    by_id = pathlib.Path("/dev/serial/by-id")
    if by_id.is_dir():
        matches = [
            path
            for path in sorted(by_id.iterdir())
            if expected in path.name.upper()
            and re.search(r"-if0*2$", path.name, flags=re.IGNORECASE)
            and path.exists()
        ]
        if len(matches) == 1:
            return str(matches[0])
        if len(matches) > 1:
            raise ValueError(
                f"UID {expected} has multiple CDC serial links: "
                + ", ".join(str(path) for path in matches)
            )

    matches_by_device: Dict[str, str] = {}
    for info in list_ports.comports(include_links=True):
        if normalize_uid(info.serial_number) == expected:
            matches_by_device[canonical_port(info.device)] = info.device
    if len(matches_by_device) == 1:
        return next(iter(matches_by_device.values()))
    if len(matches_by_device) > 1:
        raise ValueError(
            f"UID {expected} has multiple CDC serial devices: "
            + ", ".join(sorted(matches_by_device.values()))
        )
    raise ValueError(f"no CDC serial port found for CMSIS-DAP UID {expected}")


def resolve_target(
    label: str,
    port: Optional[str],
    uid: Optional[str],
    fqbn: str,
    fallback_port: str,
) -> BoardTarget:
    expected_uid = normalize_uid(uid)
    resolved_port = port or (
        port_for_uid(expected_uid) if expected_uid else fallback_port
    )
    actual_uid = detect_port_uid(resolved_port)
    if expected_uid and actual_uid and actual_uid != expected_uid:
        raise ValueError(
            f"{label} port {resolved_port} belongs to UID {actual_uid}, "
            f"not requested UID {expected_uid}"
        )
    if expected_uid and not actual_uid:
        raise ValueError(
            f"could not verify UID {expected_uid} for {label} port {resolved_port}"
        )
    if not fqbn.strip():
        raise ValueError(f"{label} FQBN must not be empty")
    return BoardTarget(label, resolved_port, fqbn.strip(), expected_uid or actual_uid)


def validate_targets(board1: BoardTarget, board2: BoardTarget) -> None:
    if canonical_port(board1.port) == canonical_port(board2.port):
        raise ValueError("board 1 and board 2 resolve to the same serial port")
    if board1.uid and board2.uid and board1.uid == board2.uid:
        raise ValueError("board 1 and board 2 resolve to the same CMSIS-DAP UID")


def localize_fqbn(fqbn: str, vendor: str = LOCAL_VENDOR) -> str:
    parts = fqbn.strip().split(":", 2)
    if len(parts) != 3 or not all(parts):
        raise ValueError(f"invalid FQBN: {fqbn!r}")
    return f"{vendor}:{parts[1]}:{parts[2]}"


def verify_target_identity(board: BoardTarget) -> None:
    if not pathlib.Path(board.port).exists():
        raise ValueError(f"{board.label} serial port is unavailable: {board.port}")
    if not board.uid:
        return
    actual_uid = detect_port_uid(board.port)
    if actual_uid != board.uid:
        raise ValueError(
            f"{board.label} identity changed: {board.port} is UID "
            f"{actual_uid or 'unknown'}, expected {board.uid}"
        )


def ensure_sketchbook(sketchbook: pathlib.Path) -> None:
    if not PLATFORM_ROOT.is_dir():
        raise SystemExit(f"Missing local platform root: {PLATFORM_ROOT}")

    hardware_dir = sketchbook / "hardware"
    vendor_dir = hardware_dir / LOCAL_VENDOR
    link = vendor_dir / "nrf54l15clean"
    vendor_dir.mkdir(parents=True, exist_ok=True)

    if link.is_symlink():
        link.unlink()
    elif link.exists():
        shutil.rmtree(link)
    shutil.copytree(
        PLATFORM_ROOT,
        link,
        ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "build", ".git"),
    )


def command_env(sketchbook: pathlib.Path) -> Dict[str, str]:
    env = os.environ.copy()
    env["ARDUINO_DIRECTORIES_USER"] = str(sketchbook)
    return env


def safe_port_name(port: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", port.strip("/")) or "unknown"


def flash_board(
    arduino_cli: str,
    board: BoardTarget,
    example: pathlib.Path,
    env: Dict[str, str],
    log_dir: pathlib.Path,
) -> bool:
    verify_target_identity(board)
    identity = f" uid={board.uid}" if board.uid else ""
    print(f"flashing {board.label} port={board.port}{identity} fqbn={board.fqbn}")
    build_path = log_dir / "upload-build" / safe_port_name(board.port)
    shutil.rmtree(build_path, ignore_errors=True)
    log_file = log_dir / f"{safe_port_name(board.port)}.compile-upload.log"
    log_file.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        [
            arduino_cli,
            "compile",
            "--upload",
            "-p",
            board.port,
            "-b",
            board.fqbn,
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
    if line.startswith("soak_reset"):
        result.unicast.clear()
        result.downlink.clear()
        result.multicast.clear()
        result.done = False

    match = re.search(r"role=([A-Za-z0-9_]+)", line)
    if match:
        result.role = match.group(1)
    match = re.search(r"rloc16=0x([0-9a-fA-F]+)", line)
    if match:
        result.rloc16 = match.group(1)
    match = re.search(r"part=0x([0-9a-fA-F]+)", line)
    if match:
        result.partition_id = match.group(1)
    match = re.search(r"begin_ok=(\d)", line)
    if match:
        result.begin_ok = match.group(1) == "1"
    match = re.search(r"mcast_sub(?:scribed)?=(\d)", line)
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
    match = re.search(r"soak_downlink_pass\s+len=(\d+)", line)
    if match:
        result.downlink[int(match.group(1))] = "pass"
    match = re.search(r"soak_downlink_fail\s+len=(\d+)\s+mode=([A-Za-z0-9_]+)", line)
    if match:
        result.downlink[int(match.group(1))] = f"fail_{match.group(2)}"
    match = re.search(
        r"soak_result\s+len=(\d+)\s+uplink=([A-Za-z0-9_]+)\s+downlink=([A-Za-z0-9_]+)\s+multicast=([A-Za-z0-9_]+)",
        line,
    )
    if match:
        length = int(match.group(1))
        uplink_state = match.group(2)
        downlink_state = match.group(3)
        multicast_state = match.group(4)
        if uplink_state != "unknown":
            result.unicast[length] = (
                "pass" if uplink_state == "pass" else f"fail_{uplink_state}"
            )
        if downlink_state != "unknown":
            result.downlink[length] = (
                "pass" if downlink_state == "pass" else f"fail_{downlink_state}"
            )
        if multicast_state != "unknown":
            result.multicast[length] = (
                "pass" if multicast_state == "pass" else f"fail_{multicast_state}"
            )
    match = re.search(
        r"soak_result\s+len=(\d+)\s+unicast=([A-Za-z0-9_]+)\s+multicast=([A-Za-z0-9_]+)",
        line,
    )
    if match:
        length = int(match.group(1))
        unicast_state = match.group(2)
        multicast_state = match.group(3)
        if unicast_state != "unknown":
            result.unicast[length] = (
                "pass" if unicast_state == "pass" else f"fail_{unicast_state}"
            )
        if multicast_state != "unknown":
            result.multicast[length] = (
                "pass" if multicast_state == "pass" else f"fail_{multicast_state}"
            )
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
        size = int(part, 0)
        if size not in PAYLOAD_SIZES:
            raise ValueError(
                f"unsupported payload size {size}; choose from "
                + ",".join(str(item) for item in PAYLOAD_SIZES)
            )
        if size not in sizes:
            sizes.append(size)
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


def identify_roles(
    board1: BoardResults, board2: BoardResults
) -> Tuple[Optional[BoardResults], Optional[BoardResults], str]:
    boards = (board1, board2)
    leaders = [board for board in boards if board.role == "leader"]
    peers = [board for board in boards if board.role in ("child", "router")]
    if len(leaders) != 1 or len(peers) != 1 or leaders[0] is peers[0]:
        return None, None, (
            "expected exactly one leader and one child/router, got "
            f"{board1.role} and {board2.role}"
        )
    return leaders[0], peers[0], ""


def validate_topology(
    board1: BoardResults, board2: BoardResults
) -> Tuple[bool, Optional[BoardResults], Optional[BoardResults]]:
    leader, peer, role_error = identify_roles(board1, board2)
    if role_error:
        print(f"\nTopology: FAIL ({role_error})")
        return False, None, None

    assert leader is not None and peer is not None
    errors: List[str] = []
    if not board1.rloc16 or not board2.rloc16:
        errors.append("both RLOC16 values were not observed")
    elif int(board1.rloc16, 16) == int(board2.rloc16, 16):
        errors.append("boards reported the same RLOC16")
    if not board1.partition_id or not board2.partition_id:
        errors.append("both Thread partition IDs were not observed")
    elif int(board1.partition_id, 16) != int(board2.partition_id, 16):
        errors.append("boards joined different Thread partitions")
    if (
        board1.dataset_hex
        and board2.dataset_hex
        and board1.dataset_hex.lower() != board2.dataset_hex.lower()
    ):
        errors.append("boards reported different active datasets")
    if board1.begin_ok is not True or board2.begin_ok is not True:
        errors.append("successful Thread begin was not observed on both boards")
    if not peer.done:
        errors.append("child/router did not report soak_done")

    if errors:
        print("\nTopology: FAIL (" + "; ".join(errors) + ")")
        return False, leader, peer

    print(
        "\nTopology: PASS "
        f"leader={leader.port} peer={peer.port} partition=0x{board1.partition_id}"
    )
    return True, leader, peer


def validate_run(
    board1: BoardResults,
    board2: BoardResults,
    required_unicast: List[int],
    required_downlink: List[int],
    required_multicast: List[int],
) -> bool:
    topology_ok, leader, peer = validate_topology(board1, board2)
    if leader is None or peer is None:
        return False

    # Results are intentionally not merged: each direction must be recorded by
    # the board that transmitted it, otherwise one serial stream could mask a
    # missing or failed reverse path on the other board.
    print_matrix("Uplink child/router-to-leader", peer.unicast)
    print_matrix("Downlink leader-to-child/router", leader.downlink)
    print_matrix("Multicast child/router-to-leader", peer.multicast)
    unicast_ok = validate_required(
        "Uplink child/router-to-leader", peer.unicast, required_unicast
    )
    downlink_ok = validate_required(
        "Downlink leader-to-child/router", leader.downlink, required_downlink
    )
    multicast_ok = validate_required(
        "Multicast child/router-to-leader", peer.multicast, required_multicast
    )
    return topology_ok and unicast_ok and downlink_ok and multicast_ok


def write_serial_logs(
    log_dir: pathlib.Path, board1: BoardResults, board2: BoardResults
) -> None:
    log_dir.mkdir(parents=True, exist_ok=True)
    for board in (board1, board2):
        path = log_dir / f"{safe_port_name(board.port)}.serial.log"
        path.write_text("\n".join(board.lines) + "\n", encoding="utf-8")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--port1",
        help=f"Board 1 CDC port (default: UID lookup or {DEFAULT_PORT1}).",
    )
    parser.add_argument(
        "--port2",
        help=f"Board 2 CDC port (default: UID lookup or {DEFAULT_PORT2}).",
    )
    parser.add_argument("--uid1", help="Expected board 1 CMSIS-DAP UID.")
    parser.add_argument("--uid2", help="Expected board 2 CMSIS-DAP UID.")
    parser.add_argument(
        "--fqbn",
        default=DEFAULT_FQBN,
        help="Common FQBN used when --fqbn1/--fqbn2 is omitted.",
    )
    parser.add_argument("--fqbn1", help="Board 1 FQBN (overrides --fqbn).")
    parser.add_argument("--fqbn2", help="Board 2 FQBN (overrides --fqbn).")
    parser.add_argument("--example", type=pathlib.Path, default=DEFAULT_EXAMPLE)
    parser.add_argument("--arduino-cli", default="arduino-cli")
    parser.add_argument("--sketchbook", type=pathlib.Path, default=DEFAULT_SKETCHBOOK)
    parser.add_argument("--log-root", type=pathlib.Path, default=DEFAULT_LOG_ROOT)
    parser.add_argument("--skip-flash", action="store_true")
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument("--post-upload-delay", type=float, default=0.5)
    parser.add_argument(
        "--completion-grace",
        type=float,
        default=2.0,
        help="Capture time after soak_done so the peer can emit its final matrix.",
    )
    parser.add_argument(
        "--require-unicast",
        default="safe",
        help="Required unicast payload sizes: safe, all, none, or comma list.",
    )
    parser.add_argument(
        "--require-downlink",
        default="safe",
        help="Required leader-to-child payload sizes: safe, all, none, or comma list.",
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
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    try:
        require_pyserial()
    except RuntimeError as exc:
        parser.error(str(exc))

    try:
        board1_target = resolve_target(
            "board1", args.port1, args.uid1,
            localize_fqbn(args.fqbn1 or args.fqbn), DEFAULT_PORT1
        )
        board2_target = resolve_target(
            "board2", args.port2, args.uid2,
            localize_fqbn(args.fqbn2 or args.fqbn), DEFAULT_PORT2
        )
        validate_targets(board1_target, board2_target)
        required_unicast = (
            list(PAYLOAD_SIZES)
            if args.require_fragmentation
            else parse_size_list(args.require_unicast, SAFE_UNICAST_SIZES)
        )
        required_downlink = (
            list(PAYLOAD_SIZES)
            if args.require_fragmentation
            else parse_size_list(args.require_downlink, SAFE_DOWNLINK_SIZES)
        )
        required_multicast = (
            list(PAYLOAD_SIZES)
            if args.require_fragmentation
            else parse_size_list(args.require_multicast, SAFE_MULTICAST_SIZES)
        )
    except ValueError as exc:
        parser.error(str(exc))

    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.post_upload_delay < 0:
        parser.error("--post-upload-delay must not be negative")
    if args.completion_grace < 0:
        parser.error("--completion-grace must not be negative")

    ensure_sketchbook(args.sketchbook)
    env = command_env(args.sketchbook)
    run_dir = args.log_root / datetime.now().strftime("%Y%m%d-%H%M%S")

    for board in (board1_target, board2_target):
        identity = f" uid={board.uid}" if board.uid else ""
        print(f"{board.label}: port={board.port}{identity} fqbn={board.fqbn}")

    if not args.skip_flash:
        if not flash_board(
            args.arduino_cli, board1_target, args.example, env, run_dir
        ):
            return 1
        if not flash_board(
            args.arduino_cli, board2_target, args.example, env, run_dir
        ):
            return 1
        time.sleep(args.post_upload_delay)

    try:
        verify_target_identity(board1_target)
        verify_target_identity(board2_target)
    except ValueError as exc:
        parser.error(str(exc))

    board1 = BoardResults(board1_target.port)
    board2 = BoardResults(board2_target.port)
    ser1 = serial.Serial(board1_target.port, 115200, timeout=0.3)
    ser2 = serial.Serial(board2_target.port, 115200, timeout=0.3)
    try:
        ser1.reset_input_buffer()
        ser2.reset_input_buffer()
        deadline = time.monotonic() + args.timeout
        completion_deadline: Optional[float] = None
        while time.monotonic() < deadline:
            for line in read_available(ser1, board1, 0.5):
                if args.dump_lines:
                    print(f"{board1_target.port}: {line}")
            for line in read_available(ser2, board2, 0.5):
                if args.dump_lines:
                    print(f"{board2_target.port}: {line}")
            if (board1.done or board2.done) and completion_deadline is None:
                completion_deadline = time.monotonic() + args.completion_grace
            if (
                completion_deadline is not None
                and time.monotonic() >= completion_deadline
            ):
                break
    finally:
        ser1.close()
        ser2.close()

    write_serial_logs(run_dir, board1, board2)
    print(
        f"\nboard1={board1.port} role={board1.role} "
        f"rloc16=0x{board1.rloc16} partition=0x{board1.partition_id}"
    )
    print(
        f"board2={board2.port} role={board2.role} "
        f"rloc16=0x{board2.rloc16} partition=0x{board2.partition_id}"
    )
    print(f"logs={run_dir}")
    dataset_hex = board1.dataset_hex or board2.dataset_hex
    if dataset_hex:
        print(f"\ndataset_hex={dataset_hex}")

    return (
        0
        if validate_run(
            board1,
            board2,
            required_unicast,
            required_downlink,
            required_multicast,
        )
        else 2
    )


if __name__ == "__main__":
    sys.exit(main())
