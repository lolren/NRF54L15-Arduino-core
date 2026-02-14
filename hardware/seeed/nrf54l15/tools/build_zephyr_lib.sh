#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if command -v python3 >/dev/null 2>&1; then
    exec python3 "${SCRIPT_DIR}/build_zephyr_lib.py" "$@"
fi

if command -v python >/dev/null 2>&1; then
    exec python "${SCRIPT_DIR}/build_zephyr_lib.py" "$@"
fi

echo "Python 3 is required to run build_zephyr_lib.py" >&2
exit 1

