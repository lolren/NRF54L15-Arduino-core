#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
local_site="${script_dir}/pydeps"
export PYTHONPATH="${local_site}${PYTHONPATH:+:${PYTHONPATH}}"

if command -v python3 >/dev/null 2>&1; then
    exec python3 -m west "$@"
fi

if command -v python >/dev/null 2>&1; then
    exec python -m west "$@"
fi

echo "Python 3 is required to run west." >&2
exit 1
