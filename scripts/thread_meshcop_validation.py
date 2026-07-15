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
      --commissioner-fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage \
      --joiner-fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54lm20b:clean_thread=stage \
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

from test_thread_udp_soak import (
    BoardTarget,
    localize_fqbn,
    resolve_target,
    validate_targets,
    verify_target_identity,
)


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
        default="",
        help="Legacy override that uses one Arduino FQBN for both boards.",
    )
    parser.add_argument(
        "--commissioner-fqbn",
        default=DEFAULT_FQBN,
        help=f"Commissioner Arduino FQBN. Default: {DEFAULT_FQBN}",
    )
    parser.add_argument(
        "--joiner-fqbn",
        default=DEFAULT_FQBN,
        help=f"Joiner/restore/negative-test Arduino FQBN. Default: {DEFAULT_FQBN}",
    )
    parser.add_argument(
        "--arduino-cli",
        default="arduino-cli",
        help="arduino-cli executable.",
    )
    parser.add_argument(
        "--pyocd",
        default="pyocd",
        help="pyOCD executable used for the settings-preserving reboot check.",
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
        "--commissioner-uid",
        default="",
        help="Expected commissioner CMSIS-DAP UID; resolves a stable CDC path.",
    )
    parser.add_argument(
        "--joiner-uid",
        default="",
        help="Expected joiner CMSIS-DAP UID; resolves a stable CDC path.",
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
    if not PLATFORM_ROOT.is_dir():
        raise SystemExit(f"Missing local platform root: {PLATFORM_ROOT}")

    hardware_dir = sketchbook / "hardware"
    platform_copy = hardware_dir / LOCAL_VENDOR / "nrf54l15clean"
    platform_copy.parent.mkdir(parents=True, exist_ok=True)
    if platform_copy.is_symlink() or platform_copy.is_file():
        platform_copy.unlink()
    elif platform_copy.exists():
        shutil.rmtree(platform_copy)
    shutil.copytree(
        PLATFORM_ROOT,
        platform_copy,
        ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "build", ".git"),
    )


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


def sketch_fqbn(sketch_name: str, args: argparse.Namespace) -> str:
    configured = args.fqbn or (
        args.commissioner_fqbn
        if sketch_name == "commissioner"
        else args.joiner_fqbn
    )
    return localize_fqbn(configured, LOCAL_VENDOR)


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
                sketch_fqbn(sketch_name, args),
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


def resolve_hardware_targets(
    args: argparse.Namespace,
) -> tuple[BoardTarget, BoardTarget]:
    if not args.commissioner_port and not args.commissioner_uid:
        raise ValueError("hardware validation needs --commissioner-port or --commissioner-uid")
    if not args.joiner_port and not args.joiner_uid:
        raise ValueError("hardware validation needs --joiner-port or --joiner-uid")

    commissioner = resolve_target(
        "commissioner",
        args.commissioner_port or None,
        args.commissioner_uid or None,
        sketch_fqbn("commissioner", args),
        args.commissioner_port,
    )
    joiner = resolve_target(
        "joiner",
        args.joiner_port or None,
        args.joiner_uid or None,
        sketch_fqbn("joiner", args),
        args.joiner_port,
    )
    validate_targets(commissioner, joiner)
    return commissioner, joiner


def upload_sketch(
    sketch_name: str,
    target: BoardTarget,
    args: argparse.Namespace,
    env: Dict[str, str],
    log_dir: pathlib.Path,
) -> StepResult:
    sketch = SKETCHES[sketch_name]
    port = target.port
    build_path = log_dir / "upload-build" / sketch_name
    shutil.rmtree(build_path, ignore_errors=True)
    try:
        verify_target_identity(target)
        run_command(
            [
                args.arduino_cli,
                "compile",
                "--upload",
                "--port",
                port,
                "--fqbn",
                target.fqbn,
                "--build-path",
                str(build_path),
                str(sketch),
            ],
            env,
            log_dir / f"{sketch_name}.{sanitize_port(port)}.upload.log",
            timeout_s=300.0,
        )
        return StepResult(f"upload {sketch_name}", True, [port])
    except (RuntimeError, ValueError, subprocess.TimeoutExpired) as exc:
        return StepResult(f"upload {sketch_name}", False, [str(exc)])


def pyocd_target_name(target: BoardTarget) -> str:
    return "nrf54lm20a" if ":xiao_nrf54lm20b" in target.fqbn else "nrf54l"


