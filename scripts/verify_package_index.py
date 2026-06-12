#!/usr/bin/env python3
"""Verify package index metadata matches the release archive."""

from __future__ import annotations

import argparse
import hashlib
import json
import tarfile
import zipfile
from pathlib import Path

REQUIRED_PLATFORM_TOOL_DEPENDENCIES = {
    ("arduino", "arm-none-eabi-gcc", "7-2017q4"),
    ("arduino", "openocd", "0.11.0-arduino2"),
}


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def archive_link_entries(path: Path) -> list[str]:
    if path.suffix == ".zip":
        with zipfile.ZipFile(path) as zf:
            links = []
            for entry in zf.infolist():
                mode = (entry.external_attr >> 16) & 0o170000
                if mode == 0o120000:
                    links.append(entry.filename)
            return links

    with tarfile.open(path, "r:*") as tar:
        return [
            member.name
            for member in tar.getmembers()
            if member.issym() or member.islnk()
        ]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--index", required=True, type=Path, help="Path to package index JSON")
    parser.add_argument("--archive", required=True, type=Path, help="Path to release archive")
    parser.add_argument(
        "--version",
        default=None,
        help="Platform version to verify. If omitted, locate the entry by archiveFileName.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    index_path = args.index.resolve()
    archive_path = args.archive.resolve()

    if not index_path.is_file():
        raise SystemExit(f"Index file not found: {index_path}")
    if not archive_path.is_file():
        raise SystemExit(f"Archive file not found: {archive_path}")

    index = json.loads(index_path.read_text(encoding="utf-8"))

    try:
        platforms = index["packages"][0]["platforms"]
    except (KeyError, IndexError, TypeError) as exc:
        raise SystemExit(f"Unexpected package index structure: {exc}") from exc

    if not isinstance(platforms, list):
        raise SystemExit("Unexpected package index structure: platforms is not a list")

    platform = None
    for candidate in platforms:
        if not isinstance(candidate, dict):
            continue
        if args.version is not None and str(candidate.get("version", "")) == args.version:
            platform = candidate
            break
        if args.version is None and str(candidate.get("archiveFileName", "")) == archive_path.name:
            platform = candidate
            break

    if platform is None:
        if args.version is not None:
            raise SystemExit(f"Platform version {args.version!r} not found in package index")
        raise SystemExit(f"Archive {archive_path.name!r} not found in package index")

    expected_name = platform.get("archiveFileName")
    if expected_name != archive_path.name:
        raise SystemExit(
            f"archiveFileName mismatch: index={expected_name!r}, archive={archive_path.name!r}"
        )

    expected_size = str(archive_path.stat().st_size)
    actual_size = str(platform.get("size", ""))
    if actual_size != expected_size:
        raise SystemExit(f"size mismatch: index={actual_size}, archive={expected_size}")

    expected_sha = sha256_file(archive_path)
    actual_checksum = str(platform.get("checksum", ""))
    expected_checksum = f"SHA-256:{expected_sha}"
    if actual_checksum != expected_checksum:
        raise SystemExit(
            f"checksum mismatch: index={actual_checksum}, expected={expected_checksum}"
        )

    dependency_keys = {
        (str(dep.get("packager", "")), str(dep.get("name", "")), str(dep.get("version", "")))
        for dep in platform.get("toolsDependencies", [])
        if isinstance(dep, dict)
    }
    missing_dependencies = sorted(REQUIRED_PLATFORM_TOOL_DEPENDENCIES - dependency_keys)
    if missing_dependencies:
        raise SystemExit(f"missing required tool dependencies: {missing_dependencies}")

    links = archive_link_entries(archive_path)
    if links:
        preview = ", ".join(links[:10])
        suffix = "" if len(links) <= 10 else f", ... ({len(links)} total)"
        raise SystemExit(f"archive contains link entries: {preview}{suffix}")

    print("package index verification OK")
    print(f"archive: {archive_path}")
    print(f"size:    {expected_size}")
    print(f"sha256:  {expected_sha}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
