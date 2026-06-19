#!/usr/bin/env python3
"""Build all examples for all boards. Each board runs in parallel."""

import subprocess, sys, json, os
from pathlib import Path
from collections import defaultdict

ROOT = Path(__file__).resolve().parent.parent
EXAMPLES = ROOT / "hardware" / "nrf54l15clean" / "nrf54l15clean" / "libraries" \
           / "Nrf54L15-Clean-Implementation" / "examples"
BOARDS = ["xiao_nrf54lm20b","holyiot_25007_nrf54l15","holyiot_25008_nrf54l15","generic_nrf54l15_module_36pin","nrf54l15dk_pca10156"]

def get_menus(board, edir, name):
    cfg = []
    if "Thread/" in edir or name.startswith("Thread") or name.startswith("OpenThread"):
        cfg.append("clean_thread=stage")
    if "Matter/" in edir or name.startswith("Matter"):
        cfg.append("clean_matter=stage")
        if "clean_thread=stage" not in cfg:
            cfg.append("clean_thread=stage")
    # Board-specific
    if "Xiao_nRF54LM20B" in name or "xiao_nrf54lm20b" in name.lower():
        if board != "xiao_nrf54lm20b": return None
    if "Holyiot25008" in name or "holyiot25008" in name.lower():
        if board != "holyiot_25008_nrf54l15": return None
    if "Holyiot25007" in name or "holyiot25007" in name.lower():
        if board != "holyiot_25007_nrf54l15": return None
    # Skip Thread/Matter on nRF54L15 boards (pre-compiled library is nRF54LM20B-only)
    nrf54l15_boards = ["holyiot_25007_nrf54l15", "holyiot_25008_nrf54l15", "generic_nrf54l15_module_36pin", "nrf54l15dk_pca10156"]
    if board in nrf54l15_boards and ("clean_thread=stage" in cfg or "clean_matter=stage" in cfg):
        return None
    # Skip ChipPhase* examples (incomplete development examples)
    if name.startswith("ChipPhase"):
        return None
    return cfg

def build(ino, board, menus):
    fqbn = f"nrf54l15clean:nrf54l15clean:{board}"
    if menus: fqbn += ":" + ",".join(menus)
    bp = f"/tmp/arduino-build-{board}-{ino.stem}"
    cmd = ["arduino-cli","compile","--fqbn",fqbn,"--build-path",bp,str(ino.parent)]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=300, cwd=str(ROOT))
        return r.returncode == 0, r.stderr[-500:] if r.returncode else ""
    except subprocess.TimeoutExpired:
        return False, "TIMEOUT"
    except Exception as e:
        return False, str(e)

def main():
    examples = sorted(EXAMPLES.rglob("*.ino"))
    results = defaultdict(lambda: {"pass":0,"fail":0,"skip":0,"errors":[]})
    total = passed = failed = skipped = 0

    for ino in examples:
        name = ino.stem
        edir = str(ino.parent.relative_to(EXAMPLES))
        for board in BOARDS:
            menus = get_menus(board, edir, name)
            if menus is None:
                skipped += 1; results[name]["skip"] += 1; continue
            total += 1
            ok, err = build(ino, board, menus)
            if ok:
                passed += 1; results[name]["pass"] += 1
                print(f"  PASS: {name} on {board}")
            else:
                failed += 1; results[name]["fail"] += 1
                results[name]["errors"].append(f"{board}: {err[:300]}")
                print(f"  FAIL: {name} on {board}: {err[:200]}")

    print(f"\n{'='*80}")
    print(f"TOTAL: {total} | PASS: {passed} | FAIL: {failed} | SKIP: {skipped}")
    print(f"{'='*80}")
    if failed:
        print(f"\nFAILED ({failed} unique):")
        for n,d in sorted(results.items()):
            if d["fail"]:
                print(f"\n  {n}:")
                for e in d["errors"]: print(f"    {e[:200]}")
    s = {"total":total,"passed":passed,"failed":failed,"skipped":skipped,
         "examples":{n:{"pass":d["pass"],"fail":d["fail"],"skip":d["skip"],"errors":d["errors"]} for n,d in sorted(results.items())}}
    out = ROOT/"build"/"examples_compile_report.json"
    out.parent.mkdir(parents=True,exist_ok=True)
    with open(out,"w") as f: json.dump(s,f,indent=2)
    print(f"\nReport: {out}")
    return 0 if failed==0 else 1

if __name__=="__main__": sys.exit(main())
