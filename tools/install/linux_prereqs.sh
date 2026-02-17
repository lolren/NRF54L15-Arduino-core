#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
CORE_SOURCE_DIR="${REPO_ROOT}/hardware/nrf54l15/nrf54l15"
SKIP_CORE_COPY=0
SKIP_BOOTSTRAP=0
SHOW_HELP=0
WARMUP_BUILD_RETRIES="${WARMUP_BUILD_RETRIES:-2}"

print_help() {
  cat <<'EOF'
nRF54L15 Linux setup helper

Usage:
  bash ./install_linux.sh [--skip-core-copy] [--skip-bootstrap]

Default behavior:
  1) Ensure required host dependencies are installed.
  2) Copy this core into Arduino15 packages hardware path.
  3) Download/bootstrap nRF Connect workspace and Zephyr SDK.
  4) Run a warmup Zephyr build so Arduino IDE compiles are sketch-focused.

Options:
  --skip-core-copy   Skip copying repo core into Arduino15 packages path.
  --skip-bootstrap   Skip NCS/SDK bootstrap and warmup build.
  --help             Show this help text.

Environment overrides:
  ARDUINO_DATA_DIR        Arduino data root (default: ~/.arduino15)
  ARDUINO_PACKAGE_VENDOR  Package vendor path (default: nrf54l15)
EOF
}

log() {
  printf '[nrf54l15-setup] %s\n' "$*"
}

die() {
  echo "Error: $*" >&2
  exit 1
}

have_cmd() {
  command -v "$1" >/dev/null 2>&1
}

parse_args() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --skip-core-copy)
        SKIP_CORE_COPY=1
        ;;
      --skip-bootstrap)
        SKIP_BOOTSTRAP=1
        ;;
      --help)
        SHOW_HELP=1
        ;;
      *)
        die "unknown argument: $1 (use --help)"
        ;;
    esac
    shift
  done
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
        curl wget tar bzip2 xz-utils unzip ca-certificates \
        cmake ninja-build device-tree-compiler rsync
      ;;
    dnf)
      as_root dnf install -y \
        git python3 python3-pip \
        curl wget tar bzip2 xz unzip ca-certificates \
        cmake ninja-build dtc rsync
      ;;
    yum)
      as_root yum install -y \
        git python3 python3-pip \
        curl wget tar bzip2 xz unzip ca-certificates \
        cmake ninja-build dtc rsync
      ;;
    pacman)
      as_root pacman -Sy --noconfirm \
        git python python-pip \
        curl wget tar bzip2 xz unzip ca-certificates \
        cmake ninja dtc rsync
      ;;
    zypper)
      as_root zypper --non-interactive install \
        git python3 python3-pip \
        curl wget tar bzip2 xz unzip ca-certificates \
        cmake ninja dtc rsync
      ;;
    apk)
      as_root apk add --no-cache \
        git python3 py3-pip \
        curl wget tar bzip2 xz unzip ca-certificates bash \
        cmake ninja dtc rsync
      ;;
    *)
      die "unsupported package manager: ${pm}"
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

detect_arduino_data_dir() {
  if [[ -n "${ARDUINO_DATA_DIR:-}" ]]; then
    printf '%s\n' "${ARDUINO_DATA_DIR}"
    return
  fi

  local candidate
  for candidate in \
    "${HOME}/.arduino15" \
    "${HOME}/.var/app/cc.arduino.IDE2/data/arduino15"; do
    if [[ -d "${candidate}" ]]; then
      printf '%s\n' "${candidate}"
      return
    fi
  done

  printf '%s\n' "${HOME}/.arduino15"
}

detect_core_version() {
  local platform_file="${CORE_SOURCE_DIR}/platform.txt"
  local version

  version="$(awk -F= '/^[[:space:]]*version[[:space:]]*=/{gsub(/^[[:space:]]+|[[:space:]]+$/, "", $2); print $2; exit}' "${platform_file}" || true)"
  if [[ -z "${version}" ]]; then
    version="dev"
  fi
  printf '%s\n' "${version}"
}

resolve_package_vendor() {
  local vendor="${ARDUINO_PACKAGE_VENDOR:-nrf54l15}"
  vendor="${vendor//[[:space:]]/}"
  if [[ -z "${vendor}" ]]; then
    vendor="nrf54l15"
  fi
  printf '%s\n' "${vendor}"
}

resolve_python() {
  if have_cmd python3; then
    printf '%s' "python3"
    return 0
  fi
  if have_cmd python; then
    if python - <<'PY' >/dev/null 2>&1
import sys
raise SystemExit(0 if sys.version_info.major == 3 else 1)
PY
    then
      printf '%s' "python"
      return 0
    fi
  fi
  return 1
}

install_core_to_arduino_packages() {
  local arduino_data_dir="$1"
  local package_vendor="$2"
  local core_version="$3"
  local target_parent="${arduino_data_dir}/packages/${package_vendor}/hardware/nrf54l15"
  local target="${target_parent}/${core_version}"

  mkdir -p "${arduino_data_dir}"
  mkdir -p "${target_parent}"

  if have_cmd rsync; then
    # Preserve heavy local caches between reruns.
    rsync -a --delete \
      --exclude 'tools/ncs/' \
      --exclude 'tools/zephyr-sdk/' \
      --exclude 'tools/.arduino-zephyr-build/' \
      --exclude 'tools/pydeps/' \
      --exclude 'tools/mingit/' \
      --exclude 'variants/xiao_nrf54l15/zephyr_lib/' \
      --exclude 'zephyr-sdk-*.7z' \
      --exclude 'zephyr-sdk-*.tar.xz' \
      --exclude 'zephyr-sdk-*.zip' \
      --exclude 'hosttools_*.tar.xz' \
      --exclude 'MinGit-*.zip' \
      "${CORE_SOURCE_DIR}/" "${target}/"
  else
    rm -rf "${target}"
    cp -a "${CORE_SOURCE_DIR}" "${target_parent}/"
  fi

  log "Core installed to: ${target}"
}

