#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
impl_rel="hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation"
impl_src="${repo_root}/${impl_rel}"

fqbn="${CS_FQBN:-nrf54l15clean:nrf54l15clean:xiao_nrf54l15}"
central_port="${CS_CENTRAL_PORT:-/dev/ttyACM1}"
peripheral_port="${CS_PERIPHERAL_PORT:-/dev/ttyACM0}"
central_uid="${CS_CENTRAL_UID:-761FDE87}"
peripheral_uid="${CS_PERIPHERAL_UID:-E91217E8}"
capture_seconds="${CS_CAPTURE_SECONDS:-30}"
sync_installed="${CS_SYNC_INSTALLED:-1}"
regenerate_vpr="${CS_REGENERATE_VPR:-1}"

installed_base="${CS_INSTALLED_BASE:-${HOME}/.arduino15/packages/nrf54l15clean/hardware/nrf54l15clean}"
if [[ -z "${CS_INSTALLED_VERSION:-}" ]]; then
  installed_version="$(find "${installed_base}" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort -V | tail -n 1)"
else
  installed_version="${CS_INSTALLED_VERSION}"
fi
installed_impl="${installed_base}/${installed_version}/libraries/Nrf54L15-Clean-Implementation"
nrf_ocd="${installed_base}/${installed_version}/tools/nrf_ocd"

peripheral_sketch="${impl_src}/examples/BLE/ChannelSounding/BleChannelSoundingLlControlPeripheral"
central_sketch="${impl_src}/examples/BLE/ChannelSounding/BleChannelSoundingLlControlWorkflowCentral"
central_tmp_root="$(mktemp -d)"
central_tmp_sketch="${central_tmp_root}/BleChannelSoundingLlControlWorkflowCentral"
mkdir -p "${central_tmp_sketch}"
{
  printf '#define CS_AUTO_MEASUREMENT_PROOF_ONLY 1\n'
  cat "${central_sketch}/BleChannelSoundingLlControlWorkflowCentral.ino"
} >"${central_tmp_sketch}/BleChannelSoundingLlControlWorkflowCentral.ino"

if [[ "${regenerate_vpr}" != "0" ]]; then
  python3 "${impl_src}/tools/generate_vpr_cs_transport_stub.py"
  python3 "${impl_src}/tools/generate_vpr_cs_controller_stub.py"
fi

if [[ ! -x "${nrf_ocd}" ]]; then
  echo "nrf_ocd not found or not executable: ${nrf_ocd}" >&2
  exit 2
fi

if [[ "${sync_installed}" != "0" ]]; then
  rsync -a "${impl_src}/src/" "${installed_impl}/src/"
  rsync -a "${impl_src}/examples/BLE/ChannelSounding/BleChannelSoundingLlControlPeripheral" \
           "${installed_impl}/examples/BLE/ChannelSounding/"
  rsync -a "${impl_src}/examples/BLE/ChannelSounding/BleChannelSoundingLlControlWorkflowCentral" \
           "${installed_impl}/examples/BLE/ChannelSounding/"
fi

rm -rf /tmp/cs_vpr_auto_peripheral_regression \
       /tmp/cs_vpr_auto_central_regression

arduino-cli compile --upload \
  -p "${peripheral_port}" \
  --fqbn "${fqbn}" \
  "${peripheral_sketch}" \
  --build-path /tmp/cs_vpr_auto_peripheral_regression

arduino-cli compile --upload \
  -p "${central_port}" \
  --fqbn "${fqbn}" \
  "${central_tmp_sketch}" \
  --build-path /tmp/cs_vpr_auto_central_regression

tmpdir="$(mktemp -d)"
central_log="${tmpdir}/central.log"
peripheral_log="${tmpdir}/peripheral.log"

stty -F "${central_port}" 115200 raw -echo -hupcl 2>/dev/null || true
stty -F "${peripheral_port}" 115200 raw -echo -hupcl 2>/dev/null || true

timeout "${capture_seconds}s" cat "${central_port}" >"${central_log}" &
central_pid=$!
timeout "${capture_seconds}s" cat "${peripheral_port}" >"${peripheral_log}" 2>"${peripheral_log}.err" &
peripheral_pid=$!

sleep 0.5
"${nrf_ocd}" -t nrf54l15 -u "${peripheral_uid}" reset >/dev/null
"${nrf_ocd}" -t nrf54l15 -u "${central_uid}" reset >/dev/null

wait "${central_pid}" || true
wait "${peripheral_pid}" || true

echo "logs=${tmpdir}"

if grep -q "cs_vpr_auto_measurement=FAIL" "${central_log}"; then
  echo "VPR auto measurement proof reported FAIL" >&2
  sed -n '1,260p' "${central_log}" >&2
  exit 1
fi

if ! grep -Eq "cs_vpr_auto_measurement=PASS .*work_flags=0x[89A-Fa-f][0-9A-Fa-f].*work_auto=1.*exec_flags=0x[1-9A-Fa-f][0-9A-Fa-f]*.*exec_snap=1.*exec2_flags=0x[1-9A-Fa-f][0-9A-Fa-f]*.*exec2_snap=1.*stable=1.*no_host_execute=1" "${central_log}"; then
  echo "VPR auto measurement proof did not report the expected 0x80/0x10 no-host-execute evidence" >&2
  echo "central log:" >&2
  sed -n '1,260p' "${central_log}" >&2
  echo "peripheral log:" >&2
  sed -n '1,200p' "${peripheral_log}" >&2
  exit 1
fi

grep "cs_vpr_auto_measurement=PASS" "${central_log}" | tail -n 1
grep "VPR inject op=0x34" "${central_log}" | tail -n 1 || true
