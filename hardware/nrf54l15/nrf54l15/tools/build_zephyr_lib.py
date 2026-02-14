#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import shlex
import sys
import shutil
import subprocess
import time
from contextlib import contextmanager
from pathlib import Path
from typing import List, Sequence

from zephyr_common import (
    detect_supported_host_for_sdk,
    core_paths,
    exe_name,
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
        out_dir / "app_base.a",
        out_dir / "link_command.template",
        out_dir / "libzephyr.a",
        out_dir / "include" / "modules" / "cmsis_6" / "cmsis_core.h",
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


def ncs_workspace_complete(ncs_dir: Path) -> bool:
    return (
        (ncs_dir / ".west").is_dir()
        and (ncs_dir / ".west" / "config").is_file()
        and (ncs_dir / "nrf" / "west.yml").is_file()
        and (ncs_dir / "zephyr" / "scripts" / "west_commands").is_dir()
    )


def ensure_ncs_workspace(script_dir: Path, ncs_dir: Path, quiet: bool) -> None:
    # A partially initialized workspace (for example missing .west/config)
    # can exist after interrupted first-run bootstrap.
    if ncs_workspace_complete(ncs_dir):
        return
    log(not quiet, "nRF Connect SDK workspace missing/incomplete, bootstrapping...")
    run([sys.executable, str(script_dir / "get_nrf_connect.py")], check=True)
    if not ncs_workspace_complete(ncs_dir):
        raise RuntimeError(f"Failed to bootstrap NCS workspace at {ncs_dir}")


def parse_lock_owner_pid(lock_dir: Path) -> int | None:
    owner_file = lock_dir / "owner.txt"
    if not owner_file.is_file():
        return None
    try:
        text = owner_file.read_text(encoding="utf-8")
    except OSError:
        return None
    for raw in text.splitlines():
        line = raw.strip()
        if not line.startswith("pid="):
            continue
        try:
            return int(line.split("=", 1)[1])
        except ValueError:
            return None
    return None


def pid_is_running(pid: int) -> bool:
    if pid <= 0:
        return False
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    except OSError:
        return False


@contextmanager
def build_lock(lock_dir: Path, quiet: bool, wait_timeout_sec: int = 60 * 60, stale_timeout_sec: int = 2 * 60 * 60):
    lock_dir.parent.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    last_notice = 0.0

    while True:
        try:
            lock_dir.mkdir()
            (lock_dir / "owner.txt").write_text(f"pid={os.getpid()}\n", encoding="utf-8")
            break
        except FileExistsError:
            now = time.monotonic()
            owner_pid = parse_lock_owner_pid(lock_dir)
            if owner_pid is not None and not pid_is_running(owner_pid):
                shutil.rmtree(lock_dir, ignore_errors=True)
                continue

            try:
                age_sec = time.time() - lock_dir.stat().st_mtime
            except OSError:
                continue

            if age_sec > stale_timeout_sec:
                shutil.rmtree(lock_dir, ignore_errors=True)
                continue

            if not quiet and (now - last_notice) >= 5.0:
                waited = int(now - start)
                owner = f" pid={owner_pid}" if owner_pid is not None else ""
                print(f"Another Zephyr artifact build is running ({waited}s){owner}, waiting...")
                last_notice = now

            if (now - start) >= wait_timeout_sec:
                raise RuntimeError(f"Timed out waiting for build lock: {lock_dir}")

            time.sleep(1.0)

    try:
        yield
    finally:
        shutil.rmtree(lock_dir, ignore_errors=True)


def ensure_zephyr_sdk(script_dir: Path, sdk_dir: Path, quiet: bool) -> Path:
    compiler = sdk_tool(sdk_dir, "arm-zephyr-eabi-gcc")
    if compiler and compiler.is_file():
        return sdk_dir

    log(not quiet, "Zephyr SDK not found, bootstrapping...")
    run([sys.executable, str(script_dir / "get_toolchain.py")], check=True)

    compiler = sdk_tool(sdk_dir, "arm-zephyr-eabi-gcc")
    if not compiler or not compiler.is_file():
        raise RuntimeError(f"Zephyr SDK install did not provide compiler at {sdk_dir}")
    return sdk_dir


def resolve_dtc(tools_dir: Path, sdk_dir: Path) -> str:
    host_os, host_arch = detect_supported_host_for_sdk()
    host_tag = f"{host_os}-{host_arch}"
    dtc_name = exe_name("dtc")

    candidates = [
        str(tools_dir / "host-tools" / host_tag / dtc_name),
        str(sdk_dir.parent / "host-tools" / host_tag / dtc_name),
        str(sdk_tool(sdk_dir, "dtc")),
        "dtc",
    ]
    resolved = resolve_program(candidates)
    if not resolved:
        raise RuntimeError("Could not find 'dtc' executable.")
    return resolved


def resolve_ninja(sdk_dir: Path) -> str:
    candidates = [
        str(sdk_tool(sdk_dir, "ninja")),
        "ninja",
    ]
    resolved = resolve_program(candidates)
    if not resolved:
        raise RuntimeError("Could not find 'ninja' executable.")
    return resolved


def sanitize_compile_command(cmd: str, out_dir: Path) -> List[str]:
    parts = shlex.split(cmd)
    if not parts:
        return []
    
    out = []
    skip_next = False
    for i, part in enumerate(parts):
        if i == 0: continue 
        if skip_next:
            skip_next = False
            continue
        if part in ("-o", "-c"):
            skip_next = True
            continue
        if "arduino_base" in part and part.endswith(".c"):
            continue
        out.append(part)
    return out


def ensure_standalone_git_markers(ncs_dir: Path) -> None:
    for marker in (ncs_dir / "nrf" / ".git", ncs_dir / "zephyr" / ".git"):
        if marker.exists() and marker.is_file():
            marker.unlink()

        if not marker.exists():
            marker.mkdir(parents=True, exist_ok=True)

        index_file = marker / "index"
        if not index_file.exists():
            index_file.write_bytes(b"")

        head_file = marker / "HEAD"
        if not head_file.exists():
            head_file.write_text("ref: refs/heads/standalone\n", encoding="utf-8")

        (marker / "refs" / "heads").mkdir(parents=True, exist_ok=True)


def remap_seeed_path(raw_path: str, candidate_roots: Sequence[Path]) -> str:
    candidate = Path(raw_path)
    if candidate.exists():
        return str(candidate.resolve())

    normalized = raw_path.replace("\\", "/")
    marker = "/seeed/nrf54l15/"
    if marker in normalized:
        suffix = normalized.split(marker, 1)[1]
        for root in candidate_roots:
            resolved = root / suffix
            if resolved.exists():
                return str(resolved.resolve())

    if not candidate.is_absolute():
        for root in candidate_roots:
            resolved = root / raw_path
            if resolved.exists():
                return str(resolved.resolve())

    return raw_path


def normalize_paths(raw_items: Sequence[str], candidate_roots: Sequence[Path]) -> List[str]:
    normalized = []
    for item in raw_items:
        if not item:
            continue
        normalized.append(remap_seeed_path(item, candidate_roots))
    return normalized


def assert_files_exist(items: Sequence[str], kind: str) -> None:
    missing = [item for item in items if not Path(item).is_file()]
    if not missing:
        return
    raise RuntimeError(f"Missing {kind} file(s): {', '.join(missing)}")


def prune_non_header_generated_sources(include_root: Path) -> None:
    if not include_root.is_dir():
        return
    for pattern in ("*.c", "*.cc", "*.cpp", "*.cxx", "*.S", "*.s", "*.asm"):
        for source in include_root.rglob(pattern):
            source.unlink()


def main() -> int:
    args = parse_args()
    paths = core_paths(Path(__file__))
    script_dir = paths["script_dir"]
    platform_dir = paths["platform_dir"]
    app_platform_dir = paths["app_platform_dir"]
    tooling_platform_dir = paths["tooling_platform_dir"]
    fallback_platform_dir = paths["fallback_platform_dir"]
    tools_dir = paths["tools_dir"]
    ncs_dir = paths["ncs_dir"]
    sdk_dir = paths["sdk_dir"]

    app_dir = app_platform_dir / "zephyr" / "arduino_base"
    if not app_dir.is_dir():
        raise RuntimeError(f"Missing Zephyr Arduino base directory: {app_dir}")
    
    variant_dir = platform_dir / "variants" / "xiao_nrf54l15"
    out_dir = variant_dir / "zephyr_lib"

    board = os.environ.get("ARDUINO_ZEPHYR_BOARD", "xiao_nrf54l15/nrf54l15/cpuapp")
    board_sanitized = board.replace("/", "_")
    build_dir_default = platform_dir / "tools" / ".arduino-zephyr-build" / board_sanitized
    build_dir = Path(os.environ.get("ARDUINO_ZEPHYR_BUILD_DIR", str(build_dir_default)))

    candidate_roots = []
    for root in (platform_dir, app_platform_dir, tooling_platform_dir, fallback_platform_dir):
        if root not in candidate_roots:
            candidate_roots.append(root)

    extra_conf_files = normalize_paths(args.extra_conf, candidate_roots)
    extra_dtc_overlays = normalize_paths(args.extra_dtc_overlay, candidate_roots)
    assert_files_exist(extra_conf_files, "Kconfig fragment")
    assert_files_exist(extra_dtc_overlays, "DTC overlay")

    extra_conf_file = join_semicolon(extra_conf_files)
    extra_dtc_overlay_file = join_semicolon(extra_dtc_overlays)

    metadata_file = out_dir / "metadata.env"
    if not args.force and is_build_ready(out_dir, build_dir):
        prune_non_header_generated_sources(out_dir / "include")
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

    dtc_bin = ""
    ninja_bin = ""
    env: dict[str, str] = {}
    build_lock_dir = tools_dir / ".build_zephyr_lib.lock"
    with build_lock(build_lock_dir, args.quiet):
        # Recheck under lock to avoid stale decisions when concurrent compile
        # processes race on first-run bootstrap/build state.
        if not args.force and is_build_ready(out_dir, build_dir):
            prune_non_header_generated_sources(out_dir / "include")
            if should_rebuild_due_to_metadata(
                metadata_file,
                platform_dir,
                build_dir,
                extra_conf_file,
                extra_dtc_overlay_file,
            ):
                log(
                    not args.quiet,
                    "Zephyr artifacts were generated from a different path/options, rebuilding...",
                )
            else:
                log(not args.quiet, f"Zephyr artifacts are already prepared: {out_dir}")
                return 0

        ensure_ncs_workspace(script_dir, ncs_dir, args.quiet)
        ensure_standalone_git_markers(ncs_dir)
        sdk_dir = ensure_zephyr_sdk(script_dir, sdk_dir, args.quiet)
        dtc_bin = resolve_dtc(tools_dir, sdk_dir)
        ninja_bin = resolve_ninja(sdk_dir)

        env = with_west_pythonpath(tooling_platform_dir)
        env["ZEPHYR_SDK_INSTALL_DIR"] = str(sdk_dir)
        env["ZEPHYR_BASE"] = str((ncs_dir / "zephyr").resolve())
        prepend_sdk_tool_paths(env, sdk_dir)

        cmake_args = [f"-DDTC={dtc_bin}"]
        if extra_conf_file:
            cmake_args.append(f"-DEXTRA_CONF_FILE={extra_conf_file}")
        if extra_dtc_overlay_file:
            cmake_args.append(f"-DEXTRA_DTC_OVERLAY_FILE={extra_dtc_overlay_file}")

        log(not args.quiet, "Building Zephyr base artifacts for Arduino core...")

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
        try:
            run(cmd, cwd=ncs_dir, env=env, check=True)
        except subprocess.CalledProcessError:
            # If workspace integrity degraded mid-run (interrupted update,
            # mixed versions, etc.), heal once and retry.
            if ncs_workspace_complete(ncs_dir):
                raise
            log(not args.quiet, "NCS workspace became incomplete, repairing and retrying build...")
            ensure_ncs_workspace(script_dir, ncs_dir, args.quiet)
            ensure_standalone_git_markers(ncs_dir)
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

    inc_dir = out_dir / "include"
    if inc_dir.exists():
        shutil.rmtree(inc_dir)
    inc_dir.mkdir()
    
    # Proper include structure: copy include directories themselves
    shutil.copytree(build_dir / "zephyr" / "include" / "generated", inc_dir / "generated")
    # Zephyr base includes are in ncs_dir/zephyr/include
    shutil.copytree(ncs_dir / "zephyr" / "include" / "zephyr", inc_dir / "zephyr")
    # NRF base includes are in ncs_dir/nrf/include
    shutil.copytree(ncs_dir / "nrf" / "include", inc_dir / "nrf")
    # Some Zephyr headers are included without "zephyr/" prefix (e.g. cmsis_core.h).
    for module_name in ("cmsis", "cmsis_6"):
        module_dir = ncs_dir / "zephyr" / "modules" / module_name
        if module_dir.is_dir():
            shutil.copytree(module_dir, inc_dir / "modules" / module_name)
    prune_non_header_generated_sources(inc_dir)

    (out_dir / "cflags.rsp").write_text(" ".join(sanitize_compile_command(c_entry, out_dir)) + "\n", encoding="utf-8")
    (out_dir / "cppflags.rsp").write_text(" ".join(sanitize_compile_command(cpp_entry, out_dir)) + "\n", encoding="utf-8")

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

    zephyr_lib = build_dir / "zephyr" / "libzephyr.a"
    if zephyr_lib.is_file():
        (out_dir / "libzephyr.a").write_bytes(zephyr_lib.read_bytes())

    app_archive = build_dir / "app" / "libapp.a"
    if app_archive.is_file():
        (out_dir / "app_base.a").write_bytes(app_archive.read_bytes())
    
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

    log(not args.quiet, f"Zephyr artifacts prepared in {out_dir}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)
