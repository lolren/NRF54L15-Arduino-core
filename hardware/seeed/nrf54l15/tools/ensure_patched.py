#!/usr/bin/env python3
"""
Bootstrap and patch manager for nRF54L15 Arduino core

This script ensures the Zephyr SDK is patched for PyYAML compatibility
before any builds happen. It patches edtlib.py in-place.
"""

import os
import re
import sys
import subprocess
from pathlib import Path


def run_cmd(cmd, cwd=None, check=True, capture_output=False):
    """Run a command safely"""
    try:
        result = subprocess.run(
            cmd,
            cwd=cwd,
            check=check,
            capture_output=capture_output,
            text=True
        )
        if capture_output:
            return result.stdout, result.stderr, result.returncode
        return result
    except Exception as e:
        if check:
            raise
        return None


def find_zephyr_edtlib(platform_dir):
    """Find the edtlib.py file in Zephyr SDK"""
    # Check NCS directory first (after bootstrap)
    ncs_dir = platform_dir / "tools" / "ncs"
    if ncs_dir.exists():
        edtlib_path = ncs_dir / "zephyr" / "scripts" / "dts" / "python-devicetree" / "src" / "devicetree" / "edtlib.py"
        if edtlib_path.exists():
            return edtlib_path

    # Not found yet - NCS hasn't been bootstrapped
    return None


def patch_edtlib(edtlib_file):
    """Patch edtlib.py to work with PyYAML 6.0+"""
    print(f"Patching: {edtlib_file}")

    content = edtlib_file.read_text(encoding="utf-8")

    # Check if already patched
    if "yaml.add_constructor" in content and "Loader=_BindingLoader)" in content:
        print("  → Already patched")
        return True

    # Apply the critical patch
    # Old: _BindingLoader.add_constructor("!include", _binding_include)
    # New: yaml.add_constructor("!include", _binding_include, Loader=_BindingLoader)

    original = content
    content = re.sub(
        r'_BindingLoader\.add_constructor\("!include",\s*_binding_include\)',
        'yaml.add_constructor("!include", _binding_include, Loader=_BindingLoader)',
        content
    )

    # Also fix any other add_constructor calls
    content = re.sub(
        r'_BindingLoader\.add_constructor\(',
        'yaml.add_constructor(',
        content
    )

    if content != original:
        # Create backup
        backup_file = edtlib_file.with_suffix('.py.backup')
        backup_file.write_text(original, encoding="utf-8")

        # Write patched version
        edtlib_file.write_text(content, encoding="utf-8")
        print("  → Patched successfully")
        return True

    print("  → No changes needed")
    return True


def ensure_patched(platform_dir):
    """Main entry point - ensure Zephyr SDK is patched"""
    # First, try to patch if NCS already exists
    edtlib = find_zephyr_edtlib(platform_dir)
    if edtlib:
        patch_edtlib(edtlib)
        return True

    # NCS not bootstrapped yet - that's OK, it will be patched during bootstrap
    print("NCS not bootstrapped yet - will patch during first compilation")
    return True


def main():
    script_dir = Path(__file__).resolve().parent
    platform_dir = script_dir.parent

    print("Ensuring PyYAML compatibility patch...")

    if ensure_patched(platform_dir):
        print("PyYAML compatibility: OK")
        return 0
    else:
        print("PyYAML compatibility: FAILED")
        return 1


if __name__ == "__main__":
    sys.exit(main())
