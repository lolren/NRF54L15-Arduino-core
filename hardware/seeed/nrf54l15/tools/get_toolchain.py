#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import shutil
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path
from typing import Dict, List, Optional, Tuple

from zephyr_common import (
    core_paths,
    detect_supported_host_for_sdk,
    run,
    sdk_tool,
)


def log(message: str) -> None:
    print(message)


def ensure_cmake_on_windows_path() -> None:
    if os.name != "nt":
        return
    if shutil.which("cmake"):
        return

    candidate_bins = [
        Path(os.environ.get("ProgramFiles", "")) / "CMake" / "bin",
        Path(os.environ.get("ProgramFiles(x86)", "")) / "CMake" / "bin",
        Path(os.environ.get("LocalAppData", "")) / "Programs" / "CMake" / "bin",
    ]
    for bin_dir in candidate_bins:
        cmake_exe = bin_dir / "cmake.exe"
        if not cmake_exe.is_file():
            continue
        old_path = os.environ.get("PATH", "")
        os.environ["PATH"] = str(bin_dir) + (os.pathsep + old_path if old_path else "")
        log(f"Using CMake from {bin_dir}")
        return


def fetch_release_assets(version: str) -> List[Dict[str, str]]:
    url = f"https://api.github.com/repos/zephyrproject-rtos/sdk-ng/releases/tags/v{version}"
    with urllib.request.urlopen(url) as response:  # nosec B310 - fixed GitHub API endpoint
        data = json.loads(response.read().decode("utf-8"))
    assets = data.get("assets", [])
    return [
        {"name": asset.get("name", ""), "url": asset.get("browser_download_url", "")}
        for asset in assets
        if asset.get("name") and asset.get("browser_download_url")
    ]


def pick_sdk_asset(version: str, host_os: str, host_arch: str, assets: List[Dict[str, str]]) -> Dict[str, str]:
    prefix = f"zephyr-sdk-{version}_"
    target = f"_{host_os}-{host_arch}"
    candidates: List[Tuple[int, Dict[str, str]]] = []

    for asset in assets:
        name = asset["name"]
        if not name.startswith(prefix):
            continue
        if target not in name:
            continue
        if "_minimal" in name:
            continue
        if not (name.endswith(".tar.xz") or name.endswith(".7z") or name.endswith(".zip")):
            continue

        score = 0
        if "_gnu" in name:
            score += 40
        if "_llvm" in name:
            score += 10
        if name.endswith(".tar.xz"):
            score += 10
        if name.endswith(".7z"):
            score += 8
        if name.endswith(".zip"):
            score += 6
        score += 20  # full (non-minimal) bundle
        candidates.append((score, asset))

    if not candidates:
        raise RuntimeError(
            f"Could not find a Zephyr SDK bundle for host {host_os}-{host_arch} (version {version})."
        )

    candidates.sort(key=lambda item: item[0], reverse=True)
    return candidates[0][1]


