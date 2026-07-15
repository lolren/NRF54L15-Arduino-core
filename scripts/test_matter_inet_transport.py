#!/usr/bin/env python3
"""Flash and validate the CHIP Inet adapter across two Thread boards."""

from __future__ import annotations

import argparse
import pathlib
import re
import time
from dataclasses import dataclass, field
from datetime import datetime
from typing import List, Optional, Tuple

try:
    import serial
except ImportError:
    serial = None

from test_thread_udp_soak import (
    BoardTarget,
    command_env,
    flash_board,
    resolve_target,
    safe_port_name,
    validate_targets,
    verify_target_identity,
)


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
IMPLEMENTATION = (
    REPO_ROOT
    / "hardware/nrf54l15clean/nrf54l15clean/libraries"
    / "Nrf54L15-Clean-Implementation"
)
DEFAULT_EXAMPLE = IMPLEMENTATION / "examples/Chip/ChipPhase5TransportTest"
DEFAULT_SKETCHBOOK = pathlib.Path("/tmp/nrf54-matter-inet-sketchbook")
DEFAULT_LOG_ROOT = REPO_ROOT / "build/matter-inet-transport-validation"
LOCAL_VENDOR = "localnrf54"
LOCAL_ARCHITECTURE = "nrf54l15clean"
LOCAL_PLATFORM = REPO_ROOT / "hardware/nrf54l15clean/nrf54l15clean"
DEFAULT_FQBN1 = (
    "localnrf54:nrf54l15clean:xiao_nrf54l15:"
    "clean_thread=stage,clean_matter=stage"
)
DEFAULT_FQBN2 = (
    "localnrf54:nrf54l15clean:xiao_nrf54lm20b:"
    "clean_thread=stage,clean_matter=stage"
)
PAYLOAD_SIZES = (8, 64, 512, 960, 1200)
EXPECTED_TRANSPORT_MODES = {
    8: "unicast",
    64: "unicast",
    512: "unicast",
    960: "unicast",
    1200: "unicast",
}
EXPECTED_DATASET_HEX = (
    "0E080000000000010001000300000F4A0300000F350600040001000002081122334455"
    "6677880708FD5415C0DE00000005101032547698BADCFE0123456789ABCDEF030A4E72"
    "663534537461676501025D6A0410A54C8D11723F90BE4A6218D4CE07395B0C0402A0"
    "F67B"
)


def require_pyserial() -> None:
    if serial is None:
        raise RuntimeError(
            "pyserial is required for hardware capture: "
            "python3 -m pip install --user pyserial"
        )


@dataclass
class InetResults:
    port: str
    role: str = "unknown"
    rloc16: str = ""
    partition_id: str = ""
    begin_ok: Optional[bool] = None
    role_mode: str = "unknown"
    wipe: Optional[bool] = None
    dataset_ok: Optional[bool] = None
    dataset_match: Optional[bool] = None
    dataset_hex: str = ""
    radio_snapshot_seen: bool = False
    radio_state: Optional[int] = None
    radio_enabled: Optional[bool] = None
    radio_ready: Optional[bool] = None
    radio_channel: Optional[int] = None
    tx_requests: Optional[int] = None
    tx_done: Optional[int] = None
    tx_error: Optional[int] = None
    tx_acked: Optional[bool] = None
    tx_length: Optional[int] = None
    tx_header: str = ""
    rx_polls: Optional[int] = None
    rx_done: Optional[int] = None
    rx_filtered: Optional[int] = None
    rx_crc_errors: Optional[int] = None
    rx_invalid_lengths: Optional[int] = None
    rx_length: Optional[int] = None
    rx_header: str = ""
    discovery_passed: bool = False
    discovery_failure: str = ""
    passes: set[int] = field(default_factory=set)
    pass_modes: dict[int, str] = field(default_factory=dict)
    failures: dict[int, str] = field(default_factory=dict)
    done: bool = False
    reported_passes: Optional[int] = None
    reported_failures: Optional[int] = None
    lines: List[str] = field(default_factory=list)


def local_fqbn(fqbn: str) -> str:
    """Force a board selector to use the checkout-backed local vendor."""

    fields = fqbn.split(":", 2)
    if len(fields) != 3 or fields[1] != LOCAL_ARCHITECTURE:
        raise ValueError(
            "FQBN must select the nrf54l15clean architecture: " + fqbn
        )
    return f"{LOCAL_VENDOR}:{fields[1]}:{fields[2]}"