run_python_file() {
  local script_path="$1"
  shift

  [[ -f "${script_path}" ]] || die "Python script not found: ${script_path}"

  local python_cmd
  python_cmd="$(resolve_python || true)"
  [[ -n "${python_cmd}" ]] || die "Python 3 is required to run installer helper scripts."

  "${python_cmd}" "${script_path}" "$@"
}

prepare_zephyr() {
  local warmup_core="$1"
  local ncs_script="${warmup_core}/tools/get_nrf_connect.py"
  local sdk_script="${warmup_core}/tools/get_toolchain.py"
  local build_script="${warmup_core}/tools/build_zephyr_lib.py"

  [[ -f "${ncs_script}" ]] || die "missing ${ncs_script}"
  [[ -f "${sdk_script}" ]] || die "missing ${sdk_script}"
  [[ -f "${build_script}" ]] || die "missing ${build_script}"

  log "Bootstrapping nRF Connect workspace..."
  run_python_file "${ncs_script}"

  log "Bootstrapping Zephyr SDK toolchain..."
  run_python_file "${sdk_script}"

  local old_zephyr_base="__UNSET__"
  if [[ "${ZEPHYR_BASE+x}" == "x" ]]; then
    old_zephyr_base="${ZEPHYR_BASE}"
  fi
  unset ZEPHYR_BASE

  local attempt=1
  while (( attempt <= WARMUP_BUILD_RETRIES )); do
    log "Warmup build attempt ${attempt}/${WARMUP_BUILD_RETRIES} in '${warmup_core}'..."
    if run_python_file "${build_script}" --quiet; then
      if [[ "${old_zephyr_base}" == "__UNSET__" ]]; then
        unset ZEPHYR_BASE
      else
        export ZEPHYR_BASE="${old_zephyr_base}"
      fi
      log "Warmup build completed."
      return 0
    fi
    sleep 3
    attempt=$((attempt + 1))
  done

  if [[ "${old_zephyr_base}" == "__UNSET__" ]]; then
    unset ZEPHYR_BASE
  else
    export ZEPHYR_BASE="${old_zephyr_base}"
  fi
  return 1
}

main() {
  parse_args "$@"
  if [[ "${SHOW_HELP}" -eq 1 ]]; then
    print_help
    return 0
  fi

  [[ -f "${CORE_SOURCE_DIR}/platform.txt" ]] || die "core source not found at ${CORE_SOURCE_DIR}"

  log "Starting Linux setup helper"
  log "Core source expected at: ${CORE_SOURCE_DIR}"

  local pm
  pm="$(detect_pkg_manager || true)"
  if [[ -z "${pm}" ]]; then
    die "no supported package manager found (apt/dnf/yum/pacman/zypper/apk)"
  fi

  log "Step 1/7: Detected package manager: ${pm}"
  log "Step 2/7: Installing dependencies (git, python3, pip, cmake, ninja, dtc, archive tools)..."
  install_dependencies "${pm}"

  local ide_path
  ide_path="$(detect_arduino_ide || true)"
  if [[ -n "${ide_path}" ]]; then
    log "Step 3/7: Arduino IDE detected: ${ide_path}"
  else
    log "Step 3/7: Arduino IDE executable not detected (optional for setup)."
  fi

  local arduino_data_dir
  arduino_data_dir="$(detect_arduino_data_dir)"
  arduino_data_dir="${arduino_data_dir%/}"
  log "Step 4/7: Detected Arduino data path: ${arduino_data_dir}"

  local package_vendor
  package_vendor="$(resolve_package_vendor)"

  local core_version
  core_version="$(detect_core_version)"

  local default_target="${arduino_data_dir}/packages/${package_vendor}/hardware/nrf54l15/${core_version}"
  local warmup_core_dir
  if [[ "${SKIP_CORE_COPY}" -eq 1 ]]; then
    warmup_core_dir="${CORE_SOURCE_DIR}"
    log "Step 5/7: Skipping core copy (using repo path): ${warmup_core_dir}"
  else
    log "Step 5/7: Installing core into Arduino packages path (vendor=${package_vendor}, version=${core_version})..."
    install_core_to_arduino_packages "${arduino_data_dir}" "${package_vendor}" "${core_version}"
    warmup_core_dir="${default_target}"
  fi

  if [[ "${SKIP_BOOTSTRAP}" -eq 1 ]]; then
    log "Step 6/7: Skipping NCS/SDK bootstrap and warmup build (--skip-bootstrap)."
    log "Step 7/7: Done."
    return 0
  fi

  log "Step 6/7: Bootstrapping NCS/SDK and running warmup build..."
  if ! prepare_zephyr "${warmup_core_dir}"; then
    log "ERROR: SDK/bootstrap or warmup build failed."
    log "You can retry manually with:"
    log "  $(resolve_python || echo python3) \"${warmup_core_dir}/tools/get_nrf_connect.py\""
    log "  $(resolve_python || echo python3) \"${warmup_core_dir}/tools/get_toolchain.py\""
    log "  $(resolve_python || echo python3) \"${warmup_core_dir}/tools/build_zephyr_lib.py\" --quiet"
    exit 1
  fi

  log "Step 7/7: Done."
  log "Restart Arduino IDE, then select: Tools > Board > XIAO nRF54L15 (Zephyr-Based - NO BLE)"
}

main "$@"
