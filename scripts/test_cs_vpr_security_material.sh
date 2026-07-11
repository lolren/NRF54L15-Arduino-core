#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
impl_rel="hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation"
impl_src="${repo_root}/${impl_rel}"
fixture_root="${impl_src}/extras/tests/channel_sounding"

fqbn="${CS_FQBN:-nrf54l15clean:nrf54l15clean:xiao_nrf54l15}"
board_port="${CS_BOARD_PORT:-/dev/ttyACM1}"
board_uid="${CS_BOARD_UID:-761FDE87}"
capture_seconds="${CS_CAPTURE_SECONDS:-12}"
sync_installed="${CS_SYNC_INSTALLED:-1}"
regenerate_vpr="${CS_REGENERATE_VPR:-0}"

installed_base="${CS_INSTALLED_BASE:-${HOME}/.arduino15/packages/nrf54l15clean/hardware/nrf54l15clean}"
if [[ -z "${CS_INSTALLED_VERSION:-}" ]]; then
  installed_version="$(find "${installed_base}" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort -V | tail -n 1)"
else
  installed_version="${CS_INSTALLED_VERSION}"
fi
installed_impl="${installed_base}/${installed_version}/libraries/Nrf54L15-Clean-Implementation"
nrf_ocd="${installed_base}/${installed_version}/tools/nrf_ocd"

sketch="${fixture_root}/BleChannelSoundingVprInvalidParams"

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
  mkdir -p "${installed_impl}/extras/tests/channel_sounding"
  rsync -a "${sketch}" "${installed_impl}/extras/tests/channel_sounding/"
fi

arduino-cli compile --upload \
  -p "${board_port}" \
  --fqbn "${fqbn}" \
  "${sketch}" \
  --build-path /tmp/cs_vpr_security_material_regression

tmpdir="$(mktemp -d)"
board_log="${tmpdir}/board.log"

stty -F "${board_port}" 115200 raw -echo -hupcl 2>/dev/null || true
timeout "${capture_seconds}s" cat "${board_port}" >"${board_log}" &
cat_pid=$!

sleep 0.5
"${nrf_ocd}" -t nrf54l15 -u "${board_uid}" reset >/dev/null
wait "${cat_pid}" || true

echo "logs=${tmpdir}"

if ! grep -q "cs_vpr_invalid_params=PASS" "${board_log}"; then
  echo "VPR invalid-params probe did not pass" >&2
  sed -n '1,220p' "${board_log}" >&2
  exit 1
fi

if ! grep -Eq "cs_vpr_security_material=PASS .*create=0 .*pre_flags=0x0 .*pre_params=C .*sec_status=0 .*post_flags=0x7 .*post_conn=0x41 .*post_cfg=2 .*post_nonce=0x[1-9A-Fa-f][0-9A-Fa-f]* .*post_token=0x[1-9A-Fa-f][0-9A-Fa-f]* .*post_ctr=[1-9][0-9]* .*post_params=0 .*enable=0" "${board_log}"; then
  echo "VPR security material proof did not pass" >&2
  sed -n '1,260p' "${board_log}" >&2
  exit 1
fi

grep "cs_vpr_invalid_params=PASS" "${board_log}" | tail -n 1
grep "cs_vpr_security_material=PASS" "${board_log}" | tail -n 1
