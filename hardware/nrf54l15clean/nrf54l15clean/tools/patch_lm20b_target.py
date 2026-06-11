"""Standalone: installs nRF54LM20A target into pyOCD. Called at import time."""
import os, shutil, glob
_here = os.path.dirname(os.path.abspath(__file__))
_bundled = os.path.join(_here, "target_nRF54LM20A.py")
if not os.path.exists(_bundled):
    raise SystemExit(0)

_targets = []

# Shim pyOCD (AppImage IDE, fresh installs)
_shim = os.path.normpath(os.path.join(_here, "..", "..", "..", "tools",
    "nrf54l15hosttools", "1.1.2", "runtime", "pyocd-site", "pyocd", "target", "builtin"))
if os.path.isdir(os.path.dirname(_shim)):
    _targets.append(_shim)

# System pyOCD
try:
    import pyocd.target.builtin
    _targets.append(os.path.dirname(pyocd.target.builtin.__file__))
except ImportError:
    pass

for _d in _targets:
    try:
        os.makedirs(_d, exist_ok=True)
        shutil.copy2(_bundled, os.path.join(_d, "target_nRF54LM20A.py"))
        _init = os.path.join(_d, "__init__.py")
        if os.path.exists(_init):
            _c = open(_init).read()
            if "target_nRF54LM20A" not in _c:
                _c = _c.replace("from . import target_nRF54L15",
                    "from . import target_nRF54L15\nfrom . import target_nRF54LM20A")
                _c = _c.replace("'nrf54l' : target_nRF54L15.NRF54L15,",
                    "'nrf54l' : target_nRF54L15.NRF54L15,\n          'nrf54lm20a' : target_nRF54LM20A.NRF54LM20A,")
                open(_init, "w").write(_c)
        for _p in glob.glob(os.path.join(_d, "__pycache__")):
            shutil.rmtree(_p, ignore_errors=True)
    except Exception:
        pass