def download_file(url: str, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(url) as response, dest.open("wb") as out:  # nosec B310 - fixed URL from GitHub API
        shutil.copyfileobj(response, out)


def safe_extract_tar(archive: Path, target_dir: Path) -> None:
    with tarfile.open(archive, mode="r:*") as tar:
        for member in tar.getmembers():
            member_path = (target_dir / member.name).resolve()
            if not str(member_path).startswith(str(target_dir.resolve())):
                raise RuntimeError("Refusing to extract archive with unsafe path.")
        tar.extractall(target_dir)


def extract_archive(archive: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    suffix = archive.name.lower()

    if suffix.endswith(".tar.xz") or suffix.endswith(".tar.gz") or suffix.endswith(".tgz"):
        safe_extract_tar(archive, destination)
        return

    if suffix.endswith(".7z"):
        seven_zip = shutil.which("7z") or shutil.which("7za")
        if seven_zip:
            run([seven_zip, "x", str(archive), f"-o{destination}", "-y"], check=True)
            return

        # Windows commonly provides bsdtar as `tar.exe`, which supports 7z.
        tar_bin = shutil.which("tar")
        if tar_bin:
            log("7z was not found; trying system tar for .7z extraction...")
            try:
                run([tar_bin, "-xf", str(archive), "-C", str(destination)], check=True)
                return
            except Exception:
                pass  # Fall through to py7zr

        # Try py7zr as last resort
        try:
            import py7zr
            with py7zr.SevenZipFile(archive, mode="r") as zf:
                zf.extractall(path=str(destination))
            log("Extracted .7z archive using py7zr library")
            return
        except ImportError:
            # py7zr not available, try installing it
            try:
                import subprocess
                subprocess.run([sys.executable, "-m", "pip", "install", "py7zr"], check=True, capture_output=True)
                import py7zr
                with py7zr.SevenZipFile(archive, mode="r") as zf:
                    zf.extractall(path=str(destination))
                log("Extracted .7z archive using py7zr library (installed)")
                return
            except Exception:
                pass

        raise RuntimeError(
            "Unable to extract .7z archive: neither 7z/7za nor tar is available on PATH."
        )

    if suffix.endswith(".zip"):
        import zipfile

        with zipfile.ZipFile(archive) as zf:
            zf.extractall(destination)
        return

    raise RuntimeError(f"Unsupported archive format: {archive.name}")


def find_extracted_sdk_dir(extract_root: Path, version: str) -> Path:
    candidates = [p for p in extract_root.iterdir() if p.is_dir()]
    if not candidates:
        raise RuntimeError("SDK archive extracted but no directory was found.")

    if len(candidates) == 1:
        return candidates[0]

    prefix = f"zephyr-sdk-{version}"
    matches = [p for p in candidates if p.name.startswith(prefix)]
    if matches:
        matches.sort(key=lambda p: len(p.name))
        return matches[0]

    matches = [p for p in candidates if p.name.startswith("zephyr-sdk-")]
    if matches:
        return matches[0]

    raise RuntimeError("Unable to locate extracted Zephyr SDK directory.")


def try_setup_script(sdk_dir: Path) -> None:
    # Full bundles usually already include arm toolchain; this is fallback only.
    setup_sh = sdk_dir / "setup.sh"
    setup_cmd = sdk_dir / "setup.cmd"
    setup_bat = sdk_dir / "setup.bat"

    if setup_sh.is_file() and os.name != "nt":
        run([str(setup_sh), "-t", "arm-zephyr-eabi", "-h", "-c"], cwd=sdk_dir, check=True)
        return

    setup_win = setup_cmd if setup_cmd.is_file() else setup_bat
    if setup_win.is_file():
        run([str(setup_win), "-t", "arm-zephyr-eabi", "-h", "-c"], cwd=sdk_dir, check=True)


def dir_size_bytes(path: Path) -> int:
    if not path.exists():
        return 0
    total = 0
    for root, _dirs, files in os.walk(path):
        for name in files:
            fp = Path(root) / name
            try:
                total += fp.stat().st_size
            except OSError:
                pass
    return total


def prune_multilib_tree(base: Path, keep_dir_name: str) -> None:
    if not base.is_dir():
        return
    for entry in base.iterdir():
        if not entry.is_dir():
            continue
        if entry.name == keep_dir_name:
            continue
        shutil.rmtree(entry, ignore_errors=True)


def prune_arm_tree(base: Path) -> None:
    if base.is_dir():
        shutil.rmtree(base, ignore_errors=True)


def prune_sdk_for_nrf54l15(sdk_dir: Path) -> None:
    """
    Keep only the multilib slices needed by this core target:
      - cortex-m33 thumb => v8-m.main/nofp
    """
    # Keep one thumb multilib across each replicated library tree.
    tool_root = sdk_dir / "arm-zephyr-eabi"

    prune_multilib_tree(tool_root / "arm-zephyr-eabi" / "lib" / "thumb", "v8-m.main")
    prune_multilib_tree(tool_root / "lib" / "gcc" / "arm-zephyr-eabi" / "12.2.0" / "thumb", "v8-m.main")
    prune_multilib_tree(tool_root / "picolibc" / "arm-zephyr-eabi" / "lib" / "thumb", "v8-m.main")
    prune_multilib_tree(tool_root / "picolibc" / "lib" / "gcc" / "arm-zephyr-eabi" / "12.2.0" / "thumb", "v8-m.main")

    # Drop ARM mode multilibs (not used by this Cortex-M target).
    prune_arm_tree(tool_root / "arm-zephyr-eabi" / "lib" / "arm")
    prune_arm_tree(tool_root / "lib" / "gcc" / "arm-zephyr-eabi" / "12.2.0" / "arm")
    prune_arm_tree(tool_root / "picolibc" / "arm-zephyr-eabi" / "lib" / "arm")
    prune_arm_tree(tool_root / "picolibc" / "lib" / "gcc" / "arm-zephyr-eabi" / "12.2.0" / "arm")

    # Mark to avoid repeating heavy prune work on each invocation.
    marker = sdk_dir / ".nrf54l15-minimal-pruned"
    marker.write_text("1\n", encoding="utf-8")


def sdk_is_already_minimal_for_nrf54l15(sdk_dir: Path) -> bool:
    tool_root = sdk_dir / "arm-zephyr-eabi"
    thumb = tool_root / "arm-zephyr-eabi" / "lib" / "thumb"
    if not thumb.is_dir():
        return False
    dirs = sorted([p.name for p in thumb.iterdir() if p.is_dir()])
    if dirs != ["v8-m.main"]:
        return False
    if (tool_root / "arm-zephyr-eabi" / "lib" / "arm").exists():
        return False
    return True


def apply_prune_policy(sdk_dir: Path, prune_sdk: bool, prune_multilib: bool) -> None:
    if not prune_sdk:
        return

    before = dir_size_bytes(sdk_dir)

    sysroots = sdk_dir / "sysroots"
    if sysroots.is_dir():
        shutil.rmtree(sysroots)
    for installer in sdk_dir.glob("zephyr-sdk-*-hosttools-standalone-*"):
        if installer.is_file():
            installer.unlink(missing_ok=True)

    if prune_multilib:
        marker = sdk_dir / ".nrf54l15-minimal-pruned"
        if not marker.exists() or not sdk_is_already_minimal_for_nrf54l15(sdk_dir):
            prune_sdk_for_nrf54l15(sdk_dir)

    after = dir_size_bytes(sdk_dir)
    saved = before - after
    if saved > 0:
        print(f"Pruned Zephyr SDK size: saved ~{saved / (1024 * 1024):.1f} MB")


def main() -> int:
    paths = core_paths(Path(__file__))
    tools_dir = paths["tools_dir"]
    sdk_dir = paths["sdk_dir"]

    ensure_cmake_on_windows_path()

    sdk_version = os.environ.get("SDK_VERSION", "0.16.8")
    # Keep the downloaded SDK archive by default so failed/partial installs
    # can resume without re-downloading multi-GB payloads.
    keep_archive = os.environ.get("KEEP_ZEPHYR_SDK_ARCHIVE", "1") == "1"
    prune_sdk = os.environ.get("PRUNE_ZEPHYR_SDK", "1") == "1"
    prune_multilib = os.environ.get("PRUNE_ZEPHYR_SDK_MULTIARCH", "1") == "1"

    compiler = sdk_tool(sdk_dir, "arm-zephyr-eabi-gcc")
    if compiler and compiler.is_file():
        apply_prune_policy(sdk_dir, prune_sdk, prune_multilib)
        print(f"Zephyr SDK already installed: {sdk_dir}")
        return 0

    host_os, host_arch = detect_supported_host_for_sdk()
    print(f"Resolving Zephyr SDK for host {host_os}-{host_arch} (v{sdk_version})...")

    assets = fetch_release_assets(sdk_version)
    chosen = pick_sdk_asset(sdk_version, host_os, host_arch, assets)
    archive_name = chosen["name"]
    archive_url = chosen["url"]
    archive_path = tools_dir / archive_name

    if not archive_path.is_file():
        print(f"Downloading {archive_name} ...")
        download_file(archive_url, archive_path)
    else:
        print(f"Using existing SDK archive: {archive_path}")

    with tempfile.TemporaryDirectory(prefix="zephyr-sdk-extract-", dir=str(tools_dir)) as temp_dir:
        extract_root = Path(temp_dir)
        print("Extracting Zephyr SDK...")
        extract_archive(archive_path, extract_root)
        extracted_dir = find_extracted_sdk_dir(extract_root, sdk_version)

        if sdk_dir.exists():
            shutil.rmtree(sdk_dir)
        shutil.move(str(extracted_dir), str(sdk_dir))

    compiler = sdk_tool(sdk_dir, "arm-zephyr-eabi-gcc")
    if not compiler or not compiler.is_file():
        print("Compiler not present after extraction, running setup script...")
        try_setup_script(sdk_dir)
        compiler = sdk_tool(sdk_dir, "arm-zephyr-eabi-gcc")

    if not compiler or not compiler.is_file():
        raise RuntimeError(f"Failed to provision arm-zephyr-eabi toolchain under {sdk_dir}")

    apply_prune_policy(sdk_dir, prune_sdk, prune_multilib)

    if not keep_archive:
        archive_path.unlink(missing_ok=True)

    print(f"Toolchain setup complete: {sdk_dir}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # pragma: no cover - helper script
        print(str(exc), file=sys.stderr)
        raise
