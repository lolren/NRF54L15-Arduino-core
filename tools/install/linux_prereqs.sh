#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
CORE_SOURCE_DIR="${REPO_ROOT}/hardware/nrf54l15/nrf54l15"

if [[ ! -f "${CORE_SOURCE_DIR}/platform.txt" ]]; then
  echo "Error: core source not found at ${CORE_SOURCE_DIR}" >&2
  exit 1
fi

log() {
  printf '[nrf54l15-setup] %s\n' "$*"
}

have_cmd() {
  command -v "$1" >/dev/null 2>&1
}

as_root() {
  if [[ "${EUID}" -eq 0 ]]; then
    "$@"
    return
  fi
  if have_cmd sudo; then
    sudo "$@"
    return
  fi
  echo "Error: need root privileges (sudo not found)." >&2
  exit 1
}

detect_pkg_manager() {
  local pm
  for pm in apt-get dnf yum pacman zypper apk; do
    if have_cmd "${pm}"; then
      printf '%s' "${pm}"
      return 0
    fi
  done
  return 1
}

install_dependencies() {
  local pm="$1"
  case "${pm}" in
    apt-get)
      as_root apt-get update
      as_root apt-get install -y \
        git python3 python3-pip python3-venv \
        curl wget tar bzip2 xz-utils unzip ca-certificates
      ;;
    dnf)
      as_root dnf install -y \
        git python3 python3-pip \
        curl wget tar bzip2 xz unzip ca-certificates
      ;;
    yum)
      as_root yum install -y \
        git python3 python3-pip \
        curl wget tar bzip2 xz unzip ca-certificates
      ;;
    pacman)
      as_root pacman -Sy --noconfirm \
        git python python-pip \
        curl wget tar bzip2 xz unzip ca-certificates
      ;;
    zypper)
      as_root zypper --non-interactive install \
        git python3 python3-pip \
        curl wget tar bzip2 xz unzip ca-certificates
      ;;
    apk)
      as_root apk add --no-cache \
        git python3 py3-pip \
        curl wget tar bzip2 xz unzip ca-certificates bash
      ;;
    *)
      echo "Error: unsupported package manager: ${pm}" >&2
      exit 1
      ;;
  esac
}

detect_arduino_ide() {
  if have_cmd arduino-ide; then
    arduino-ide
    return
  fi
  if have_cmd arduino; then
    arduino
    return
  fi

  local candidate
  for candidate in \
    "${HOME}/.local/bin/arduino-ide" \
    "${HOME}/Applications/arduino-ide" \
    "/usr/local/bin/arduino-ide" \
    "/opt/arduino-ide/arduino-ide"; do
    if [[ -x "${candidate}" ]]; then
      printf '%s\n' "${candidate}"
      return
    fi
  done
}

detect_sketchbook_dir() {
  python3 - <<'PY'
import json
from pathlib import Path

home = Path.home()

def emit_if_valid(path_str: str) -> bool:
    if not path_str:
        return False
    p = Path(path_str).expanduser()
    if p.is_absolute():
        print(str(p))
        return True
    return False

settings_json = home / ".arduinoIDE" / "settings.json"
if settings_json.is_file():
    try:
        data = json.loads(settings_json.read_text(encoding="utf-8"))
    except Exception:
        data = {}
    for key in ("sketchbook.path", "sketchbookPath"):
        value = data.get(key)
        if isinstance(value, str) and emit_if_valid(value):
            raise SystemExit(0)

prefs = home / ".arduino15" / "preferences.txt"
if prefs.is_file():
    for line in prefs.read_text(encoding="utf-8", errors="ignore").splitlines():
        if line.startswith("sketchbook.path="):
            if emit_if_valid(line.split("=", 1)[1].strip()):
                raise SystemExit(0)

for candidate in (home / "Arduino", home / "Documents" / "Arduino"):
    if candidate.exists():
        print(str(candidate))
        raise SystemExit(0)

print(str(home / "Arduino"))
PY
}

install_core_to_sketchbook() {
  local sketchbook="$1"
  local target_parent="${sketchbook}/hardware/nrf54l15"
  local target="${target_parent}/nrf54l15"

  mkdir -p "${target_parent}"

  if have_cmd rsync; then
    rsync -a --delete "${CORE_SOURCE_DIR}/" "${target}/"
  else
    rm -rf "${target}"
    cp -a "${CORE_SOURCE_DIR}" "${target_parent}/"
  fi

  log "Core installed to: ${target}"
}

main() {
  local pm
  pm="$(detect_pkg_manager || true)"
  if [[ -z "${pm}" ]]; then
    echo "Error: no supported package manager found (apt/dnf/yum/pacman/zypper/apk)." >&2
    exit 1
  fi

  log "Detected package manager: ${pm}"
  log "Installing dependencies (git, python3, pip, curl, wget, archive tools)..."
  install_dependencies "${pm}"

  local ide_path
  ide_path="$(detect_arduino_ide || true)"
  if [[ -n "${ide_path}" ]]; then
    log "Arduino IDE detected: ${ide_path}"
  else
    log "Arduino IDE executable not detected (this is optional for prerequisite setup)."
  fi

  local sketchbook
  sketchbook="$(detect_sketchbook_dir)"
  log "Detected sketchbook path: ${sketchbook}"

  local default_target="${sketchbook}/hardware/nrf54l15/nrf54l15"
  read -r -p "Install this core now to '${default_target}'? [y/N]: " answer
  case "${answer}" in
    y|Y|yes|YES)
      install_core_to_sketchbook "${sketchbook}"
      ;;
    *)
      log "Skipping core copy step."
      ;;
  esac

  log "Done."
}

main "$@"