def configured_role(fqbn: str) -> str:
    """Return the role selected by the Phase5 board-family compile macro."""

    fields = fqbn.split(":", 3)
    if len(fields) < 3:
        raise ValueError("invalid FQBN: " + fqbn)
    return "child" if "lm20" in fields[2].lower() else "leader"


def validate_role_pair(first_fqbn: str, second_fqbn: str) -> None:
    roles = {configured_role(first_fqbn), configured_role(second_fqbn)}
    if roles != {"leader", "child"}:
        raise ValueError(
            "Phase5 requires one nRF54L15 leader and one nRF54LM20 child"
        )


def ensure_local_platform(sketchbook: pathlib.Path) -> None:
    """Expose this checkout through Arduino's vendor/architecture layout."""

    if not LOCAL_PLATFORM.is_dir():
        raise SystemExit(f"Missing local platform: {LOCAL_PLATFORM}")
    link = sketchbook / "hardware" / LOCAL_VENDOR / LOCAL_ARCHITECTURE
    link.parent.mkdir(parents=True, exist_ok=True)
    expected = LOCAL_PLATFORM.resolve()
    if link.is_symlink():
        if link.resolve(strict=False) != expected:
            link.unlink()
            link.symlink_to(expected, target_is_directory=True)
    elif link.exists():
        raise SystemExit(
            f"{link} exists and is not the repo symlink. Move it or pass "
            "--sketchbook to use a clean temp directory."
        )
    else:
        link.symlink_to(expected, target_is_directory=True)


def parse_line(line: str, result: InetResults) -> None:
    role = re.search(r"role=([A-Za-z0-9_]+)", line)
    if role:
        result.role = role.group(1)
    rloc = re.search(r"rloc16=0x([0-9A-Fa-f]+)", line)
    if rloc:
        result.rloc16 = rloc.group(1)
    partition = re.search(r"part=0x([0-9A-Fa-f]+)", line)
    if partition:
        result.partition_id = partition.group(1)
    begin = re.search(r"begin_ok=(\d)", line)
    if begin:
        result.begin_ok = begin.group(1) == "1"
    role_mode = re.search(r"role_mode=(leader|child)", line)
    if role_mode:
        result.role_mode = role_mode.group(1)
    wipe = re.search(r"wipe=(\d)", line)
    if wipe:
        result.wipe = wipe.group(1) == "1"
    dataset_ok = re.search(r"dataset_ok=(\d)", line)
    if dataset_ok:
        result.dataset_ok = dataset_ok.group(1) == "1"
    dataset_match = re.search(r"dataset_match=(\d)", line)
    if dataset_match:
        result.dataset_match = dataset_match.group(1) == "1"
    dataset_hex = re.search(r"dataset_hex=([0-9A-Fa-f]+)", line)
    if dataset_hex:
        result.dataset_hex = dataset_hex.group(1).upper()
    if line.startswith("inet_radio "):
        result.radio_snapshot_seen = (
            result.radio_snapshot_seen or "snapshot=1" in line
        )
        radio_fields = (
            ("state", "radio_state", int),
            ("enabled", "radio_enabled", lambda value: value == "1"),
            ("ready", "radio_ready", lambda value: value == "1"),
            ("ch", "radio_channel", int),
            ("tx_req", "tx_requests", int),
            ("tx_done", "tx_done", int),
            ("tx_err", "tx_error", int),
            ("tx_ack", "tx_acked", lambda value: value == "1"),
            ("tx_len", "tx_length", int),
        )
        for key, attribute, convert in radio_fields:
            field_match = re.search(rf"(?:^|\s){key}=(\d+)", line)
            if field_match:
                setattr(result, attribute, convert(field_match.group(1)))
        header = re.search(r"(?:^|\s)tx_hdr=([0-9A-Fa-f]+)", line)
        if header:
            result.tx_header = header.group(1).upper()
    if line.startswith("inet_radio_rx "):
        rx_fields = (
            ("poll", "rx_polls"),
            ("done", "rx_done"),
            ("filter", "rx_filtered"),
            ("crc", "rx_crc_errors"),
            ("invalid", "rx_invalid_lengths"),
            ("len", "rx_length"),
        )
        for key, attribute in rx_fields:
            field_match = re.search(rf"(?:^|\s){key}=(\d+)", line)
            if field_match:
                setattr(result, attribute, int(field_match.group(1)))
        header = re.search(r"(?:^|\s)hdr=([0-9A-Fa-f]+)", line)
        if header:
            result.rx_header = header.group(1).upper()
    if line.startswith("inet_discovery_pass mode=multicast"):
        result.discovery_passed = True
        result.discovery_failure = ""
    discovery_failed = re.search(
        r"inet_discovery_fail reason=([A-Za-z0-9_-]+)", line
    )
    if discovery_failed:
        result.discovery_failure = discovery_failed.group(1)
        result.discovery_passed = False
    passed = re.search(
        r"inet_pass len=(\d+) mode=(multicast|unicast)", line
    )
    if passed:
        length = int(passed.group(1))
        result.passes.add(length)
        result.pass_modes[length] = passed.group(2)
        result.failures.pop(length, None)
    failed = re.search(
        r"inet_fail len=(\d+) mode=(multicast|unicast) "
        r"reason=([A-Za-z0-9_-]+)",
        line,
    )
    if failed:
        length = int(failed.group(1))
        result.failures[length] = failed.group(3)
        result.pass_modes.pop(length, None)
        result.passes.discard(length)
    if line.startswith("inet_done"):
        result.done = True
        counts = re.search(r"pass=(\d+)\s+fail=(\d+)", line)
        if counts:
            result.reported_passes = int(counts.group(1))
            result.reported_failures = int(counts.group(2))


