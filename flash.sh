#!/bin/bash
# Flash frank-gamepad to a connected Pico 2 via picotool.
set -e

FIRMWARE="${1:-./build/frank-gamepad.uf2}"

if [ ! -f "$FIRMWARE" ]; then
    echo "Error: $FIRMWARE not found. Run ./build.sh first." >&2
    exit 1
fi

echo "Flashing: $FIRMWARE"
picotool load -f "$FIRMWARE" && picotool reboot -f
