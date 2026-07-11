#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fqbn="${ARDUINO_FQBN:-nrf54l15clean:nrf54l15clean:xiao_nrf54l15}"
fixtures_root="${repo_root}/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/extras/tests/channel_sounding"
bluefruit_lib="${repo_root}/hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib"
hal_lib="${repo_root}/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation"

compile_example() {
  local sketch="$1"
  echo "compile=${sketch}"
  arduino-cli compile --clean --fqbn "${fqbn}" \
    --library "${bluefruit_lib}" \
    --library "${hal_lib}" \
    "${fixtures_root}/${sketch}/${sketch}.ino" >/tmp/cs_zephyr_bridge_${sketch}.log
  tail -n 4 "/tmp/cs_zephyr_bridge_${sketch}.log"
}

compile_example BleChannelSoundingZephyrCompatInitiator
compile_example BleChannelSoundingZephyrCompatReflector

python3 -m py_compile "${repo_root}/scripts/zephyr_channel_sounding_validation.py"

echo "cs_zephyr_bridge_compile=PASS fqbn=${fqbn}"
