#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
platform_rel="hardware/nrf54l15clean/nrf54l15clean"
platform_root="${repo_root}/${platform_rel}"
examples_root="${platform_root}/libraries/Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding"

default_initiator_port="/dev/serial/by-id/usb-Seeed_Studio_Seeed_Studio_XIAO_nrf54_CMSIS-DAP_761FDE87-if02"
default_reflector_port="/dev/serial/by-id/usb-Seeed_Studio_Seeed_Studio_XIAO_nRF54LM20A_CMSIS-DAP_3377B9D6-if02"

initiator_fqbn="${CS_INITIATOR_FQBN:-localnrf54:nrf54l15clean:xiao_nrf54l15:clean_ble=on,cpu_freq=128m}"
reflector_fqbn="${CS_REFLECTOR_FQBN:-localnrf54:nrf54l15clean:xiao_nrf54lm20b:clean_ble=on,cpu_freq=128m}"
initiator_target="${CS_INITIATOR_TARGET:-nrf54l}"
reflector_target="${CS_REFLECTOR_TARGET:-nrf54lm20a}"
initiator_uid="${CS_INITIATOR_UID:-761FDE87}"
reflector_uid="${CS_REFLECTOR_UID:-3377B9D6}"
initiator_port="${CS_INITIATOR_PORT:-${default_initiator_port}}"
reflector_port="${CS_REFLECTOR_PORT:-${default_reflector_port}}"

capture_seconds="${CS_CAPTURE_SECONDS:-35}"
negative_seconds="${CS_NEGATIVE_CAPTURE_SECONDS:-12}"
recovery_seconds="${CS_RECOVERY_CAPTURE_SECONDS:-35}"
minimum_results="${CS_MIN_RESULTS:-3}"
run_negative="${CS_RUN_SILENT_PEER_TEST:-1}"
build_root="${CS_BUILD_ROOT:-${TMPDIR:-/tmp}/cs_controller_pair_build}"
log_dir="${CS_LOG_DIR:-}"
pyocd_bin="${CS_PYOCD:-pyocd}"
arduino_cli_config="${CS_ARDUINO_CLI_CONFIG:-}"
compiler_path="${CS_COMPILER_PATH:-}"

initiator_sketch="${examples_root}/BleChannelSoundingInitiator"
reflector_sketch="${examples_root}/BleChannelSoundingReflector"
initiator_build="${build_root}/initiator"
reflector_build="${build_root}/reflector"
initiator_hex="${initiator_build}/BleChannelSoundingInitiator.ino.hex"
reflector_hex="${reflector_build}/BleChannelSoundingReflector.ino.hex"
initiator_bin="${initiator_build}/BleChannelSoundingInitiator.ino.bin"
reflector_bin="${reflector_build}/BleChannelSoundingReflector.ino.bin"

capture_pids=()
restore_pair=0

die() {
  echo "error: $*" >&2
  exit 1
}

usage() {
  cat <<'USAGE'
Usage: scripts/test_cs_controller_pair.sh [--skip-negative]

Builds the source-tree Bluetooth LE Channel Sounding Test examples at 128 MHz,
chip-erases and flashes both probes, verifies the vector words, then requires
repeated HCI 0x31/0x32 reassembly, CRC-matched peer transfers, and finite PBR
ranges. The silent-peer negative and recovery phases run by default.

Set CS_INITIATOR_* and CS_REFLECTOR_* to swap boards or use another pair.
USAGE
}

case "${1:-}" in
  "") ;;
  --skip-negative) run_negative=0 ;;
  -h|--help) usage; exit 0 ;;
  *) usage >&2; exit 2 ;;
esac

case "${run_negative,,}" in
  1|true|yes) run_negative=1 ;;
  0|false|no) run_negative=0 ;;
  *) die "CS_RUN_SILENT_PEER_TEST must be 0 or 1" ;;
esac

