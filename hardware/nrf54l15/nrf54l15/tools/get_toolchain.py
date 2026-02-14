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


def fetch_release_assets(version: str) -> List[Dict[str, str]]:
    url = f"https://api.github.com/repos/zephyrproject-rtos/sdk-ng/releases/tags/v{version}"
    with urllib.request.urlopen(url) as response:  # nosec B310 - fixed GitHub API endpoint
        data = json.loads(response.read().decode("utf-8"))
    assets = data.get("assets", [])
    return [
        {
            "name": asset.get("name", ""),
            "url": asset.get("browser_download_url", ""),
            "size": str(asset.get("size", 0)),
        }
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


def pick_hosttools_asset(host_os: str, host_arch: str, assets: List[Dict[str, str]]) -> Optional[Dict[str, str]]:
    target = f"hosttools_{host_os}-{host_arch}.tar.xz"
    for asset in assets:
        if asset.get("name") == target:
            return asset
    return None


def download_file(url: str, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    curl = shutil.which("curl")
    if curl:
        run(
            [
                curl,
                "-L",
                "--fail",
                "--retry",
                "8",
                "--retry-all-errors",
                "--retry-delay",
                "2",
                "-C",
                "-",
                "-o",
                str(dest),
                url,
            ],
            check=True,
        )
        return

    wget = shutil.which("wget")
    if wget:
        run(
            [
                wget,
                "--continue",
                "--tries=10",
                "--retry-connrefused",
                "-O",
                str(dest),
                url,
            ],
            check=True,
        )
        return

    with urllib.request.urlopen(url) as response, dest.open("wb") as out:  # nosec B310 - fixed URL from GitHub API
        shutil.copyfileobj(response, out)


def parse_positive_int_env(name: str, default: int) -> int:
    raw = os.environ.get(name, "").strip()
    if not raw:
        return default
    try:
        value = int(raw)
    except ValueError:
        return default
    return value if value > 0 else default


def expected_size(asset: Dict[str, str]) -> int:
    raw = str(asset.get("size", "")).strip()
    if not raw:
        return 0
    try:
        value = int(raw)
    except ValueError:
        return 0
    return value if value > 0 else 0


def archive_size_matches(path: Path, expected_bytes: int) -> bool:
    if expected_bytes <= 0:
        return True
    try:
        return path.stat().st_size == expected_bytes
    except OSError:
        return False


def ensure_hosttools_installer_available(
    *,
    tools_dir: Path,
    sdk_dir: Path,
    host_os: str,
    host_arch: str,
    assets: List[Dict[str, str]],
) -> bool:
    if any(sdk_dir.glob("zephyr-sdk-*-hosttools-standalone-*")):
        return True

    asset = pick_hosttools_asset(host_os, host_arch, assets)
    if not asset:
        return False

    archive_name = asset["name"]
    archive_url = asset["url"]
    archive_expected_size = expected_size(asset)
    archive_path = tools_dir / archive_name

    if (not archive_path.is_file()) or (not archive_size_matches(archive_path, archive_expected_size)):
        print(f"Downloading host-tools installer archive: {archive_name}")
        download_file(archive_url, archive_path)

    print("Extracting host-tools installer archive...")
    extract_archive(archive_path, sdk_dir)
    return any(sdk_dir.glob("zephyr-sdk-*-hosttools-standalone-*"))


def ensure_dtc_available(
    *,
    tools_dir: Path,
    sdk_dir: Path,
    sdk_version: str,
    host_os: str,
    host_arch: str,
    assets: Optional[List[Dict[str, str]]] = None,
) -> bool:
    dtc_tool = sdk_tool(sdk_dir, "dtc")
    if dtc_tool and dtc_tool.is_file():
        return True

    print("DTC host tool missing, running setup script...")
    try:
        try_setup_script(sdk_dir)
    except Exception as exc:
        print(f"Host-tools setup failed ({exc}), attempting host-tools installer fallback...")

    dtc_tool = sdk_tool(sdk_dir, "dtc")
    if dtc_tool and dtc_tool.is_file():
        return True

    loaded_assets = assets if assets is not None else fetch_release_assets(sdk_version)
    if not ensure_hosttools_installer_available(
        tools_dir=tools_dir,
        sdk_dir=sdk_dir,
        host_os=host_os,
        host_arch=host_arch,
        assets=loaded_assets,
    ):
        return False

    print("Retrying setup script after host-tools installer extraction...")
    try:
        try_setup_script(sdk_dir)
    except Exception as exc:
        print(f"Host-tools setup retry failed ({exc}).")

    dtc_tool = sdk_tool(sdk_dir, "dtc")
    return bool(dtc_tool and dtc_tool.is_file())


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

    sdk_version = os.environ.get("SDK_VERSION", "0.16.8")
    keep_archive = os.environ.get("KEEP_ZEPHYR_SDK_ARCHIVE", "0") == "1"
    prune_sdk = os.environ.get("PRUNE_ZEPHYR_SDK", "1") == "1"
    prune_multilib = os.environ.get("PRUNE_ZEPHYR_SDK_MULTIARCH", "1") == "1"
    download_retries = parse_positive_int_env("ZEPHYR_SDK_DOWNLOAD_RETRIES", 10)
    host_os, host_arch = detect_supported_host_for_sdk()

    compiler = sdk_tool(sdk_dir, "arm-zephyr-eabi-gcc")
    if compiler and compiler.is_file():
        if not ensure_dtc_available(
            tools_dir=tools_dir,
            sdk_dir=sdk_dir,
            sdk_version=sdk_version,
            host_os=host_os,
            host_arch=host_arch,
        ):
            print(
                "Warning: dtc tool not found in SDK hosttools; "
                "build will rely on PATH/packaged host-tools."
            )
        apply_prune_policy(sdk_dir, prune_sdk, prune_multilib)
        print(f"Zephyr SDK already installed: {sdk_dir}")
        return 0

    print(f"Resolving Zephyr SDK for host {host_os}-{host_arch} (v{sdk_version})...")

    assets = fetch_release_assets(sdk_version)
    chosen = pick_sdk_asset(sdk_version, host_os, host_arch, assets)
    archive_name = chosen["name"]
    archive_url = chosen["url"]
    archive_expected_size = expected_size(chosen)
    archive_path = tools_dir / archive_name

    for attempt in range(1, download_retries + 1):
        if archive_path.is_file():
            if archive_size_matches(archive_path, archive_expected_size):
                print(f"Using existing SDK archive: {archive_path}")
            else:
                print(
                    "Existing SDK archive size mismatch, attempting resume/re-download: "
                    f"{archive_path}"
                )

        if (not archive_path.is_file()) or (not archive_size_matches(archive_path, archive_expected_size)):
            print(f"Downloading {archive_name} (attempt {attempt}/{download_retries}) ...")
            download_file(archive_url, archive_path)

        size_matches = archive_size_matches(archive_path, archive_expected_size)
        if not size_matches:
            actual_size = archive_path.stat().st_size if archive_path.is_file() else 0
            print(
                "Downloaded SDK archive size mismatch "
                f"(expected {archive_expected_size}, got {actual_size}). "
                "Attempting extraction anyway..."
            )

        try:
            with tempfile.TemporaryDirectory(prefix="zephyr-sdk-extract-", dir=str(tools_dir)) as temp_dir:
                extract_root = Path(temp_dir)
                print("Extracting Zephyr SDK...")
                extract_archive(archive_path, extract_root)
                extracted_dir = find_extracted_sdk_dir(extract_root, sdk_version)

                if sdk_dir.exists():
                    shutil.rmtree(sdk_dir)
                shutil.move(str(extracted_dir), str(sdk_dir))
            if not size_matches:
                print("Extraction succeeded despite size mismatch.")
            break
        except (EOFError, tarfile.ReadError, OSError, RuntimeError) as exc:
            archive_path.unlink(missing_ok=True)
            if attempt >= download_retries:
                raise RuntimeError(
                    "Failed to extract Zephyr SDK archive after retries; "
                    "download may be corrupted."
                ) from exc
            print(
                "SDK archive extract failed; removing cached archive and retrying download. "
                f"(attempt {attempt}/{download_retries})"
            )

    compiler = sdk_tool(sdk_dir, "arm-zephyr-eabi-gcc")
    if not compiler or not compiler.is_file():
        print("Compiler not present after extraction, running setup script...")
        try_setup_script(sdk_dir)
        compiler = sdk_tool(sdk_dir, "arm-zephyr-eabi-gcc")

    if not compiler or not compiler.is_file():
        raise RuntimeError(f"Failed to provision arm-zephyr-eabi toolchain under {sdk_dir}")

    if not ensure_dtc_available(
        tools_dir=tools_dir,
        sdk_dir=sdk_dir,
        sdk_version=sdk_version,
        host_os=host_os,
        host_arch=host_arch,
        assets=assets,
    ):
        print(
            "Warning: dtc tool not found in SDK hosttools; "
            "build will rely on PATH/packaged host-tools."
        )

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
