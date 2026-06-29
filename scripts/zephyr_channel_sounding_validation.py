#!/usr/bin/env python3
"""Build and flash the official Zephyr connected-CS sample on XIAO nRF54L15."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import math
import os
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BOARD = "xiao_nrf54l15/nrf54l15/cpuapp"
DEFAULT_SAMPLE = "connected_cs"
DEFAULT_WORKSPACE_CANDIDATES = (
    REPO_ROOT.parent / "ncs-workspace",
    REPO_ROOT.parent / "zephyr-main",
)
DEFAULT_TOOLS_CANDIDATES = (
    REPO_ROOT / "hardware/nrf54l15clean/nrf54l15clean/tools",
    Path.home()
    / ".local/share/Trash/files/NRF54L15-Arduino-core/hardware/seeed/nrf54l15/tools",
    Path.home()
    / ".local/share/Trash/files/here/xiao-nrf54l15-arduino-core/hardware/seeed/nrf54l15/tools",
)
DEFAULT_SDK_CANDIDATES = tuple(candidate / "zephyr-sdk" for candidate in DEFAULT_TOOLS_CANDIDATES)
ROLE_TO_SAMPLE_PATH = {
    "initiator": Path("zephyr/samples/bluetooth/channel_sounding/connected_cs/initiator"),
    "reflector": Path("zephyr/samples/bluetooth/channel_sounding/connected_cs/reflector"),
}
ROLE_TO_PASS_MARKERS = {
    "initiator": (
        "Starting Channel Sounding Demo",
        "Connected to ",
        "MTU exchange success",
        "Security changed to level",
        "CS capability exchange completed.",
        "CS config creation complete. ID:",
        "CS security enabled.",
        "CS procedures enabled.",
        "Estimated distance",
    ),
    "reflector": (
        "Starting Channel Sounding Demo",
        "Connected to ",
        "MTU exchange success",
        "Found expected UUID",
        "CS capability exchange completed.",
        "CS config creation complete. ID:",
        "CS security enabled.",
    ),
}
FLOAT_RE = r"([+-]?(?:\d+(?:\.\d+)?|\.\d+|nan|na))"
ZEPHYR_DISTANCE_PATTERNS = {
    "rtt": re.compile(
        rf"Round-Trip Timing method:\s*{FLOAT_RE}\s+meters"
        r"(?:\s*\(derived from\s*(\d+)\s+samples\))?",
        re.IGNORECASE,
    ),
    "phase": re.compile(
        rf"Phase-Based Ranging method:\s*{FLOAT_RE}\s+meters"
        r"(?:\s*\(derived from\s*(\d+)\s+samples\))?",
        re.IGNORECASE,
    ),
}


@dataclass(frozen=True)
class ZephyrLayout:
    workspace: Path
    zephyr_base: Path
    sdk_dir: Path | None
    pydeps_dir: Path | None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate controller-backed BLE Channel Sounding on XIAO nRF54L15."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    build = subparsers.add_parser("build", help="Build Zephyr connected_cs role(s).")
    add_common_build_args(build)
    build.add_argument(
        "--role",
        choices=("initiator", "reflector", "both"),
        default="both",
        help="Which role to build.",
    )

    flash = subparsers.add_parser("flash", help="Flash a previously built role.")
    add_common_build_args(flash)
    flash.add_argument("--role", choices=("initiator", "reflector"), required=True)
    add_probe_args(flash)

    reset = subparsers.add_parser("reset", help="Reset a board through pyOCD.")
    add_common_build_args(reset)
    add_probe_args(reset)

    pair = subparsers.add_parser(
        "pair-demo",
        help="Build, flash, and reset both connected_cs roles on two boards.",
    )
    add_common_build_args(pair)
    pair.add_argument("--skip-build", action="store_true", help="Reuse existing build outputs.")
    pair.add_argument(
        "--initiator-port",
        default="/dev/ttyACM0",
        help="Serial port whose attached probe should receive the initiator image.",
    )
    pair.add_argument(
        "--reflector-port",
        default="/dev/ttyACM1",
        help="Serial port whose attached probe should receive the reflector image.",
    )
    pair.add_argument(
        "--capture-seconds",
        type=float,
        default=0.0,
        help="Capture and validate Zephyr serial logs for this many seconds after reset.",
    )
    pair.add_argument(
        "--log-dir",
        default="",
        help="Directory for captured logs. Defaults to a timestamped dist/ path.",
    )

    return parser.parse_args()


def add_common_build_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--workspace",
        default="",
        help=(
            "Zephyr/NCS workspace containing zephyr/. Overrides packaged --tools-dir "
            "source discovery when set."
        ),
    )
    parser.add_argument(
        "--tools-dir",
        default="",
        help="Path to a tools dir containing ncs/, pydeps/, and zephyr-sdk/.",
    )
    parser.add_argument(
        "--sdk-dir",
        default="",
        help="Optional Zephyr SDK directory. Auto-detected from packaged tools when omitted.",
    )
    parser.add_argument(
        "--board",
        default=DEFAULT_BOARD,
        help=f"Zephyr board target. Default: {DEFAULT_BOARD}",
    )
    parser.add_argument(
        "--build-root",
        default=str(REPO_ROOT / "dist" / "zephyr_channel_sounding"),
        help="Root directory for west build outputs.",
    )


def add_probe_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--port",
        default="",
        help="Serial port used to infer the matching CMSIS-DAP UID on Linux.",
    )
    parser.add_argument(
        "--uid",
        default="",
        help="Explicit CMSIS-DAP UID. Overrides --port inference.",
    )


def normalize_uid(uid: str) -> str | None:
    cleaned = uid.strip()
    return cleaned or None


def infer_uid_from_port(port: str) -> str | None:
    if not port or not sys.platform.startswith("linux"):
        return None

    by_id_dir = Path("/dev/serial/by-id")
    if not by_id_dir.is_dir():
        return None

    try:
        target = Path(port).resolve(strict=True)
    except OSError:
        return None

    for entry in by_id_dir.iterdir():
        try:
            if entry.resolve(strict=True) != target:
                continue
        except OSError:
            continue

        match = re.search(r"_([0-9A-Fa-f]+)-if\d+$", entry.name)
        if match:
            return match.group(1)
    return None


def require_uid(port: str, uid: str) -> str:
    explicit = normalize_uid(uid)
    if explicit:
        return explicit

    inferred = infer_uid_from_port(port)
    if inferred:
        return inferred

    raise RuntimeError("Unable to resolve CMSIS-DAP UID. Pass --uid or a Linux --port.")


def detect_tools_dir(explicit: str) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit).expanduser())

    env_tools = os.environ.get("XIAO_NRF54L15_TOOLS_DIR", "").strip()
    if env_tools:
        candidates.append(Path(env_tools).expanduser())

    for candidate in DEFAULT_TOOLS_CANDIDATES:
        candidates.append(candidate)

    seen: set[Path] = set()
    for candidate in candidates:
        resolved = candidate.resolve()
        if resolved in seen:
            continue
        seen.add(resolved)
        if not resolved.is_dir():
            continue
        if (
            (resolved / "ncs" / "zephyr").is_dir()
            and (resolved / "pydeps").is_dir()
            and (resolved / "zephyr-sdk").is_dir()
        ):
            return resolved

    raise RuntimeError(
        "Could not find an NCS tools directory. Pass --tools-dir or set "
        "XIAO_NRF54L15_TOOLS_DIR."
    )


def looks_like_workspace(path: Path) -> bool:
    return (path / "zephyr").is_dir() and (
        (path / ".west" / "config").is_file() or (path / "zephyr" / "samples").is_dir()
    )


def detect_workspace(explicit: str) -> Path | None:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit).expanduser())

    env_workspace = os.environ.get("XIAO_NRF54L15_ZEPHYR_WORKSPACE", "").strip()
    if env_workspace:
        candidates.append(Path(env_workspace).expanduser())

    candidates.extend(DEFAULT_WORKSPACE_CANDIDATES)

    seen: set[Path] = set()
    for candidate in candidates:
        resolved = candidate.resolve()
        if resolved in seen:
            continue
        seen.add(resolved)
        if resolved.is_dir() and looks_like_workspace(resolved):
            return resolved
    return None


def parse_version_tuple(value: str) -> tuple[int, ...]:
    parts: list[int] = []
    for part in re.split(r"[^0-9]+", value.strip()):
        if part:
            parts.append(int(part))
    return tuple(parts)


def parse_distance_token(token: str) -> float | None:
    lowered = token.strip().lower()
    if lowered in {"", "nan", "na", "none"}:
        return None
    try:
        value = float(lowered)
    except ValueError:
        return None
    if not math.isfinite(value):
        return None
    return value


def extract_zephyr_distance_samples(log_path: Path) -> list[dict[str, object]]:
    samples: list[dict[str, object]] = []
    if not log_path.is_file():
        return samples

    with log_path.open("r", encoding="utf-8", errors="replace") as handle:
        for line_number, line in enumerate(handle, start=1):
            for method, pattern in ZEPHYR_DISTANCE_PATTERNS.items():
                match = pattern.search(line)
                if not match:
                    continue
                value = parse_distance_token(match.group(1))
                if value is None:
                    continue
                record: dict[str, object] = {
                    "method": method,
                    "value_m": value,
                    "log_path": str(log_path),
                    "line_number": line_number,
                }
                if match.group(2):
                    record["derived_samples"] = int(match.group(2))
                samples.append(record)
                break
    return samples


def zephyr_distance_values(
    samples: list[dict[str, object]], method: str
) -> list[float]:
    values: list[float] = []
    for sample in samples:
        if str(sample.get("method", "")) != method:
            continue
        value = sample.get("value_m")
        if isinstance(value, (int, float)) and math.isfinite(float(value)):
            values.append(float(value))
    return values


def summarize_zephyr_distance_samples(
    samples: list[dict[str, object]], method: str
) -> dict[str, float] | None:
    values = zephyr_distance_values(samples, method)
    if not values:
        return None
    median_value = statistics.median(values)
    mean_value = sum(values) / float(len(values))
    variance = sum((value - mean_value) ** 2 for value in values) / float(len(values))
    return {
        "count": float(len(values)),
        "median": median_value,
        "mean": mean_value,
        "mad": statistics.median(abs(value - median_value) for value in values),
        "stddev": math.sqrt(variance),
        "minimum": min(values),
        "maximum": max(values),
    }


def print_zephyr_distance_summary(logs: dict[str, Path]) -> None:
    all_samples: list[dict[str, object]] = []
    for path in logs.values():
        all_samples.extend(extract_zephyr_distance_samples(path))

    for method in ("phase", "rtt"):
        summary = summarize_zephyr_distance_samples(all_samples, method)
        if summary is None:
            print(f"zephyr_distance_{method}=NONE")
            continue
        print(
            f"zephyr_distance_{method}=PASS "
            f"count={int(summary['count'])} "
            f"median_m={summary['median']:.6f} "
            f"mad_m={summary['mad']:.6f} "
            f"min_m={summary['minimum']:.6f} "
            f"max_m={summary['maximum']:.6f}"
        )


def read_sdk_version(sdk_dir: Path) -> tuple[int, ...] | None:
    version_file = sdk_dir / "sdk_version"
    if not version_file.is_file():
        return None
    try:
        return parse_version_tuple(version_file.read_text().strip())
    except (OSError, ValueError):
        return None


def minimum_sdk_for_workspace(workspace: Path) -> tuple[int, ...] | None:
    version_file = workspace / "zephyr" / "VERSION"
    if not version_file.is_file():
        return None
    values: dict[str, int] = {}
    try:
        for line in version_file.read_text().splitlines():
            match = re.match(r"(VERSION_MAJOR|VERSION_MINOR)\s*=\s*(\d+)", line)
            if match:
                values[match.group(1)] = int(match.group(2))
    except OSError:
        return None
    if (values.get("VERSION_MAJOR", 0), values.get("VERSION_MINOR", 0)) >= (4, 4):
        return (1, 0)
    return None


def version_at_least(found: tuple[int, ...] | None, minimum: tuple[int, ...] | None) -> bool:
    if minimum is None:
        return True
    if found is None:
        return False
    width = max(len(found), len(minimum))
    padded_found = found + (0,) * (width - len(found))
    padded_minimum = minimum + (0,) * (width - len(minimum))
    return padded_found >= padded_minimum


def detect_sdk_dir(
    explicit: str,
    tools_dir: Path | None,
    minimum: tuple[int, ...] | None,
) -> Path | None:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit).expanduser())

    env_sdk = os.environ.get("ZEPHYR_SDK_INSTALL_DIR", "").strip()
    if env_sdk:
        candidates.append(Path(env_sdk).expanduser())

    if tools_dir is not None:
        candidates.append(tools_dir / "zephyr-sdk")

    candidates.extend(DEFAULT_SDK_CANDIDATES)

    seen: set[Path] = set()
    for candidate in candidates:
        resolved = candidate.resolve()
        if resolved in seen:
            continue
        seen.add(resolved)
        compiler = resolved / "arm-zephyr-eabi" / "bin" / "arm-zephyr-eabi-gcc"
        if resolved.is_dir() and compiler.is_file():
            version = read_sdk_version(resolved)
            if not version_at_least(version, minimum):
                if explicit:
                    min_text = ".".join(str(v) for v in minimum or ())
                    found_text = ".".join(str(v) for v in version or ())
                    raise RuntimeError(
                        f"Zephyr SDK {found_text or 'unknown'} at {resolved} is too old; "
                        f"this workspace requires >= {min_text}."
                    )
                continue
            return resolved
    return None


def detect_layout(workspace_arg: str, tools_arg: str, sdk_arg: str) -> ZephyrLayout:
    env_workspace = os.environ.get("XIAO_NRF54L15_ZEPHYR_WORKSPACE", "").strip()
    if workspace_arg or env_workspace or not tools_arg:
        workspace = detect_workspace(workspace_arg)
    else:
        workspace = None
    tools_dir: Path | None = None

    if workspace is None:
        tools_dir = detect_tools_dir(tools_arg)
        workspace = tools_dir / "ncs"
    elif tools_arg:
        tools_dir = detect_tools_dir(tools_arg)
    else:
        env_tools = os.environ.get("XIAO_NRF54L15_TOOLS_DIR", "").strip()
        if env_tools:
            candidate = Path(env_tools).expanduser().resolve()
            if candidate.is_dir():
                tools_dir = candidate

    zephyr_base = workspace / "zephyr"
    if not zephyr_base.is_dir():
        raise RuntimeError(f"Zephyr base not found under workspace: {zephyr_base}")

    minimum_sdk = minimum_sdk_for_workspace(workspace)
    sdk_dir = detect_sdk_dir(sdk_arg, tools_dir, minimum_sdk)
    if minimum_sdk is not None and sdk_dir is None:
        min_text = ".".join(str(v) for v in minimum_sdk)
        raise RuntimeError(
            f"Zephyr SDK >= {min_text} is required for {workspace}. "
            "Install a compatible SDK or pass --sdk-dir."
        )
    pydeps_dir = None
    if tools_dir is not None and (tools_dir / "pydeps").is_dir():
        pydeps_dir = tools_dir / "pydeps"

    return ZephyrLayout(
        workspace=workspace.resolve(),
        zephyr_base=zephyr_base.resolve(),
        sdk_dir=sdk_dir.resolve() if sdk_dir is not None else None,
        pydeps_dir=pydeps_dir.resolve() if pydeps_dir is not None else None,
    )


def run(cmd: list[str], *, cwd: Path, env: dict[str, str]) -> None:
    print("$", " ".join(cmd), flush=True)
    proc = subprocess.run(cmd, cwd=cwd, env=env, check=False, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"command failed with exit code {proc.returncode}")


def zephyr_env(layout: ZephyrLayout) -> dict[str, str]:
    env = dict(os.environ)
    env["ZEPHYR_BASE"] = str(layout.zephyr_base)
    path_entries: list[Path] = []
    if layout.sdk_dir is not None:
        env["ZEPHYR_SDK_INSTALL_DIR"] = str(layout.sdk_dir)
        path_entries.extend(
            [
                layout.sdk_dir / "arm-zephyr-eabi" / "bin",
                layout.sdk_dir / "hosttools" / "bin",
                layout.sdk_dir / "hosttools" / "usr" / "bin",
            ]
        )
    if layout.pydeps_dir is not None:
        pydeps_dir = str(layout.pydeps_dir)
        existing_pythonpath = env.get("PYTHONPATH", "")
        env["PYTHONPATH"] = (
            pydeps_dir
            if not existing_pythonpath
            else os.pathsep.join([pydeps_dir, existing_pythonpath])
        )
    existing_path = env.get("PATH", "")
    if path_entries:
        env["PATH"] = os.pathsep.join([str(p) for p in path_entries] + [existing_path])
    return env


def detect_python() -> str:
    python = shutil.which("python3")
    if python:
        return python
    python = shutil.which("python")
    if python:
        return python
    raise RuntimeError("python3 is required")


def detect_pyocd() -> list[str]:
    exe = shutil.which("pyocd")
    if exe:
        return [exe]

    python = detect_python()
    probe = subprocess.run(
        [python, "-m", "pyocd", "--version"],
        check=False,
        capture_output=True,
        text=True,
    )
    if probe.returncode == 0:
        return [python, "-m", "pyocd"]
    raise RuntimeError("pyOCD is required")


def west_cmd(_layout: ZephyrLayout) -> list[str]:
    west = shutil.which("west")
    if west:
        return [west]
    return [detect_python(), "-m", "west"]


def build_dir_for(build_root: Path, role: str) -> Path:
    return build_root / DEFAULT_SAMPLE / role


def app_dir_for(layout: ZephyrLayout, role: str) -> Path:
    return layout.workspace / ROLE_TO_SAMPLE_PATH[role]


def build_role(layout: ZephyrLayout, build_root: Path, board: str, role: str) -> Path:
    env = zephyr_env(layout)
    build_dir = build_dir_for(build_root, role)
    app_dir = app_dir_for(layout, role)
    build_dir.parent.mkdir(parents=True, exist_ok=True)
    run(
        west_cmd(layout)
        + ["build", "-p", "always", "-b", board, str(app_dir), "-d", str(build_dir)],
        cwd=layout.workspace,
        env=env,
    )
    return build_dir


def flash_role(_layout: ZephyrLayout, build_root: Path, role: str, uid: str) -> None:
    build_dir = build_dir_for(build_root, role)
    merged_hex = build_dir / "merged.hex"
    if not merged_hex.is_file():
        raise RuntimeError(f"Missing build artifact: {merged_hex}")

    cmd = [*detect_pyocd(), "load", "-W", "-t", "nrf54l", "-u", uid, str(merged_hex), "--format", "hex"]
    run(cmd, cwd=REPO_ROOT, env=dict(os.environ))


def reset_probe(uid: str) -> None:
    cmd = [*detect_pyocd(), "commander", "-t", "nrf54l", "-u", uid, "-c", "reset"]
    run(cmd, cwd=REPO_ROOT, env=dict(os.environ))


def print_monitor_hint(initiator_port: str, reflector_port: str) -> None:
    print()
    print("Open serial before reset to catch Zephyr boot logs:")
    print(f"  stty -F {initiator_port} 115200 raw -echo && stdbuf -oL cat {initiator_port}")
    print(f"  stty -F {reflector_port} 115200 raw -echo && stdbuf -oL cat {reflector_port}")
    print("Then run this command again with `reset` or `pair-demo` to retrigger boot output.")


def capture_serial_pair(
    initiator_uid: str,
    reflector_uid: str,
    initiator_port: str,
    reflector_port: str,
    capture_seconds: float,
    log_dir: Path,
) -> bool:
    log_dir.mkdir(parents=True, exist_ok=True)
    logs = {
        "initiator": log_dir / "zephyr_initiator.log",
        "reflector": log_dir / "zephyr_reflector.log",
    }

    for port in (initiator_port, reflector_port):
        subprocess.run(
            ["stty", "-F", port, "115200", "raw", "-echo", "-hupcl"],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    with logs["initiator"].open("wb") as initiator_out, logs["reflector"].open("wb") as reflector_out:
        initiator_cat = subprocess.Popen(["cat", initiator_port], stdout=initiator_out)
        reflector_cat = subprocess.Popen(["cat", reflector_port], stdout=reflector_out)
        time.sleep(0.5)
        reset_probe(reflector_uid)
        reset_probe(initiator_uid)
        time.sleep(capture_seconds)
        for proc in (initiator_cat, reflector_cat):
            proc.terminate()
        for proc in (initiator_cat, reflector_cat):
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=2)

    print(f"logs={log_dir}")
    ok = validate_zephyr_logs(logs)
    print_zephyr_distance_summary(logs)
    return ok


def validate_zephyr_logs(logs: dict[str, Path]) -> bool:
    passed = True
    for role, path in logs.items():
        text = path.read_text(errors="replace") if path.is_file() else ""
        missing = [marker for marker in ROLE_TO_PASS_MARKERS[role] if marker not in text]
        if missing:
            passed = False
            print(f"zephyr_{role}=FAIL missing={','.join(missing)}")
            print(f"--- {path} ---")
            print(text[-4000:])
        else:
            print(f"zephyr_{role}=PASS")
    if passed:
        print("zephyr_connected_cs_pair=PASS")
    else:
        print("zephyr_connected_cs_pair=FAIL")
    return passed


def main() -> int:
    args = parse_args()
    layout = detect_layout(args.workspace, args.tools_dir, args.sdk_dir)
    build_root = Path(args.build_root).expanduser().resolve()

    if args.command == "build":
        roles = ("initiator", "reflector") if args.role == "both" else (args.role,)
        for role in roles:
            build_path = build_role(layout, build_root, args.board, role)
            print(f"Built {role}: {build_path}")
        return 0

    if args.command == "flash":
        uid = require_uid(args.port, args.uid)
        flash_role(layout, build_root, args.role, uid)
        print(f"Flashed {args.role} using UID {uid}")
        return 0

    if args.command == "reset":
        uid = require_uid(args.port, args.uid)
        reset_probe(uid)
        print(f"Reset board UID {uid}")
        return 0

    if args.command == "pair-demo":
        if not args.skip_build:
            for role in ("initiator", "reflector"):
                build_path = build_role(layout, build_root, args.board, role)
                print(f"Built {role}: {build_path}")

        initiator_uid = require_uid(args.initiator_port, "")
        reflector_uid = require_uid(args.reflector_port, "")
        flash_role(layout, build_root, "initiator", initiator_uid)
        flash_role(layout, build_root, "reflector", reflector_uid)
        print(f"Initiator flashed/reset on {args.initiator_port} ({initiator_uid})")
        print(f"Reflector flashed/reset on {args.reflector_port} ({reflector_uid})")
        if args.capture_seconds > 0.0:
            if args.log_dir:
                log_dir = Path(args.log_dir).expanduser().resolve()
            else:
                stamp = time.strftime("%Y%m%d_%H%M%S")
                log_dir = REPO_ROOT / "dist" / "zephyr_channel_sounding" / f"logs_{stamp}"
            ok = capture_serial_pair(
                initiator_uid,
                reflector_uid,
                args.initiator_port,
                args.reflector_port,
                args.capture_seconds,
                log_dir,
            )
            return 0 if ok else 1
        reset_probe(initiator_uid)
        reset_probe(reflector_uid)
        print_monitor_hint(args.initiator_port, args.reflector_port)
        return 0

    raise RuntimeError(f"Unsupported command: {args.command}")


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