[[ "${capture_seconds}" =~ ^[1-9][0-9]*$ ]] || die "CS_CAPTURE_SECONDS must be a positive integer"
[[ "${negative_seconds}" =~ ^[1-9][0-9]*$ ]] || die "CS_NEGATIVE_CAPTURE_SECONDS must be a positive integer"
[[ "${recovery_seconds}" =~ ^[1-9][0-9]*$ ]] || die "CS_RECOVERY_CAPTURE_SECONDS must be a positive integer"
[[ "${minimum_results}" =~ ^[1-9][0-9]*$ ]] || die "CS_MIN_RESULTS must be a positive integer"

cleanup() {
  local pid
  for pid in "${capture_pids[@]}"; do
    kill "${pid}" 2>/dev/null || true
    wait "${pid}" 2>/dev/null || true
  done
  capture_pids=()
  if [[ "${restore_pair}" == 1 ]]; then
    reset_probe "${reflector_target}" "${reflector_uid}" >/dev/null 2>&1 || true
    sleep 0.4
    reset_probe "${initiator_target}" "${initiator_uid}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

reset_probe() {
  "${pyocd_bin}" commander --no-config -W -t "$1" -u "$2" -c reset
}

halt_probe() {
  "${pyocd_bin}" commander --no-config -W -t "$1" -u "$2" -c halt
}

require_port() {
  [[ -e "$1" ]] || die "serial port does not exist: $1"
  [[ -c "$(readlink -f "$1")" ]] || die "not a character device: $1"
}

register_source_core() {
  if [[ -n "${arduino_cli_config}" ]]; then
    [[ -f "${arduino_cli_config}" ]] || die "Arduino CLI config not found: ${arduino_cli_config}"
    arduino-cli board details --config-file "${arduino_cli_config}" \
      --fqbn "localnrf54:nrf54l15clean:xiao_nrf54l15" >/dev/null
    return
  fi
  local hardware_parent="${HOME}/Arduino/hardware/localnrf54"
  local link="${hardware_parent}/nrf54l15clean"
  mkdir -p "${hardware_parent}"
  if [[ -e "${link}" && ! -L "${link}" &&
        "$(readlink -f "${link}")" != "$(readlink -f "${platform_root}")" ]]; then
    die "local core path exists and is not the source tree: ${link}"
  fi
  ln -sfn "${platform_root}" "${link}"
  arduino-cli board details \
    --fqbn "localnrf54:nrf54l15clean:xiao_nrf54l15" >/dev/null
}

build_role() {
  local role="$1"
  local fqbn="$2"
  local build_path="$3"
  local sketch="$4"
  echo "build_role=${role} fqbn=${fqbn}"
  rm -rf "${build_path}"
  local -a cmd=(arduino-cli compile --warnings all --fqbn "${fqbn}"
    --build-path "${build_path}")
  if [[ -n "${arduino_cli_config}" ]]; then
    cmd+=(--config-file "${arduino_cli_config}")
  fi
  if [[ -n "${compiler_path}" ]]; then
    [[ -x "${compiler_path}/arm-none-eabi-g++" ]] || \
      die "compiler path is invalid: ${compiler_path}"
    cmd+=(--build-property "compiler.path=${compiler_path}/")
  fi
  cmd+=(--build-property \
    "recipe.hooks.savehex.postsavehex.1.pattern=/usr/bin/true")
  cmd+=("${sketch}")
  "${cmd[@]}"
}

vector_words_from_bin() {
  od -An -N8 -tx4 "$1" | tr '[:lower:]' '[:upper:]' | xargs
}

vector_words_from_probe() {
  local output
  local line
  local words
  output="$("${pyocd_bin}" commander --no-config -W -t "$1" -u "$2" \
    -c 'read32 0x00000000 8')"
  line="$(grep -E '00000000|0x00000000' <<<"${output}" | tail -n 1)"
  words="$(grep -Eo '(0x)?[0-9A-Fa-f]{8}' <<<"${line}" | \
    sed -E 's/^0x//' | tail -n +2 | head -n 2 | \
    tr '[:lower:]' '[:upper:]' | xargs)"
  [[ -n "${words}" ]] || die "could not parse vector readback for UID $2"
  printf '%s\n' "${words}"
}

flash_and_verify() {
  local role="$1"
  local target="$2"
  local uid="$3"
  local hex="$4"
  local bin="$5"
  local expected
  local actual
  local attempt
  [[ -f "${hex}" && -f "${bin}" ]] || die "missing ${role} build artifacts"
  expected="$(vector_words_from_bin "${bin}")"
  for attempt in 1 2; do
    echo "flash_role=${role} erase=chip attempt=${attempt} uid=${uid}"
    "${pyocd_bin}" load --no-config -W -e chip -t "${target}" -u "${uid}" \
      --format hex "${hex}"
    actual="$(vector_words_from_probe "${target}" "${uid}")"
    if [[ "${actual}" == "${expected}" ]]; then
      echo "vector_verify=${role}:PASS words=${actual}"
      return 0
    fi
    echo "vector_verify=${role}:RETRY expected=${expected} actual=${actual}" >&2
  done
  die "${role} vector readback mismatch after chip erase"
}

capture_pair() {
  local initiator_log="$1"
  local reflector_log="$2"
  local seconds="$3"
  : >"${initiator_log}"
  : >"${reflector_log}"
  stty -F "${initiator_port}" 115200 raw -echo -hupcl
  stty -F "${reflector_port}" 115200 raw -echo -hupcl
  timeout 0.2s cat "${initiator_port}" >/dev/null 2>&1 || true
  timeout 0.2s cat "${reflector_port}" >/dev/null 2>&1 || true
  timeout "${seconds}s" cat "${initiator_port}" >"${initiator_log}" &
  capture_pids+=("$!")
  timeout "${seconds}s" cat "${reflector_port}" >"${reflector_log}" &
  capture_pids+=("$!")
  sleep 0.5
  reset_probe "${reflector_target}" "${reflector_uid}" >/dev/null
  sleep 0.4
  reset_probe "${initiator_target}" "${initiator_uid}" >/dev/null
  wait "${capture_pids[0]}" || true
  wait "${capture_pids[1]}" || true
  capture_pids=()
}

capture_initiator_only() {
  local log="$1"
  local seconds="$2"
  : >"${log}"
  stty -F "${initiator_port}" 115200 raw -echo -hupcl
  timeout 0.2s cat "${initiator_port}" >/dev/null 2>&1 || true
  timeout "${seconds}s" cat "${initiator_port}" >"${log}" &
  capture_pids+=("$!")
  sleep 0.5
  reset_probe "${initiator_target}" "${initiator_uid}" >/dev/null
  wait "${capture_pids[0]}" || true
  capture_pids=()
}

count_valid_initiator_results() {
  awk '
    /cs_result role=initiator result=PASS/ {
      pbr = ""; distance = ""; used = 0; cont = 0; dropped = 1;
      for (i = 1; i <= NF; ++i) {
        gsub(/\r/, "", $i);
        split($i, field, "=");
        if (field[1] == "pbr_m") pbr = field[2];
        if (field[1] == "distance_m") distance = field[2];
        if (field[1] == "used_channels") used = field[2] + 0;
        if (field[1] == "hci_continue") cont = field[2] + 0;
        if (field[1] == "dropped") dropped = field[2] + 0;
      }
      number = "^[-+]?([0-9]+([.][0-9]*)?|[.][0-9]+)$";
      if (pbr ~ number && pbr + 0 > 0 && distance ~ number &&
          distance + 0 > 0 && used >= 4 && cont >= 1 && dropped == 0) ++count;
    }
    END { print count + 0 }
  ' "$1"
}

count_valid_reflector_results() {
  awk '
    /cs_result role=reflector result=PASS/ {
      steps = 0; bytes = 0; acks = 0; cont = 0; dropped = 1; rejected = 1;
      for (i = 1; i <= NF; ++i) {
        gsub(/\r/, "", $i);
        split($i, field, "=");
        if (field[1] == "steps") steps = field[2] + 0;
        if (field[1] == "bytes") bytes = field[2] + 0;
        if (field[1] == "transfer_acks") acks = field[2] + 0;
        if (field[1] == "hci_continue") cont = field[2] + 0;
        if (field[1] == "dropped") dropped = field[2] + 0;
        if (field[1] == "rejected") rejected = field[2] + 0;
      }
      if (steps > 3 && bytes > 0 && acks > 0 && cont >= 1 &&
          dropped == 0 && rejected == 0) ++count;
    }
    END { print count + 0 }
  ' "$1"
}

extract_transfer_keys() {
  local role="$1"
  local log="$2"
  awk -v role="${role}" '
    $0 ~ ("cs_result role=" role " result=PASS") {
      id = ""; crc = ""; token = "";
      pbr = ""; distance = ""; used = 0; steps = 0; bytes = 0;
      acks = 0; cont = 0; dropped = 1; rejected = 1;
      for (i = 1; i <= NF; ++i) {
        gsub(/\r/, "", $i);
        split($i, field, "=");
        if (field[1] == "transfer_id") id = field[2];
        if (field[1] == "transfer_crc32") crc = field[2];
        if (field[1] == "session_token") token = field[2];
        if (field[1] == "pbr_m") pbr = field[2];
        if (field[1] == "distance_m") distance = field[2];
        if (field[1] == "used_channels") used = field[2] + 0;
        if (field[1] == "steps") steps = field[2] + 0;
        if (field[1] == "bytes") bytes = field[2] + 0;
        if (field[1] == "transfer_acks") acks = field[2] + 0;
        if (field[1] == "hci_continue") cont = field[2] + 0;
        if (field[1] == "dropped") dropped = field[2] + 0;
        if (field[1] == "rejected") rejected = field[2] + 0;
      }
      number = "^[-+]?([0-9]+([.][0-9]*)?|[.][0-9]+)$";
      valid = 0;
      if (role == "initiator") {
        valid = pbr ~ number && pbr + 0 > 0 && distance ~ number &&
                distance + 0 > 0 && used >= 4 && cont >= 1 && dropped == 0;
      } else if (role == "reflector") {
        valid = steps > 3 && bytes > 0 && acks > 0 && cont >= 1 &&
                dropped == 0 && rejected == 0;
      }
      if (valid && id != "" && crc != "" && token != "") {
        print id, crc, token;
      }
    }
  ' "${log}" | sort -u
}

slice_from_last_start() {
  local marker="$1"
  local source="$2"
  local destination="$3"
  local start_line
  start_line="$(grep -nF "${marker}" "${source}" | tail -n 1 | cut -d: -f1)"
  [[ -n "${start_line}" ]] || die "startup marker missing from ${source}"
  tail -n "+${start_line}" "${source}" >"${destination}"
}

validate_positive() {
  local phase="$1"
  local initiator_log="$2"
  local reflector_log="$3"
  local initiator_count
  local reflector_count
  local matched
  local initiator_session="${initiator_log}.session"
  local reflector_session="${reflector_log}.session"
  slice_from_last_start 'CoreBleChannelSoundingInitiator start' \
    "${initiator_log}" "${initiator_session}"
  slice_from_last_start 'CoreBleChannelSoundingReflector start' \
    "${reflector_log}" "${reflector_session}"
  grep -Fq 'role=initiator sounding=bluetooth_le_cs_test cpu_mhz=128' \
    "${initiator_session}" || die "${phase}: initiator startup marker missing"
  grep -Fq 'role=reflector sounding=bluetooth_le_cs_test cpu_mhz=128' \
    "${reflector_session}" || die "${phase}: reflector startup marker missing"
  ! grep -Eq 'fatal_stage=|dropped=[1-9][0-9]*|rejected=[1-9][0-9]*' \
    "${initiator_session}" "${reflector_session}" || die "${phase}: fatal/drop/reject marker"
  initiator_count="$(count_valid_initiator_results "${initiator_session}")"
  reflector_count="$(count_valid_reflector_results "${reflector_session}")"
  (( initiator_count >= minimum_results )) || \
    die "${phase}: only ${initiator_count} valid initiator ranges"
  (( reflector_count >= minimum_results )) || \
    die "${phase}: only ${reflector_count} valid reflector transfers"
  matched="$(comm -12 \
    <(extract_transfer_keys initiator "${initiator_session}") \
    <(extract_transfer_keys reflector "${reflector_session}") | wc -l)"
  (( matched >= minimum_results )) || \
    die "${phase}: only ${matched} transfer ID/CRC/session-token tuples matched"
  echo "${phase}=PASS initiator_ranges=${initiator_count} reflector_results=${reflector_count} matched_sessions=${matched}"
}

if [[ "${CS_FUNCTIONS_ONLY:-0}" == 1 ]]; then
  return 0 2>/dev/null || exit 0
fi

command -v arduino-cli >/dev/null || die "arduino-cli is required"
command -v "${pyocd_bin}" >/dev/null || die "pyOCD is required"
command -v timeout >/dev/null || die "GNU timeout is required"
command -v od >/dev/null || die "od is required"
[[ -f "${initiator_sketch}/BleChannelSoundingInitiator.ino" ]] || die "initiator sketch missing"
[[ -f "${reflector_sketch}/BleChannelSoundingReflector.ino" ]] || die "reflector sketch missing"
require_port "${initiator_port}"
require_port "${reflector_port}"
[[ "$(readlink -f "${initiator_port}")" != "$(readlink -f "${reflector_port}")" ]] || die "ports resolve to one device"
[[ "${initiator_uid}" != "${reflector_uid}" ]] || die "probe UIDs must differ"

probe_list="$("${pyocd_bin}" list)"
grep -Fq "${initiator_uid}" <<<"${probe_list}" || die "initiator probe not found"
grep -Fq "${reflector_uid}" <<<"${probe_list}" || die "reflector probe not found"

register_source_core
mkdir -p "${build_root}"
if [[ -z "${log_dir}" ]]; then
  log_dir="$(mktemp -d "${TMPDIR:-/tmp}/cs_controller_pair_logs.XXXXXX")"
fi
mkdir -p "${log_dir}"

build_role reflector "${reflector_fqbn}" "${reflector_build}" "${reflector_sketch}"
build_role initiator "${initiator_fqbn}" "${initiator_build}" "${initiator_sketch}"
flash_and_verify reflector "${reflector_target}" "${reflector_uid}" \
  "${reflector_hex}" "${reflector_bin}"
flash_and_verify initiator "${initiator_target}" "${initiator_uid}" \
  "${initiator_hex}" "${initiator_bin}"
restore_pair=1

positive_initiator_log="${log_dir}/positive-initiator.log"
positive_reflector_log="${log_dir}/positive-reflector.log"
capture_pair "${positive_initiator_log}" "${positive_reflector_log}" \
  "${capture_seconds}"
validate_positive positive "${positive_initiator_log}" "${positive_reflector_log}"

if [[ "${run_negative}" == 1 ]]; then
  halt_probe "${reflector_target}" "${reflector_uid}" >/dev/null
  negative_log="${log_dir}/silent-peer-initiator.log"
  capture_initiator_only "${negative_log}" "${negative_seconds}"
  negative_session="${negative_log}.session"
  slice_from_last_start 'CoreBleChannelSoundingInitiator start' \
    "${negative_log}" "${negative_session}"
  ! grep -Fq 'cs_result role=initiator result=PASS' "${negative_session}" || \
    die "silent-peer: initiator accepted a range"
  ! grep -Eq 'fatal_stage=|dropped=[1-9][0-9]*|rejected=[1-9][0-9]*' \
    "${negative_session}" || die "silent-peer: fatal/drop/reject marker"
  grep -Eq 'cs_result role=initiator result=RETRY .*reason=session_sync .*bytes=0([[:space:]]|$)' \
    "${negative_session}" || \
    die "silent-peer: expected zero-byte session-sync timeout was not observed"
  echo "silent_peer=PASS accepted_ranges=0"

  recovery_initiator_log="${log_dir}/recovery-initiator.log"
  recovery_reflector_log="${log_dir}/recovery-reflector.log"
  capture_pair "${recovery_initiator_log}" "${recovery_reflector_log}" \
    "${recovery_seconds}"
  validate_positive recovery "${recovery_initiator_log}" \
    "${recovery_reflector_log}"
fi

echo "channel_sounding_controller_pair=PASS logs=${log_dir}"
