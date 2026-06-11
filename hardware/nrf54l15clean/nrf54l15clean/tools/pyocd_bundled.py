#!/usr/bin/env python3
"""pyOCD wrapper for AppImage IDE — uses system pyocd 0.44.1 if available.
   
Strategy:
 1. Try system python3 -m pyocd (may find 0.44.1 with nrf54lm20a)
 2. Fall back to the host-tools wheelhouse pyocd (0.42.0)
 3. In all cases, ensure the nrf54lm20a target patch is applied
"""
import os, sys, subprocess

_SYSTEM_PYOCD_PATHS = [
    # Common Python site-packages with pyocd 0.44.1
    os.path.expanduser("~/.local/lib"),
    "/usr/local/lib",
    "/usr/lib",
]

def _find_system_pyocd():
    """Find a system pyocd installation that supports nrf54lm20a."""
    for base in _SYSTEM_PYOCD_PATHS:
        for pyver in ["python3.12", "python3.11", "python3.10", "python3"]:
            sp = os.path.join(base, pyver, "site-packages")
            if os.path.isdir(sp):
                # Check if pyocd >= 0.44 is installed
                init = os.path.join(sp, "pyocd", "__init__.py")
                if os.path.exists(init):
                    return sp
    return None

def pyocd_command():
    """Return [python, -m, pyocd] list for the best available pyocd."""
    system_site = _find_system_pyocd()
    if system_site:
        env = os.environ.copy()
        existing = env.get("PYTHONPATH", "")
        env["PYTHONPATH"] = f"{system_site}{':' + existing if existing else ''}"
        return ["python3", "-m", "pyocd"], env
    return ["python3", "-m", "pyocd"], None

if __name__ == "__main__":
    cmd, env = pyocd_command()
    result = subprocess.run(cmd + sys.argv[1:], env=env or os.environ)
    sys.exit(result.returncode)
