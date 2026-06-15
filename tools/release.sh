#!/bin/bash
# Release script for nRF54L15 Clean Arduino Core
# Usage: ./tools/release.sh [version]
#   If no version given, reads from platform.txt

set -euo pipefail
# Core releases upload only the board package archive. Host tools are resolved
# through package_index toolsDependencies from the permanent host-tools release.

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"
PACKAGE_TMP=""
ARCHIVE_LIST=""
ARCHIVE_TABLE=""

cleanup_release_tmp() {
    if [ -n "$PACKAGE_TMP" ]; then
        rm -rf "$PACKAGE_TMP"
    fi
    if [ -n "$ARCHIVE_LIST" ]; then
        rm -f "$ARCHIVE_LIST"
    fi
    if [ -n "$ARCHIVE_TABLE" ]; then
        rm -f "$ARCHIVE_TABLE"
    fi
}
trap cleanup_release_tmp EXIT

# ── 1. Check clean state ───────────────────────────────────────
if ! git diff --quiet || ! git diff --cached --quiet; then
    echo "ERROR: Uncommitted changes. Commit or stash first."
    exit 1
fi

# ── 2. Determine and set version ────────────────────────────────
PLATFORM_TXT="hardware/nrf54l15clean/nrf54l15clean/platform.txt"
CORE_VERSION_L15="hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/CoreVersionGenerated.h"
CORE_VERSION_LM20="hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/CoreVersionGenerated.h"
if [ -n "${1:-}" ]; then
    VERSION="$1"
    sed -i "s/^version=.*/version=$VERSION/" "$PLATFORM_TXT"
else
    VERSION=$(grep "^version=" "$PLATFORM_TXT" | cut -d= -f2)
fi

IFS=. read -r VERSION_MAJOR VERSION_MINOR VERSION_PATCH_EXTRA <<< "$VERSION"
VERSION_PATCH="${VERSION_PATCH_EXTRA%%[^0-9]*}"
if [ -z "${VERSION_MAJOR:-}" ] || [ -z "${VERSION_MINOR:-}" ] || [ -z "${VERSION_PATCH:-}" ]; then
    echo "ERROR: Version must look like MAJOR.MINOR.PATCH"
    exit 1
fi

for CORE_VERSION in "$CORE_VERSION_L15" "$CORE_VERSION_LM20"; do
    sed -i "s/^#define ARDUINO_NRF54L15_CLEAN_VERSION_MAJOR .*/#define ARDUINO_NRF54L15_CLEAN_VERSION_MAJOR $VERSION_MAJOR/" "$CORE_VERSION"
    sed -i "s/^#define ARDUINO_NRF54L15_CLEAN_VERSION_MINOR .*/#define ARDUINO_NRF54L15_CLEAN_VERSION_MINOR $VERSION_MINOR/" "$CORE_VERSION"
    sed -i "s/^#define ARDUINO_NRF54L15_CLEAN_VERSION_PATCH .*/#define ARDUINO_NRF54L15_CLEAN_VERSION_PATCH $VERSION_PATCH/" "$CORE_VERSION"
    sed -i "s/^#define ARDUINO_NRF54L15_CLEAN_VERSION_STRING .*/#define ARDUINO_NRF54L15_CLEAN_VERSION_STRING \"$VERSION\"/" "$CORE_VERSION"
done

git add "$PLATFORM_TXT" "$CORE_VERSION_L15" "$CORE_VERSION_LM20"
ARCHIVE_TREE=$(git write-tree)
echo "=== Release v$VERSION ==="

# ── 3. Build core archive ──────────────────────────────────────
ARCHIVE="nrf54l15clean-${VERSION}.tar.bz2"
PACKAGE_TMP=$(mktemp -d)
git archive --format=tar \
    --prefix="nrf54l15clean/" \
    "$ARCHIVE_TREE:hardware/nrf54l15clean/nrf54l15clean/" | \
    tar -C "$PACKAGE_TMP" -xf -

while IFS= read -r LINK_PATH; do
    TARGET=$(readlink "$LINK_PATH")
    TARGET_PATH="$(dirname "$LINK_PATH")/$TARGET"
    if [ ! -e "$TARGET_PATH" ]; then
        echo "ERROR: Broken symlink in package tree: $LINK_PATH -> $TARGET"
        exit 1
    fi
    rm "$LINK_PATH"
    cp -aL "$TARGET_PATH" "$LINK_PATH"
done < <(find "$PACKAGE_TMP/nrf54l15clean" -type l)

tar -C "$PACKAGE_TMP" -cf - "nrf54l15clean" | \
    bzip2 > "/tmp/$ARCHIVE"

CHECKSUM=$(sha256sum "/tmp/$ARCHIVE" | awk '{print $1}')
SIZE=$(stat -c%s "/tmp/$ARCHIVE")
echo "Core archive: $SIZE bytes, SHA-256:$CHECKSUM"

# ── 4. Verify archive (no symlinks, correct structure) ──────────
ARCHIVE_LIST=$(mktemp)
ARCHIVE_TABLE=$(mktemp)
tar tjf "/tmp/$ARCHIVE" > "$ARCHIVE_LIST"
tar tvjf "/tmp/$ARCHIVE" > "$ARCHIVE_TABLE"

SYMLINKS=$(grep -c "^l" "$ARCHIVE_TABLE" || true)
if [ "$SYMLINKS" -gt 0 ]; then
    echo "ERROR: Archive contains $SYMLINKS symlinks! Windows will fail."
    exit 1
fi
# Check single root directory
ROOT_DIRS=$(cut -d/ -f1 "$ARCHIVE_LIST" | sort -u | wc -l)
if [ "$ROOT_DIRS" -ne 1 ]; then
    echo "ERROR: Archive must have a single root directory"
    exit 1
fi
echo "Archive verified: no symlinks, single root dir ✅"

# ── 5. Update package index ─────────────────────────────────────
HOST_TOOLS_VERSION="2.0.0"
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

host_tools = None
for pkg in data["packages"]:
    if pkg["name"] == "nrf54l15clean":
        for tool in pkg.get("tools", []):
            if (tool.get("name") == "nrf54l15hosttools" and
                    tool.get("version") == HOST_TOOLS_VERSION):
                host_tools = tool
                break
        break

if host_tools is None:
    raise SystemExit(
        f"Missing nrf54l15hosttools@{HOST_TOOLS_VERSION} in {INDEX}")

expected_release_path = f"/host-tools-v{HOST_TOOLS_VERSION}/"
bad_urls = [
    system.get("url", "")
    for system in host_tools.get("systems", [])
    if expected_release_path not in system.get("url", "")
]
if bad_urls:
    raise SystemExit(
        "Host tools must be served from the permanent "
        f"host-tools-v{HOST_TOOLS_VERSION} release: {bad_urls[0]}")

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
        {"name": "XIAO nRF54L15 / Sense"},
        {"name": "XIAO nRF54LM20A"},
        {"name": "HOLYIOT nRF54L15 Modules"}
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
git add "$PLATFORM_TXT" "$CORE_VERSION_L15" "$CORE_VERSION_LM20" "$INDEX"
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
echo "   Host tools are served from host-tools-v$HOST_TOOLS_VERSION — no per-release tool upload."
