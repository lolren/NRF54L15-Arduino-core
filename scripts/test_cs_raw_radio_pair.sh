#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
impl_rel="hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation"
impl_src="${repo_root}/${impl_rel}"

fqbn="${CS_FQBN:-nrf54l15clean:nrf54l15clean:xiao_nrf54l15}"
initiator_port="${CS_INITIATOR_PORT:-/dev/ttyACM1}"
reflector_port="${CS_REFLECTOR_PORT:-/dev/ttyACM2}"
initiator_uid="${CS_INITIATOR_UID:-761FDE87}"
reflector_uid="${CS_REFLECTOR_UID:-E91217E8}"
capture_seconds="${CS_CAPTURE_SECONDS:-20}"
sync_installed="${CS_SYNC_INSTALLED:-1}"
min_reflector_replies="${CS_MIN_REFLECTOR_REPLIES:-50}"

installed_base="${CS_INSTALLED_BASE:-${HOME}/.arduino15/packages/nrf54l15clean/hardware/nrf54l15clean}"
if [[ -z "${CS_INSTALLED_VERSION:-}" ]]; then
  installed_version="$(find "${installed_base}" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort -V | tail -n 1)"
else
  installed_version="${CS_INSTALLED_VERSION}"
fi
installed_impl="${installed_base}/${installed_version}/libraries/Nrf54L15-Clean-Implementation"
nrf_ocd="${installed_base}/${installed_version}/tools/nrf_ocd"

initiator_sketch="${impl_src}/examples/BLE/ChannelSounding/BleChannelSoundingInitiator"
reflector_sketch="${impl_src}/examples/BLE/ChannelSounding/BleChannelSoundingReflector"

if [[ ! -x "${nrf_ocd}" ]]; then
  echo "nrf_ocd not found or not executable: ${nrf_ocd}" >&2
  exit 2
fi

if [[ "${sync_installed}" != "0" ]]; then
  rsync -a "${impl_src}/src/" "${installed_impl}/src/"
  rsync -a "${initiator_sketch}" \
           "${reflector_sketch}" \
           "${installed_impl}/examples/BLE/ChannelSounding/"
fi

arduino-cli compile --upload \
  -p "${reflector_port}" \
  --fqbn "${fqbn}" \
  "${reflector_sketch}" \
  --build-path /tmp/cs_raw_reflector_regression

arduino-cli compile --upload \
  -p "${initiator_port}" \
  --fqbn "${fqbn}" \
  "${initiator_sketch}" \
  --build-path /tmp/cs_raw_initiator_regression

tmpdir="$(mktemp -d)"
initiator_log="${tmpdir}/initiator.log"
reflector_log="${tmpdir}/reflector.log"

stty -F "${initiator_port}" 115200 raw -echo -hupcl || true
stty -F "${reflector_port}" 115200 raw -echo -hupcl || true

timeout "${capture_seconds}s" cat "${initiator_port}" >"${initiator_log}" &
initiator_pid=$!
timeout "${capture_seconds}s" cat "${reflector_port}" >"${reflector_log}" &
reflector_pid=$!

sleep 0.5
"${nrf_ocd}" -t nrf54l15 -u "${reflector_uid}" reset >/dev/null
"${nrf_ocd}" -t nrf54l15 -u "${initiator_uid}" reset >/dev/null

wait "${initiator_pid}" || true
wait "${reflector_pid}" || true

echo "logs=${tmpdir}"

if ! grep -q "raw_cs_init=ok" "${initiator_log}"; then
  echo "Raw CS initiator did not initialize" >&2
  sed -n '1,180p' "${initiator_log}" >&2
  exit 1
fi

if ! grep -Eq "valid_channels=([1-9]|[1-9][0-9])" "${initiator_log}"; then
  echo "Raw CS initiator did not report any valid channels" >&2
  sed -n '1,220p' "${initiator_log}" >&2
  exit 1
fi

if ! grep -q "dfe_zero=0" "${initiator_log}"; then
  echo "Raw CS initiator did not capture non-zero DFE data" >&2
  sed -n '1,220p' "${initiator_log}" >&2
  exit 1
fi

if ! grep -q "std_est=1" "${initiator_log}"; then
  echo "Raw CS measurements did not round-trip through Mode 2 subevent results" >&2
  sed -n '1,220p' "${initiator_log}" >&2
  exit 1
fi

if ! grep -q "host_est=1" "${initiator_log}"; then
  echo "Raw CS measurements did not reach the controller host result ingress path" >&2
  sed -n '1,220p' "${initiator_log}" >&2
  exit 1
fi

last_replies="$(
  awk '
    {
      for (i = 1; i <= NF; ++i) {
        if ($i ~ /^replies=/) {
          split($i, a, "=");
          value = a[2] + 0;
        }
      }
    }
    END { print value + 0 }
  ' "${reflector_log}"
)"

if [[ "${last_replies}" -lt "${min_reflector_replies}" ]]; then
  echo "Raw CS reflector replies too low: ${last_replies}" >&2
  sed -n '1,180p' "${reflector_log}" >&2
  exit 1
fi

last_initiator="$(
  grep "host_est=1" "${initiator_log}" | tail -n 1 || \
    grep "std_est=1" "${initiator_log}" | tail -n 1 || \
    grep "valid_channels=" "${initiator_log}" | tail -n 1 || true
)"
last_reflector="$(
  grep "replies=" "${reflector_log}" | tail -n 1 || true
)"

echo "cs_raw_radio_pair=PASS replies=${last_replies}"
echo "${last_initiator}"
echo "${last_reflector}"
