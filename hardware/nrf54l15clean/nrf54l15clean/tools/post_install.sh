#!/bin/bash
# Ensure nrf_ocd binary is executable
if [ -f "$(dirname "$0")/nrf_ocd" ]; then
    chmod +x "$(dirname "$0")/nrf_ocd"
fi

# Post-install: symlink shim pyocd to system pyocd if available
SYS_PYOCD=$(python3 -c "import site; print(site.getsitepackages()[0])" 2>/dev/null)
if [ -n "$SYS_PYOCD" ]; then
    RUNTIME=$(dirname "$(dirname "$0")")/../../../tools/nrf54l15hosttools/*/runtime
    for d in $RUNTIME; do
        [ -d "$d" ] && rm -rf "$d/pyocd-site" && ln -sf "$SYS_PYOCD" "$d/pyocd-site" 2>/dev/null
    done
fi
