#!/usr/bin/env python3
"""Compare Arduino CS distance output against Zephyr at one fixed placement.

This is intentionally a parity test, not a final calibration fixture. It checks
whether the clean-core Arduino median is close to a Zephyr median captured with
the boards left in the same physical position.
"""

from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[0]
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from test_cs_accuracy_calibration import extract_samples, summarize_field  # noqa: E402
from zephyr_channel_sounding_validation import (  # noqa: E402
    extract_zephyr_distance_samples,
    summarize_zephyr_distance_samples,
)


DEFAULT_OUTPUT_ROOT = REPO_ROOT / "dist" / "cs_distance_parity"
DEFAULT_TOLERANCE_M = 0.50


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Compare clean-core Arduino connected-CS distance records against "
            "official Zephyr connected-CS logs captured at the same board spacing."
        )
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    compare = subparsers.add_parser(
        "compare",
        help="Compare existing Arduino and Zephyr logs.",
    )
    add_compare_args(compare)

    capture = subparsers.add_parser(
        "capture",
        help="Run Arduino CS accuracy capture, then compare it to Zephyr logs if supplied.",
    )
    capture.add_argument("--runs", type=int, default=1)
    capture.add_argument(
        "--profiles",
        default="0",
        help="Comma-separated Arduino CS channel profiles. Default: 0.",
    )
    capture.add_argument("--capture-seconds", type=float, default=45.0)
    capture.add_argument("--central-port", default=os.environ.get("CS_CENTRAL_PORT", "/dev/ttyACM1"))
    capture.add_argument("--peripheral-port", default=os.environ.get("CS_PERIPHERAL_PORT", "/dev/ttyACM0"))
    capture.add_argument("--central-uid", default=os.environ.get("CS_CENTRAL_UID", "761FDE87"))
    capture.add_argument("--peripheral-uid", default=os.environ.get("CS_PERIPHERAL_UID", "E91217E8"))
    capture.add_argument("--enable-rtt", action="store_true")
    add_compare_args(capture, arduino_logs_required=False)

    return parser.parse_args()


def add_compare_args(parser: argparse.ArgumentParser, *, arduino_logs_required: bool = True) -> None:
    parser.add_argument(
        "--arduino-log",
        nargs="+",
        required=arduino_logs_required,
        default=[],
        help="One or more Arduino logs containing cs_accuracy_sample records.",
    )
    parser.add_argument(
        "--zephyr-log",
        nargs="*",
        default=[],
        help=(
            "One or more official Zephyr connected_cs logs. Missing Zephyr logs "
            "make the comparison BLOCKED unless --allow-missing-zephyr is set."
        ),
    )
    parser.add_argument("--arduino-source", default="connected")
    parser.add_argument(
        "--arduino-field",
        choices=("phase_raw_m", "phase_m", "dist_raw_m", "dist_m"),
        default="phase_raw_m",
        help="Arduino distance field to compare. Default: phase_raw_m.",
    )
    parser.add_argument(
        "--zephyr-method",
        choices=("phase", "rtt"),
        default="phase",
        help="Zephyr distance method to compare. Default: phase.",
    )
    parser.add_argument(
        "--compare-mode",
        choices=("absolute", "raw"),
        default="absolute",
        help=(
            "Compare absolute medians or signed raw medians. Zephyr phase-slope "
            "can be signed while Arduino reports physical positive distance. "
            "Default: absolute."
        ),
    )
    parser.add_argument(
        "--tolerance-m",
        type=float,
        default=DEFAULT_TOLERANCE_M,
        help=f"Allowed absolute median delta in meters. Default: {DEFAULT_TOLERANCE_M}.",
    )
    parser.add_argument(
        "--allow-missing-zephyr",
        action="store_true",
        help="Write BLOCKED output but exit 0 when no Zephyr distance samples are available.",
    )
    parser.add_argument(
        "--output-dir",
        default="",
        help="Directory for parity JSON/Markdown outputs.",
    )


def timestamped_output_dir(prefix: str) -> Path:
    stamp = time.strftime("%Y%m%d_%H%M%S")
    return DEFAULT_OUTPUT_ROOT / f"{prefix}_{stamp}"


def summary_median(summary: dict[str, float] | None) -> float | None:
    if not summary:
        return None
    value = summary.get("median")
    if value is None or not math.isfinite(float(value)):
        return None
    return float(value)


def collect_arduino_summary(
    log_paths: list[Path], source: str, field: str
) -> tuple[list[dict[str, object]], dict[str, float] | None]:
    samples = extract_samples(log_paths, source)
    return samples, summarize_field(samples, field)


