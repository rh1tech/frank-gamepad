#!/bin/bash
# Usage: ./build.sh [USB_HID_ENABLED]
#   ./build.sh           -> debug build: CDC stdio on native USB (no gamepad)
#   ./build.sh ON        -> release: USB Host active, UART stdio only
set -e

USB_HID="${1:-OFF}"

rm -rf ./build
mkdir build
cd build

cmake \
    -DPICO_PLATFORM=rp2350 \
    -DBOARD_VARIANT=M2 \
    -DUSB_HID_ENABLED=${USB_HID} \
    ..

make -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
