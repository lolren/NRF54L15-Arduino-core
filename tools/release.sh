#!/bin/bash
# Release script for nRF54L15 Clean Arduino Core
# Usage: ./tools/release.sh [version]
#   If no version given, reads from platform.txt

set -euo pipefail
# Allow upload failures (network issues, timeouts on large files)
# The core archive is what matters — tools can be uploaded later.

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

# ── 1. Determine version ──────────────────────────────────────
PLATFORM_TXT="hardware/nrf54l15clean/nrf54l15clean/platform.txt"
if [ -n "${1:-}" ]; then
    VERSION="$1"
    # Update platform.txt
    sed -i "s/^version=.*/version=$VERSION/" "$PLATFORM_TXT"
else
    VERSION=$(grep "^version=" "$PLATFORM_TXT" | cut -d= -f2)
fi
echo "=== Release v$VERSION ==="

# ── 2. Check clean state ───────────────────────────────────────
if ! git diff --quiet || ! git diff --cached --quiet; then
    echo "ERROR: Uncommitted changes. Commit or stash first."
    exit 1
fi

# ── 3. Build core archive ──────────────────────────────────────
ARCHIVE="nrf54l15clean-${VERSION}.tar.bz2"
git archive --format=tar \
    --prefix="nrf54l15clean/" \
    HEAD:hardware/nrf54l15clean/nrf54l15clean/ | \
    bzip2 > "/tmp/$ARCHIVE"

CHECKSUM=$(sha256sum "/tmp/$ARCHIVE" | awk '{print $1}')
SIZE=$(stat -c%s "/tmp/$ARCHIVE")
echo "Core archive: $SIZE bytes, SHA-256:$CHECKSUM"

# ── 4. Verify archive (no symlinks, correct structure) ──────────
SYMLINKS=$(tar tjf "/tmp/$ARCHIVE" | grep -c "^l" || true)
if [ "$SYMLINKS" -gt 0 ]; then
    echo "ERROR: Archive contains $SYMLINKS symlinks! Windows will fail."
    exit 1
fi
# Check single root directory
ROOT_DIRS=$(tar tjf "/tmp/$ARCHIVE" | head -1 | cut -d/ -f1 | sort -u | wc -l)
if [ "$ROOT_DIRS" -ne 1 ]; then
    echo "ERROR: Archive must have a single root directory"
    exit 1
fi
echo "Archive verified: no symlinks, single root dir ✅"

# ── 5. Update package index ─────────────────────────────────────
HOST_TOOLS_VERSION="1.1.4"
INDEX="package_nrf54l15clean_index.json"

export VERSION CHECKSUM SIZE HOST_TOOLS_VERSION INDEX
python3 << 'PYEOF'
import json, os

VERSION = os.environ["VERSION"]
CHECKSUM = os.environ["CHECKSUM"]
SIZE = os.environ["SIZE"]
HOST_TOOLS_VERSION = os.environ["HOST_TOOLS_VERSION"]
INDEX = os.environ["INDEX"]

with open(INDEX) as f:
    data = json.load(f)

entry = {
    "name": "nRF54L15 Boards",
    "architecture": "nrf54l15clean",
    "version": VERSION,
    "category": "Contributed",
    "url": f"https://github.com/lolren/nrf54-arduino-core/releases/download/v{VERSION}/nrf54l15clean-{VERSION}.tar.bz2",
    "archiveFileName": f"nrf54l15clean-{VERSION}.tar.bz2",
    "checksum": f"SHA-256:{CHECKSUM}",
    "size": SIZE,
    "help": {"online": "https://github.com/lolren/nrf54-arduino-core"},
    "boards": [
        {"name": "XIAO nRF54L15"},
        {"name": "XIAO nRF54LM20B"}
    ],
    "toolsDependencies": [
        {"packager": "arduino", "name": "arm-none-eabi-gcc", "version": "7-2017q4"},
        {"packager": "arduino", "name": "openocd", "version": "0.11.0-arduino2"},
        {"packager": "nrf54l15clean", "name": "nrf54l15hosttools", "version": HOST_TOOLS_VERSION}
    ]
}

for pkg in data["packages"]:
    if pkg["name"] == "nrf54l15clean":
        pkg["platforms"] = [p for p in pkg["platforms"] if p["version"] != VERSION]
        pkg["platforms"].insert(0, entry)
        break

with open(INDEX, "w") as f:
    json.dump(data, f, indent=2)
print(f"Package index updated: v{VERSION}")
PYEOF

# ── 6. Commit & push ────────────────────────────────────────────
git add "$PLATFORM_TXT" "$INDEX"
git commit -m "release: v$VERSION"
git push origin main

git tag "v$VERSION"
git push origin "v$VERSION"

# ── 7. Create GitHub release ────────────────────────────────────
gh release create "v$VERSION" "/tmp/$ARCHIVE" \
    --title "v$VERSION" \
    --notes "Release v$VERSION"

echo ""
echo "✅ v$VERSION released!"
echo "   Archive: /tmp/$ARCHIVE"
echo "   Install: arduino-cli core install nrf54l15clean:nrf54l15clean@$VERSION"
echo ""
echo "   Host tools are served from the v0.9.53 release — no need to re-upload."
