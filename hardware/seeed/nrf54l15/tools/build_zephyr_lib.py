#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import shlex
import subprocess
import sys
from pathlib import Path
from typing import List, Sequence

from zephyr_common import (
    core_paths,
    ensure_zephyr_python_deps,
    prepend_sdk_tool_paths,
    read_metadata,
    resolve_program,
    run,
    sdk_tool,
    west_cmd_with_zephyr_base,
    with_west_pythonpath,
    write_metadata,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build base Zephyr artifacts for Arduino core.")
    parser.add_argument("--force", action="store_true", help="Force rebuild even if artifacts exist.")
    parser.add_argument("--quiet", action="store_true", help="Reduce log output.")
    parser.add_argument(
        "--extra-conf",
        action="append",
        nargs="?",
        const="",
        default=[],
        help="Extra Kconfig fragment.",
    )
    parser.add_argument(
        "--extra-dtc-overlay",
        action="append",
        nargs="?",
        const="",
        default=[],
        help="Extra DTC overlay.",
    )
    return parser.parse_args()


def log(enabled: bool, message: str) -> None:
    if enabled:
        print(message)


def join_semicolon(items: Sequence[str]) -> str:
    return ";".join([item for item in items if item])


def is_build_ready(out_dir: Path, build_dir: Path) -> bool:
    required = [
        out_dir / "cflags.rsp",
        out_dir / "cppflags.rsp",
        out_dir / "link_command.template",
    ]
    if not all(path.is_file() for path in required):
        return False

    zephyr_candidates = [
        build_dir / "zephyr" / "zephyr.elf",
        build_dir / "zephyr" / "zephyr_pre0.elf",
        build_dir / "zephyr" / "zephyr.hex",
    ]
    return any(path.is_file() for path in zephyr_candidates)


def should_rebuild_due_to_metadata(
    metadata_file: Path,
    platform_dir: Path,
    build_dir: Path,
    extra_conf_file: str,
    extra_dtc_overlay_file: str,
) -> bool:
    if not metadata_file.is_file():
        return True

    values = read_metadata(metadata_file)
    if values.get("ARDUINO_PLATFORM_DIR", "") != str(platform_dir):
        return True
    if values.get("ARDUINO_ZEPHYR_BUILD_DIR", "") not in ("", str(build_dir)):
        return True
    if values.get("ARDUINO_ZEPHYR_EXTRA_CONF_FILE", "") != extra_conf_file:
        return True
    if values.get("ARDUINO_ZEPHYR_EXTRA_DTC_OVERLAY_FILE", "") != extra_dtc_overlay_file:
        return True
    return False


def ensure_ncs_workspace(script_dir: Path, ncs_dir: Path, quiet: bool) -> None:
    if (ncs_dir / ".west").is_dir():
        return

    log(not quiet, "nRF Connect SDK workspace not found, bootstrapping...")

    # On Unix systems, use file locking to prevent multiple simultaneous bootstraps
    if sys.platform != "win32":
        import fcntl
        import time

        lock_file = ncs_dir / ".bootstrap_lock"
        ncs_dir.mkdir(parents=True, exist_ok=True)

        # Try to acquire exclusive lock
        with open(lock_file, "w") as f:
            try:
                fcntl.flock(f.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            except (IOError, BlockingIOError):
                # Lock is held by another process
                wait_time = 0
                max_wait = 300  # 5 minutes
                while wait_time < max_wait:
                    try:
                        fcntl.flock(f.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
                        # Got the lock, check if workspace was initialized by other process
                        if (ncs_dir / ".west").is_dir():
                            log(not quiet, "nRF Connect SDK workspace initialized by another process.")
                            return
                        break
                    except (IOError, BlockingIOError):
                        wait_time += 5
                        if wait_time % 30 == 0 and not quiet:
                            log(not quiet, f"Waiting for bootstrap... ({wait_time}s)")
                        time.sleep(5)

                if wait_time >= max_wait:
                    raise RuntimeError("Timeout waiting for nRF Connect SDK bootstrap. "
                                     "Another process may be stuck holding the lock.")

        # Clean up lock file when done
        lock_file.unlink(missing_ok=True)

    run([sys.executable, str(script_dir / "get_nrf_connect.py")], check=True)


def ensure_zephyr_sdk(script_dir: Path, platform_dir: Path, quiet: bool) -> Path:
    sdk_dir = platform_dir / "tools" / "zephyr-sdk"
    compiler = sdk_tool(sdk_dir, "arm-zephyr-eabi-gcc")
    if compiler and compiler.is_file():
        return sdk_dir

    log(not quiet, "Zephyr SDK not found, bootstrapping...")
    run([sys.executable, str(script_dir / "get_toolchain.py")], check=True)

    compiler = sdk_tool(sdk_dir, "arm-zephyr-eabi-gcc")
    if not compiler or not compiler.is_file():
        raise RuntimeError(f"Zephyr SDK install did not provide compiler at {sdk_dir}")
    return sdk_dir


def resolve_dtc(platform_dir: Path, sdk_dir: Path) -> Path:
    dtc_env = Path(os.environ["ARDUINO_DTC"]) if "ARDUINO_DTC" in os.environ else None
    if dtc_env and dtc_env.is_file():
        return dtc_env

    from_path = resolve_program(["dtc"])
    if from_path:
        return Path(from_path)

    sdk_dtc = sdk_tool(sdk_dir, "dtc")
    if sdk_dtc:
        return sdk_dtc

    host_tools_candidates = [
        platform_dir / "tools" / "host-tools" / "linux-x86_64" / "dtc",
        platform_dir / "tools" / "host-tools" / "macos-x86_64" / "dtc",
        platform_dir / "tools" / "host-tools" / "macos-aarch64" / "dtc",
        platform_dir / "tools" / "host-tools" / "windows-x86_64" / "dtc.exe",
    ]
    for candidate in host_tools_candidates:
        if candidate.is_file():
            return candidate

    raise RuntimeError("Unable to find a working dtc binary. Install dtc or set ARDUINO_DTC.")


def sanitize_compile_command(command: str) -> List[str]:
    tokens = shlex.split(command)
    if not tokens:
        return []
    tokens = tokens[1:]  # drop compiler executable

    out: List[str] = []
    i = 0
    while i < len(tokens):
        token = tokens[i]

        if token in ("-c",):
            i += 1
            continue

        if token in ("-o", "-MF", "-MT"):
            i += 2
            continue

        if token.startswith("-o") and token != "-o":
            i += 1
            continue

        if token.startswith("-MF") and token != "-MF":
            i += 1
            continue

        if token.startswith("-MT") and token != "-MT":
            i += 1
            continue

        if not token.startswith("-") and token.endswith((".c", ".cc", ".cpp", ".cxx", ".S", ".s")):
            i += 1
            continue

        out.append(token)
        i += 1

    return out


def resolve_ninja(sdk_dir: Path) -> str:
    sdk_ninja = sdk_tool(sdk_dir, "ninja")
    if sdk_ninja and sdk_ninja.is_file():
        return str(sdk_ninja)
    ninja = resolve_program(["ninja", "ninja-build"])
    if ninja:
        return ninja
    raise RuntimeError("Unable to find ninja executable.")


def ensure_patched(platform_dir: Path) -> None:
    """
    Ensure Zephyr SDK's edtlib.py is patched for PyYAML 6.0+ compatibility.
    This is a no-op if the patch is already applied.
    """
    patch_script = platform_dir / "tools" / "ensure_patched.py"
    if patch_script.exists():
        import subprocess
        subprocess.run(
            [sys.executable, str(patch_script)],
            capture_output=True,
            check=False  # Don't fail if patch script has issues
        )


def ensure_standalone_git_markers(ncs_dir: Path) -> None:
    """
    Some NCS/Zephyr generated headers depend on `<module>/.git` path existence.
    In this standalone packaged core we may not ship real git repos, so we add
    lightweight marker files when missing.
    """
    for marker in (ncs_dir / "nrf" / ".git", ncs_dir / "zephyr" / ".git"):
        if marker.exists() and marker.is_file():
            marker.unlink()

        if not marker.exists():
            marker.mkdir(parents=True, exist_ok=True)

        # Some generated-header rules depend on `.git/index` specifically.
        index_file = marker / "index"
        if not index_file.exists():
            index_file.write_bytes(b"")

        head_file = marker / "HEAD"
        if not head_file.exists():
            head_file.write_text("ref: refs/heads/standalone\n", encoding="utf-8")

        (marker / "refs" / "heads").mkdir(parents=True, exist_ok=True)


def main() -> int:
    args = parse_args()
    paths = core_paths(Path(__file__))
    script_dir = paths["script_dir"]
    platform_dir = paths["platform_dir"]
    ncs_dir = paths["ncs_dir"]
    app_dir = platform_dir / "zephyr" / "arduino_base"
    variant_dir = platform_dir / "variants" / "xiao_nrf54l15"
    out_dir = variant_dir / "zephyr_lib"

    # Ensure PyYAML compatibility by applying patch to edtlib.py
    # This works on any system with python3 - no venv or PyYAML downgrade needed
    ensure_patched(platform_dir)

    board = os.environ.get("ARDUINO_ZEPHYR_BOARD", "xiao_nrf54l15/nrf54l15/cpuapp")
    board_sanitized = board.replace("/", "_")
    if os.name == "nt":
        local_app_data = os.environ.get("LOCALAPPDATA", str(Path.home() / "AppData" / "Local"))
        build_root = Path(
            os.environ.get(
                "ARDUINO_NRF54L15_BUILD_ROOT",
                str(Path(local_app_data) / "nrf54l15-build"),
            )
        )
        build_dir_default = build_root / board_sanitized
    else:
        build_dir_default = platform_dir / "tools" / ".arduino-zephyr-build" / board_sanitized
    build_dir = Path(os.environ.get("ARDUINO_ZEPHYR_BUILD_DIR", str(build_dir_default)))

    extra_conf_files = [item for item in args.extra_conf if item]
    extra_dtc_overlays = [item for item in args.extra_dtc_overlay if item]
    extra_conf_file = join_semicolon(extra_conf_files)
    extra_dtc_overlay_file = join_semicolon(extra_dtc_overlays)

    ensure_ncs_workspace(script_dir, ncs_dir, args.quiet)
    ensure_standalone_git_markers(ncs_dir)
    sdk_dir = ensure_zephyr_sdk(script_dir, platform_dir, args.quiet)
    dtc_bin = resolve_dtc(platform_dir, sdk_dir)
    ninja_bin = resolve_ninja(sdk_dir)

    metadata_file = out_dir / "metadata.env"
    if not args.force and is_build_ready(out_dir, build_dir):
        if should_rebuild_due_to_metadata(
            metadata_file,
            platform_dir,
            build_dir,
            extra_conf_file,
            extra_dtc_overlay_file,
        ):
            log(not args.quiet, "Zephyr artifacts were generated from a different path/options, rebuilding...")
        else:
            log(not args.quiet, f"Zephyr artifacts are already prepared: {out_dir}")
            return 0

    out_dir.mkdir(parents=True, exist_ok=True)

    env = with_west_pythonpath(platform_dir)
    ensure_zephyr_python_deps(platform_dir, ncs_dir, env)
    env["ZEPHYR_SDK_INSTALL_DIR"] = str(sdk_dir)
    env["ZEPHYR_BASE"] = str((ncs_dir / "zephyr").resolve())
    prepend_sdk_tool_paths(env, sdk_dir)

    # No need to set WEST_PYTHON - the patch makes edtlib.py work with system Python
    cmake_args = [f"-DDTC={dtc_bin}"]
    if extra_conf_file:
        cmake_args.append(f"-DEXTRA_CONF_FILE={extra_conf_file}")
    if extra_dtc_overlay_file:
        cmake_args.append(f"-DEXTRA_DTC_OVERLAY_FILE={extra_dtc_overlay_file}")

    log(not args.quiet, "Building Zephyr base artifacts for Arduino core...")
    log(not args.quiet, f"Board: {board}")
    log(not args.quiet, f"Build dir: {build_dir}")
    log(not args.quiet, f"DTC: {dtc_bin}")
    if extra_conf_file:
        log(not args.quiet, f"Extra conf: {extra_conf_file}")
    if extra_dtc_overlay_file:
        log(not args.quiet, f"Extra DTC overlay: {extra_dtc_overlay_file}")

    cmd = west_cmd_with_zephyr_base(ncs_dir) + [
        "build",
        "--build-dir",
        str(build_dir),
        "-p",
        "always",
        "-b",
        board,
        "--no-sysbuild",
        str(app_dir),
        "--",
        *cmake_args,
    ]
    run(cmd, cwd=ncs_dir, env=env, check=True)

    compile_commands = build_dir / "compile_commands.json"
    if not compile_commands.is_file():
        raise RuntimeError(f"Missing {compile_commands}")

    entries = json.loads(compile_commands.read_text(encoding="utf-8"))
    c_entry = None
    cpp_entry = None
    for entry in entries:
        source = entry.get("file", "")
        command = entry.get("command")
        if not command:
            continue
        if c_entry is None and source.endswith(".c"):
            c_entry = command
        if cpp_entry is None and source.endswith((".cpp", ".cc", ".cxx")):
            cpp_entry = command
        if c_entry and cpp_entry:
            break

    if c_entry is None:
        raise RuntimeError("No C compile command found in compile_commands.json")
    if cpp_entry is None:
        cpp_entry = c_entry

    (out_dir / "cflags.rsp").write_text(" ".join(sanitize_compile_command(c_entry)) + "\n", encoding="utf-8")
    (out_dir / "cppflags.rsp").write_text(" ".join(sanitize_compile_command(cpp_entry)) + "\n", encoding="utf-8")

    link_cmd_output = run(
        [ninja_bin, "-C", str(build_dir), "-t", "commands", "zephyr/zephyr.elf"],
        env=env,
        check=True,
        capture_output=True,
    ).stdout
    link_lines = [line.strip() for line in link_cmd_output.splitlines() if line.strip()]
    if not link_lines:
        raise RuntimeError("Failed to resolve Zephyr final link command.")
    link_command = link_lines[-1]

    link_template = link_command.replace(" -specs=picolibc.specs", " __ARDUINO_APP_OBJECTS__ -specs=picolibc.specs")
    if link_template == link_command:
        link_template = f"{link_command} __ARDUINO_APP_OBJECTS__"
    (out_dir / "link_command.template").write_text(link_template + "\n", encoding="utf-8")

    write_metadata(
        metadata_file,
        {
            "ARDUINO_ZEPHYR_BOARD": board,
            "ARDUINO_DTC": str(dtc_bin),
            "ARDUINO_ZEPHYR_BUILD_DIR": str(build_dir),
            "ARDUINO_PLATFORM_DIR": str(platform_dir),
            "ARDUINO_ZEPHYR_EXTRA_CONF_FILE": extra_conf_file,
            "ARDUINO_ZEPHYR_EXTRA_DTC_OVERLAY_FILE": extra_dtc_overlay_file,
        },
    )

    app_archive = build_dir / "app" / "libapp.a"
    if app_archive.is_file():
        (out_dir / "app_base.a").write_bytes(app_archive.read_bytes())

    log(not args.quiet, f"Zephyr artifacts prepared in {out_dir}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # pragma: no cover - simple script error surface
        print(str(exc), file=sys.stderr)
        raise
