#!/usr/bin/env python3
"""Compile all examples with all relevant board options to catch regressions."""
import subprocess, sys
from pathlib import Path

BOARD = "nrf54l15clean:nrf54l15clean:xiao_nrf54lm20b"
BASE = Path(__file__).resolve().parent.parent
PP = "/home/lolren/.arduino15/packages/nrf54l15clean/hardware/nrf54l15clean/0.9.161"

THREAD_STAGE = {
    "build.thread_flags": "-DNRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE=1 -DNRF54L15_CLEAN_OPENTHREAD_MESHCOP_ENABLE=1 -DOPENTHREAD_FTD=1 -DOPENTHREAD_MTD=0 -DOPENTHREAD_RADIO=0",
    "build.thread_seam_flags": f"-DOPENTHREAD_CONFIG_CORE_USER_CONFIG_HEADER_ENABLE=1 -I{PP}/libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core/src/core -I{PP}/libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core/include -I{PP}/libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core/src/include -I{PP}/libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core/third_party/mbedtls/repo/include -I{PP}/libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core/third_party/mbedtls/repo/library -I{PP}/libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core/third_party/mbedtls",
}
MATTER_STAGE = {
    "build.matter_flags": "-DNRF54L15_CLEAN_MATTER_CORE_ENABLE=1 -DNRF54L15_CLEAN_MATTER_TRANSPORT_THREAD=1 -DNRF54L15_CLEAN_MATTER_RENDEZVOUS_ON_NETWORK_ONLY=1 -DNRF54L15_CLEAN_MATTER_BLE_RENDEZVOUS=0 -DNRF54L15_CLEAN_MATTER_FIRST_DEVICE_ONOFF_LIGHT=1",
    "build.matter_seam_flags": f"-DNRF54L15_CLEAN_MATTER_HEADER_SEED_AVAILABLE=1 -DNRF54L15_CLEAN_MATTER_SUPPORT_SEED_AVAILABLE=1 -DNRF54L15_CLEAN_MATTER_CORE_ERROR_SEED_AVAILABLE=1 -DNRF54L15_CLEAN_MATTER_CORE_KEY_SEED_AVAILABLE=1 -DNRF54L15_CLEAN_MATTER_TIME_SEED_AVAILABLE=1 -DNRF54L15_CLEAN_MATTER_HEX_SEED_AVAILABLE=1 -DNRF54L15_CLEAN_MATTER_THREAD_DATASET_SEED_AVAILABLE=1 -DNRF54L15_CLEAN_MATTER_DATA_MODEL_SEED_AVAILABLE=1 -I{PP}/libraries/Nrf54L15-Clean-Implementation/src/matter_core_stage -I{PP}/libraries/Nrf54L15-Clean-Implementation/third_party/connectedhomeip/config/arduino -I{PP}/libraries/Nrf54L15-Clean-Implementation/third_party/connectedhomeip/src/lib/core -I{PP}/libraries/Nrf54L15-Clean-Implementation/third_party/connectedhomeip/src/lib/support -I{PP}/libraries/Nrf54L15-Clean-Implementation/third_party/connectedhomeip/src/system -I{PP}/libraries/Nrf54L15-Clean-Implementation/third_party/connectedhomeip/src/inet -I{PP}/libraries/Nrf54L15-Clean-Implementation/third_party/connectedhomeip/src/crypto -I{PP}/libraries/Nrf54L15-Clean-Implementation/third_party/connectedhomeip/src/protocols -I{PP}/libraries/Nrf54L15-Clean-Implementation/third_party/connectedhomeip/src/transport -I{PP}/libraries/Nrf54L15-Clean-Implementation/third_party/connectedhomeip/src/messaging -I{PP}/libraries/Nrf54L15-Clean-Implementation/third_party/connectedhomeip/src -I{PP}/libraries/Nrf54L15-Clean-Implementation/src/platform/arduino",
}

EXAMPLES = [
    ("Power/SystemOffWakeReset", "SystemOffWake", {}),
    ("Power/nPM1300_BatteryCurrent", "PMIC_Battery", {}),
    ("XiaoLM20A/XiaoLM20A_ImuAccelGyro", "IMU", {}),
    ("XiaoLM20A/XiaoLM20A_MicLevel", "MIC_PDM", {}),
    ("./libraries/Bluefruit52Lib/examples/Peripheral/bleuart", "BLE_Peripheral", {}),
    ("./libraries/Bluefruit52Lib/examples/Central/central_bleuart", "BLE_Central", {}),
    ("./libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnOffLightApiDemo", "Matter_ApiDemo", {**THREAD_STAGE, **MATTER_STAGE}),
    ("./libraries/Nrf54L15-Clean-Implementation/examples/PMIC/nPM1300_LM20A_RailControl", "PMIC_Rail", {}),
]

def run(label, rel_src, props):
    src = BASE / "hardware/nrf54l15clean/nrf54l15clean" / ("examples/" if not rel_src.startswith("./libraries") else "") / rel_src
    if not src.exists():
        return "SKIP"
    cmd = ["arduino-cli", "compile", "-b", BOARD, "--build-path", f"/tmp/atest_{label}"]
    for k, v in props.items():
        cmd.extend(["--build-property", f"{k}={v}"])
    cmd.append(str(src))
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        errs = [l for l in r.stderr.split("\n") if "error:" in l]
        if r.returncode == 0 and not errs:
            return f"OK ({r.stderr.count('warning:')}w)"
        return f"FAIL ({len(errs)} errors)"
    except subprocess.TimeoutExpired:
        return "TIMEOUT"

print(f"Testing {len(EXAMPLES)} examples on {BOARD}")
print("=" * 60)
results = []
for src, label, props in EXAMPLES:
    print(f"  {label:25s}...", end=" ", flush=True)
    r = run(label, src, props)
    print(r)
    results.append((label, r))

# L15 basic test
print("  L15_Basic     ...", end=" ", flush=True)
r = subprocess.run(["arduino-cli","compile","-b","nrf54l15clean:nrf54l15clean:xiao_nrf54l15","--build-path","/tmp/atest_l15",
    str(BASE/"hardware/nrf54l15clean/nrf54l15clean/examples/Power/SystemOffWakeReset")],
    capture_output=True, text=True, timeout=60)
print("OK" if r.returncode == 0 else "FAIL")
results.append(("L15_Basic", "OK" if r.returncode == 0 else "FAIL"))

print("\nSummary:")
for label, r in results:
    mark = "✅" if r.startswith("OK") else ("⏭️ " if r == "SKIP" else "❌")
    print(f"  {mark} {label}: {r}")
print(f"\nPassed: {sum(1 for _,r in results if r.startswith('OK'))}/{len(results)}")