def identify_roles(
    first: InetResults, second: InetResults
) -> Tuple[Optional[InetResults], Optional[InetResults]]:
    boards = (first, second)
    leaders = [board for board in boards if board.role == "leader"]
    peers = [board for board in boards if board.role in ("child", "router")]
    if len(leaders) != 1 or len(peers) != 1 or leaders[0] is peers[0]:
        return None, None
    return leaders[0], peers[0]


def validate(first: InetResults, second: InetResults) -> bool:
    leader, peer = identify_roles(first, second)
    errors: List[str] = []
    if leader is None or peer is None:
        errors.append(f"invalid roles: {first.role}, {second.role}")
    if first.begin_ok is not True or second.begin_ok is not True:
        errors.append("Thread begin did not succeed on both boards")
    if {first.role_mode, second.role_mode} != {"leader", "child"}:
        errors.append(
            f"invalid configured role modes: {first.role_mode}, {second.role_mode}"
        )
    for result in (first, second):
        if result.wipe is not True:
            errors.append(f"{result.port} did not confirm destructive settings wipe")
        if result.dataset_ok is not True or result.dataset_match is not True:
            errors.append(f"{result.port} did not confirm the fixed dataset")
        if result.dataset_hex != EXPECTED_DATASET_HEX:
            errors.append(f"{result.port} reported an unexpected dataset")
        if result.role_mode == "leader" and result.role != "leader":
            errors.append(f"{result.port} did not reach its fixed leader role")
        if result.role_mode == "child" and result.role != "child":
            errors.append(f"{result.port} did not reach its fixed child role")
        if not result.radio_snapshot_seen:
            errors.append(f"{result.port} did not report radio diagnostics")
        if result.radio_enabled is not True or result.radio_ready is not True:
            errors.append(f"{result.port} radio backend was not ready")
        if result.radio_channel != 15:
            errors.append(
                f"{result.port} radio was not listening on channel 15: "
                f"{result.radio_channel}"
            )
    if not first.partition_id or first.partition_id != second.partition_id:
        errors.append("boards did not report the same Thread partition")
    if not first.rloc16 or not second.rloc16 or first.rloc16 == second.rloc16:
        errors.append("boards did not report distinct RLOC16 values")
    if peer is not None:
        if not peer.discovery_passed or peer.discovery_failure:
            errors.append(
                "multicast discovery did not pass: "
                f"{peer.discovery_failure or 'missing'}"
            )
        missing = sorted(set(PAYLOAD_SIZES) - peer.passes)
        if missing:
            errors.append("missing payload passes: " + ",".join(map(str, missing)))
        wrong_modes = {
            length: peer.pass_modes.get(length)
            for length, expected in EXPECTED_TRANSPORT_MODES.items()
            if peer.pass_modes.get(length) != expected
        }
        if wrong_modes:
            errors.append(f"unexpected payload transport modes: {wrong_modes}")
        if peer.failures:
            errors.append(f"payload failures: {peer.failures}")
        if not peer.done:
            errors.append("sender did not report inet_done")
        if peer.reported_passes != len(PAYLOAD_SIZES):
            errors.append(
                "sender completion pass count was not "
                f"{len(PAYLOAD_SIZES)}: {peer.reported_passes}"
            )
        if peer.reported_failures != 0:
            errors.append(
                f"sender completion reported failures: {peer.reported_failures}"
            )

    if errors:
        print("FAIL: " + "; ".join(errors))
        return False
    assert leader is not None and peer is not None
    print(
        "PASS: CHIP Inet transport "
        f"leader={leader.port} peer={peer.port} "
        f"partition=0x{first.partition_id} "
        "multicast=discovery unicast=8,64,512,960,1200"
    )
    return True


