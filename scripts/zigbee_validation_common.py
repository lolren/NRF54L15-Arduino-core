#!/usr/bin/env python3
"""Shared result handling for Zigbee hardware validation scripts."""

from pathlib import Path
from typing import Mapping, AbstractSet


REPO_ROOT = Path(__file__).resolve().parents[1]


def default_output_dir(name: str) -> str:
    return str(REPO_ROOT / ".build" / name)


def write_boolean_summary(
    path: Path,
    summary: Mapping[str, bool],
    *,
    expected_false: AbstractSet[str] = frozenset(),
) -> int:
    """Write results and return nonzero unless every expectation is satisfied."""
    path.write_text(
        "".join(f"{key}={str(value).lower()}\n" for key, value in summary.items())
    )
    print(path)
    failures = []
    if not summary:
        failures.append("<empty summary>")
    for key, value in summary.items():
        expected = key not in expected_false
        print(f"{key}={str(value).lower()}")
        if value != expected:
            failures.append(key)
    if failures:
        print("FAILED: " + ", ".join(failures))
        return 1
    return 0
