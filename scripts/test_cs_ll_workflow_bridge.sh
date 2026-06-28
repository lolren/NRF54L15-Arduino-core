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
  rsync -a "${impl_src}/src/" "${installed_impl}/src/"
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

if ! grep -Eq "cs_connected_physical .*ok=1.*status=0.*local_tone=1.*peer_tone=1" "${central_log}"; then
  echo "Connected-window raw CS did not complete with valid local and peer tones" >&2
  echo "central log:" >&2
  sed -n '1,260p' "${central_log}" >&2
  echo "peripheral log:" >&2
  sed -n '1,220p' "${peripheral_log}" >&2
  exit 1
fi

if ! grep -Eq "cs_connected_sweep=PASS .*valid_channels=([3-9]|[1-9][0-9]).*host_est=1.*ctrl_ing=1.*local_pkt_delta=[0-9]+.*peer_pkt_delta=[0-9]+.*peer_marker_delta=[0-9]+.*work_applied=1.*work_exec=1.*work_exec_mismatch=0x0.*work_tok=1.*work_rf=1.*work_rf_hw=1.*work_rf_prim=1.*work_rf_retune=1.*work_rf_rx=1.*work_rf_pkt=1.*work_rf_pkt_flags=0xFF.*work_rf_pkt_cte=0x4A.*work_rf_buf=1.*work_rf_timed=1.*work_rf_timed_status=0.*work_rf_timing=1.*work_tone_snap=1.*work_tone_snap_flags=0x37.*work_tone_timed=1.*work_tone_timed_status=0.*work_result_timed=1.*work_result_timed_local=1.*work_result_timed_peer=1.*work_result_timed_all=1.*work_result_timed_matches=([3-9]|[1-9][0-9]*)/([3-9]|[1-9][0-9]*)/([3-9]|[1-9][0-9]*).*work_timed_obs=([3-9]|[1-9][0-9]*):.*work_comp_est=1.*work_comp_mask=0x0.*work_proc=1.*work_ch=[1-9][0-9]*:.*host_cfg=1.*host_proc=1" "${central_log}"; then
  echo "Connected-window raw CS sweep did not reach the minimum valid channel count and host estimate" >&2
  echo "central log:" >&2
  sed -n '1,320p' "${central_log}" >&2
  echo "peripheral log:" >&2
  sed -n '1,260p' "${peripheral_log}" >&2
  exit 1
fi

if [[ -s "${peripheral_log}" ]] &&
   ! grep -Eq "connected_physical_reflector .*reply=1.*status=0" "${peripheral_log}"; then
  echo "Connected-window raw CS reflector did not send a valid report" >&2
  echo "central log:" >&2
  sed -n '1,260p' "${central_log}" >&2
  echo "peripheral log:" >&2
  sed -n '1,220p' "${peripheral_log}" >&2
  exit 1
fi

if ! grep -q "rx=0x3F" "${central_log}" ||
   ! grep -q "vpr_pdu=3" "${central_log}" ||
   ! grep -q "local=1 peer=1 proc=1 est=1" "${central_log}" ||
   ! grep -Eq "sched=1 .*sched_proc=1 .*sched_sub=[0-9]+/[1-9]" "${central_log}" ||
   ! grep -Eq "work=1 .*work_proc=1 .*work_sub=[0-9]+/[1-9].*work_steps=[1-9][0-9]*/[1-9].*work_ch=[1-9][0-9]*:" "${central_log}"; then
  echo "CS LL workflow bridge PASS was incomplete" >&2
  sed -n '1,220p' "${central_log}" >&2
  exit 1
fi

grep "cs_ll_workflow_bridge=PASS" "${central_log}" | tail -n 1
grep "cs_connected_sweep=PASS" "${central_log}" | tail -n 1
grep "cs_ll_physical_followup=PASS" "${central_log}" | tail -n 1
grep "queued CS_PROC_RSP\\|queued CS_START\\|queued CS_ABORT" "${peripheral_log}" || true
grep "physical reflector replies=" "${peripheral_log}" | tail -n 1 || true