def collect_zephyr_summary(
    log_paths: list[Path], method: str
) -> tuple[list[dict[str, object]], dict[str, float] | None]:
    samples: list[dict[str, object]] = []
    for path in log_paths:
        samples.extend(extract_zephyr_distance_samples(path))
    return samples, summarize_zephyr_distance_samples(samples, method)


def serializable_summary(summary: dict[str, float] | None) -> dict[str, float] | None:
    if summary is None:
        return None
    return {key: float(value) for key, value in summary.items()}


def build_result(
    *,
    arduino_logs: list[Path],
    zephyr_logs: list[Path],
    arduino_source: str,
    arduino_field: str,
    zephyr_method: str,
    tolerance_m: float,
    compare_mode: str,
) -> tuple[dict[str, object], int]:
    arduino_samples, arduino_summary = collect_arduino_summary(
        arduino_logs, arduino_source, arduino_field
    )
    zephyr_samples, zephyr_summary = collect_zephyr_summary(zephyr_logs, zephyr_method)

    arduino_median = summary_median(arduino_summary)
    zephyr_median = summary_median(zephyr_summary)

    result: dict[str, object] = {
        "arduino_logs": [str(path) for path in arduino_logs],
        "zephyr_logs": [str(path) for path in zephyr_logs],
        "arduino_source": arduino_source,
        "arduino_field": arduino_field,
        "zephyr_method": zephyr_method,
        "compare_mode": compare_mode,
        "tolerance_m": tolerance_m,
        "arduino_sample_count": len(arduino_samples),
        "zephyr_sample_count": len(zephyr_samples),
        "zephyr_method_sample_count": int(zephyr_summary["count"]) if zephyr_summary else 0,
        "arduino_summary": serializable_summary(arduino_summary),
        "zephyr_summary": serializable_summary(zephyr_summary),
    }

    if arduino_median is None:
        result["status"] = "FAIL"
        result["reason"] = "no_arduino_samples"
        return result, 1

    if zephyr_median is None:
        result["status"] = "BLOCKED"
        result["reason"] = "no_zephyr_distance_samples"
        result["arduino_median_m"] = arduino_median
        return result, 2

    arduino_compare_m = abs(arduino_median) if compare_mode == "absolute" else arduino_median
    zephyr_compare_m = abs(zephyr_median) if compare_mode == "absolute" else zephyr_median
    delta_m = abs(arduino_compare_m - zephyr_compare_m)
    result.update(
        {
            "arduino_median_m": arduino_median,
            "zephyr_median_m": zephyr_median,
            "arduino_compare_m": arduino_compare_m,
            "zephyr_compare_m": zephyr_compare_m,
            "delta_m": delta_m,
            "status": "PASS" if delta_m <= tolerance_m else "FAIL",
        }
    )
    return result, 0 if delta_m <= tolerance_m else 1


