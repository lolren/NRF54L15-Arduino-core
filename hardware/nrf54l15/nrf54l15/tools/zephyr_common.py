#!/usr/bin/env python3
"""
Shared helpers for cross-platform Zephyr core tooling.
"""

from __future__ import annotations

import os
import platform
import shlex
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Dict, Iterable, List, Mapping, MutableMapping, Optional, Sequence, Tuple

MIN_WEST_VERSION = (0, 14, 0)


def is_windows() -> bool:
    return sys.platform.startswith("win")


def is_macos() -> bool:
    return sys.platform == "darwin"


def exe_name(base: str) -> str:
    return f"{base}.exe" if is_windows() else base


def host_os_tag() -> str:
    if is_windows():
        return "windows"
    if is_macos():
        return "macos"
    if sys.platform.startswith("linux"):
        return "linux"
    raise RuntimeError(f"Unsupported host OS: {sys.platform}")


def host_arch_tag() -> str:
    machine = platform.machine().lower()
    if machine in ("x86_64", "amd64"):
        return "x86_64"
    if machine in ("aarch64", "arm64"):
        return "aarch64"
    raise RuntimeError(f"Unsupported host architecture: {platform.machine()}")


def _version_sort_key(version: str) -> Tuple[Tuple[int, ...], str]:
    nums: List[int] = []
    current = ""
    for ch in version:
        if ch.isdigit():
            current += ch
            continue
        if current:
            nums.append(int(current))
            current = ""
    if current:
        nums.append(int(current))
    if not nums:
        nums = [0]
    return (tuple(nums), version)


def _candidate_arduino_data_dirs() -> List[Path]:
    candidates: List[Path] = []
    env_override = os.environ.get("ARDUINO_DATA_DIR")
    if env_override:
        candidates.append(Path(env_override))
    if is_windows():
        localapp = os.environ.get("LOCALAPPDATA")
        appdata = os.environ.get("APPDATA")
        if localapp:
            candidates.append(Path(localapp) / "Arduino15")
        if appdata:
            candidates.append(Path(appdata) / "Arduino15")
    elif is_macos():
        candidates.append(Path.home() / "Library" / "Arduino15")
    else:
        candidates.append(Path.home() / ".arduino15")
    return candidates


def find_installed_seeed_platform_dir() -> Optional[Path]:
    explicit = os.environ.get("ARDUINO_SEEED_NRF54L15_PLATFORM_DIR")
    if explicit:
        explicit_path = Path(explicit)
        if explicit_path.is_dir():
            return explicit_path

    for data_dir in _candidate_arduino_data_dirs():
        # Support both legacy vendor roots and this package's published root.
        for vendor in ("nrf54l15", "seeed", "Seeeduino"):
            root = data_dir / "packages" / vendor / "hardware" / "nrf54l15"
            if not root.is_dir():
                continue
            versions = [entry for entry in root.iterdir() if entry.is_dir()]
            if not versions:
                continue
            versions.sort(key=lambda entry: _version_sort_key(entry.name), reverse=True)
            return versions[0]
    return None


def _first_existing_dir(candidates: Sequence[Optional[Path]]) -> Optional[Path]:
    for candidate in candidates:
        if candidate and candidate.is_dir():
            return candidate
    for candidate in candidates:
        if candidate is not None:
            return candidate
    return None


def _shared_tools_dir_for_installed_core(platform_dir: Path) -> Optional[Path]:
    override = os.environ.get("ARDUINO_NRF54L15_SHARED_TOOLS_DIR", "").strip()
    if override:
        return Path(override)

    # Boards Manager layout:
    #   <arduino-data>/packages/<vendor>/hardware/nrf54l15/<version>
    arch_dir = platform_dir.parent
    if arch_dir.name != "nrf54l15":
        return None
    hardware_dir = arch_dir.parent
    if hardware_dir.name != "hardware":
        return None
    vendor_dir = hardware_dir.parent
    packages_dir = vendor_dir.parent
    if packages_dir.name != "packages":
        return None
    return vendor_dir / "tools"


