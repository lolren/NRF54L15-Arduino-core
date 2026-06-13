#!/usr/bin/env python3
"""Detach CMSIS-DAP probe so SYSTEM OFF works after upload."""
import sys, os, subprocess

def find_pyocd_cmd():
    """Find pyocd command path."""
    try:
        r = subprocess.run(['which', 'pyocd'], capture_output=True, text=True, timeout=5)
        if r.returncode == 0: return r.stdout.strip()
    except: pass
    for p in ['/home/lolren/.local/bin/pyocd', '/home/lolren/pinokio/bin/miniconda/bin/pyocd']:
        if os.path.isfile(p): return p
    return None

def detach(target, uid):
    pyocd = find_pyocd_cmd()
    if not pyocd: return False
    cmd = [pyocd, 'commander', '-W', '-t', target]
    if uid: cmd.extend(['-u', uid])
    cmd.extend(['-c', 'resume'])
    try:
        subprocess.run(cmd, timeout=5.0, capture_output=True)
        return True
    except:
        return False

if __name__ == '__main__':
    target = sys.argv[1] if len(sys.argv) > 1 else 'nrf54l'
    uid = sys.argv[2] if len(sys.argv) > 2 else None
    sys.exit(0 if detach(target, uid) else 1)
