#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import zipfile
from contextlib import contextmanager
from pathlib import Path

from zephyr_common import core_paths, is_windows, run, west_cmd, with_west_pythonpath

MINGIT_API_URL = "https://api.github.com/repos/git-for-windows/git/releases/latest"
MINGIT_USER_AGENT = "xiao-nrf54l15-arduino-core-ncs-bootstrap/0.1.14"


def workspace_ready(ncs_dir: Path) -> bool:
    return (ncs_dir / ".west").is_dir() and (ncs_dir / "nrf").is_dir() and (ncs_dir / "zephyr").is_dir()


def log(message: str) -> None:
    print(message, file=sys.stderr, flush=True)


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
def workspace_lock(lock_dir: Path, wait_timeout_sec: int = 3 * 60 * 60, stale_timeout_sec: int = 6 * 60 * 60):
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
                log(f"Removing stale nRF Connect lock held by dead pid={owner_pid}: {lock_dir}")
                shutil.rmtree(lock_dir, ignore_errors=True)
                continue

            try:
                age_sec = time.time() - lock_dir.stat().st_mtime
            except OSError:
                continue

            if age_sec > stale_timeout_sec:
                log(f"Removing stale nRF Connect lock: {lock_dir}")
                shutil.rmtree(lock_dir, ignore_errors=True)
                continue

            if (now - last_notice) >= 5.0:
                waited = int(now - start)
                owner = f" pid={owner_pid}" if owner_pid is not None else ""
                log(
                    "Another nRF Connect bootstrap is running "
                    f"({waited}s){owner}, waiting... "
                    "First run may download >2 GB of SDK sources."
                )
                last_notice = now

            if (now - start) >= wait_timeout_sec:
                raise RuntimeError(f"Timed out waiting for nRF Connect bootstrap lock: {lock_dir}")

            time.sleep(1.0)

    try:
        yield
    finally:
        shutil.rmtree(lock_dir, ignore_errors=True)


def workspace_metadata_valid(west: list[str], env: dict[str, str], ncs_dir: Path) -> bool:
    west_dir = ncs_dir / ".west"
    if not west_dir.is_dir():
        return False
    if not (west_dir / "config").is_file():
        return False
    try:
        out = run(west + ["topdir"], cwd=ncs_dir, env=env, check=True, capture_output=True).stdout.strip()
    except subprocess.CalledProcessError:
        return False
    if not out:
        return False
    try:
        return Path(out).resolve() == ncs_dir.resolve()
    except OSError:
        return False


def reset_workspace(ncs_dir: Path, reason: str) -> None:
    print(f"Resetting nRF Connect SDK workspace ({reason}): {ncs_dir}")
    shutil.rmtree(ncs_dir, ignore_errors=True)


def print_first_bootstrap_notice(ncs_dir: Path) -> None:
    log("Initializing nRF Connect SDK workspace...")
    log(f"Workspace path: {ncs_dir}")
    log("First-time bootstrap downloads >2 GB and can take a while on slower connections.")
    log("Tip: enable Arduino compile verbose output to see full bootstrap progress.")


def github_dns_ok() -> bool:
    try:
        socket.getaddrinfo("github.com", 443, type=socket.SOCK_STREAM)
        return True
    except OSError:
        return False


def output_indicates_dns_failure(output: str) -> bool:
    lowered = output.lower()
    patterns = (
        "could not resolve host",
        "name or service not known",
        "temporary failure in name resolution",
        "nodename nor servname provided",
    )
    return any(pattern in lowered for pattern in patterns)


def extract_failed_project(output: str) -> str | None:
    match = re.search(r"update failed for project ([^\s]+)", output, flags=re.IGNORECASE)
    if not match:
        return None
    return match.group(1).strip()


def output_tail(output: str, max_lines: int = 80) -> str:
    lines = output.splitlines()
    if len(lines) <= max_lines:
        return "\n".join(lines)
    head = f"... ({len(lines) - max_lines} lines omitted) ..."
    return "\n".join([head, *lines[-max_lines:]])


