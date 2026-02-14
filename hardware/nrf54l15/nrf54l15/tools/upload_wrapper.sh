#!/bin/bash
# Upload wrapper for XIAO nRF54L15 (Clean Core)
# This script is called by Arduino IDE when uploading

# Get arguments
HEX_FILE="$1"
SERIAL_PORT="$2"

echo "Debug: HEX_FILE=$HEX_FILE"
echo "Debug: SERIAL_PORT=$SERIAL_PORT"

# Find the XIAO board's CMSIS-DAP probe
# Get the line containing XIAO nRF54L15
PROBE_LINE=$(pyocd list 2>/dev/null | grep -i "xiao.*nrf54")

if [ -z "$PROBE_LINE" ]; then
    echo "Available probes:"
    pyocd list
    echo "No XIAO nRF54L15 board found!"
    exit 1
fi

echo "Debug: Probe line: $PROBE_LINE"

# Extract Unique ID using a more robust method
# The format has multiple spaces between fields, so we parse specifically for the ID
PROBE_ID=$(echo "$PROBE_LINE" | grep -oE '[A-F0-9]{8}' | head -1)

if [ -z "$PROBE_ID" ]; then
    echo "Error: Could not extract probe ID from: $PROBE_LINE"
    exit 1
fi

echo "Uploading to XIAO nRF54L15..."
echo "Hex file: $HEX_FILE"
echo "Probe ID: $PROBE_ID"

# Upload using pyocd
if pyocd load -t nrf54l -u "$PROBE_ID" "$HEX_FILE" --format hex; then
    # Reset the board
    pyocd reset -t nrf54l -u "$PROBE_ID" 2>/dev/null
    echo "Upload complete!"
else
    echo "Upload failed!"
    exit 1
fi
