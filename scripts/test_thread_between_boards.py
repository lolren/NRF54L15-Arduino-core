#!/usr/bin/env python3
"""Compatibility entry point for the maintained Thread UDP soak runner.

This historical script used fixed ports, one hard-coded nRF54L15 FQBN, and a
UART bridge that is no longer part of the supported two-board test path.  Its
arguments are now forwarded to ``test_thread_udp_soak.py``.
"""

from __future__ import annotations

import sys

from test_thread_udp_soak import main


if __name__ == "__main__":
    print(
        "test_thread_between_boards.py is deprecated; "
        "using test_thread_udp_soak.py",
        file=sys.stderr,
    )
    sys.exit(main())