def pyocd_command_prefix(
    args: argparse.Namespace, target: BoardTarget, subcommand: str
) -> List[str]:
    command = [args.pyocd, subcommand, "--no-config", "-W"]
    if pyocd_target_name(target) == "nrf54lm20a":
        command.extend(
            ["--script", str(PLATFORM_ROOT / "tools" / "pyocd_register_lm20b.py")]
        )
    return command


def erase_target(
    target: BoardTarget,
    args: argparse.Namespace,
    env: Dict[str, str],
    log_dir: pathlib.Path,
) -> StepResult:
    if not target.uid:
        return StepResult(
            f"erase {target.label}",
            False,
            ["explicit chip erase requires a verifiable CMSIS-DAP UID"],
        )

    try:
        verify_target_identity(target)
        command = pyocd_command_prefix(args, target, "erase")
        command.extend(
            ["--chip", "-t", pyocd_target_name(target), "-u", target.uid]
        )
        run_command(
            command,
            env,
            log_dir / f"{target.label}.chip-erase.log",
            timeout_s=60.0,
        )
        return StepResult(
            f"erase {target.label}", True, [f"UID {target.uid} chip erase"]
        )
    except (RuntimeError, ValueError, subprocess.TimeoutExpired) as exc:
        return StepResult(f"erase {target.label}", False, [str(exc)])


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
    targets: Dict[str, BoardTarget],
    args: argparse.Namespace,
    log_dir: pathlib.Path,
) -> Dict[str, List[str]]:
    serial = serial_module()
    handles = {}
    logs = {}
    captured: Dict[str, List[str]] = {label: [] for label in targets}

    try:
        for label, target in targets.items():
            verify_target_identity(target)
            port = target.port
            log_path = log_dir / f"{label}.{sanitize_port(port)}.serial.log"
            logs[label] = log_path.open("w", encoding="utf-8", errors="replace")
            handle = serial.Serial(port, args.baud, timeout=0.1)
            handle.reset_input_buffer()
            handles[label] = handle

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


def validate_restore_lines(restore: List[str]) -> StepResult:
    checks = [
        (
            any_line(restore, r"restore_attempted=1"),
            "joiner attempted settings restore after reset",
        ),
        (
            any_line(restore, r"restore_restored=1"),
            "joiner restored the commissioned dataset",
        ),
        (
            any_line(restore, r"dataset_configured=1"),
            "joiner has a configured dataset after reset",
        ),
        (
            any_line(
                restore,
                r"reboot_restore_(?:mode|ready)=1|preexisting_dataset_before_joiner=1",
            ),
            "joiner entered reboot restore mode instead of MeshCoP",
        ),
        (
            any_line(restore, r"status_active_dataset=1"),
            "restored active dataset is visible to OpenThread",
        ),
        (
            not any_line(restore, r"FATAL|begin=0|joiner_start=1"),
            "restore boot did not fail or restart MeshCoP",
        ),
    ]
    return result_from_checks("MeshCoP restore", checks)


def validate_wrong_pskd_lines(
    commissioner: List[str], joiner: List[str]
) -> StepResult:
    security_failure_observed = any_line(
        joiner,
        r"(?:joiner_callback error=|callback_seen=1[^\n]*callback_error=)8\b",
    )
    commissioner_ready = any_line(
        commissioner,
        r"commissioner_active=1|commissioner_state=active",
    ) and any_line(commissioner, r"joiner_entry_added=1|joiner_added=1")
    commissioner_saw_attempt = any_line(
        commissioner,
        r"meshcop_event_count=[1-9][0-9]*|meshcop_joiner_event=(?:0|1|3)\b",
    )
    checks = [
        (
            security_failure_observed,
            "wrong PSKd produced OT_ERROR_SECURITY on the Joiner",
        ),
        (
            commissioner_ready,
            "commissioner was active with the expected Joiner entry",
        ),
        (
            commissioner_saw_attempt,
            "commissioner observed the rejected Joiner attempt",
        ),
        (
            any_line(joiner, r"before_joiner_active_dataset=0|active_dataset=0"),
            "negative test did not expose a preexisting active dataset",
        ),
        (
            not any_line(
                joiner,
                r"unexpected_(?:join_)?success=1|active_dataset=1|FATAL",
            ),
            "negative test did not persist a dataset or report unexpected success",
        ),
        (
            not any_line(
                commissioner,
                r"JOINER_ACCEPTED|meshcop_joiner_finalize(?:_callback)?=1|"
                r"meshcop_finalize_(?:count|seen)=[1-9][0-9]*|joiner_connected=1",
            ),
            "commissioner did not finalize or accept the wrong-PSKd Joiner",
        ),
    ]
    return result_from_checks("wrong-PSKd MeshCoP negative test", checks)


