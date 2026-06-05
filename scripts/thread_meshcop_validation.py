#!/usr/bin/env python3
"""
Compile and optionally run the staged MeshCoP validation examples.

This script intentionally tests the local repo checkout by creating an Arduino
sketchbook hardware symlink under /tmp. It does not depend on the version
installed in ~/.arduino15.

Typical compile-only gate:
  python3 scripts/thread_meshcop_validation.py compile

Typical two-board hardware run:
  python3 scripts/thread_meshcop_validation.py all \
      --commissioner-port /dev/ttyACM0 \
      --joiner-port /dev/ttyACM1 \
      --timeout 180 \
      --dump-lines
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
from typing import Dict, Iterable, List, Optional


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
DEFAULT_SKETCHBOOK = pathlib.Path("/tmp/nrf54-thread-meshcop-sketchbook")
DEFAULT_LOG_ROOT = REPO_ROOT / "build" / "thread-meshcop-validation"

SKETCHES = {
    "commissioner": THREAD_EXAMPLES / "ThreadExperimentalCommissioner",
    "joiner": THREAD_EXAMPLES / "ThreadExperimentalJoiner",
    "restore": THREAD_EXAMPLES / "ThreadExperimentalMeshcopRestoreProbe",
    "wrong-pskd": THREAD_EXAMPLES / "ThreadExperimentalMeshcopWrongPskdJoiner",
}


@dataclass
class StepResult:
    name: str
    ok: bool
    details: List[str] = field(default_factory=list)

    def print(self) -> None:
        status = "PASS" if self.ok else "FAIL"
        print(f"{self.name}: {status}")
        for detail in self.details:
            print(f"  {detail}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate staged OpenThread MeshCoP examples."
    )
    parser.add_argument(
        "action",
        nargs="?",
        default="compile",
        choices=("compile", "fresh-join", "restore", "wrong-pskd", "all"),
        help="Validation action to run.",
    )
    parser.add_argument(
        "--fqbn",
        default=DEFAULT_FQBN,
        help=f"Arduino FQBN. Default: {DEFAULT_FQBN}",
    )
    parser.add_argument(
        "--arduino-cli",
        default="arduino-cli",
        help="arduino-cli executable.",
    )
    parser.add_argument(
        "--sketchbook",
        type=pathlib.Path,
        default=DEFAULT_SKETCHBOOK,
        help="Temporary Arduino sketchbook used to point at this checkout.",
    )
    parser.add_argument(
        "--log-root",
        type=pathlib.Path,
        default=DEFAULT_LOG_ROOT,
        help="Directory for compile/upload/serial logs.",
    )
    parser.add_argument(
        "--commissioner-port",
        default="",
        help="Serial/upload port for ThreadExperimentalCommissioner.",
    )
    parser.add_argument(
        "--joiner-port",
        default="",
        help="Serial/upload port for joiner, restore, and wrong-PSKd examples.",
    )
    parser.add_argument(
        "--baud",
        type=int,
        default=115200,
        help="Serial baud rate for validation output.",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=180.0,
        help="Seconds to capture serial output for each hardware phase.",
    )
    parser.add_argument(
        "--skip-upload",
        action="store_true",
        help="Capture serial from already-flashed boards instead of uploading.",
    )
    parser.add_argument(
        "--dump-lines",
        action="store_true",
        help="Print captured serial lines while running.",
    )
    return parser.parse_args()


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


def run_command(
    cmd: List[str],
    env: Dict[str, str],
    log_file: pathlib.Path,
    timeout_s: float,
) -> None:
    log_file.parent.mkdir(parents=True, exist_ok=True)
    print("+ " + " ".join(cmd))
    completed = subprocess.run(
        cmd,
        env=env,
        cwd=str(REPO_ROOT),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout_s,
    )
    log_file.write_text(completed.stdout, encoding="utf-8", errors="replace")
    if completed.returncode != 0:
        raise RuntimeError(
            f"command failed with exit {completed.returncode}; see {log_file}"
        )


def compile_sketch(
    sketch_name: str,
    args: argparse.Namespace,
    env: Dict[str, str],
    log_dir: pathlib.Path,
) -> StepResult:
    sketch = SKETCHES[sketch_name]
    build_path = log_dir / "build" / sketch_name
    shutil.rmtree(build_path, ignore_errors=True)
    try:
        run_command(
            [
                args.arduino_cli,
                "compile",
                "--fqbn",
                args.fqbn,
                "--build-path",
                str(build_path),
                str(sketch),
            ],
            env,
            log_dir / f"{sketch_name}.compile.log",
            timeout_s=240.0,
        )
        return StepResult(f"compile {sketch_name}", True, [str(sketch)])
    except (RuntimeError, subprocess.TimeoutExpired) as exc:
        return StepResult(f"compile {sketch_name}", False, [str(exc)])


def compile_all(args: argparse.Namespace, env: Dict[str, str], log_dir: pathlib.Path) -> bool:
    results = [
        compile_sketch(name, args, env, log_dir)
        for name in ("commissioner", "joiner", "restore", "wrong-pskd")
    ]
    for result in results:
        result.print()
    return all(result.ok for result in results)


def require_hardware_ports(args: argparse.Namespace) -> None:
    missing = []
    if not args.commissioner_port:
        missing.append("--commissioner-port")
    if not args.joiner_port:
        missing.append("--joiner-port")
    if missing:
        raise SystemExit("Hardware validation needs " + " and ".join(missing))


def upload_sketch(
    sketch_name: str,
    port: str,
    args: argparse.Namespace,
    env: Dict[str, str],
    log_dir: pathlib.Path,
) -> StepResult:
    sketch = SKETCHES[sketch_name]
    build_path = log_dir / "upload-build" / sketch_name
    shutil.rmtree(build_path, ignore_errors=True)
    try:
        run_command(
            [
                args.arduino_cli,
                "compile",
                "--upload",
                "--port",
                port,
                "--fqbn",
                args.fqbn,
                "--build-path",
                str(build_path),
                str(sketch),
            ],
            env,
            log_dir / f"{sketch_name}.{sanitize_port(port)}.upload.log",
            timeout_s=300.0,
        )
        return StepResult(f"upload {sketch_name}", True, [port])
    except (RuntimeError, subprocess.TimeoutExpired) as exc:
        return StepResult(f"upload {sketch_name}", False, [str(exc)])


def sanitize_port(port: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", port.strip("/") or "port")


def serial_module():
    try:
        import serial  # type: ignore

        return serial
    except ImportError as exc:
        raise SystemExit(
            "pyserial is required for hardware capture. Install it outside the "
            "script environment, for example: python3 -m pip install pyserial"
        ) from exc


def capture_serial(
    ports: Dict[str, str],
    args: argparse.Namespace,
    log_dir: pathlib.Path,
) -> Dict[str, List[str]]:
    serial = serial_module()
    handles = {}
    logs = {}
    captured: Dict[str, List[str]] = {label: [] for label in ports}

    for label, port in ports.items():
        log_path = log_dir / f"{label}.{sanitize_port(port)}.serial.log"
        logs[label] = log_path.open("w", encoding="utf-8", errors="replace")
        handle = serial.Serial(port, args.baud, timeout=0.1)
        handle.reset_input_buffer()
        handles[label] = handle

    try:
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            for label, handle in handles.items():
                raw = handle.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                captured[label].append(line)
                logs[label].write(line + "\n")
                logs[label].flush()
                if args.dump_lines:
                    print(f"[{label}] {line}")
    finally:
        for handle in handles.values():
            handle.close()
        for log in logs.values():
            log.close()

    return captured


def any_line(lines: Iterable[str], pattern: str) -> bool:
    regex = re.compile(pattern)
    return any(regex.search(line) for line in lines)


def result_from_checks(name: str, checks: List[tuple[bool, str]]) -> StepResult:
    return StepResult(name, all(ok for ok, _ in checks), [detail for _, detail in checks])


def run_fresh_join(
    args: argparse.Namespace,
    env: Dict[str, str],
    log_dir: pathlib.Path,
) -> StepResult:
    require_hardware_ports(args)
    if not args.skip_upload:
        for result in (
            upload_sketch("commissioner", args.commissioner_port, args, env, log_dir),
            upload_sketch("joiner", args.joiner_port, args, env, log_dir),
        ):
            result.print()
            if not result.ok:
                return StepResult("fresh MeshCoP join", False, result.details)
            time.sleep(2.0)

    captured = capture_serial(
        {
            "commissioner": args.commissioner_port,
            "joiner": args.joiner_port,
        },
        args,
        log_dir,
    )
    commissioner = captured["commissioner"]
    joiner = captured["joiner"]

    checks = [
        (
            any_line(commissioner, r"JOINER_ACCEPTED|meshcop_joiner_finalize(_callback)?=1|meshcop_finalize_seen=[1-9]"),
            "commissioner saw MeshCoP finalize/JOINER_ACCEPTED",
        ),
        (
            any_line(joiner, r"JOIN_SUCCESS|meshcop_joiner_callback_success=1|joiner_complete=1"),
            "joiner reported successful MeshCoP join",
        ),
        (
            any_line(joiner, r"preexisting_dataset_before_joiner=0"),
            "joiner started from clean settings before Joiner start",
        ),
        (
            not any_line(joiner, r"FATAL|preexisting_dataset_before_joiner=1|JOIN_FAILED"),
            "joiner did not report fatal/preexisting-dataset failure",
        ),
    ]
    return result_from_checks("fresh MeshCoP join", checks)


def run_restore(
    args: argparse.Namespace,
    env: Dict[str, str],
    log_dir: pathlib.Path,
) -> StepResult:
    require_hardware_ports(args)
    if not args.skip_upload:
        result = upload_sketch("restore", args.joiner_port, args, env, log_dir)
        result.print()
        if not result.ok:
            return StepResult("MeshCoP restore", False, result.details)
        time.sleep(2.0)

    captured = capture_serial({"restore": args.joiner_port}, args, log_dir)
    restore = captured["restore"]
    checks = [
        (
            any_line(restore, r"restore_attempted=1"),
            "restore probe attempted settings restore",
        ),
        (
            any_line(restore, r"restore_restored=1"),
            "restore probe restored a commissioned dataset",
        ),
        (
            any_line(restore, r"dataset_configured=1"),
            "restore probe has a configured dataset",
        ),
        (
            not any_line(restore, r"FATAL|begin=0"),
            "restore probe did not report fatal/begin failure",
        ),
    ]
    return result_from_checks("MeshCoP restore", checks)


def run_wrong_pskd(
    args: argparse.Namespace,
    env: Dict[str, str],
    log_dir: pathlib.Path,
) -> StepResult:
    require_hardware_ports(args)
    if not args.skip_upload:
        for result in (
            upload_sketch("commissioner", args.commissioner_port, args, env, log_dir),
            upload_sketch("wrong-pskd", args.joiner_port, args, env, log_dir),
        ):
            result.print()
            if not result.ok:
                return StepResult("wrong-PSKd MeshCoP negative test", False, result.details)
            time.sleep(2.0)

    captured = capture_serial(
        {
            "commissioner": args.commissioner_port,
            "wrong_pskd_joiner": args.joiner_port,
        },
        args,
        log_dir,
    )
    joiner = captured["wrong_pskd_joiner"]
    checks = [
        (
            any_line(joiner, r"expected_join_failure=1|callback_seen=1"),
            "wrong PSKd produced expected Joiner failure callback",
        ),
        (
            any_line(joiner, r"before_joiner_active_dataset=0|active_dataset=0"),
            "negative test did not expose a preexisting active dataset",
        ),
        (
            not any_line(joiner, r"unexpected_join_success=1|active_dataset=1|FATAL"),
            "negative test did not persist a dataset or report unexpected success",
        ),
    ]
    return result_from_checks("wrong-PSKd MeshCoP negative test", checks)


def main() -> int:
    args = parse_args()
    ensure_sketchbook(args.sketchbook)
    env = command_env(args.sketchbook)

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    log_dir = args.log_root / stamp
    log_dir.mkdir(parents=True, exist_ok=True)
    print(f"log_dir={log_dir}")
    print(f"sketchbook={args.sketchbook}")
    print(f"fqbn={args.fqbn}")

    if args.action == "compile":
        return 0 if compile_all(args, env, log_dir) else 1

    if not compile_all(args, env, log_dir):
        return 1

    hardware_results: List[StepResult] = []
    if args.action in ("fresh-join", "all"):
        hardware_results.append(run_fresh_join(args, env, log_dir))
    if args.action in ("restore", "all"):
        hardware_results.append(run_restore(args, env, log_dir))
    if args.action in ("wrong-pskd", "all"):
        hardware_results.append(run_wrong_pskd(args, env, log_dir))

    for result in hardware_results:
        result.print()

    return 0 if all(result.ok for result in hardware_results) else 1


if __name__ == "__main__":
    sys.exit(main())
