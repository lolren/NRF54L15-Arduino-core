#!/bin/bash
# Quick upload script for XIAO nRF54L15 (Clean Core)
# Usage: ./upload.sh <sketch.ino>

SKETCH="${1:-examples/01.Basics/Blink/Blink.ino}"
BOARD="nrf54l15:nrf54l15:xiao_nrf54l15"
BUILD_DIR="/tmp/xiao_upload_$$"

echo "Building $SKETCH..."
arduino-cli compile -b "$BOARD" --build-path "$BUILD_DIR" \
    "/home/lolren/Desktop/Xiaonrf54l15/Nrf54L15_clean/hardware/nrf54l15/$SKETCH"

if [ $? -eq 0 ]; then
    echo "Uploading to XIAO nRF54L15..."
    pyocd load -t nrf54l -u E91217E8 "$BUILD_DIR/Blink.ino.hex" --format hex
    echo "Resetting board..."
    pyocd reset -t nrf54l -u E91217E8
    echo "Done!"
fi

rm -rf "$BUILD_DIR"