def core_paths(script_file: Path) -> Dict[str, Path]:
    script_dir = script_file.resolve().parent
    platform_dir = script_dir.parent
    local_tools_dir = platform_dir / "tools"
    local_ncs_dir = local_tools_dir / "ncs"
    local_sdk_dir = local_tools_dir / "zephyr-sdk"
    local_pydeps_dir = local_tools_dir / "pydeps"
    local_arduino_base_dir = platform_dir / "zephyr" / "arduino_base"
    shared_tools_dir = _shared_tools_dir_for_installed_core(platform_dir)

    local_seeed_platform_dir = platform_dir.parent.parent / "seeed" / "nrf54l15"
    installed_seeed_platform_dir = find_installed_seeed_platform_dir()
    local_seeed_tools_dir = local_seeed_platform_dir / "tools"
    installed_seeed_tools_dir = (
        (installed_seeed_platform_dir / "tools") if installed_seeed_platform_dir else None
    )

    fallback_platform_dir = _first_existing_dir([local_seeed_platform_dir, installed_seeed_platform_dir])
    if fallback_platform_dir is None:
        fallback_platform_dir = platform_dir

    app_platform_dir = platform_dir if local_arduino_base_dir.is_dir() else fallback_platform_dir

    tooling_platform_dir = _first_existing_dir(
        [
            platform_dir if local_pydeps_dir.is_dir() else None,
            fallback_platform_dir,
        ]
    )
    if tooling_platform_dir is None:
        tooling_platform_dir = platform_dir

    if shared_tools_dir is not None:
        # Keep heavyweight bootstrap artifacts outside versioned core folders
        # so switching board options or upgrading the core does not redownload.
        tools_dir = shared_tools_dir
        ncs_dir = shared_tools_dir / "ncs"
        sdk_dir = shared_tools_dir / "zephyr-sdk"
    else:
        tools_dir = _first_existing_dir(
            [
                local_tools_dir,
                local_seeed_tools_dir,
                installed_seeed_tools_dir,
            ]
        )
        ncs_dir = _first_existing_dir(
            [
                local_ncs_dir,
                local_seeed_tools_dir / "ncs",
                (installed_seeed_tools_dir / "ncs") if installed_seeed_tools_dir else None,
            ]
        )
        sdk_dir = _first_existing_dir(
            [
                local_sdk_dir,
                local_seeed_tools_dir / "zephyr-sdk",
                (installed_seeed_tools_dir / "zephyr-sdk") if installed_seeed_tools_dir else None,
            ]
        )
    pydeps_dir = _first_existing_dir(
        [
            local_pydeps_dir,
            local_seeed_tools_dir / "pydeps",
            (installed_seeed_tools_dir / "pydeps") if installed_seeed_tools_dir else None,
        ]
    )

    if tools_dir is None:
        tools_dir = local_tools_dir
    if ncs_dir is None:
        ncs_dir = tools_dir / "ncs"
    if sdk_dir is None:
        sdk_dir = tools_dir / "zephyr-sdk"
    if pydeps_dir is None:
        pydeps_dir = tools_dir / "pydeps"

    return {
        "script_dir": script_dir,
        "platform_dir": platform_dir,
        "app_platform_dir": app_platform_dir,
        "tooling_platform_dir": tooling_platform_dir,
        "fallback_platform_dir": fallback_platform_dir,
        "shared_tools_dir": shared_tools_dir if shared_tools_dir is not None else local_tools_dir,
        "tools_dir": tools_dir,
        "ncs_dir": ncs_dir,
        "sdk_dir": sdk_dir,
        "pydeps_dir": pydeps_dir,
    }


