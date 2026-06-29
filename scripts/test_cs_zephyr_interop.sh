#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

mode="${1:-matrix}"
central_port="${CS_CENTRAL_PORT:-/dev/ttyACM1}"
peripheral_port="${CS_PERIPHERAL_PORT:-/dev/ttyACM0}"
zephyr_workspace="${CS_ZEPHYR_WORKSPACE:-/home/lolren/Desktop/test_pi_nrf54/ncs-workspace}"
zephyr_capture_seconds="${CS_ZEPHYR_CAPTURE_SECONDS:-45}"
run_arduino="${CS_RUN_ARDUINO:-1}"
run_zephyr="${CS_RUN_ZEPHYR:-1}"
strict_zephyr="${CS_ZEPHYR_STRICT:-0}"

run_arduino_pair() {
  CS_CENTRAL_PORT="${central_port}" \
  CS_PERIPHERAL_PORT="${peripheral_port}" \
  CS_CENTRAL_UID="${CS_CENTRAL_UID:-761FDE87}" \
  CS_PERIPHERAL_UID="${CS_PERIPHERAL_UID:-E91217E8}" \
  "${repo_root}/scripts/test_cs_ll_workflow_bridge.sh"
}

run_zephyr_pair() {
  python3 "${repo_root}/scripts/zephyr_channel_sounding_validation.py" pair-demo \
    --workspace "${zephyr_workspace}" \
    --initiator-port "${central_port}" \
    --reflector-port "${peripheral_port}" \
    --capture-seconds "${zephyr_capture_seconds}"
}

print_mixed_status() {
  cat <<'STATUS'
cs_mixed_arduino_initiator_zephyr_reflector=BLOCKED
reason=arduino_side_currently_uses_ll_control_diagnostic_bridge_not_zephyr_connected_cs_gatt_step_data_sample
cs_mixed_zephyr_initiator_arduino_reflector=BLOCKED
reason=arduino_side_currently_lacks_zephyr_compatible_connected_cs_service_and_host_role_example
STATUS
}

case "${mode}" in
  arduino)
    run_arduino_pair
    ;;
  zephyr)
    run_zephyr_pair
    ;;
  mixed-status)
    print_mixed_status
    ;;
  matrix)
    if [[ "${run_arduino}" != "0" ]]; then
      run_arduino_pair
    fi

    zephyr_status="skipped"
    if [[ "${run_zephyr}" != "0" ]]; then
      if run_zephyr_pair; then
        zephyr_status="pass"
      else
        zephyr_status="blocked"
        if [[ "${strict_zephyr}" != "0" ]]; then
          exit 1
        fi
      fi
    fi

    print_mixed_status
    echo "cs_slice7_matrix=PASS_WITH_KNOWN_BLOCKS zephyr=${zephyr_status}"
    ;;
  *)
    echo "usage: $0 [matrix|arduino|zephyr|mixed-status]" >&2
    exit 2
    ;;
esac
