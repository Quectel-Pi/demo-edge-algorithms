#!/bin/bash
# build.sh — 板端编译（原生 aarch64）
# 用法: bash build.sh          （算法包路径已固定为板端实际目录，无需传参）
# 并行度：默认单进程(-j1)，适配低内存板卡(如 RK 平台 4GB 无 swap)，避免并发编译被打爆内存；
#         内存充裕时可用 export BUILD_JOBS=4 覆盖。
set -e
cd "$(dirname "$0")/.."

# 算法包目录：默认取仓库根目录下的 PeoplePostureDet_RK3576_Ubuntu 子目录；
# 如需覆盖可 export POSTURE_SDK_ROOT=<新路径>
SDK_ROOT="${POSTURE_SDK_ROOT:-$PWD/PeoplePostureDet_RK3576_Ubuntu}"
JOBS="${BUILD_JOBS:-1}"

# 校验算法包路径：目录不存在/缺头文件时尽早报错，避免 configure 阶段失败难排查
[ -d "$SDK_ROOT" ] || { echo "[ERR] 算法包目录不存在: $SDK_ROOT"; \
    echo "      请放入算法包，或用 export POSTURE_SDK_ROOT=<路径> 指定"; exit 1; }
[ -f "$SDK_ROOT/include/PeopleKptDet.h" ] || { echo "[ERR] 未找到 $SDK_ROOT/include/PeopleKptDet.h"; exit 1; }
echo "[INFO] SDK_ROOT = $SDK_ROOT"

# build 目录权限自检：CMake 需要能写 build/CMakeFiles。若 build/ 已存在但当前用户
# 不可写（常见于用 sudo 或 root 生成后切回普通用户重编），提示后自动重建。
if [ -d build ]; then
    if [ ! -w build ] || [ ! -w build/CMakeFiles ]; then
        echo "[WARN] build/ 目录不可写（可能由 root/sudo 创建，属主不是当前用户）"
        if rm -rf build 2>/dev/null; then
            echo "[INFO] 已删除旧 build/，将重新创建可写的构建目录"
        else
            echo "[ERR] 无法删除 build/（可能含 root 属主文件）"; \
            echo "      请手动执行: sudo rm -rf build  再重跑 build.sh"; exit 1
        fi
    fi
fi

cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DPOSTURE_SDK_ROOT="$SDK_ROOT"

cmake --build build --target posture_demo -j "$JOBS"

echo "[OK] 编译完成: $(pwd)/build/posture_demo"
echo "     运行:  bash scripts/run.sh"