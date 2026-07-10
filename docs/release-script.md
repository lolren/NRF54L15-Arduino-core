# Board Manager Release Script

This repo builds Arduino Board Manager updates with one canonical implementation:
`scripts/build_release.py`. `tools/release.sh` is a checked wrapper around it.

## What the script does

`tools/release.sh <version>` prepares the complete release artifact set:

- updates `hardware/nrf54l15clean/nrf54l15clean/platform.txt`
- updates both generated core version headers:
  - `cores/nrf54l15/CoreVersionGenerated.h`
  - `cores/nrf54lm20b/CoreVersionGenerated.h`
- builds a content-addressed `nrf54l15clean-<version>-<sha>.tar.bz2`
- includes the OpenThread sources required by the advertised Thread and Matter menus
- dereferences package symlinks so Windows installs do not break
- verifies the archive has one root directory and no symlinks
- updates `package_nrf54l15clean_index.json`
- verifies all three generated indexes against the exact archive
- compiles the advertised Thread and Matter targets from the extracted archive
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

## After Release Verification

Use a clean Arduino CLI data directory to verify users can install the new release from the public package index:

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