def with_west_pythonpath(platform_dir: Path, env: Optional[Mapping[str, str]] = None) -> Dict[str, str]:
    out: Dict[str, str] = dict(os.environ if env is None else env)
    pydeps = str(platform_dir / "tools" / "pydeps")
    current = out.get("PYTHONPATH", "")
    out["PYTHONPATH"] = f"{pydeps}{os.pathsep}{current}" if current else pydeps
    return out


def _parse_version_tuple(version: str) -> Tuple[int, int, int]:
    numbers: List[int] = []
    current = ""
    for ch in version:
        if ch.isdigit():
            current += ch
            continue
        if current:
            numbers.append(int(current))
            current = ""
    if current:
        numbers.append(int(current))
    while len(numbers) < 3:
        numbers.append(0)
    return tuple(numbers[:3])  # type: ignore[return-value]


def _west_probe(env: Mapping[str, str]) -> Tuple[bool, str]:
    probe = subprocess.run(
        [
            sys.executable,
            "-c",
            "import west,colorama;from west.version import __version__ as v;print(v,end='')",
        ],
        env=dict(env),
        check=False,
        capture_output=True,
        text=True,
    )
    if probe.returncode != 0:
        return (False, "")
    return (True, (probe.stdout or "").strip())


def _west_version_supported(version: str) -> bool:
    if not version:
        return False
    if version.startswith("0.0.0+bundled"):
        return False
    return _parse_version_tuple(version) >= MIN_WEST_VERSION


def ensure_west_pydeps(platform_dir: Path, env: Optional[Mapping[str, str]] = None) -> Dict[str, str]:
    out: Dict[str, str] = dict(os.environ if env is None else env)
    pydeps_dir = platform_dir / "tools" / "pydeps"
    pydeps_dir.mkdir(parents=True, exist_ok=True)

    ok, version = _west_probe(out)
    if ok and _west_version_supported(version):
        return out

    pip_check = subprocess.run(
        [sys.executable, "-m", "pip", "--version"],
        env=out,
        check=False,
        capture_output=True,
        text=True,
    )
    if pip_check.returncode != 0:
        run([sys.executable, "-m", "ensurepip", "--upgrade"], env=out, check=True)

    print("Installing Python west dependencies into tools/pydeps...")
    run(
        [
            sys.executable,
            "-m",
            "pip",
            "install",
            "--disable-pip-version-check",
            "--no-input",
            "--upgrade",
            "--target",
            str(pydeps_dir),
            "west",
            "colorama",
        ],
        env=out,
        check=True,
    )

    ok, version = _west_probe(out)
    if not ok or not _west_version_supported(version):
        raise RuntimeError(
            f"Unsupported west version after install: {version or 'not found'} "
            "(requires >= 0.14.0)."
        )

    return out


def ensure_zephyr_python_deps(
    platform_dir: Path,
    ncs_dir: Path,
    env: Optional[Mapping[str, str]] = None,
) -> Dict[str, str]:
    out = ensure_west_pydeps(platform_dir, env)
    pydeps_dir = platform_dir / "tools" / "pydeps"
    requirements_base = ncs_dir / "zephyr" / "scripts" / "requirements-base.txt"

    probe = subprocess.run(
        [sys.executable, "-c", "import jsonschema,elftools"],
        env=out,
        check=False,
        capture_output=True,
        text=True,
    )
    if probe.returncode == 0:
        return out

    if not requirements_base.is_file():
        raise RuntimeError(f"Missing Zephyr requirements file: {requirements_base}")

    print("Installing Zephyr Python build dependencies into tools/pydeps...")
    run(
        [
            sys.executable,
            "-m",
            "pip",
            "install",
            "--disable-pip-version-check",
            "--no-input",
            "--upgrade",
            "--target",
            str(pydeps_dir),
            "-r",
            str(requirements_base),
        ],
        env=out,
        check=True,
    )

    probe = subprocess.run(
        [sys.executable, "-c", "import jsonschema,elftools"],
        env=out,
        check=False,
        capture_output=True,
        text=True,
    )
    if probe.returncode != 0:
        raise RuntimeError(
            "Zephyr Python dependency install failed; missing jsonschema/elftools "
            "after requirements-base installation."
        )

    return out


