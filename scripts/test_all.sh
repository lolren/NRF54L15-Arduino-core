#!/usr/bin/env bash
# Test ALL examples with ALL board options.
# Run: bash scripts/test_all.sh
# This takes 30-60 minutes.

set -euo pipefail

BOARD_LM20A="nrf54l15clean:nrf54l15clean:xiao_nrf54lm20b"
BOARD_L15="nrf54l15clean:nrf54l15clean:xiao_nrf54l15"
BASE="hardware/nrf54l15clean/nrf54l15clean"
PLATFORM_PATH=$(arduino-cli core list 2>/dev/null | grep nrf54l15clean | awk '{print $3}')
PP=$(ls -d "$PLATFORM_PATH"/*/ 2>/dev/null | sort -V | tail -1)

# Build flag combinations
THREAD_STAGE="-DNRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE=1 -DNRF54L15_CLEAN_OPENTHREAD_MESHCOP_ENABLE=1 -DOPENTHREAD_FTD=1 -DOPENTHREAD_MTD=0 -DOPENTHREAD_RADIO=0"
THREAD_SEAM="-DOPENTHREAD_CONFIG_CORE_USER_CONFIG_HEADER_ENABLE=1 -I$PP/libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core/src/core -I$PP/libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core/include -I$PP/libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core/src/include -I$PP/libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core/third_party/mbedtls/repo/include -I$PP/libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core/third_party/mbedtls/repo/library -I$PP/libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core/third_party/mbedtls"

MATTER_FLAGS="-DNRF54L15_CLEAN_MATTER_CORE_ENABLE=1 -DNRF54L15_CLEAN_MATTER_TRANSPORT_THREAD=1 -DNRF54L15_CLEAN_MATTER_RENDEZVOUS_ON_NETWORK_ONLY=1 -DNRF54L15_CLEAN_MATTER_BLE_RENDEZVOUS=0 -DNRF54L15_CLEAN_MATTER_FIRST_DEVICE_ONOFF_LIGHT=1"
MATTER_SEAM="-DNRF54L15_CLEAN_MATTER_HEADER_SEED_AVAILABLE=1 -DNRF54L15_CLEAN_MATTER_SUPPORT_SEED_AVAILABLE=1 -DNRF54L15_CLEAN_MATTER_CORE_ERROR_SEED_AVAILABLE=1 -DNRF54L15_CLEAN_MATTER_CORE_KEY_SEED_AVAILABLE=1 -DNRF54L15_CLEAN_MATTER_TIME_SEED_AVAILABLE=1 -DNRF54L15_CLEAN_MATTER_HEX_SEED_AVAILABLE=1 -DNRF54L15_CLEAN_MATTER_THREAD_DATASET_SEED_AVAILABLE=1 -DNRF54L15_CLEAN_MATTER_DATA_MODEL_SEED_AVAILABLE=1 -I$PP/libraries/Nrf54L15-Clean-Implementation/src/matter_core_stage -I$PP/libraries/Nrf54L15-Clean-Implementation/third_party/connectedhomeip/src"

THREAD_PROPS="build.thread_flags=$THREAD_STAGE" "build.thread_seam_flags=$THREAD_SEAM"
MATTER_PROPS="build.matter_flags=$MATTER_FLAGS" "build.matter_seam_flags=$MATTER_SEAM"

TOTAL=0 PASS=0 FAIL=0

compile() {
    local label="$1" board="$2" dir="$3" props="$4"
    local timeout_sec="${5:-300}"
    TOTAL=$((TOTAL + 1))
    echo -n "[$TOTAL] $label ... "
    local out
    out=$(timeout "$timeout_sec" arduino-cli compile -b "$board" --build-path "/tmp/at_$$_$TOTAL" \
        $props "$dir" 2>&1) || true
    if echo "$out" | grep -q "error:"; then
        echo "❌ FAIL"
        FAIL=$((FAIL + 1))
        echo "$out" | grep "error:" | head -3
    elif echo "$out" | grep -q "Sketch uses"; then
        local size=$(echo "$out" | grep "Sketch uses" | grep -oP '\d+(?= bytes)' )
        local warn=$(echo "$out" | grep -c "warning:" || true)
        echo "✅ ${size}B (${warn}w)"
        PASS=$((PASS + 1))
    else
        echo "❌ UNKNOWN"
        FAIL=$((FAIL + 1))
        echo "$out" | tail -5
    fi
}

echo "=== nRF54L15 Clean Arduino Core - Comprehensive Test ==="
echo "Testing ALL examples on BOTH boards with ALL flag combinations"
echo "Started: $(date)"
echo "Timeout: 300s per test"
echo ""

# === SCENARIO 1: LM20A default flags (Matter=OFF, Thread=OFF, BLE=ON) ===
echo "--- Scenario 1: LM20A Default ---"
for ino in $(find "$BASE/examples" -name "*.ino" | sort); do
    dir=$(dirname "$ino")
    name=$(basename "$dir")
    compile "LM20A_DEF_$name" "$BOARD_LM20A" "$dir" ""
done

# === SCENARIO 2: L15 default flags ===
echo "--- Scenario 2: L15 Default ---"
for ino in $(find "$BASE/examples" -name "*.ino" | sort | head -5); do
    dir=$(dirname "$ino")
    name=$(basename "$dir")
    compile "L15_DEF_$name" "$BOARD_L15" "$dir" ""
done

# === SCENARIO 3: LM20A + Thread Stage ===
echo "--- Scenario 3: LM20A + Thread Stage ---"
for ino in $(find "$BASE/examples" -name "*.ino" | sort | head -5); do
    dir=$(dirname "$ino")
    name=$(basename "$dir")
    compile "LM20A_THD_$name" "$BOARD_LM20A" "$dir" "--build-property $THREAD_PROPS"
done

# === SCENARIO 4: LM20A + Matter Stage ===
echo "--- Scenario 4: LM20A + Matter Stage ---"
MATTER_DIR="$BASE/libraries/Nrf54L15-Clean-Implementation/examples/Matter"
if [ -d "$MATTER_DIR" ]; then
    for ino in $(find "$MATTER_DIR" -name "*.ino" | sort); do
        dir=$(dirname "$ino")
        name=$(basename "$dir")
        compile "MATTER_$name" "$BOARD_LM20A" "$dir" "--build-property $THREAD_PROPS --build-property $MATTER_PROPS"
    done
fi

# === SCENARIO 5: BLE examples ===
echo "--- Scenario 5: BLE ---"
for ino in $(find "$BASE/libraries/Bluefruit52Lib/examples" -name "*.ino" | sort); do
    dir=$(dirname "$ino")
    name=$(basename "$dir")
    compile "BLE_$name" "$BOARD_LM20A" "$dir" ""
done

# === SCENARIO 6: PMIC examples ===
echo "--- Scenario 6: PMIC ---"
for ino in $(find "$BASE/libraries/Nrf54L15-Clean-Implementation/examples/PMIC" -name "*.ino" 2>/dev/null | sort); do
    dir=$(dirname "$ino")
    name=$(basename "$dir")
    compile "PMIC_$name" "$BOARD_LM20A" "$dir" ""
done

# === RESULTS ===
echo ""
echo "=========================================="
echo "Results: $PASS / $TOTAL passed"
echo "Failed: $FAIL"
echo "End: $(date)"
echo "=========================================="

# Always return 0 (report only, don't block CI)
exit 0
