# Board Manager Release Script

This repo builds Arduino Board Manager updates with one canonical implementation:
`scripts/build_release.py`. `tools/release.sh` is a checked wrapper around it.

## What the script does

`tools/release.sh <version>` prepares the complete release artifact set. The
version may be a final `MAJOR.MINOR.PATCH` or a SemVer prerelease such as
`1.0.0-rc1`; leading zeroes such as `1.0.00` are rejected.

- updates `hardware/nrf54l15clean/nrf54l15clean/platform.txt`
- updates both generated core version headers:
  - `cores/nrf54l15/CoreVersionGenerated.h`
  - `cores/nrf54lm20b/CoreVersionGenerated.h`
- builds a content-addressed `nrf54l15clean-<version>-<sha>.tar.bz2`
- includes the OpenThread sources required by the advertised Thread and Matter menus
- dereferences package symlinks so Windows installs do not break
- verifies the archive has one root directory and no symlinks
- updates the stable, stable-alias, and complete archive package indexes
- verifies every generated index that advertises the new version against the exact archive
- compiles representative BLE security/privacy, Thread, and Matter targets from
  the extracted archive
- copies the verified indexes from `dist/` to the repository root

The wrapper never commits, pushes, tags, or publishes implicitly. Review the generated
diff and artifacts first, then perform those operations explicitly.

Host tools are not bundled in every core release. The package index points to
the permanent `host-tools-v1.1.5` GitHub release, so users get consistent,
self-describing host-tool downloads without making every board package archive
huge.

## Normal Release Flow

Run all tests and hardware gates first. Commit the source changes that should go
into the release, then build the release metadata and archives:

```bash
set -euo pipefail
VERSION=1.0.0
git status --short
git add <changed files>
git commit -m "release: nRF54 Arduino Core $VERSION"

# The first run creates v1.1.5 host assets if the tracked indexes do not yet
# contain that immutable tool version.
NRF54_REBUILD_HOSTTOOLS=1 ./tools/release.sh "$VERSION"
```

Validate a proposed version without changing the source tree or building an
archive:

```bash
python3 scripts/build_release.py \
  --source-version nrf54l15clean \
  --version 1.0.0-rc1 \
  --validate-version-only
```

Inspect `dist/release-manifest.json`, verify the repository diff, and commit the
generated version metadata and package indexes. Then rebuild once from that
clean commit; the resulting bytes must reproduce the committed index hashes:

```bash
set -euo pipefail
git add hardware/nrf54l15clean/nrf54l15clean/platform.txt \
  hardware/nrf54l15clean/nrf54l15clean/cores/*/CoreVersionGenerated.h \
  package_nrf54l15clean*.json
git commit --amend --no-edit

test -z "$(git status --short)"
NRF54_REBUILD_HOSTTOOLS=1 ./tools/release.sh "$VERSION"
test -z "$(git status --short)"
```

When introducing a new host-tool version, publish its five immutable assets
before the core tag. `host-tools-v1.1.5` does not match the core release workflow
tag pattern:

```bash
set -euo pipefail
HOST_TAG=host-tools-v1.1.5

# Immutable releases prevent published tags and assets from being replaced.
gh api -X PUT "repos/lolren/nrf54-arduino-core/immutable-releases"
test "$(gh api "repos/lolren/nrf54-arduino-core/immutable-releases" \
  --jq '.enabled')" = "true"

git tag -a "$HOST_TAG" -m "Host Tools 1.1.5"
git push origin "$HOST_TAG"

mapfile -t HOST_ASSETS < <(python3 - <<'PY'
import json
from pathlib import Path
manifest = json.loads(Path("dist/release-manifest.json").read_text())
print(*(item["archivePath"] for item in manifest["tools"]), sep="\n")
PY
)
test "${#HOST_ASSETS[@]}" -eq 5
gh release create "$HOST_TAG" "${HOST_ASSETS[@]}" \
  --draft --verify-tag --latest=false \
  --title "Host Tools 1.1.5 - pyOCD 0.44.1 Bootstrap" \
  --notes "Pinned online pyOCD recovery bootstrap for all supported hosts; no redistributed dependency wheelhouse."

VERIFY_DIR="$(mktemp -d)"
trap 'rm -rf "$VERIFY_DIR"' EXIT
gh release download "$HOST_TAG" --dir "$VERIFY_DIR"
python3 - "$VERIFY_DIR" <<'PY'
import hashlib
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
manifest = json.loads(Path("dist/release-manifest.json").read_text())
expected = {entry["archiveFileName"]: entry for entry in manifest["tools"]}
actual = {path.name: path for path in root.iterdir() if path.is_file()}
assert expected.keys() == actual.keys(), (expected.keys(), actual.keys())
for name, entry in expected.items():
    path = actual[name]
    payload = path.read_bytes()
    assert len(payload) == entry["size"], path
    assert hashlib.sha256(payload).hexdigest() == entry["checksum"].split(":", 1)[1], path
PY
rm -rf "$VERIFY_DIR"
trap - EXIT
gh release edit "$HOST_TAG" --draft=false --latest=false
test "$(gh api \
  "repos/lolren/nrf54-arduino-core/releases/tags/${HOST_TAG}" \
  --jq '.immutable')" = "true"

python3 scripts/verify_public_release.py \
  --index dist/package_nrf54l15clean_index.json \
  --version "$VERSION" --include-tools --tools-only
```

