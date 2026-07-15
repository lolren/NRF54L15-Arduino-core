#!/usr/bin/env python3
"""Compile the staged Thread/Matter examples from this checkout.

The normal package FQBN can be shadowed by an installed Board Manager release
with the same package and architecture. This script copies the local platform
under a temporary vendor namespace and compiles through that namespace, so the
result always validates the current working tree.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from typing import Iterable


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
PLATFORM_ROOT = REPO_ROOT / "hardware" / "nrf54l15clean" / "nrf54l15clean"
EXAMPLES_ROOT = (
    PLATFORM_ROOT
    / "libraries"
    / "Nrf54L15-Clean-Implementation"
    / "examples"
)


@dataclass(frozen=True)
class CompileCase:
    name: str
    board: str
    options: str
    sketch: pathlib.Path


DEFAULT_CASES = (
    CompileCase(
        "xiao_l15_thread_udp_soak",
        "xiao_nrf54l15",
        "clean_thread=stage",
        EXAMPLES_ROOT / "Thread" / "ThreadExperimentalUdpSoak",
    ),
    CompileCase(
        "xiao_l15_matter_command_surface",
        "xiao_nrf54l15",
        "clean_thread=stage,clean_matter=stage",
        EXAMPLES_ROOT / "Matter" / "MatterOnNetworkOnOffLightCommandSurfaceDemo",
    ),
    CompileCase(
        "xiao_lm20b_thread_pskc_udp",
        "xiao_nrf54lm20b",
        "clean_thread=stage",
        EXAMPLES_ROOT / "Thread" / "ThreadExperimentalPskcUdpHello",
    ),
    CompileCase(
        "xiao_lm20b_matter_onnetwork_node",
        "xiao_nrf54lm20b",
        "clean_thread=stage,clean_matter=stage",
        EXAMPLES_ROOT / "Matter" / "MatterOnNetworkOnOffLightNodeDemo",
    ),
    CompileCase(
        "holyiot25007_thread_pskc_udp",
        "holyiot_25007_nrf54l15",
        "clean_thread=stage",
        EXAMPLES_ROOT / "Thread" / "ThreadExperimentalPskcUdpHello",
    ),
    CompileCase(
        "holyiot25008_thread_pskc_udp",
        "holyiot_25008_nrf54l15",
        "clean_thread=stage",
        EXAMPLES_ROOT / "Thread" / "ThreadExperimentalPskcUdpHello",
    ),
    CompileCase(
        "generic_matter_onnetwork_node",
        "generic_nrf54l15_module_36pin",
        "clean_thread=stage,clean_matter=stage",
        EXAMPLES_ROOT / "Matter" / "MatterOnNetworkOnOffLightNodeDemo",
    ),
    CompileCase(
        "dk_thread_udp_soak",
        "nrf54l15dk_pca10156",
        "clean_thread=stage",
        EXAMPLES_ROOT / "Thread" / "ThreadExperimentalUdpSoak",
    ),
)


FULL_EXTRA_CASES = (
    CompileCase(
        "xiao_l15_chip_system_layer",
        "xiao_nrf54l15",
        "clean_thread=stage,clean_matter=stage",
        EXAMPLES_ROOT / "Chip" / "ChipPhase1SystemLayerTest",
    ),
    CompileCase(
        "xiao_l15_chip_crypto",
        "xiao_nrf54l15",
        "clean_thread=stage,clean_matter=stage",
        EXAMPLES_ROOT / "Chip" / "ChipPhase3CryptoTest",
    ),
    CompileCase(
        "xiao_lm20b_chip_crypto",
        "xiao_nrf54lm20b",
        "clean_thread=stage,clean_matter=stage",
        EXAMPLES_ROOT / "Chip" / "ChipPhase3CryptoTest",
    ),
    CompileCase(
        "xiao_l15_chip_inet_transport",
        "xiao_nrf54l15",
        "clean_thread=stage,clean_matter=stage",
        EXAMPLES_ROOT / "Chip" / "ChipPhase5TransportTest",
    ),
    CompileCase(
        "xiao_l15_thread_reconnect_stress",
        "xiao_nrf54l15",
        "clean_thread=stage",
        EXAMPLES_ROOT / "Thread" / "ThreadExperimentalReconnectStress",
    ),
    CompileCase(
        "xiao_l15_thread_reference_attach",
        "xiao_nrf54l15",
        "clean_thread=stage",
        EXAMPLES_ROOT / "Thread" / "ThreadExperimentalReferenceDatasetAttach",
    ),
    CompileCase(
        "xiao_l15_thread_sleepy_child",
        "xiao_nrf54l15",
        "clean_thread=stage",
        EXAMPLES_ROOT / "Thread" / "ThreadExperimentalSleepyChild",
    ),
    CompileCase(
        "xiao_l15_matter_pase_case_full",
        "xiao_nrf54l15",
        "clean_thread=stage,clean_matter=stage",
        EXAMPLES_ROOT / "Matter" / "MatterPaseCaseFullDemo",
    ),
    CompileCase(
        "xiao_l15_matter_commission_control",
        "xiao_nrf54l15",
        "clean_thread=stage,clean_matter=stage",
        EXAMPLES_ROOT / "Matter" / "MatterCommissionAndControl",
    ),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compile staged Thread/Matter examples from the local checkout."
    )
    parser.add_argument("--arduino-cli", default="arduino-cli")
    parser.add_argument(
        "--data-dir",
        type=pathlib.Path,
        default=pathlib.Path(
            os.environ.get(
                "ARDUINO_DIRECTORIES_DATA", pathlib.Path.home() / ".arduino15"
            )
        ),
        help="Arduino CLI data directory containing installed tools.",
    )
    parser.add_argument(
        "--work-dir",
        type=pathlib.Path,
        default=None,
        help="Temporary work directory. Defaults to a new /tmp directory.",
    )
    parser.add_argument(
        "--vendor",
        default="localnrf54",
        help="Temporary package vendor name used to avoid installed-core shadowing.",
    )
    parser.add_argument(
        "--full",
        action="store_true",
        help="Also compile slower stress/recovery Matter and Thread examples.",
    )
    parser.add_argument(
        "--case",
        action="append",
        choices=[case.name for case in DEFAULT_CASES + FULL_EXTRA_CASES],
        help="Compile only this named case. May be supplied more than once.",
    )
    parser.add_argument(
        "--keep",
        action="store_true",
        help="Keep the temporary sketchbook, logs, and build directories.",
    )
    return parser.parse_args()


def prepare_work_dir(args: argparse.Namespace) -> pathlib.Path:
    if args.work_dir is not None:
        work_dir = args.work_dir.resolve()
        shutil.rmtree(work_dir, ignore_errors=True)
        work_dir.mkdir(parents=True)
        return work_dir

    return pathlib.Path(tempfile.mkdtemp(prefix="nrf54-thread-matter-matrix-"))


def copy_platform(work_dir: pathlib.Path, vendor: str) -> pathlib.Path:
    if not PLATFORM_ROOT.is_dir():
        raise SystemExit(f"Missing platform root: {PLATFORM_ROOT}")

    platform_copy = work_dir / "sketchbook" / "hardware" / vendor / "nrf54l15clean"
    platform_copy.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(
        PLATFORM_ROOT,
        platform_copy,
        ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "build", ".git"),
    )
    return platform_copy


def write_cli_config(work_dir: pathlib.Path, data_dir: pathlib.Path) -> pathlib.Path:
    config_path = work_dir / "arduino-cli.yaml"
    sketchbook = work_dir / "sketchbook"
    data_dir = data_dir.expanduser().resolve()
    config_path.write_text(
        "\n".join(
            [
                "directories:",
                f"  data: {data_dir}",
                f"  downloads: {data_dir / 'staging'}",
                f"  user: {sketchbook}",
                "",
            ]
        ),
        encoding="utf-8",
    )
    return config_path


def fqbn(vendor: str, case: CompileCase) -> str:
    return f"{vendor}:nrf54l15clean:{case.board}:{case.options}"


def run_case(
    arduino_cli: str,
    config_path: pathlib.Path,
    vendor: str,
    work_dir: pathlib.Path,
    case: CompileCase,
) -> bool:
    log_dir = work_dir / "logs"
    build_dir = work_dir / "build" / case.name
    log_dir.mkdir(parents=True, exist_ok=True)
    shutil.rmtree(build_dir, ignore_errors=True)

    log_path = log_dir / f"{case.name}.log"
    command = [
        arduino_cli,
        "--config-file",
        str(config_path),
        "compile",
        "--fqbn",
        fqbn(vendor, case),
        str(case.sketch),
        "--warnings",
        "all",
        "--clean",
        "--build-path",
        str(build_dir),
    ]

    print(f"== {case.name} ==", flush=True)
    result = subprocess.run(
        command,
        cwd=str(REPO_ROOT),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=360,
    )
    log_path.write_text(result.stdout, encoding="utf-8", errors="replace")
    tail = "\n".join(result.stdout.splitlines()[-6:])
    if tail:
        print(tail, flush=True)
    if result.returncode != 0:
        print(f"FAILED: full log at {log_path}", flush=True)
        return False
    if "warning:" in result.stdout.lower():
        print(f"FAILED: compiler warning in {log_path}", flush=True)
        return False
    return True


def run_cases(
    arduino_cli: str,
    config_path: pathlib.Path,
    vendor: str,
    work_dir: pathlib.Path,
    cases: Iterable[CompileCase],
) -> bool:
    ok = True
    for case in cases:
        if not run_case(arduino_cli, config_path, vendor, work_dir, case):
            ok = False
            break
    return ok


def main() -> int:
    args = parse_args()
    work_dir = prepare_work_dir(args)
    try:
        copy_platform(work_dir, args.vendor)
        config_path = write_cli_config(work_dir, args.data_dir)
        if args.case:
            selected = set(args.case)
            cases = tuple(
                case for case in DEFAULT_CASES + FULL_EXTRA_CASES
                if case.name in selected
            )
        else:
            cases = DEFAULT_CASES + (FULL_EXTRA_CASES if args.full else ())
        ok = run_cases(args.arduino_cli, config_path, args.vendor, work_dir, cases)
        if ok:
            print(f"PASS: compiled {len(cases)} Thread/Matter cases", flush=True)
            if args.keep:
                print(f"kept work dir: {work_dir}", flush=True)
            return 0
        return 1
    finally:
        if not args.keep:
            shutil.rmtree(work_dir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