def write_summary_md(path: Path, result: dict[str, object]) -> None:
    lines = [
        "# CS Distance Parity Summary",
        "",
        f"- Status: {result.get('status')}",
        f"- Arduino metric: `{result.get('arduino_field')}`",
        f"- Zephyr metric: `{result.get('zephyr_method')}`",
        f"- Compare mode: `{result.get('compare_mode')}`",
        f"- Tolerance: {float(result.get('tolerance_m', 0.0)):.3f} m",
        "",
    ]
    if "reason" in result:
        lines.append(f"- Reason: {result['reason']}")
    if "arduino_median_m" in result:
        lines.append(f"- Arduino median: {float(result['arduino_median_m']):.6f} m")
    if "zephyr_median_m" in result:
        lines.append(f"- Zephyr median: {float(result['zephyr_median_m']):.6f} m")
    if "arduino_compare_m" in result and "zephyr_compare_m" in result:
        lines.append(
            "- Compared medians: "
            f"Arduino {float(result['arduino_compare_m']):.6f} m, "
            f"Zephyr {float(result['zephyr_compare_m']):.6f} m"
        )
    if "delta_m" in result:
        lines.append(f"- Median delta: {float(result['delta_m']):.6f} m")
    lines.extend(
        [
            f"- Arduino samples: {result.get('arduino_sample_count', 0)}",
            f"- Zephyr parsed distance lines: {result.get('zephyr_sample_count', 0)}",
            f"- Zephyr compared-method samples: {result.get('zephyr_method_sample_count', 0)}",
            "",
            "## Interpretation",
            "",
            (
                "This test is valid only when Arduino and Zephyr logs were captured "
                "without moving the boards. It proves fixed-placement parity; it does "
                "not replace measured multi-distance calibration."
            ),
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8")


def write_outputs(output_dir: Path, result: dict[str, object]) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "cs_distance_parity_summary.json").write_text(
        json.dumps(result, indent=2) + "\n",
        encoding="utf-8",
    )
    write_summary_md(output_dir / "cs_distance_parity_summary.md", result)


def print_result(result: dict[str, object], output_dir: Path) -> None:
    status = str(result.get("status", "UNKNOWN"))
    fields = [
        f"cs_distance_parity={status}",
        f"output={output_dir}",
        f"arduino_samples={result.get('arduino_sample_count', 0)}",
        f"zephyr_samples={result.get('zephyr_sample_count', 0)}",
        f"zephyr_method_samples={result.get('zephyr_method_sample_count', 0)}",
    ]
    if "arduino_median_m" in result:
        fields.append(f"arduino_m={float(result['arduino_median_m']):.6f}")
    if "zephyr_median_m" in result:
        fields.append(f"zephyr_m={float(result['zephyr_median_m']):.6f}")
    if "arduino_compare_m" in result and "zephyr_compare_m" in result:
        fields.append(f"arduino_cmp_m={float(result['arduino_compare_m']):.6f}")
        fields.append(f"zephyr_cmp_m={float(result['zephyr_compare_m']):.6f}")
    if "delta_m" in result:
        fields.append(f"delta_m={float(result['delta_m']):.6f}")
    if "reason" in result:
        fields.append(f"reason={result['reason']}")
    print(" ".join(fields))


def run_compare(args: argparse.Namespace) -> int:
    output_dir = (
        Path(args.output_dir).expanduser().resolve()
        if args.output_dir
        else timestamped_output_dir("compare")
    )
    arduino_logs = [Path(path).expanduser().resolve() for path in args.arduino_log]
    zephyr_logs = [Path(path).expanduser().resolve() for path in args.zephyr_log]
    result, exit_code = build_result(
        arduino_logs=arduino_logs,
        zephyr_logs=zephyr_logs,
        arduino_source=args.arduino_source,
        arduino_field=args.arduino_field,
        zephyr_method=args.zephyr_method,
        tolerance_m=args.tolerance_m,
        compare_mode=args.compare_mode,
    )
    write_outputs(output_dir, result)
    print_result(result, output_dir)
    if exit_code == 2 and args.allow_missing_zephyr:
        return 0
    return exit_code


def run_arduino_capture(args: argparse.Namespace, output_dir: Path) -> list[Path]:
    arduino_dir = output_dir / "arduino_capture"
    cmd = [
        sys.executable,
        str(SCRIPT_DIR / "test_cs_accuracy_calibration.py"),
        "capture",
        "--runs",
        str(args.runs),
        "--profiles",
        args.profiles,
        "--capture-seconds",
        str(args.capture_seconds),
        "--central-port",
        args.central_port,
        "--peripheral-port",
        args.peripheral_port,
        "--central-uid",
        args.central_uid,
        "--peripheral-uid",
        args.peripheral_uid,
        "--output-dir",
        str(arduino_dir),
        "--skip-calibration-artifacts",
    ]
    if args.enable_rtt:
        cmd.append("--enable-rtt")

    proc = subprocess.run(
        cmd,
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "arduino_capture_runner.log").write_text(proc.stdout, encoding="utf-8")
    print(proc.stdout, end="")
    if proc.returncode != 0:
        raise RuntimeError(f"Arduino capture failed with exit code {proc.returncode}")

    central_logs = sorted(arduino_dir.glob("profile*_run*/central.log"))
    if central_logs:
        return central_logs

    match = re.search(r"\boutput=([^ \r\n]+)", proc.stdout)
    if match:
        parsed_dir = Path(match.group(1)).expanduser().resolve()
        return sorted(parsed_dir.glob("profile*_run*/central.log"))
    return []


def run_capture(args: argparse.Namespace) -> int:
    output_dir = (
        Path(args.output_dir).expanduser().resolve()
        if args.output_dir
        else timestamped_output_dir("capture")
    )
    arduino_logs = run_arduino_capture(args, output_dir)
    args.arduino_log = [str(path) for path in arduino_logs]
    args.output_dir = str(output_dir)
    return run_compare(args)


def main() -> int:
    args = parse_args()
    try:
        if args.command == "compare":
            return run_compare(args)
        if args.command == "capture":
            return run_capture(args)
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    raise RuntimeError(f"Unsupported command: {args.command}")


if __name__ == "__main__":
    raise SystemExit(main())
