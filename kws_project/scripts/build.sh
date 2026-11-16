#!/bin/bash
# build.sh — configure and build the demo.
set -e
cd "$(dirname "$0")/.."

SDK_ROOT="${KWS_SDK_ROOT:-$(pwd)/aivox-scr-xiaoyuantongxue-zipformer-960k-zh/SDK}"

if [ ! -f "$SDK_ROOT/include/aivox_ovkws.h" ]; then
    echo "[ERROR] AIVOX KWS SDK not found at: $SDK_ROOT"
    echo "        Copy the algorithm package into the project root:"
    echo "        aivox-scr-xiaoyuantongxue-zipformer-960k-zh/SDK"
    echo "        Or pass: KWS_SDK_ROOT=/path/to/SDK bash scripts/build.sh"
    exit 1
fi

if [ ! -f "$SDK_ROOT/lib/linux_arm_v8/libaivox_ovkws.so" ]; then
    echo "[ERROR] Missing authorized ARM v8 library in $SDK_ROOT/lib/linux_arm_v8"
    exit 1
fi

cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DKWS_SDK_ROOT="$SDK_ROOT"
cmake --build build -j"$(nproc)"

echo ""
echo "Build finished: ./build/kws_demo"
echo "Run: bash scripts/run.sh"
