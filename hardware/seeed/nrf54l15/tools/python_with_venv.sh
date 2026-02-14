#!/usr/bin/env bash
#
# python_with_venv.sh - Python wrapper for nRF54L15 Arduino core
#
# This wrapper ensures PyYAML compatibility by running a patch check
# before executing any Python commands. The patch makes edtlib.py
# compatible with all PyYAML versions (5.x and 6.x+).
#

set -euo pipefail

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLATFORM_DIR="$(dirname "$SCRIPT_DIR")"
PYDEPS_DIR="${PLATFORM_DIR}/tools/pydeps"

# Ensure PyYAML compatibility patch is applied BEFORE running Python
PATCH_SCRIPT="${PLATFORM_DIR}/tools/ensure_patched.py"
if [[ -f "$PATCH_SCRIPT" ]]; then
    # Run patch synchronously and wait for it to complete
    python3 "$PATCH_SCRIPT" >/dev/null 2>&1
fi

# Set PYTHONPATH to include bundled pydeps (west, etc.)
export PYTHONPATH="${PYDEPS_DIR}:${PYTHONPATH:-}"

# Use system Python - the patch makes it work with any PyYAML version
exec python3 "$@"
