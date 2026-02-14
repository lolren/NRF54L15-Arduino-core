#!/usr/bin/env python3
"""
Patch Zephyr's edtlib.py to work with PyYAML 6.0+

This script patches the edtlib.py file in the NCS workspace to use
yaml.add_constructor() instead of _BindingLoader.add_constructor(),
which was removed in PyYAML 6.0+.
"""

import os
import re
import sys
from pathlib import Path


def patch_edtlib(edtlib_file: Path) -> bool:
    """Patch edtlib.py to use yaml.add_constructor instead of _BindingLoader.add_constructor"""
    if not edtlib_file.exists():
        print(f"edtlib.py not found at: {edtlib_file}")
        return False

    content = edtlib_file.read_text(encoding="utf-8")
    original_content = content

    # Replace _BindingLoader.add_constructor with yaml.add_constructor
    # Pattern: _BindingLoader.add_constructor("!include", _binding_include)
    # Replacement: yaml.add_constructor("!include", _binding_include, Loader=_BindingLoader)

    # Fix the line that's causing the error
    content = re.sub(
        r'_BindingLoader\.add_constructor\("!include",\s*_binding_include\)',
        'yaml.add_constructor("!include", _binding_include, Loader=_BindingLoader)',
        content
    )

    # Also fix any other potential _BindingLoader.add_constructor calls
    content = re.sub(
        r'_BindingLoader\.add_constructor\(',
        'yaml.add_constructor(',
        content
    )

    # If no changes were made, the file might already be patched
    if content == original_content:
        print("edtlib.py already patched or uses different API")
        return True

    # Write the patched content
    edtlib_file.write_text(content, encoding="utf-8")
    print(f"Patched: {edtlib_file}")
    return True


def main():
    # Find the NCS directory
    script_dir = Path(__file__).resolve().parent
    platform_dir = script_dir.parent
    ncs_dir = platform_dir / "tools" / "ncs"

    edtlib_path = ncs_dir / "zephyr" / "scripts" / "dts" / "python-devicetree" / "src" / "devicetree" / "edtlib.py"

    if not edtlib_path.exists():
        # If NCS hasn't been bootstrapped yet, create a marker to patch later
        marker = ncs_dir / ".patch_pyyaml_marker"
        marker.parent.mkdir(parents=True, exist_ok=True)
        marker.write_text("1")
        print(f"NCS not bootstrapped yet, created marker: {marker}")
        return 0

    if patch_edtlib(edtlib_path):
        print("PyYAML compatibility patch applied successfully")
        return 0
    else:
        print("Failed to patch edtlib.py")
        return 1


if __name__ == "__main__":
    sys.exit(main())
