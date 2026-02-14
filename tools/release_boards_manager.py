#!/usr/bin/env python3
from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import tarfile
from pathlib import Path
from typing import Dict, Iterable, List

DEFAULT_EXCLUDES = [
    ".DS_Store",
    "tools/.arduino-zephyr-build/**",
    "tools/ncs/**",
    "tools/ncs.partial/**",
    "tools/zephyr-sdk/**",
    "tools/host-tools/**",
    "tools/toolchain-bootstrap/**",
    "variants/*/zephyr_lib/**",
    "**/__pycache__/**",
    "**/*.pyc",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create nRF54L15 Boards Manager archive + package index entry."
    )
    parser.add_argument("--version", required=True, help="Core version (for example: 1.0.3)")
    parser.add_argument(
        "--repo",
        required=True,
        help="GitHub repository in owner/name format (for example: user/repo)",
    )
    parser.add_argument(
        "--release-tag",
        default="",
        help="GitHub release tag. Default: v<version>",
    )
    parser.add_argument(
        "--core-dir",
        default="hardware/nrf54l15/nrf54l15",
        help="Path to platform root directory to package.",
    )
    parser.add_argument(
        "--dist-dir",
        default="dist",
        help="Directory where the archive is written.",
    )
    parser.add_argument(
        "--index-path",
        default="package_nrf54l15_zephyr_based_index.json",
        help="Package index JSON path to create/update.",
    )
    parser.add_argument(
        "--archive-name",
        default="",
        help="Archive filename. Default: nrf54l15-zephyr-based-<version>.tar.bz2",
    )
    parser.add_argument(
        "--archive-root",
        default="",
        help="Top-level directory name inside archive.",
    )
    parser.add_argument(
        "--archive-url",
        default="",
        help="Override archive URL in index. Default uses GitHub release URL.",
    )
    parser.add_argument("--package-name", default="nrf54l15")
    parser.add_argument("--architecture", default="nrf54l15")
    parser.add_argument("--platform-name", default="Seeed nRF54L15 (Zephyr-Based)")
    parser.add_argument("--board-name", default="XIAO nRF54L15 (Zephyr-Based - NO BLE)")
    parser.add_argument("--category", default="Arduino")
    parser.add_argument("--maintainer", default="Seeed Studio")
    parser.add_argument(
        "--website-url",
        default="",
        help="Package website URL. Default: https://github.com/<repo>",
    )
    parser.add_argument(
        "--help-url",
        default="",
        help="Online help URL. Default: website URL",
    )
    parser.add_argument(
        "--source-date-epoch",
        default="1704067200",
        help=(
            "Timestamp used for packaged file mtimes for reproducible archives. "
            "Default: 1704067200 (2024-01-01T00:00:00Z)"
        ),
    )
    return parser.parse_args()


def version_key(version: str) -> tuple:
    parts: List[int] = []
    cur = ""
    for ch in version:
        if ch.isdigit():
            cur += ch
        else:
            if cur:
                parts.append(int(cur))
                cur = ""
    if cur:
        parts.append(int(cur))
    if not parts:
        parts = [0]
    return tuple(parts)


def matches_exclude(rel_posix: str, patterns: Iterable[str]) -> bool:
    for pattern in patterns:
        if fnmatch.fnmatch(rel_posix, pattern):
            return True
        if pattern.endswith("/**"):
            base = pattern[:-3].rstrip("/")
            if rel_posix == base or rel_posix.startswith(base + "/"):
                return True
    return False


