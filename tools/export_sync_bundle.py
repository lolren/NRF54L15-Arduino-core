#!/usr/bin/env python3
from __future__ import annotations

import argparse
import fnmatch
import os
import shutil
import zipfile
from pathlib import Path
from typing import Iterable, List

DEFAULT_EXCLUDES = [
    ".git/**",
    ".git",
    "dist/**",
    "sync_export/**",
    "**/__pycache__/**",
    "**/*.pyc",
    "**/.DS_Store",
    "**/tools/.arduino-zephyr-build/**",
    "**/tools/ncs/**",
    "**/tools/ncs.partial/**",
    "**/tools/zephyr-sdk/**",
    "**/tools/host-tools/**",
    "**/tools/toolchain-bootstrap/**",
    "**/tools/mingit/**",
    "**/variants/*/zephyr_lib/**",
    "**/zephyr-sdk-*.7z",
    "**/zephyr-sdk-*.zip",
    "**/zephyr-sdk-*.tar.xz",
    "**/hosttools_*.tar.xz",
    "**/MinGit-*.zip",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Create a clean sync folder (and optional zip) without local SDK/cache "
            "artifacts, suitable for GitHub upload."
        )
    )
    parser.add_argument(
        "--source",
        default=".",
        help="Repository root to export (default: current directory).",
    )
    parser.add_argument(
        "--output",
        default="sync_export",
        help="Export output directory (default: sync_export).",
    )
    parser.add_argument(
        "--name",
        default="NRF54L15-Arduino-core-sync",
        help="Export folder name inside output directory.",
    )
    parser.add_argument(
        "--zip",
        action="store_true",
        help="Also create <name>.zip from the exported folder.",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Delete existing export folder before writing.",
    )
    return parser.parse_args()


def matches_exclude(rel_posix: str, patterns: Iterable[str]) -> bool:
    for pattern in patterns:
        if fnmatch.fnmatch(rel_posix, pattern):
            return True
        if pattern.endswith("/**"):
            base = pattern[:-3].rstrip("/")
            if rel_posix == base or rel_posix.startswith(base + "/"):
                return True
    return False


def iter_source_files(source_root: Path, output_root: Path, excludes: Iterable[str]) -> List[Path]:
    files: List[Path] = []
    source_root_resolved = source_root.resolve()
    output_root_resolved = output_root.resolve()

    for path in sorted(source_root.rglob("*")):
        if not path.is_file():
            continue

        path_resolved = path.resolve()
        if path_resolved == output_root_resolved or output_root_resolved in path_resolved.parents:
            continue

        rel_posix = path.relative_to(source_root_resolved).as_posix()
        if matches_exclude(rel_posix, excludes):
            continue

        files.append(path)

    return files


def copy_files(source_root: Path, export_dir: Path, files: List[Path]) -> int:
    copied = 0
    for src in files:
        rel = src.relative_to(source_root)
        dst = export_dir / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        copied += 1
    return copied


def zip_dir(folder: Path, zip_path: Path) -> int:
    entries = 0
    with zipfile.ZipFile(zip_path, mode="w", compression=zipfile.ZIP_DEFLATED) as zf:
        for path in sorted(folder.rglob("*")):
            if not path.is_file():
                continue
            rel = path.relative_to(folder.parent).as_posix()
            zf.write(path, arcname=rel)
            entries += 1
    return entries


def main() -> int:
    args = parse_args()
    source_root = Path(args.source).resolve()
    output_root = Path(args.output).resolve()
    export_dir = output_root / args.name
    zip_path = output_root / f"{args.name}.zip"

    if not source_root.is_dir():
        raise SystemExit(f"Source directory not found: {source_root}")

    output_root.mkdir(parents=True, exist_ok=True)

    if export_dir.exists():
        if not args.clean:
            raise SystemExit(f"Export folder already exists: {export_dir} (use --clean)")
        shutil.rmtree(export_dir)

    files = iter_source_files(source_root, output_root, DEFAULT_EXCLUDES)
    copied = copy_files(source_root, export_dir, files)

    print(f"Export folder: {export_dir}")
    print(f"Files copied: {copied}")

    if args.zip:
        if zip_path.exists():
            zip_path.unlink()
        zipped = zip_dir(export_dir, zip_path)
        size = zip_path.stat().st_size
        print(f"Zip file: {zip_path}")
        print(f"Zip entries: {zipped}")
        print(f"Zip size: {size}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

