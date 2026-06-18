#!/usr/bin/env bash
set -euo pipefail

# OpenOCD flash programming is not supported for nRF54L15/nRF54LM20A
# (no RRAM flash driver in OpenOCD 0.11/0.12).
# This script tries nrf_ocd as fallback, then exits with an error.

HEX_PATH="${4:-}"
NRF_OCD="$(dirname "$0")/nrf_ocd"
TARGET="nrf54l15"

# Auto-detect target from probe
if [ -x "$NRF_OCD" ]; then
    echo "OpenOCD flash not supported. Falling back to nrf_ocd..."
    exec "$NRF_OCD" load "$HEX_PATH"
fi

echo "ERROR: OpenOCD flash is not supported for nRF54L series." >&2
echo "Use pyOCD or nrf_ocd upload method." >&2
exit 1
