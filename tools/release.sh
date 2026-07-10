#!/usr/bin/env bash
# Canonical Board Manager release-artifact builder.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLATFORM="$ROOT/hardware/nrf54l15clean/nrf54l15clean/platform.txt"

if [[ ${1:-} == --* ]]; then
    VERSION="$(sed -n 's/^version=//p' "$PLATFORM" | head -n 1)"
else
    VERSION="${1:-$(sed -n 's/^version=//p' "$PLATFORM" | head -n 1)}"
    if [[ $# -gt 0 ]]; then
        shift
    fi
fi

if [[ ! $VERSION =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "ERROR: version must look like MAJOR.MINOR.PATCH" >&2
    exit 2
fi

ARGS=(
    --root "$ROOT"
    --version "$VERSION"
    --source-version nrf54l15clean
    --repo-url https://github.com/lolren/nrf54-arduino-core
    --release-base-url 'https://github.com/lolren/nrf54-arduino-core/releases/download/v{version}'
)
if [[ ${NRF54_REBUILD_HOSTTOOLS:-0} != 1 ]]; then
    ARGS+=(--reuse-existing-hosttools)
fi

python3 "$ROOT/scripts/build_release.py" "${ARGS[@]}" "$@"

MANIFEST="$ROOT/dist/release-manifest.json"
ARCHIVE="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["platform"]["archivePath"])' "$MANIFEST")"

for NAME in \
    package_nrf54l15clean_index.json \
    package_nrf54l15clean_stable_index.json \
    package_nrf54l15clean_archive_index.json; do
    python3 "$ROOT/scripts/verify_package_index.py" \
        --index "$ROOT/dist/$NAME" \
        --archive "$ARCHIVE" \
        --version "$VERSION"
    cp "$ROOT/dist/$NAME" "$ROOT/$NAME"
done

python3 "$ROOT/scripts/verify_release_archive.py" --archive "$ARCHIVE"

echo
echo "Release artifacts are ready in $ROOT/dist"
echo "Platform archive: $ARCHIVE"
echo "Review and commit the version/index changes before publishing v$VERSION."