Finally rebuild the platform-only release set, tag the exact clean commit, and
let the Release workflow publish it:

```bash
set -euo pipefail
./tools/release.sh "$VERSION"
test -z "$(git status --short)"
git tag -a "v$VERSION" -m "nRF54 Arduino Core $VERSION"
git push origin "v$VERSION"

COMMIT="$(git rev-parse HEAD)"
RUN_ID=""
for _ in $(seq 1 30); do
  RUN_ID="$(gh run list --workflow Release --commit "$COMMIT" --event push \
    --limit 1 --json databaseId --jq '.[0].databaseId // empty')"
  test -n "$RUN_ID" && break
  sleep 2
done
test -n "$RUN_ID"
gh run watch "$RUN_ID" --exit-status

# The normal raw-main Board Manager feed changes only after every referenced
# public asset has passed the release workflow.
git push origin main

CI_RUN_ID=""
for _ in $(seq 1 30); do
  CI_RUN_ID="$(gh run list --workflow CI --commit "$COMMIT" --event push \
    --limit 1 --json databaseId --jq '.[0].databaseId // empty')"
  test -n "$CI_RUN_ID" && break
  sleep 2
done
test -n "$CI_RUN_ID"
gh run watch "$CI_RUN_ID" --exit-status
```

The Release workflow rebuilds the deterministic archive, checks it against the
committed indexes, verifies that the immutable host assets are already public,
compiles advertised features from the extracted archive, publishes the core
assets, and verifies the public bytes.
Tags with a prerelease suffix are published as GitHub prereleases and are not marked latest.
Prerelease versions are added only to
`package_nrf54l15clean_archive_index.json`. The normal
`package_nrf54l15clean_index.json` and its explicitly named stable alias remain
final-only, so existing Board Manager users are not offered an RC as a normal
upgrade.

To install an RC for testing, use the opt-in archive feed:

```text
https://raw.githubusercontent.com/lolren/nrf54-arduino-core/main/package_nrf54l15clean_archive_index.json
```

Then request the exact prerelease version, for example:

```bash
arduino-cli core install "nrf54l15clean:nrf54l15clean@1.0.0-rc1"
```

## After Release Verification

Use a clean Arduino CLI data directory to verify users can install the new
release from the public package index. Use the normal index for a final release
and the archive index shown above for a prerelease:

```bash
VERSION=1.0.0
TMP_CLI=/tmp/nrf54-release-test
rm -rf "$TMP_CLI"
mkdir -p "$TMP_CLI"

cat > "$TMP_CLI/arduino-cli.yaml" <<'YAML'
directories:
  data: /tmp/nrf54-release-test/data
  downloads: /tmp/nrf54-release-test/downloads
  user: /tmp/nrf54-release-test/user
board_manager:
  additional_urls:
    - https://raw.githubusercontent.com/lolren/nrf54-arduino-core/main/package_nrf54l15clean_index.json
YAML

arduino-cli --config-file "$TMP_CLI/arduino-cli.yaml" core update-index
arduino-cli --config-file "$TMP_CLI/arduino-cli.yaml" core install \
  "nrf54l15clean:nrf54l15clean@$VERSION"
```

Then compile at least one small sketch for each supported family:

```bash
arduino-cli --config-file "$TMP_CLI/arduino-cli.yaml" compile \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15 \
  /path/to/sketch

arduino-cli --config-file "$TMP_CLI/arduino-cli.yaml" compile \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54lm20b \
  /path/to/sketch
```

## Common Failure Points

- Checksum mismatch before publication: leave the draft unpublished, correct
  the build, and recreate the draft. A published immutable tag or asset cannot
  be replaced; if bad bytes somehow become public, correct them in a new
  version rather than rewriting release history.
- Windows install failure: inspect the archive listed in `dist/release-manifest.json` with `tar tvjf`; it must not contain symlinks.
- Host tool download failure: verify every `nrf54l15hosttools@1.1.5` URL in
  `package_nrf54l15clean_index.json` points to the permanent
  `host-tools-v1.1.5` release.