def write_logs(log_dir: pathlib.Path, *results: InetResults) -> None:
    log_dir.mkdir(parents=True, exist_ok=True)
    for result in results:
        (log_dir / f"{safe_port_name(result.port)}.serial.log").write_text(
            "\n".join(result.lines) + "\n", encoding="utf-8"
        )


def monitor(
    first_target: BoardTarget,
    second_target: BoardTarget,
    timeout: float,
    completion_grace: float,
    dump_lines: bool,
) -> Tuple[InetResults, InetResults]:
    verify_target_identity(first_target)
    verify_target_identity(second_target)
    first = InetResults(first_target.port)
    second = InetResults(second_target.port)
    serials = (
        (serial.Serial(first_target.port, 115200, timeout=0.1), first),
        (serial.Serial(second_target.port, 115200, timeout=0.1), second),
    )
    for handle, _ in serials:
        handle.reset_input_buffer()

    deadline = time.monotonic() + timeout
    completed_at: Optional[float] = None
    try:
        while time.monotonic() < deadline:
            for handle, result in serials:
                raw = handle.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                result.lines.append(line)
                parse_line(line, result)
                if dump_lines:
                    print(f"{result.port}: {line}")
            if first.done or second.done:
                if completed_at is None:
                    completed_at = time.monotonic()
                elif time.monotonic() - completed_at >= completion_grace:
                    break
    finally:
        for handle, _ in serials:
            handle.close()
    return first, second


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port1")
    parser.add_argument("--port2")
    parser.add_argument("--uid1")
    parser.add_argument("--uid2")
    parser.add_argument("--fqbn1", default=DEFAULT_FQBN1)
    parser.add_argument("--fqbn2", default=DEFAULT_FQBN2)
    parser.add_argument("--example", type=pathlib.Path, default=DEFAULT_EXAMPLE)
    parser.add_argument("--arduino-cli", default="arduino-cli")
    parser.add_argument("--sketchbook", type=pathlib.Path, default=DEFAULT_SKETCHBOOK)
    parser.add_argument("--log-root", type=pathlib.Path, default=DEFAULT_LOG_ROOT)
    parser.add_argument("--skip-flash", action="store_true")
    parser.add_argument("--timeout", type=float, default=240.0)
    parser.add_argument("--completion-grace", type=float, default=4.0)
    parser.add_argument("--post-upload-delay", type=float, default=0.5)
    parser.add_argument("--dump-lines", action="store_true")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        require_pyserial()
    except RuntimeError as exc:
        parser.error(str(exc))
    if args.timeout <= 0 or args.completion_grace < 0 or args.post_upload_delay < 0:
        parser.error("timeouts must be positive and delays must not be negative")
    if not args.example.is_dir():
        parser.error(f"missing sketch: {args.example}")

    try:
        first_target = resolve_target(
            "board1", args.port1, args.uid1, local_fqbn(args.fqbn1),
            "/dev/ttyACM0"
        )
        second_target = resolve_target(
            "board2", args.port2, args.uid2, local_fqbn(args.fqbn2),
            "/dev/ttyACM1"
        )
        validate_targets(first_target, second_target)
        validate_role_pair(first_target.fqbn, second_target.fqbn)
    except ValueError as exc:
        parser.error(str(exc))

    ensure_local_platform(args.sketchbook)
    env = command_env(args.sketchbook)
    log_dir = args.log_root / datetime.now().strftime("%Y%m%d-%H%M%S")
    if not args.skip_flash:
        for target in (first_target, second_target):
            if not flash_board(
                args.arduino_cli, target, args.example, env, log_dir
            ):
                return 1
    time.sleep(args.post_upload_delay)

    first, second = monitor(
        first_target,
        second_target,
        args.timeout,
        args.completion_grace,
        args.dump_lines,
    )
    write_logs(log_dir, first, second)
    print(f"logs: {log_dir}")
    return 0 if validate(first, second) else 1


if __name__ == "__main__":
    raise SystemExit(main())
