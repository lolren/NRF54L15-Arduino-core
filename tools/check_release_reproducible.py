#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
PLATFORM_TXT = REPO_ROOT / "hardware" / "nrf54l15" / "nrf54l15" / "platform.txt"
RELEASE_SCRIPT = REPO_ROOT / "tools" / "release_boards_manager.py"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Verify release archive/index reproducibility by generating twice."
    )
    parser.add_argument(
        "--version",
        default="",
        help="Core version. Default: read from platform.txt",
    )
    parser.add_argument(
        "--repo",
        default="example/example",
        help="GitHub repository owner/name used for generated URL fields.",
    )
    parser.add_argument(
        "--source-date-epoch",
        default="1704067200",
        help="SOURCE_DATE_EPOCH passed to release script (default: 1704067200).",
    )
    return parser.parse_args()


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=str(REPO_ROOT), check=True)


def read_core_version(platform_txt: Path) -> str:
    for line in platform_txt.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith("version="):
            return line.split("=", 1)[1].strip()
    raise RuntimeError(f"Unable to find version= in {platform_txt}")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def build_once(
    *,
    version: str,
    repo: str,
    source_date_epoch: str,
    dist_dir: Path,
    index_path: Path,
) -> tuple[Path, Path]:
    archive_name = f"nrf54l15-baremetal-{version}.tar.bz2"
    run(
        [
            sys.executable,
            str(RELEASE_SCRIPT),
            "--version",
            version,
            "--repo",
            repo,
            "--dist-dir",
            str(dist_dir),
            "--index-path",
            str(index_path),
            "--source-date-epoch",
            source_date_epoch,
        ]
    )
    archive_path = dist_dir / archive_name
    if not archive_path.is_file():
        raise RuntimeError(f"Missing archive: {archive_path}")
    if not index_path.is_file():
        raise RuntimeError(f"Missing index: {index_path}")
    return archive_path, index_path


def main() -> int:
    args = parse_args()
    version = args.version.strip() or read_core_version(PLATFORM_TXT)

    if not PLATFORM_TXT.is_file():
        raise RuntimeError(f"Missing {PLATFORM_TXT}")
    if not RELEASE_SCRIPT.is_file():
        raise RuntimeError(f"Missing {RELEASE_SCRIPT}")

    with tempfile.TemporaryDirectory(prefix="nrf54l15-repro-") as temp_dir_str:
        temp_dir = Path(temp_dir_str)
        run1_dist = temp_dir / "run1-dist"
        run2_dist = temp_dir / "run2-dist"
        run1_dist.mkdir(parents=True, exist_ok=True)
        run2_dist.mkdir(parents=True, exist_ok=True)
        run1_index = temp_dir / "run1-index.json"
        run2_index = temp_dir / "run2-index.json"

        archive1, index1 = build_once(
            version=version,
            repo=args.repo,
            source_date_epoch=args.source_date_epoch,
            dist_dir=run1_dist,
            index_path=run1_index,
        )
        archive2, index2 = build_once(
            version=version,
            repo=args.repo,
            source_date_epoch=args.source_date_epoch,
            dist_dir=run2_dist,
            index_path=run2_index,
        )

        sha1 = sha256(archive1)
        sha2 = sha256(archive2)
        if sha1 != sha2:
            raise RuntimeError(
                "Archive SHA mismatch between runs: "
                f"{archive1.name}={sha1} vs {archive2.name}={sha2}"
            )

        data1 = load_json(index1)
        data2 = load_json(index2)
        if data1 != data2:
            raise RuntimeError("Index JSON content mismatch between runs")

        print("Reproducibility check passed.")
        print(f"Version: {version}")
        print(f"Archive SHA-256: {sha1}")
        print(f"Repo URL seed: {args.repo}")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
