#!/usr/bin/env python3
"""Compatibility entry point for the canonical source-local example matrix."""

import os
import sys
from pathlib import Path


runner = Path(__file__).with_name("build_all_examples.py")
os.execv(sys.executable, [sys.executable, str(runner), *sys.argv[1:]])
