#!/usr/bin/env python3
"""Capture and analyze Channel Sounding accuracy/calibration runs.

This harness wraps the two-board connected CS workflow regression, keeps the
serial logs in a stable output directory, extracts `cs_accuracy_sample` records,
and optionally emits calibration profile artifacts through
channel_sounding_calibration.py.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
ACCURACY_PREFIX = "cs_accuracy_sample "
NUMERIC_FIELDS = {
    "profile",
    "profile_channels",
    "executed_channels",
    "rtt_enabled",
    "requested_channels",
    "valid_channels",
    "used_channels",
    "total_channels",
    "rtt_channels",
    "rejected_low",
    "rejected_residual",
    "phase_raw_m",
    "phase_m",
    "rtt_m",
    "dist_raw_m",
    "dist_m",
    "slope",
    "residual",
    "rtt_var",
    "median_quality",
    "fit_delta_m",
    "confidence",
    "calib_scale",
    "calib_offset_m",
    "calibrated_window",
    "typical_error_m",
    "conservative_error_m",
    "lower_m",
    "upper_m",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run and summarize BLE Channel Sounding accuracy captures."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    capture = subparsers.add_parser(
        "capture",
        help="Flash/run the Arduino two-board workflow and analyze captured logs.",
    )
    capture.add_argument("--runs", type=int, default=1)
    capture.add_argument(
        "--profiles",
        default="0",
        help="Comma-separated channel profiles: 0=fast, 1=wide, 2=full.",
    )
    capture.add_argument("--capture-seconds", type=float, default=45.0)
    capture.add_argument("--central-port", default=os.environ.get("CS_CENTRAL_PORT", "/dev/ttyACM1"))
    capture.add_argument("--peripheral-port", default=os.environ.get("CS_PERIPHERAL_PORT", "/dev/ttyACM0"))
    capture.add_argument("--central-uid", default=os.environ.get("CS_CENTRAL_UID", "761FDE87"))
    capture.add_argument("--peripheral-uid", default=os.environ.get("CS_PERIPHERAL_UID", "E91217E8"))
    capture.add_argument("--output-dir", default="")
    capture.add_argument("--source", default="connected")
    capture.add_argument("--reference-distance", type=float, default=math.nan)
    capture.add_argument("--profile-name", default="")
    capture.add_argument("--board-pair", default="")
    capture.add_argument("--notes", default="")
    capture.add_argument(
        "--enable-rtt",
        action="store_true",
        help="Build the central diagnostic with CS_CONNECTED_ENABLE_RTT=1.",
    )
    capture.add_argument(
        "--skip-calibration-artifacts",
        action="store_true",
        help="Do not emit profile JSON/header even when --reference-distance is set.",
    )

    analyze = subparsers.add_parser(
        "analyze",
        help="Analyze existing central/peripheral logs containing cs_accuracy_sample lines.",
    )
    analyze.add_argument("logs", nargs="+")
    analyze.add_argument("--output-dir", default="")
    analyze.add_argument("--source", default="connected")
    analyze.add_argument("--reference-distance", type=float, default=math.nan)
    analyze.add_argument("--profile-name", default="")
    analyze.add_argument("--board-pair", default="")
    analyze.add_argument("--notes", default="")
    analyze.add_argument("--skip-calibration-artifacts", action="store_true")

    return parser.parse_args()


def timestamped_output_dir(prefix: str) -> Path:
    stamp = time.strftime("%Y%m%d_%H%M%S")
    return REPO_ROOT / "dist" / "cs_accuracy" / f"{prefix}_{stamp}"


def parse_number(value: str) -> float | None:
    lowered = value.strip().lower()
    if lowered in {"nan", "na", "none", ""}:
        return None
    try:
        parsed = float(lowered)
    except ValueError:
        return None
    if not math.isfinite(parsed):
        return None
    return parsed


def parse_accuracy_line(line: str, path: Path) -> dict[str, object] | None:
    if ACCURACY_PREFIX not in line:
        return None
    payload = line[line.index(ACCURACY_PREFIX) + len(ACCURACY_PREFIX):].strip()
    record: dict[str, object] = {"log_path": str(path)}
    for key, value in re.findall(r"([A-Za-z0-9_]+)=([^ \r\n]+)", payload):
        if key in NUMERIC_FIELDS:
            parsed = parse_number(value)
            if parsed is not None:
                record[key] = parsed
            else:
                record[key] = value
        else:
            record[key] = value
    return record


def extract_samples(log_paths: list[Path], source: str) -> list[dict[str, object]]:
    samples: list[dict[str, object]] = []
    for path in log_paths:
        if not path.is_file():
            continue
        with path.open("r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                record = parse_accuracy_line(line, path)
                if record is None:
                    continue
                if source and str(record.get("source", "")) != source:
                    continue
                samples.append(record)
    return samples


def numeric_values(samples: list[dict[str, object]], field: str) -> list[float]:
    values: list[float] = []
    for sample in samples:
        value = sample.get(field)
        if isinstance(value, (int, float)) and math.isfinite(float(value)):
            values.append(float(value))
    return values


def median_absolute_deviation(values: list[float], median_value: float) -> float:
    return statistics.median(abs(value - median_value) for value in values)


def summarize_field(samples: list[dict[str, object]], field: str) -> dict[str, float] | None:
    values = numeric_values(samples, field)
    if not values:
        return None
    median_value = statistics.median(values)
    mean_value = sum(values) / float(len(values))
    variance = sum((value - mean_value) ** 2 for value in values) / float(len(values))
    return {
        "count": float(len(values)),
        "median": median_value,
        "mean": mean_value,
        "mad": median_absolute_deviation(values, median_value),
        "stddev": math.sqrt(variance),
        "minimum": min(values),
        "maximum": max(values),
    }


def write_samples_csv(path: Path, samples: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "source",
        "profile",
        "profile_channels",
        "executed_channels",
        "rtt_enabled",
        "requested_channels",
        "valid_channels",
        "used_channels",
        "total_channels",
        "rtt_channels",
        "phase_raw_m",
        "phase_m",
        "dist_raw_m",
        "dist_m",
        "rtt_m",
        "residual",
        "median_quality",
        "fit_delta_m",
        "confidence",
        "confidence_label",
        "log_path",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        for sample in samples:
            writer.writerow(sample)


def write_filtered_log(path: Path, samples: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        for sample in samples:
            fields = []
            for key in (
                "source",
                "profile",
                "profile_channels",
                "executed_channels",
                "rtt_enabled",
                "requested_channels",
                "valid_channels",
                "used_channels",
                "total_channels",
                "phase_raw_m",
                "phase_m",
                "dist_raw_m",
                "dist_m",
                "confidence",
            ):
                if key in sample:
                    fields.append(f"{key}={sample[key]}")
            handle.write(f"{ACCURACY_PREFIX}{' '.join(fields)}\n")


def build_summary(samples: list[dict[str, object]], reference_distance: float) -> dict[str, object]:
    summary: dict[str, object] = {"sample_count": len(samples)}
    for field in ("phase_raw_m", "phase_m", "dist_raw_m", "dist_m", "confidence"):
        field_summary = summarize_field(samples, field)
        if field_summary is not None:
            summary[field] = field_summary
    if math.isfinite(reference_distance):
        values = numeric_values(samples, "phase_raw_m")
        if values:
            abs_errors = sorted(abs(value - reference_distance) for value in values)
            p90_index = min(len(abs_errors) - 1, math.ceil(0.9 * len(abs_errors)) - 1)
            summary["reference_distance_m"] = reference_distance
            summary["phase_raw_abs_error_median"] = statistics.median(abs_errors)
            summary["phase_raw_abs_error_p90"] = abs_errors[p90_index]
    labels: dict[str, int] = {}
    for sample in samples:
        label = str(sample.get("confidence_label", "unknown"))
        labels[label] = labels.get(label, 0) + 1
    summary["confidence_labels"] = labels
    return summary


def write_summary_md(path: Path, summary: dict[str, object], samples_csv: Path) -> None:
    lines = [
        "# Channel Sounding Accuracy Summary",
        "",
        f"- Samples: {summary.get('sample_count', 0)}",
        f"- CSV: `{samples_csv}`",
        "",
    ]
    for field in ("phase_raw_m", "phase_m", "dist_raw_m", "dist_m", "confidence"):
        field_summary = summary.get(field)
        if not isinstance(field_summary, dict):
            continue
        lines.append(f"## {field}")
        lines.append("")
        lines.append(
            "- "
            f"median={field_summary['median']:.4f}, "
            f"mad={field_summary['mad']:.4f}, "
            f"stddev={field_summary['stddev']:.4f}, "
            f"min={field_summary['minimum']:.4f}, "
            f"max={field_summary['maximum']:.4f}"
        )
        lines.append("")
    if "reference_distance_m" in summary:
        lines.append("## Reference Error")
        lines.append("")
        lines.append(
            "- "
            f"reference_m={summary['reference_distance_m']:.4f}, "
            f"median_abs_error_m={summary['phase_raw_abs_error_median']:.4f}, "
            f"p90_abs_error_m={summary['phase_raw_abs_error_p90']:.4f}"
        )
        lines.append("")
    lines.append("## Confidence Labels")
    lines.append("")
    for label, count in sorted(dict(summary.get("confidence_labels", {})).items()):
        lines.append(f"- {label}: {count}")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def maybe_emit_calibration_artifacts(
    *,
    filtered_log: Path,
    output_dir: Path,
    reference_distance: float,
    profile_name: str,
    board_pair: str,
    notes: str,
    skip: bool,
) -> None:
    if skip or not math.isfinite(reference_distance):
        return
    if not profile_name:
        profile_name = "BleCsCalibrationProfileSlice8"
    json_path = output_dir / f"{profile_name}.json"
    header_path = output_dir / f"{profile_name}.h"
    cmd = [
        sys.executable,
        str(REPO_ROOT / "scripts" / "channel_sounding_calibration.py"),
        "analyze",
        str(filtered_log),
        "--metric",
        "phase",
        "--reference-distance",
        f"{reference_distance:.6f}",
        "--profile-name",
        profile_name,
        "--board-pair",
        board_pair,
        "--notes",
        notes,
        "--emit-profile-json",
        str(json_path),
        "--emit-profile-header",
        str(header_path),
    ]
    subprocess.run(cmd, cwd=REPO_ROOT, check=True)


def analyze_logs(args: argparse.Namespace, output_dir: Path, log_paths: list[Path]) -> int:
    samples = extract_samples(log_paths, args.source)
    if not samples:
        print(f"cs_accuracy=FAIL reason=no_samples source={args.source}", file=sys.stderr)
        return 1
    samples_csv = output_dir / "cs_accuracy_samples.csv"
    filtered_log = output_dir / f"cs_accuracy_{args.source}.log"
    summary_json = output_dir / "cs_accuracy_summary.json"
    summary_md = output_dir / "cs_accuracy_summary.md"
    write_samples_csv(samples_csv, samples)
    write_filtered_log(filtered_log, samples)
    summary = build_summary(samples, args.reference_distance)
    summary_json.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    write_summary_md(summary_md, summary, samples_csv)
    maybe_emit_calibration_artifacts(
        filtered_log=filtered_log,
        output_dir=output_dir,
        reference_distance=args.reference_distance,
        profile_name=args.profile_name,
        board_pair=args.board_pair,
        notes=args.notes,
        skip=args.skip_calibration_artifacts,
    )
    print(f"cs_accuracy=PASS samples={len(samples)} output={output_dir}")
    if isinstance(summary.get("phase_raw_m"), dict):
        phase = summary["phase_raw_m"]
        print(
            "phase_raw "
            f"median={phase['median']:.4f} mad={phase['mad']:.4f} "
            f"stddev={phase['stddev']:.4f}"
        )
    if isinstance(summary.get("confidence"), dict):
        confidence = summary["confidence"]
        print(f"confidence median={confidence['median']:.1f}")
    return 0


def run_capture(args: argparse.Namespace) -> int:
    output_dir = Path(args.output_dir).expanduser().resolve() if args.output_dir else timestamped_output_dir("capture")
    output_dir.mkdir(parents=True, exist_ok=True)
    profiles = [profile.strip() for profile in args.profiles.split(",") if profile.strip()]
    if not profiles:
        raise RuntimeError("At least one channel profile is required")

    central_logs: list[Path] = []
    for profile in profiles:
        if profile not in {"0", "1", "2"}:
            raise RuntimeError(f"Unsupported channel profile: {profile}")
        for run_index in range(args.runs):
            run_dir = output_dir / f"profile{profile}_run{run_index + 1}"
            run_dir.mkdir(parents=True, exist_ok=True)
            env = dict(os.environ)
            env["CS_CENTRAL_PORT"] = args.central_port
            env["CS_PERIPHERAL_PORT"] = args.peripheral_port
            env["CS_CENTRAL_UID"] = args.central_uid
            env["CS_PERIPHERAL_UID"] = args.peripheral_uid
            env["CS_CAPTURE_SECONDS"] = str(args.capture_seconds)
            env["CS_LOG_DIR"] = str(run_dir)
            inherited_flags = env.get("CS_CENTRAL_CPP_FLAGS") or env.get("CS_CPP_EXTRA_FLAGS", "")
            profile_flag = f"-DCS_CONNECTED_PHYSICAL_CHANNEL_PROFILE={profile}"
            if args.enable_rtt:
                profile_flag = f"{profile_flag} -DCS_CONNECTED_ENABLE_RTT=1"
            env["CS_CENTRAL_CPP_FLAGS"] = (
                f"{inherited_flags} {profile_flag}".strip()
            )
            cmd = [str(REPO_ROOT / "scripts" / "test_cs_ll_workflow_bridge.sh")]
            with (run_dir / "runner.log").open("w", encoding="utf-8") as log:
                proc = subprocess.run(
                    cmd,
                    cwd=REPO_ROOT,
                    env=env,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    check=False,
                )
                log.write(proc.stdout)
            if proc.returncode != 0:
                print(proc.stdout[-6000:], file=sys.stderr)
                print(
                    f"cs_accuracy=FAIL reason=runner profile={profile} run={run_index + 1}",
                    file=sys.stderr,
                )
                return proc.returncode
            central_log = run_dir / "central.log"
            central_logs.append(central_log)

    return analyze_logs(args, output_dir, central_logs)


def run_analyze(args: argparse.Namespace) -> int:
    output_dir = Path(args.output_dir).expanduser().resolve() if args.output_dir else timestamped_output_dir("analyze")
    output_dir.mkdir(parents=True, exist_ok=True)
    logs = [Path(path).expanduser().resolve() for path in args.logs]
    return analyze_logs(args, output_dir, logs)


def main() -> int:
    args = parse_args()
    if args.command == "capture":
        return run_capture(args)
    if args.command == "analyze":
        return run_analyze(args)
    raise RuntimeError(f"Unsupported command: {args.command}")


if __name__ == "__main__":
    raise SystemExit(main())
