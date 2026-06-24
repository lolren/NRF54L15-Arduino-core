#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
impl_rel="hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation"
impl_src="${repo_root}/${impl_rel}"

fqbn="${CS_FQBN:-nrf54l15clean:nrf54l15clean:xiao_nrf54l15}"
central_port="${CS_CENTRAL_PORT:-/dev/ttyACM1}"
peripheral_port="${CS_PERIPHERAL_PORT:-/dev/ttyACM2}"
central_uid="${CS_CENTRAL_UID:-761FDE87}"
peripheral_uid="${CS_PERIPHERAL_UID:-E91217E8}"
capture_seconds="${CS_CAPTURE_SECONDS:-40}"
sync_installed="${CS_SYNC_INSTALLED:-1}"

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

if [[ ! -x "${nrf_ocd}" ]]; then
  echo "nrf_ocd not found or not executable: ${nrf_ocd}" >&2
  exit 2
fi

if [[ "${sync_installed}" != "0" ]]; then
  rsync -a "${impl_src}/src/ble_channel_sounding.cpp" \
           "${impl_src}/src/ble_channel_sounding.h" \
           "${impl_src}/src/vpr_cs_controller_stub_firmware.h" \
           "${installed_impl}/src/"
  rsync -a "${impl_src}/examples/BLE/ChannelSounding/BleChannelSoundingLlControlPeripheral" \
           "${installed_impl}/examples/BLE/ChannelSounding/"
  rsync -a "${impl_src}/examples/BLE/ChannelSounding/BleChannelSoundingLlControlWorkflowCentral" \
           "${installed_impl}/examples/BLE/ChannelSounding/"
fi

arduino-cli compile --upload \
  -p "${peripheral_port}" \
  --fqbn "${fqbn}" \
  "${peripheral_sketch}" \
  --build-path /tmp/csll_peripheral_regression

arduino-cli compile --upload \
  -p "${central_port}" \
  --fqbn "${fqbn}" \
  "${central_sketch}" \
  --build-path /tmp/csll_workflow_central_regression

tmpdir="$(mktemp -d)"
central_log="${tmpdir}/central.log"
peripheral_log="${tmpdir}/peripheral.log"

stty -F "${central_port}" 115200 raw -echo -hupcl || true
stty -F "${peripheral_port}" 115200 raw -echo -hupcl || true

timeout "${capture_seconds}s" cat "${central_port}" >"${central_log}" &
central_pid=$!
timeout "${capture_seconds}s" cat "${peripheral_port}" >"${peripheral_log}" &
peripheral_pid=$!

sleep 0.5
"${nrf_ocd}" -t nrf54l15 -u "${peripheral_uid}" reset >/dev/null
"${nrf_ocd}" -t nrf54l15 -u "${central_uid}" reset >/dev/null

wait "${central_pid}" || true
wait "${peripheral_pid}" || true

echo "logs=${tmpdir}"

if ! grep -q "cs_ll_workflow_bridge=PASS" "${central_log}"; then
  echo "CS LL workflow bridge did not report PASS" >&2
  echo "central log:" >&2
  sed -n '1,220p' "${central_log}" >&2
  echo "peripheral log:" >&2
  sed -n '1,180p' "${peripheral_log}" >&2
  exit 1
fi

if ! grep -q "cs_ll_physical_followup=PASS" "${central_log}"; then
  echo "CS LL physical follow-up did not report PASS" >&2
  echo "central log:" >&2
  sed -n '1,260p' "${central_log}" >&2
  echo "peripheral log:" >&2
  sed -n '1,220p' "${peripheral_log}" >&2
  exit 1
fi

if ! grep -q "rx=0x3F" "${central_log}" ||
   ! grep -q "vpr_pdu=3" "${central_log}" ||
   ! grep -q "local=1 peer=1 proc=1 est=1" "${central_log}"; then
  echo "CS LL workflow bridge PASS was incomplete" >&2
  sed -n '1,220p' "${central_log}" >&2
  exit 1
fi

grep "cs_ll_workflow_bridge=PASS" "${central_log}" | tail -n 1
grep "cs_ll_physical_followup=PASS" "${central_log}" | tail -n 1
grep "queued CS_PROC_RSP\\|queued CS_START\\|queued CS_ABORT" "${peripheral_log}" || true
grep "physical reflector replies=" "${peripheral_log}" | tail -n 1 || true
