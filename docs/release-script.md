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

Host tools are not bundled in every core release. The package index points to the permanent `host-tools-v1.1.4` GitHub release, so users get consistent host-tool downloads without making every board package archive huge.

## Normal Release Flow

Run all tests and compile checks first. Commit the code changes that should go into the release:

```bash
git status --short
git add <changed files>
git commit -m "fix: short description"
git push origin main
```

Run the release script from the repository root:

```bash
VERSION=0.9.216
./tools/release.sh "$VERSION"
```

Validate a proposed version without changing the source tree or building an
archive:

```bash
python3 scripts/build_release.py \
  --source-version nrf54l15clean \
  --version 1.0.0-rc1 \
  --validate-version-only
```

Inspect `dist/release-manifest.json`, verify the repository diff, and publish the exact
archive named in the manifest:

```bash
git add hardware/nrf54l15clean/nrf54l15clean/platform.txt \
  hardware/nrf54l15clean/nrf54l15clean/cores/*/CoreVersionGenerated.h \
  package_nrf54l15clean*.json
git commit -m "release: v$VERSION"
git push origin main
git tag "v$VERSION"
git push origin "v$VERSION"
gh run watch "$(gh run list --workflow Release --limit 1 --json databaseId --jq '.[0].databaseId')"
```

Pushing the tag is the only publication path. The Release workflow rebuilds the
deterministic archive, checks it against the committed indexes, compiles the advertised
features from the extracted archive, publishes the assets, and verifies the public bytes.
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
VERSION=0.9.216
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

- Checksum mismatch during user install: the package index and GitHub release asset do not match. Delete the bad GitHub release/tag and rerun from a clean state.
- Windows install failure: inspect the archive listed in `dist/release-manifest.json` with `tar tvjf`; it must not contain symlinks.
- Host tool download failure: verify every `nrf54l15hosttools@1.1.4` URL in `package_nrf54l15clean_index.json` points to the permanent `host-tools-v1.1.4` release.
