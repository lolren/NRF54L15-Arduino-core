#!/usr/bin/env python3
"""Lock the LM20 CRACEN RNG CONTROL metadata to Nordic's LM20A MDK."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
HEADER = (
    ROOT
    / "hardware/nrf54l15clean/nrf54l15clean/cores/nrf54lm20b/nrf54lm20b_types.h"
)
PREFIX = "CRACENCORE_RNGCONTROL_CONTROL_"


def field(name: str, position: int, width_mask: str) -> dict[str, str]:
    return {
        f"{name}_Pos": f"({position}UL)",
        f"{name}_Msk": f"({width_mask}UL << {PREFIX}{name}_Pos)",
    }


def main() -> int:
    source = HEADER.read_text(encoding="utf-8")
    start = source.index("/* CRACENCORE_RNGCONTROL_CONTROL: Control register */")
    end = source.index("/* CRACENCORE_RNGCONTROL_FIFOLEVEL:", start)
    control = source[start:end]

    expected = {"ResetValue": "(0x00040000UL)"}
    expected.update(field("ENABLE", 0, "0x1"))
    expected.update(field("LFSREN", 1, "0x1"))
    expected.update(field("TESTEN", 2, "0x1"))
    expected.update(
        {
            "TESTEN_Min": "(0x0UL)",
            "TESTEN_Max": "(0x1UL)",
            "TESTEN_NORMAL": "(0x0UL)",
            "TESTEN_TEST": "(0x1UL)",
        }
    )
    expected.update(field("CONDBYPASS", 3, "0x1"))
    expected.update(
        {
            "CONDBYPASS_Min": "(0x0UL)",
            "CONDBYPASS_Max": "(0x1UL)",
            "CONDBYPASS_NORMAL": "(0x0UL)",
            "CONDBYPASS_BYPASS": "(0x1UL)",
        }
    )
    expected.update(field("INTENREP", 4, "0x1"))
    expected.update(field("INTENFULL", 7, "0x1"))
    expected.update(field("SOFTRST", 8, "0x1"))
    expected.update(field("FORCEACTIVEROS", 11, "0x1"))
    expected.update(field("IGNOREHEALTHTESTSFAILFORFSM", 12, "0x1"))
    expected.update(field("NB128BITBLOCKS", 16, "0xF"))
    expected.update(field("FIFOWRITESTARTUP", 20, "0x1"))
    expected.update(field("DISREPETTESTS", 21, "0x1"))
    expected.update(field("DISPROPTESTS", 22, "0x1"))
    expected.update(field("DISAUTOCORRTESTS", 23, "0x3"))
    expected.update(field("DISCORRTESTS", 27, "0x7"))
    expected.update(field("BLENDINGMETHOD", 30, "0x3"))
    expected.update(
        {
            "BLENDINGMETHOD_Min": "(0x0UL)",
            "BLENDINGMETHOD_Max": "(0x3UL)",
            "BLENDINGMETHOD_CONCATENATION": "(0x0UL)",
            "BLENDINGMETHOD_XORLEVEL1": "(0x1UL)",
            "BLENDINGMETHOD_XORLEVEL2": "(0x2UL)",
            "BLENDINGMETHOD_VONNEUMANN": "(0x3UL)",
        }
    )

    definitions = dict(
        re.findall(rf"^\s*#define\s+{PREFIX}(\w+)\s+([^\s]+(?:\s+<<\s+[^)]+\))?)", control, re.MULTILINE)
    )
    errors = []
    missing = sorted(set(expected) - set(definitions))
    extra = sorted(set(definitions) - set(expected))
    if missing:
        errors.append(f"missing LM20 fields: {', '.join(missing)}")
    if extra:
        errors.append(f"unexpected non-LM20 fields: {', '.join(extra)}")
    for name, value in expected.items():
        actual = definitions.get(name)
        if actual is not None and actual != value:
            errors.append(f"{name}: expected {value}, found {actual}")

    if errors:
        for error in errors:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1

    print(f"PASS: {len(expected)} LM20 CRACEN RNG CONTROL definitions match the Nordic MDK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
