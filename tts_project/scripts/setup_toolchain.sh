#!/bin/bash

set -e

apt-get update

apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    libasound2-dev \
    alsa-utils

if [ "$SKIP_GUI" != "1" ]; then
    apt-get install -y \
        qt6-base-dev \
        libgl1-mesa-dev
fi

echo "[OK] 工具链安装完成。"
echo "     验证: g++ --version; cmake --version; ninja --version; pkg-config --modversion Qt6Widgets alsa"
