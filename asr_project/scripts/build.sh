#!/bin/bash
# build.sh — 板端编译（原生 aarch64）
# 用法: bash scripts/build.sh   （算法包路径已固定为板端实际目录，无需传参）
# 并行度：默认单进程(-j1)，适配低内存板卡(如 RK 平台 4GB 无 swap)，避免并发编译爆内存；
#         内存充裕时可用 export BUILD_JOBS=4 覆盖。
set -e
cd "$(dirname "$0")/.."

# 算法包已内嵌在本工程目录下（头文件已补齐），默认指向它；如需覆盖可 export SHERPA_ONNX_HOME
SHERPA_ONNX_HOME="${SHERPA_ONNX_HOME:-$(pwd)/sherpa-onnx-asr-conformer-dewatermark}"
JOBS="${BUILD_JOBS:-1}"

cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DSHERPA_ONNX_HOME="$SHERPA_ONNX_HOME"

cmake --build build --target asr_qt_demo -j "$JOBS"

echo "[OK] 编译完成: $(pwd)/build/asr_qt_demo"
echo "     运行:  bash scripts/run.sh"