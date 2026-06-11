#!/usr/bin/env python3
"""pyOCD wrapper — ensures nrf54lm20a target is always available.

For AppImage IDE: prepends system pyocd 0.44.1 site-packages to PYTHONPATH.
For standard installs: just runs pyocd -m directly.
"""
import os, sys, subprocess, glob

def _find_pyocd_044():
    """Find a pyocd >= 0.44 installation with nrf54lm20a support."""
    search_paths = []
    
    # Common site-packages paths
    for base in [
        os.path.expanduser("~/.local/lib"),
        os.path.expanduser("~/.local/share"),
        "/usr/local/lib",
        "/usr/lib",
        "/opt/homebrew/lib",  # macOS Homebrew
    ]:
        for pattern in [
            "python3.13/site-packages",
            "python3.12/site-packages", 
            "python3.11/site-packages",
            "python3.10/site-packages",
            "python3.9/site-packages",
            "python3/site-packages",
        ]:
            p = os.path.join(base, pattern)
            if os.path.isdir(p):
                search_paths.append(p)
    
    # Also check conda
    for conda_root in [
        os.path.expanduser("~/miniconda3"),
        os.path.expanduser("~/anaconda3"),
        os.path.expanduser("~/pinokio/bin/miniconda"),
        "/opt/conda",
    ]:
        p = os.path.join(conda_root, "lib")
        for py in glob.glob(os.path.join(p, "python3.*")):
            sp = os.path.join(py, "site-packages")
            if os.path.isdir(sp):
                search_paths.append(sp)
    
    for sp in search_paths:
        init = os.path.join(sp, "pyocd", "__init__.py")
        if not os.path.exists(init):
            continue
        # Check for nrf54l target support (>=0.44)
        builtin = os.path.join(sp, "pyocd", "target", "builtin", "target_nRF54L15.py")
        if os.path.exists(builtin):
            return sp
    
    return None

def pyocd_command():
    """Return ([cmd], env_dict) for the best pyocd available."""
    system_site = _find_pyocd_044()
    
    if system_site:
        env = os.environ.copy()
        existing = env.get("PYTHONPATH", "")
        env["PYTHONPATH"] = f"{system_site}{':' + existing if existing else ''}"
        # Try python3 from PATH first, then sys.executable
        for py in ["python3", sys.executable]:
            try:
                result = subprocess.run(
                    [py, "-c", "import pyocd; print(pyocd.__version__)"],
                    capture_output=True, text=True, timeout=5, env=env
                )
                if result.returncode == 0:
                    version = result.stdout.strip()
                    if version >= "0.44":
                        return [py, "-m", "pyocd"], env
            except Exception:
                continue
    
    # Fallback: use sys.executable with -m pyocd
    return [sys.executable, "-m", "pyocd"], None

if __name__ == "__main__":
    cmd, env = pyocd_command()
    full_cmd = cmd + sys.argv[1:]
    result = subprocess.run(full_cmd, env=env or os.environ)
    sys.exit(result.returncode)
