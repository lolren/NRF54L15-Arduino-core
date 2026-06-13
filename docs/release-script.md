# Board Manager Release Script

This repo publishes Arduino Board Manager updates with `tools/release.sh`.

## What the script does

`tools/release.sh <version>` performs the complete public release flow:

- updates `hardware/nrf54l15clean/nrf54l15clean/platform.txt`
- updates both generated core version headers:
  - `cores/nrf54l15/CoreVersionGenerated.h`
  - `cores/nrf54lm20b/CoreVersionGenerated.h`
- builds `nrf54l15clean-<version>.tar.bz2`
- dereferences package symlinks so Windows installs do not break
- verifies the archive has one root directory and no symlinks
- updates `package_nrf54l15clean_index.json`
- commits the release metadata
- pushes `main`
- tags `v<version>`
- creates the GitHub release and uploads the core archive

Host tools are not bundled in every core release. The package index points to the permanent `host-tools-v1.1.4` GitHub release, so users get consistent host-tool downloads without making every board package archive huge.

## Normal Release Flow

Run all tests and compile checks first. Then commit the code changes that should go into the release:

```bash
git status --short
git add <changed files>
git commit -m "fix: short description"
git push origin main
```

Run the release script from the repository root:

```bash
./tools/release.sh 0.9.59
```

The script requires a clean working tree before it starts. If it stops early, inspect `git status --short` before retrying.

## After Release Verification

Use a clean Arduino CLI data directory to verify users can install the new release from the public package index:

```bash
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
arduino-cli --config-file "$TMP_CLI/arduino-cli.yaml" core install nrf54l15clean:nrf54l15clean@0.9.59
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

- `ERROR: Uncommitted changes`: commit, stash, or revert the local changes before running the script.
- Checksum mismatch during user install: the package index and GitHub release asset do not match. Delete the bad GitHub release/tag and rerun from a clean state.
- Windows install failure: inspect the archive with `tar tvjf /tmp/nrf54l15clean-<version>.tar.bz2 | grep '^l'`. It must not contain symlinks.
- Host tool download failure: verify every `nrf54l15hosttools@1.1.4` URL in `package_nrf54l15clean_index.json` points to the permanent `host-tools-v1.1.4` release.

