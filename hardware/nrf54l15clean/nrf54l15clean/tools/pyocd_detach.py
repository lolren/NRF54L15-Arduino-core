#!/usr/bin/env python3
"""Detach CMSIS-DAP probe so SYSTEM OFF works after upload."""
import sys, os, subprocess

def find_pyocd_python():
    """Find the python that has pyocd installed."""
    # Try to find pyocd in PATH and get its python
    try:
        result = subprocess.run(['which', 'pyocd'], capture_output=True, text=True, timeout=5)
        if result.returncode == 0:
            pyocd_path = result.stdout.strip()
            # pyocd is a script, check its shebang
            with open(pyocd_path) as f:
                shebang = f.readline().strip()
            if 'python' in shebang:
                return shebang.split()[0]
    except:
        pass
    # Fallback: try system python
    for p in ['/usr/bin/python3', '/usr/bin/python']:
        if os.path.isfile(p):
            return p
    return sys.executable

def detach(target, uid):
    try:
        import pyocd.core.helpers as helpers
        import logging
        logging.disable(logging.CRITICAL)
        with helpers.ConnectHelper.session_with_chosen_probe(
            target_override=target, unique_id=uid or None, connect_mode='attach'
        ) as session:
            session.target.halt()
            session.target.resume()
        return True
    except:
        return False

if __name__ == '__main__':
    target = sys.argv[1] if len(sys.argv) > 1 else 'nrf54l'
    uid = sys.argv[2] if len(sys.argv) > 2 else None
    sys.exit(0 if detach(target, uid) else 1)