def run_west_network_step(
    west: list[str],
    args: list[str],
    *,
    cwd: Path | None = None,
    env: dict[str, str],
    step_name: str,
    max_attempts: int = 8,
    initial_delay_sec: int = 3,
) -> None:
    """
    Run a west command with retries for transient network/DNS failures.

    This is primarily for first-time bootstrap where many GitHub fetches can
    fail intermittently on Windows networks.
    """
    delay = max(1, initial_delay_sec)
    cmd = west + ["-q"] + args
    last_error: subprocess.CalledProcessError | None = None
    last_output = ""

    for attempt in range(1, max_attempts + 1):
        log(f"{step_name}: attempt {attempt}/{max_attempts}")
        try:
            result = subprocess.run(
                cmd,
                cwd=str(cwd) if cwd else None,
                env=env,
                check=False,
                capture_output=True,
                text=True,
            )
        except OSError as exc:
            last_output = str(exc)
            if attempt >= max_attempts:
                raise RuntimeError(f"{step_name} failed to start: {exc}") from exc
            log(f"{step_name} failed to start, retrying ({attempt}/{max_attempts}) in {delay}s...")
            time.sleep(delay)
            delay = min(30, delay * 2)
            continue

        combined_output = "\n".join(
            [part for part in ((result.stdout or "").strip(), (result.stderr or "").strip()) if part]
        )
        if result.returncode == 0:
            if attempt > 1:
                log(f"{step_name} succeeded on retry {attempt}/{max_attempts}.")
            return

        last_error = subprocess.CalledProcessError(result.returncode, cmd)
        last_output = combined_output

        if attempt >= max_attempts:
            break

        failed_project = extract_failed_project(combined_output)
        if failed_project:
            log(
                f"{step_name} failed for project '{failed_project}' (exit {result.returncode}), "
                f"retrying ({attempt}/{max_attempts}) in {delay}s..."
            )
        elif output_indicates_dns_failure(combined_output) or not github_dns_ok():
            log(
                f"{step_name} failed: github.com DNS lookup failed, retrying "
                f"({attempt}/{max_attempts}) in {delay}s..."
            )
            log("Check internet/VPN/proxy/DNS if retries keep failing.")
        else:
            log(
                f"{step_name} failed (exit {result.returncode}), retrying "
                f"({attempt}/{max_attempts}) in {delay}s..."
            )

        time.sleep(delay)
        delay = min(30, delay * 2)

    if last_error is None:
        raise RuntimeError(f"{step_name} failed unexpectedly.")

    if last_output:
        log(f"{step_name} final failure output tail:")
        log(output_tail(last_output))

    if output_indicates_dns_failure(last_output) or not github_dns_ok():
        raise RuntimeError(
            f"{step_name} failed because github.com could not be resolved. "
            "This is a network/DNS issue on the host. "
            "Check internet connectivity, VPN/proxy, firewall, or DNS settings and retry."
        ) from last_error
    raise last_error


def parse_positive_int_env(name: str, default: int) -> int:
    raw = os.environ.get(name, "").strip()
    if not raw:
        return default
    try:
        value = int(raw)
    except ValueError:
        return default
    return value if value > 0 else default


def parse_group_filters_env(name: str, default: str = "") -> list[str]:
    raw = os.environ.get(name, default).strip()
    if not raw:
        return []
    tokens = [item.strip() for item in re.split(r"[,\s]+", raw) if item.strip()]
    return tokens


def git_in_env(env: dict[str, str]) -> str | None:
    return shutil.which("git", path=env.get("PATH"))


def git_is_runnable(git_cmd: str, env: dict[str, str]) -> bool:
    try:
        result = subprocess.run(
            [git_cmd, "--version"],
            env=env,
            check=False,
            capture_output=True,
            text=True,
            timeout=20,
        )
    except (OSError, subprocess.TimeoutExpired):
        return False
    return result.returncode == 0


def prepend_path_entries(env: dict[str, str], entries: list[Path]) -> None:
    paths = [str(p) for p in entries if p.is_dir()]
    if not paths:
        return
    old = env.get("PATH", "")
    env["PATH"] = os.pathsep.join(paths + ([old] if old else []))