def prepend_sdk_tool_paths(env: MutableMapping[str, str], sdk_dir: Path) -> None:
    entries: List[str] = []
    for rel in (
        Path("arm-zephyr-eabi/bin"),
        Path("hosttools/bin"),
        Path("hosttools/usr/bin"),
    ):
        full = sdk_dir / rel
        if full.is_dir():
            entries.append(str(full))
    if not entries:
        return
    old = env.get("PATH", "")
    env["PATH"] = os.pathsep.join(entries + ([old] if old else []))


def shell_quote(value: str) -> str:
    return shlex.quote(value)


def write_metadata(metadata_file: Path, values: Mapping[str, str]) -> None:
    lines = []
    for key, value in values.items():
        lines.append(f"{key}={shell_quote(value)}")
    metadata_file.write_text("\n".join(lines) + "\n", encoding="utf-8")


def read_metadata(metadata_file: Path) -> Dict[str, str]:
    parsed: Dict[str, str] = {}
    if not metadata_file.is_file():
        return parsed

    for raw_line in metadata_file.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, rhs = line.split("=", 1)
        key = key.strip()
        rhs = rhs.strip()
        try:
            items = shlex.split(rhs, posix=True)
            value = items[0] if items else ""
        except ValueError:
            # Keep best effort if metadata has invalid quoting.
            value = rhs.strip("'\"")
        parsed[key] = value

    return parsed


def program_exists(name: str) -> bool:
    return shutil.which(name) is not None


def resolve_program(candidates: Iterable[str]) -> Optional[str]:
    for candidate in candidates:
        if not candidate:
            continue
        resolved = shutil.which(candidate)
        if resolved:
            return resolved
        p = Path(candidate)
        if p.is_file():
            return str(p)
    return None


def sdk_tool(sdk_dir: Path, tool_basename: str) -> Optional[Path]:
    tool = exe_name(tool_basename)
    candidates = [
        sdk_dir / "arm-zephyr-eabi" / "bin" / tool,
        sdk_dir / "hosttools" / "bin" / tool,
        sdk_dir / "hosttools" / "usr" / "bin" / tool,
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def run(
    cmd: Sequence[str],
    *,
    cwd: Optional[Path] = None,
    env: Optional[Mapping[str, str]] = None,
    check: bool = True,
    capture_output: bool = False,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        list(cmd),
        cwd=str(cwd) if cwd else None,
        env=dict(env) if env is not None else None,
        check=False,
        capture_output=capture_output,
        text=True,
    )
    if check and result.returncode != 0:
        if capture_output:
            if result.stdout:
                print(result.stdout, end="")
            if result.stderr:
                print(result.stderr, end="", file=sys.stderr)
        raise subprocess.CalledProcessError(result.returncode, cmd)
    return result


def west_cmd() -> List[str]:
    # Use current Python interpreter plus bundled pydeps for predictable behavior.
    return [sys.executable, "-m", "west"]


def west_cmd_with_zephyr_base(ncs_dir: Path) -> List[str]:
    """
    Pin west's Zephyr base to the active workspace to avoid stale global
    zephyr.base/ZEPHYR_BASE values from unrelated Arduino installs.
    """
    return west_cmd() + ["-z", str((ncs_dir / "zephyr").resolve())]


def ensure_parent_dir(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def detect_supported_host_for_sdk() -> Tuple[str, str]:
    os_tag = host_os_tag()
    arch_tag = host_arch_tag()
    if os_tag == "windows":
        # Zephyr SDK Windows bundles are x86_64-only.
        arch_tag = "x86_64"
    return os_tag, arch_tag
