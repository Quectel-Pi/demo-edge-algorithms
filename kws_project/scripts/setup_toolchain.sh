#!/bin/bash
# setup_toolchain.sh — install build dependencies on the Quectel PI M2 (Debian).
set -e
echo "==> Installing toolchain for kws_demo"
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    ninja-build \
    qt6-base-dev \
    qt6-base-dev-tools \
    libasound2-dev \
    alsa-utils

echo ""
echo "Done. Check versions:"
cmake --version | head -1
qmake6 --version 2>/dev/null || true
echo "Next: bash scripts/build.sh"
