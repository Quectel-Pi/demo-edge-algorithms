#!/bin/bash
# build.sh — 板端编译（原生 aarch64）
# 用法: bash build.sh   （算法包路径已固定为板端实际目录，无需传参）
# 并行度：默认单进程(-j1)，适配低内存板卡(RK 平台 4GB 无 swap)；内存充裕用 export BUILD_JOBS=4 覆盖
set -e
cd "$(dirname "$0")/.."

SDK_ROOT="${AECANS_SDK_ROOT:-/home/q/aec_ans_project/AIVOXAEC_ANS_V1.1.0.2.1_SDK_16k_10ms_Exp009_dewatermark_encrypt}"
JOBS="${BUILD_JOBS:-1}"

# 注意：这里不创建 output/。build.sh 常以 root 运行，若在此建目录会变成 root 属主，
# 之后普通用户运行 run.sh 就写不进去（errno=13）。目录由 run.sh 以当前用户创建。

cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DAECANS_SDK_ROOT="$SDK_ROOT"

cmake --build build --target ans_demo -j "$JOBS"

echo "[OK] 编译完成: $(pwd)/build/ans_demo"
echo "     运行:  bash scripts/run.sh"