def mingit_git_candidate(root: Path) -> Path | None:
    candidates = [
        root / "mingw64" / "bin" / "git.exe",
        root / "cmd" / "git.exe",
        root / "bin" / "git.exe",
        root / "usr" / "bin" / "git.exe",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def mingit_path_entries(root: Path) -> list[Path]:
    return [
        root / "cmd",
        root / "bin",
        root / "mingw64" / "bin",
        root / "usr" / "bin",
    ]


def urlopen_retry(request: urllib.request.Request, *, attempts: int = 6, timeout_sec: int = 60):
    last_exc: BaseException | None = None
    for attempt in range(1, attempts + 1):
        try:
            return urllib.request.urlopen(request, timeout=timeout_sec)  # nosec B310 - trusted host
        except urllib.error.HTTPError as exc:
            last_exc = exc
            if exc.code not in (408, 425, 429, 500, 502, 503, 504) or attempt >= attempts:
                raise
            log(f"HTTP {exc.code} while fetching MinGit metadata, retrying ({attempt}/{attempts})...")
            time.sleep(min(12, attempt * 2))
        except Exception as exc:
            last_exc = exc
            if attempt >= attempts:
                raise
            log(f"MinGit download/metadata request failed, retrying ({attempt}/{attempts})...")
            time.sleep(min(12, attempt * 2))

    if last_exc is not None:
        raise RuntimeError("MinGit request failed") from last_exc
    raise RuntimeError("MinGit request failed")


def fetch_mingit_asset() -> tuple[str, str, int]:
    request = urllib.request.Request(
        MINGIT_API_URL,
        headers={
            "User-Agent": MINGIT_USER_AGENT,
            "Accept": "application/vnd.github+json",
        },
    )
    with urlopen_retry(request, attempts=6, timeout_sec=45) as response:
        payload = json.loads(response.read().decode("utf-8"))

    assets = payload.get("assets", [])
    preferred = None
    fallback = None
    for asset in assets:
        name = str(asset.get("name", ""))
        if not name.startswith("MinGit-") or not name.endswith("-64-bit.zip"):
            continue
        url = str(asset.get("browser_download_url", ""))
        size = int(asset.get("size", 0) or 0)
        if not url:
            continue
        if "busybox" in name.lower():
            if fallback is None:
                fallback = (name, url, size)
            continue
        preferred = (name, url, size)
        break

    chosen = preferred or fallback
    if chosen is None:
        raise RuntimeError("Unable to locate a MinGit 64-bit asset from Git for Windows releases.")
    return chosen


def download_mingit(url: str, dest: Path, expected_size: int) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    if expected_size > 0 and dest.is_file() and dest.stat().st_size == expected_size:
        return

    part_path = dest.with_name(dest.name + ".part")
    if dest.is_file():
        dest.replace(part_path)

    downloaded = part_path.stat().st_size if part_path.is_file() else 0
    if expected_size > 0 and downloaded == expected_size:
        part_path.replace(dest)
        return
    if expected_size > 0 and downloaded > expected_size:
        part_path.unlink(missing_ok=True)
        downloaded = 0

    max_attempts = 20
    resume_allowed = True
    last_error: BaseException | None = None
    for attempt in range(1, max_attempts + 1):
        headers = {
            "User-Agent": MINGIT_USER_AGENT,
            "Accept": "application/octet-stream",
        }
        mode = "ab"
        attempt_start = downloaded
        if downloaded > 0 and resume_allowed:
            headers["Range"] = f"bytes={downloaded}-"
        else:
            if downloaded > 0 and not resume_allowed:
                downloaded = 0
            mode = "wb"

        request = urllib.request.Request(url, headers=headers)
        try:
            with urlopen_retry(request, attempts=1, timeout_sec=180) as response:
                status = int(getattr(response, "status", 200) or 200)
                if downloaded > 0 and status != 206:
                    downloaded = 0
                    mode = "wb"

                with part_path.open(mode) as out:
                    while True:
                        chunk = response.read(1024 * 1024)
                        if not chunk:
                            break
                        out.write(chunk)
                        downloaded += len(chunk)

            if expected_size > 0:
                if downloaded == expected_size:
                    break
                if downloaded > expected_size:
                    raise RuntimeError(
                        f"Downloaded MinGit archive is larger than expected ({downloaded}/{expected_size} bytes)."
                    )

                if downloaded <= attempt_start and attempt_start > 0:
                    resume_allowed = False
                    log("MinGit download made no progress while resuming; retrying with clean restart...")
                log(f"MinGit download incomplete ({downloaded}/{expected_size} bytes), retrying ({attempt}/{max_attempts})...")
                time.sleep(min(20, attempt * 2))
                continue
            break
        except urllib.error.HTTPError as exc:
            last_error = exc
            if exc.code == 416:
                downloaded = part_path.stat().st_size if part_path.is_file() else 0
                if expected_size > 0 and downloaded == expected_size:
                    break
                resume_allowed = False
                downloaded = 0
                part_path.unlink(missing_ok=True)
                log(f"MinGit range request rejected (HTTP 416), restarting ({attempt}/{max_attempts})...")
            else:
                if attempt >= max_attempts:
                    raise
                log(f"MinGit download failed with HTTP {exc.code}, retrying ({attempt}/{max_attempts})...")
            if attempt >= max_attempts:
                raise
            time.sleep(min(20, attempt * 2))
        except Exception as exc:
            last_error = exc
            if attempt >= max_attempts:
                raise
            log(f"MinGit download failed, retrying ({attempt}/{max_attempts})...")
            time.sleep(min(20, attempt * 2))
            downloaded = part_path.stat().st_size if part_path.is_file() else 0

    final_size = part_path.stat().st_size if part_path.is_file() else 0
    if expected_size > 0 and final_size != expected_size:
        if last_error is not None:
            raise RuntimeError(
                f"Downloaded MinGit archive has unexpected size ({final_size}/{expected_size} bytes)."
            ) from last_error
        raise RuntimeError(f"Downloaded MinGit archive has unexpected size ({final_size}/{expected_size} bytes).")

    try:
        with zipfile.ZipFile(part_path) as zf:
            bad_member = zf.testzip()
            if bad_member is not None:
                raise RuntimeError(f"MinGit archive is corrupted (bad entry: {bad_member}).")
    except zipfile.BadZipFile as exc:
        raise RuntimeError("MinGit archive is not a valid ZIP file.") from exc

    part_path.replace(dest)


def install_mingit_zip(zip_path: Path, install_root: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="mingit-extract-", dir=str(install_root.parent)) as temp_dir:
        temp_root = Path(temp_dir)
        with zipfile.ZipFile(zip_path) as zf:
            zf.extractall(temp_root)

        entries = list(temp_root.iterdir())
        if len(entries) == 1 and entries[0].is_dir():
            source_root = entries[0]
        else:
            source_root = temp_root

        if install_root.exists():
            shutil.rmtree(install_root, ignore_errors=True)
        install_root.mkdir(parents=True, exist_ok=True)

        for item in list(source_root.iterdir()):
            shutil.move(str(item), str(install_root / item.name))


def ensure_git_available(platform_tools_dir: Path, env: dict[str, str]) -> str:
    existing_git = git_in_env(env)
    if existing_git and git_is_runnable(existing_git, env):
        return existing_git
    if existing_git:
        log(f"Found git on PATH but it is not runnable ({existing_git}); bootstrapping portable MinGit...")

    if not is_windows():
        raise RuntimeError("git is required.")

    mingit_root = platform_tools_dir / "mingit"
    git_exe = mingit_git_candidate(mingit_root)
    if git_exe is not None:
        prepend_path_entries(env, mingit_path_entries(mingit_root))
        if git_is_runnable(str(git_exe), env):
            return str(git_exe)

    log("git not found on PATH; bootstrapping portable MinGit for Windows...")
    asset_name, asset_url, asset_size = fetch_mingit_asset()
    archive_path = platform_tools_dir / asset_name
    download_mingit(asset_url, archive_path, asset_size)
    install_mingit_zip(archive_path, mingit_root)
    prepend_path_entries(env, mingit_path_entries(mingit_root))

    git_exe = mingit_git_candidate(mingit_root)
    if git_exe is None or not git_is_runnable(str(git_exe), env):
        raise RuntimeError(
            "git is required. Automatic MinGit bootstrap failed; install Git for Windows and retry."
        )
    return str(git_exe)


def ensure_west_python_deps(platform_dir: Path, env: dict[str, str]) -> None:
    pydeps_dir = platform_dir / "tools" / "pydeps"
    pydeps_dir.mkdir(parents=True, exist_ok=True)

    probe = subprocess.run(
        [sys.executable, "-c", "import west, colorama"],
        env=env,
        check=False,
        capture_output=True,
        text=True,
    )
    if probe.returncode == 0:
        return

    log("Bundled west Python dependencies missing, installing fallback Python packages (west, colorama)...")
    run(
        [
            sys.executable,
            "-m",
            "pip",
            "install",
            "--disable-pip-version-check",
            "--no-input",
            "--target",
            str(pydeps_dir),
            "west",
            "colorama",
        ],
        env=env,
        check=True,
    )


def main() -> int:
    paths = core_paths(Path(__file__))
    platform_dir = paths["platform_dir"]
    tools_dir = paths["tools_dir"]
    ncs_dir = paths["ncs_dir"]

    ncs_manifest = os.environ.get("NCS_MANIFEST", "https://github.com/nrfconnect/sdk-nrf")
    ncs_revision = os.environ.get("NCS_REVISION", "main")
    ncs_fetch_depth = os.environ.get("NCS_FETCH_DEPTH", "1")
    ncs_network_retries = parse_positive_int_env("NCS_NETWORK_RETRIES", 20)
    # Exclude Matter by default to reduce bootstrap size/time and avoid
    # transient failures in optional modules during first compile.
    ncs_group_filters = parse_group_filters_env("NCS_GROUP_FILTER", "-matter")
    force_update = os.environ.get("NCS_FORCE_UPDATE", "0") == "1"

    env = with_west_pythonpath(platform_dir)
    env.setdefault("NO_COLOR", "1")
    west = west_cmd()
    lock_dir = ncs_dir.parent / ".get_nrf_connect.lock"

    with workspace_lock(lock_dir):
        if is_windows() and shutil.which("wmic") is None:
            log(
                "Warning: wmic.exe was not found. Some Arduino IDE Windows builds may crash "
                "with ECONNRESET/spawn wmic.exe ENOENT during long compiles."
            )
            log("If that happens, install Windows optional feature 'WMIC' or use arduino-cli.")

        git_cmd = ensure_git_available(tools_dir, env)
        run([git_cmd, "--version"], env=env, check=True, capture_output=True)

        ensure_west_python_deps(platform_dir, env)

        # Verify west imports correctly from bundled pydeps.
        run(west + ["--version"], env=env, check=True, capture_output=True)

        metadata_ok = workspace_metadata_valid(west, env, ncs_dir)
        if (ncs_dir / ".west").exists() and not metadata_ok:
            reset_workspace(ncs_dir, "broken west metadata")
            metadata_ok = False

        if not metadata_ok or force_update:
            print_first_bootstrap_notice(ncs_dir)

        if not metadata_ok:
            # west init fails on non-empty directories without workspace metadata.
            if ncs_dir.exists() and any(ncs_dir.iterdir()):
                reset_workspace(ncs_dir, "incomplete workspace contents")
            ncs_dir.parent.mkdir(parents=True, exist_ok=True)
            try:
                run_west_network_step(
                    west,
                    ["init", "-m", ncs_manifest, "--mr", ncs_revision, str(ncs_dir)],
                    env=env,
                    step_name="west init",
                    max_attempts=ncs_network_retries,
                )
            except subprocess.CalledProcessError:
                # Another process may have initialized the workspace just before this call.
                if not workspace_metadata_valid(west, env, ncs_dir):
                    raise
        elif workspace_ready(ncs_dir) and not force_update:
            print(f"nRF Connect SDK workspace already present: {ncs_dir}")
            print("Set NCS_FORCE_UPDATE=1 to fetch/update remotes.")
            return 0

        update_args = ["update", "--narrow", f"--fetch-opt=--depth={ncs_fetch_depth}"]
        for filter_item in ncs_group_filters:
            update_args.append(f"--group-filter={filter_item}")
        if ncs_group_filters:
            log("Using west group filters: " + ", ".join(ncs_group_filters))
        run_west_network_step(
            west,
            update_args,
            cwd=ncs_dir,
            env=env,
            step_name="west update",
            max_attempts=ncs_network_retries,
        )
        run(west + ["zephyr-export"], cwd=ncs_dir, env=env, check=True)

        if not workspace_ready(ncs_dir):
            raise RuntimeError(f"Incomplete NCS workspace after bootstrap: {ncs_dir}")

        # Patch edtlib.py for PyYAML 6.0+ compatibility
        patch_edtlib_for_pyyaml(ncs_dir)

    print(f"nRF Connect SDK workspace ready: {ncs_dir}")
    return 0


def patch_edtlib_for_pyyaml(ncs_dir: Path) -> None:
    """Patch Zephyr's edtlib.py to work with PyYAML 6.0+"""
    import re

    edtlib_file = ncs_dir / "zephyr" / "scripts" / "dts" / "python-devicetree" / "src" / "devicetree" / "edtlib.py"
    if not edtlib_file.exists():
        return

    try:
        content = edtlib_file.read_text(encoding="utf-8")

        # Check if already patched
        if "yaml.add_constructor" in content:
            return

        # Replace _BindingLoader.add_constructor with yaml.add_constructor
        # This fixes PyYAML 6.0+ compatibility
        content = re.sub(
            r'_BindingLoader\.add_constructor\("!include",\s*_binding_include\)',
            'yaml.add_constructor("!include", _binding_include, Loader=_BindingLoader)',
            content
        )

        edtlib_file.write_text(content, encoding="utf-8")
        print("Patched edtlib.py for PyYAML 6.0+ compatibility")
    except Exception:
        pass  # Don't fail if patching doesn't work


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # pragma: no cover - helper script
        print(str(exc), file=sys.stderr)
        raise
