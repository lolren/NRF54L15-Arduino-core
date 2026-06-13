#!/usr/bin/env python3
"""Detach CMSIS-DAP probe so SYSTEM OFF works after upload.
Resumes CPU (if halted after --no-reset flash) then disconnects probe."""
import sys, os, time

def detach(target, uid):
    for p in ['/home/lolren/.local/lib/python3.10/site-packages',
              '/home/lolren/pinokio/bin/miniconda/lib/python3.10/site-packages']:
        if os.path.isdir(p): sys.path.insert(0, p)
    try:
        from pyocd.core.helpers import ConnectHelper
        import logging; logging.disable(logging.CRITICAL)
        with ConnectHelper.session_with_chosen_probe(
            target_override=target, unique_id=uid or None, connect_mode='attach'
        ) as session:
            session.target.resume()  # Resume if halted after --no-reset
            time.sleep(0.5)  # Give CPU time to start
        return True
    except:
        return False

if __name__ == '__main__':
    target = sys.argv[1] if len(sys.argv) > 1 else 'nrf54l'
    uid = sys.argv[2] if len(sys.argv) > 2 else None
    sys.exit(0 if detach(target, uid) else 1)