def iter_source_files(source_root: Path, exclude_patterns: Iterable[str]) -> List[Path]:
    files: List[Path] = []
    for path in sorted(source_root.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(source_root).as_posix()
        if matches_exclude(rel, exclude_patterns):
            continue
        files.append(path)
    return files


def build_archive(
    source_root: Path, archive_path: Path, archive_root: str, source_date_epoch: int
) -> Dict[str, int | str]:
    files = iter_source_files(source_root, DEFAULT_EXCLUDES)
    archive_path.parent.mkdir(parents=True, exist_ok=True)

    with tarfile.open(archive_path, mode="w:bz2") as tar:
        for path in files:
            rel = path.relative_to(source_root).as_posix()
            arcname = f"{archive_root}/{rel}"
            info = tar.gettarinfo(str(path), arcname)
            mode = path.stat().st_mode
            info.mode = 0o755 if (mode & 0o111) else 0o644
            info.uid = 0
            info.gid = 0
            info.uname = ""
            info.gname = ""
            info.mtime = int(source_date_epoch)
            info.pax_headers = {}
            with path.open("rb") as fh:
                tar.addfile(info, fh)

    digest = hashlib.sha256()
    with archive_path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            digest.update(chunk)

    return {
        "sha256": digest.hexdigest(),
        "size": archive_path.stat().st_size,
        "files": len(files),
    }


def load_existing_platforms(index_path: Path, package_name: str, architecture: str, version: str) -> List[dict]:
    if not index_path.is_file():
        return []

    try:
        data = json.loads(index_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return []

    packages = data.get("packages") if isinstance(data, dict) else None
    if not isinstance(packages, list):
        return []

    for pkg in packages:
        if not isinstance(pkg, dict):
            continue
        if pkg.get("name") != package_name:
            continue
        platforms = pkg.get("platforms")
        if not isinstance(platforms, list):
            return []

        kept: List[dict] = []
        for platform in platforms:
            if not isinstance(platform, dict):
                continue
            if str(platform.get("version", "")) == version:
                continue
            if str(platform.get("architecture", "")) != architecture:
                continue
            kept.append(platform)
        return kept

    return []


def write_index(
    index_path: Path,
    *,
    package_name: str,
    maintainer: str,
    website_url: str,
    help_url: str,
    platform_name: str,
    architecture: str,
    version: str,
    category: str,
    archive_url: str,
    archive_name: str,
    sha256: str,
    size: int,
    board_name: str,
) -> None:
    existing_platforms = load_existing_platforms(index_path, package_name, architecture, version)

    new_platform = {
        "name": platform_name,
        "architecture": architecture,
        "version": version,
        "category": category,
        "url": archive_url,
        "archiveFileName": archive_name,
        "checksum": f"SHA-256:{sha256}",
        "size": str(size),
        "boards": [{"name": board_name}],
        "help": {"online": help_url},
        "toolsDependencies": [],
        "discoveryDependencies": [],
        "monitorDependencies": [],
    }

    platforms = existing_platforms + [new_platform]
    platforms.sort(key=lambda item: version_key(str(item.get("version", "0"))), reverse=True)

    index = {
        "packages": [
            {
                "name": package_name,
                "maintainer": maintainer,
                "websiteURL": website_url,
                "email": "",
                "help": {"online": help_url},
                "platforms": platforms,
                "tools": [],
            }
        ]
    }

    index_path.write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()

    repo = args.repo.strip().strip("/")
    if repo.count("/") != 1:
        raise SystemExit("--repo must be in owner/name format")

    version = args.version.strip()
    release_tag = args.release_tag.strip() or f"v{version}"
    source_date_epoch = int(args.source_date_epoch)
    if source_date_epoch < 0:
        raise SystemExit("--source-date-epoch must be >= 0")

    source_root = Path(args.core_dir).resolve()
    if not source_root.is_dir():
        raise SystemExit(f"Missing core directory: {source_root}")

    archive_name = args.archive_name.strip() or f"nrf54l15-zephyr-based-{version}.tar.bz2"
    archive_root = args.archive_root.strip() or archive_name.removesuffix(".tar.bz2")
    archive_path = Path(args.dist_dir).resolve() / archive_name

    website_url = args.website_url.strip() or f"https://github.com/{repo}"
    help_url = args.help_url.strip() or website_url
    archive_url = (
        args.archive_url.strip()
        or f"https://github.com/{repo}/releases/download/{release_tag}/{archive_name}"
    )

    result = build_archive(source_root, archive_path, archive_root, source_date_epoch)

    index_path = Path(args.index_path).resolve()
    write_index(
        index_path,
        package_name=args.package_name,
        maintainer=args.maintainer,
        website_url=website_url,
        help_url=help_url,
        platform_name=args.platform_name,
        architecture=args.architecture,
        version=version,
        category=args.category,
        archive_url=archive_url,
        archive_name=archive_name,
        sha256=str(result["sha256"]),
        size=int(result["size"]),
        board_name=args.board_name,
    )

    print(f"Archive: {archive_path}")
    print(f"Files: {result['files']}")
    print(f"SHA-256: {result['sha256']}")
    print(f"Size: {result['size']}")
    print(f"Index: {index_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
