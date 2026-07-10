#!/usr/bin/env python3
"""Compile every shipped sketch against the source checkout.

By default each portable sketch is built for both XIAO chip families and
board-specific sketches are built for their matching profile. Use
--all-board-profiles to expand portable L15 sketches across every L15 FQBN.
Any compile failure produces a non-zero exit status.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / "hardware" / "nrf54l15clean" / "nrf54l15clean"
LOCAL_PACKAGER = "localnrf54"
DECLARED_COMPILER_VERSION = "7-2017q4"
PRIMARY_BOARDS = ("xiao_nrf54l15", "xiao_nrf54lm20b")


@dataclass(frozen=True)
class Job:
    sketch: str
    fqbn: str


@dataclass
class Result:
    sketch: str
    fqbn: str
    status: str
    returncode: int
    seconds: float
    log: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--all-board-profiles", action="store_true")
    parser.add_argument("--board", action="append", default=[], help="Limit to a board ID")
    parser.add_argument("--filter", default="", help="Only paths containing this text")
    parser.add_argument("--jobs", type=int, default=max(1, min(4, os.cpu_count() or 1)))
    parser.add_argument("--timeout", type=int, default=600, help="Seconds per compile")
    parser.add_argument("--report", type=Path, default=ROOT / "build" / "examples_compile_report.json")
    parser.add_argument("--keep-builds", action="store_true")
    return parser.parse_args()


def feature_options(relative: str) -> tuple[str, ...]:
    lowered = relative.lower()
    name = Path(relative).name.lower()
    options: list[str] = []
    if (
        "/thread/" in lowered
        or "openthread" in lowered
        or "threadexperimental" in lowered
        or name.startswith("thread")
    ):
        options.append("clean_thread=stage")
    if "joinerpsk" in lowered:
        if "clean_thread=stage" not in options:
            options.append("clean_thread=stage")
        options.append("clean_matter=stage")
    if "/chip/" in lowered or name.startswith("chipphase"):
        if "clean_thread=stage" not in options:
            options.append("clean_thread=stage")
        options.append("clean_matter=stage")
    if "/matter/" in lowered or name.startswith("matter"):
        if "clean_thread=stage" not in options:
            options.append("clean_thread=stage")
        options.append("clean_matter=stage")
    return tuple(options)


def advertised_boards() -> tuple[str, ...]:
    board_ids: set[str] = set()
    for raw_line in (PLATFORM / "boards.txt").read_text(encoding="utf-8").splitlines():
        key, separator, _value = raw_line.partition("=")
        if separator and key.endswith(".name") and key.count(".") == 1:
            board_ids.add(key[:-5])
    if not board_ids:
        raise SystemExit("boards.txt does not advertise any board profiles")
    return tuple(sorted(board_ids))


def applicable_boards(relative: str, all_profiles: bool) -> tuple[str, ...]:
    lowered = relative.lower()
    name = Path(relative).stem.lower()
    if "holyiot25008" in lowered:
        return ("holyiot_25008_nrf54l15",)
    if "holyiot25007" in lowered:
        return ("holyiot_25007_nrf54l15",)
    if "nrf54l15dk" in lowered:
        return ("nrf54l15dk_pca10156",)
    if "rawi2s" in lowered:
        return ("xiao_nrf54l15",)
    if any(token in lowered for token in ("/xiaolm20a/", "lm20a", "lm20b", "npm1300", "qspiflash", "libraries/adafruit_spiflash/")):
        return ("xiao_nrf54lm20b",)
    if any(token in lowered for token in ("/xiaol15/", "xiaosense", "/i2s/")):
        return ("xiao_nrf54l15",)
    if name.startswith("chipphase"):
        return PRIMARY_BOARDS
    if all_profiles:
        return advertised_boards()
    return PRIMARY_BOARDS


def discover_jobs(args: argparse.Namespace) -> list[Job]:
    selected_boards = set(args.board)
    known_boards = set(advertised_boards())
    unknown_boards = sorted(selected_boards - known_boards)
    if unknown_boards:
        raise SystemExit(f"unknown board ID(s): {', '.join(unknown_boards)}")
    jobs: set[Job] = set()
    for ino in sorted(PLATFORM.rglob("*.ino")):
        relative = ino.relative_to(PLATFORM).as_posix()
        if args.filter and args.filter.lower() not in relative.lower():
            continue
        options = feature_options(relative)
        for board in applicable_boards(relative, args.all_board_profiles):
            if selected_boards and board not in selected_boards:
                continue
            fqbn = f"{LOCAL_PACKAGER}:nrf54l15clean:{board}"
            if options:
                fqbn += ":" + ",".join(options)
            jobs.add(Job(relative, fqbn))

    # Every advertised profile gets at least one build even when most examples
    # are intentionally constrained to representative XIAO boards.
    probe = "examples/CoreVersionProbe/CoreVersionProbe.ino"
    if not args.filter or args.filter.lower() in probe.lower():
        for board in advertised_boards():
            if not selected_boards or board in selected_boards:
                jobs.add(Job(probe, f"{LOCAL_PACKAGER}:nrf54l15clean:{board}"))
    return sorted(jobs, key=lambda job: (job.sketch, job.fqbn))


def find_compiler_path() -> Path | None:
    override = os.environ.get("NRF54_ARM_GCC_PATH")
    if override:
        candidate = Path(override).expanduser()
        return candidate if candidate.is_dir() else None
    roots = (
        Path.home() / ".arduino15" / "packages" / "arduino" / "tools" / "arm-none-eabi-gcc",
        Path.home() / "Library" / "Arduino15" / "packages" / "arduino" / "tools" / "arm-none-eabi-gcc",
        Path(os.environ.get("LOCALAPPDATA", "")) / "Arduino15" / "packages" / "arduino" / "tools" / "arm-none-eabi-gcc",
    )
    for root in roots:
        declared = root / DECLARED_COMPILER_VERSION / "bin"
        if declared.is_dir():
            return declared
    return None


def make_local_cli_config(temp_root: Path) -> Path:
    user = temp_root / "user"
    data = temp_root / "data"
    downloads = temp_root / "downloads"
    # A distinct packager namespace prevents an installed Board Manager copy
    # from shadowing this checkout.
    hardware = user / "hardware" / LOCAL_PACKAGER
    hardware.mkdir(parents=True)
    data.mkdir()
    downloads.mkdir()
    (hardware / "nrf54l15clean").symlink_to(PLATFORM, target_is_directory=True)
    config = temp_root / "arduino-cli.yaml"
    config.write_text(
        "directories:\n"
        f"  data: {data}\n"
        f"  downloads: {downloads}\n"
        f"  user: {user}\n",
        encoding="utf-8",
    )
    return config


def run_job(job: Job, config: Path, build_root: Path, compiler: Path | None, timeout: int) -> Result:
    import time

    digest = hashlib.sha256(f"{job.sketch}\0{job.fqbn}".encode()).hexdigest()[:16]
    build_path = build_root / digest
    cmd = [
        "arduino-cli",
        "compile",
        "--config-file",
        str(config),
        "--fqbn",
        job.fqbn,
        "--build-path",
        str(build_path),
    ]
    if compiler is not None:
        cmd.extend(("--build-property", f"compiler.path={compiler}{os.sep}"))
    cmd.append(str(PLATFORM / job.sketch).rsplit(os.sep, 1)[0])
    started = time.monotonic()
    try:
        completed = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        output = completed.stdout + completed.stderr
        status = "pass" if completed.returncode == 0 else "fail"
        return Result(job.sketch, job.fqbn, status, completed.returncode, time.monotonic() - started, output[-12000:])
    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout or ""
        stderr = exc.stderr or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode(errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode(errors="replace")
        output = stdout + stderr
        return Result(job.sketch, job.fqbn, "timeout", 124, time.monotonic() - started, output[-12000:])


def main() -> int:
    args = parse_args()
    if args.jobs < 1 or args.timeout < 1:
        raise SystemExit("--jobs and --timeout must be positive")
    if shutil.which("arduino-cli") is None:
        raise SystemExit("arduino-cli is required")
    jobs = discover_jobs(args)
    if not jobs:
        raise SystemExit("no matching compile jobs")

    compiler = find_compiler_path()
    if compiler is None:
        raise SystemExit(
            f"ARM compiler {DECLARED_COMPILER_VERSION} bin directory not found; "
            "set NRF54_ARM_GCC_PATH"
        )
    temp_context = tempfile.TemporaryDirectory(prefix="nrf54-example-matrix-")
    temp_root = Path(temp_context.name)
    config = make_local_cli_config(temp_root)
    build_root = (ROOT / "build" / "example-builds") if args.keep_builds else (temp_root / "builds")
    build_root.mkdir(parents=True, exist_ok=True)

    print(f"Compiling {len(jobs)} source-local jobs with {args.jobs} worker(s)")
    results: list[Result] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        future_map = {
            executor.submit(run_job, job, config, build_root, compiler, args.timeout): job
            for job in jobs
        }
        for future in concurrent.futures.as_completed(future_map):
            result = future.result()
            results.append(result)
            print(f"[{result.status.upper():7}] {result.fqbn} {result.sketch} ({result.seconds:.1f}s)")

    results.sort(key=lambda result: (result.sketch, result.fqbn))
    args.report.parent.mkdir(parents=True, exist_ok=True)
    summary = {
        "sourcePlatform": str(PLATFORM),
        "localPackager": LOCAL_PACKAGER,
        "advertisedBoards": list(advertised_boards()),
        "sketches": len({result.sketch for result in results}),
        "total": len(results),
        "passed": sum(result.status == "pass" for result in results),
        "failed": sum(result.status != "pass" for result in results),
        "results": [asdict(result) for result in results],
    }
    args.report.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    if not args.keep_builds:
        temp_context.cleanup()

    print(f"Result: {summary['passed']}/{summary['total']} passed; report: {args.report}")
    for result in results:
        if result.status != "pass":
            print(f"\n--- {result.fqbn} {result.sketch} ---\n{result.log}", file=sys.stderr)
    return 1 if summary["failed"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