def run_fresh_join(
    args: argparse.Namespace,
    env: Dict[str, str],
    log_dir: pathlib.Path,
    commissioner_target: BoardTarget,
    joiner_target: BoardTarget,
) -> StepResult:
    if not args.skip_upload:
        erase_result = erase_target(joiner_target, args, env, log_dir)
        erase_result.print()
        if not erase_result.ok:
            return StepResult("fresh MeshCoP join", False, erase_result.details)

        for result in (
            upload_sketch("commissioner", commissioner_target, args, env, log_dir),
            upload_sketch("joiner", joiner_target, args, env, log_dir),
        ):
            result.print()
            if not result.ok:
                return StepResult("fresh MeshCoP join", False, result.details)
            time.sleep(2.0)

    captured = capture_serial(
        {
            "commissioner": commissioner_target,
            "joiner": joiner_target,
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
    joiner_target: BoardTarget,
) -> StepResult:
    if not joiner_target.uid:
        return StepResult(
            "MeshCoP restore",
            False,
            ["settings-preserving reset requires --joiner-uid"],
        )

    verify_target_identity(joiner_target)
    command = pyocd_command_prefix(args, joiner_target, "commander")
    command.extend(
        [
            "-t",
            pyocd_target_name(joiner_target),
            "-u",
            joiner_target.uid,
            "-c",
            "reset",
        ]
    )
    try:
        run_command(
            command,
            env,
            log_dir / "restore.reset.log",
            timeout_s=30.0,
        )
    except (RuntimeError, ValueError, subprocess.TimeoutExpired) as exc:
        return StepResult("MeshCoP restore", False, [str(exc)])

    captured = capture_serial({"restore": joiner_target}, args, log_dir)
    restore = captured["restore"]
    return validate_restore_lines(restore)


def run_wrong_pskd(
    args: argparse.Namespace,
    env: Dict[str, str],
    log_dir: pathlib.Path,
    commissioner_target: BoardTarget,
    joiner_target: BoardTarget,
) -> StepResult:
    if not args.skip_upload:
        for result in (
            upload_sketch("commissioner", commissioner_target, args, env, log_dir),
            upload_sketch("wrong-pskd", joiner_target, args, env, log_dir),
        ):
            result.print()
            if not result.ok:
                return StepResult("wrong-PSKd MeshCoP negative test", False, result.details)
            time.sleep(2.0)

    captured = capture_serial(
        {
            "commissioner": commissioner_target,
            "wrong_pskd_joiner": joiner_target,
        },
        args,
        log_dir,
    )
    joiner = captured["wrong_pskd_joiner"]
    return validate_wrong_pskd_lines(captured["commissioner"], joiner)


def main() -> int:
    args = parse_args()
    ensure_sketchbook(args.sketchbook)
    env = command_env(args.sketchbook)

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    log_dir = args.log_root / stamp
    log_dir.mkdir(parents=True, exist_ok=True)
    print(f"log_dir={log_dir}")
    print(f"sketchbook={args.sketchbook}")
    if args.fqbn:
        print(f"fqbn={args.fqbn}")
    else:
        print(f"commissioner_fqbn={args.commissioner_fqbn}")
        print(f"joiner_fqbn={args.joiner_fqbn}")

    if args.action == "compile":
        return 0 if compile_all(args, env, log_dir) else 1

    if not compile_all(args, env, log_dir):
        return 1

    try:
        commissioner_target, joiner_target = resolve_hardware_targets(args)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    print(
        f"commissioner_target={commissioner_target.port} "
        f"uid={commissioner_target.uid or 'unbound'}"
    )
    print(
        f"joiner_target={joiner_target.port} uid={joiner_target.uid or 'unbound'}"
    )

    hardware_results: List[StepResult] = []
    if args.action in ("fresh-join", "all"):
        hardware_results.append(
            run_fresh_join(
                args, env, log_dir, commissioner_target, joiner_target
            )
        )
    if args.action in ("restore", "all"):
        hardware_results.append(run_restore(args, env, log_dir, joiner_target))
    if args.action in ("wrong-pskd", "all"):
        hardware_results.append(
            run_wrong_pskd(
                args, env, log_dir, commissioner_target, joiner_target
            )
        )

    for result in hardware_results:
        result.print()

    return 0 if all(result.ok for result in hardware_results) else 1


if __name__ == "__main__":
    sys.exit(main())
