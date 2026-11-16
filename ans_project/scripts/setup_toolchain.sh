#!/bin/bash
# setup_toolchain.sh — Debian 13 (aarch64) 板端工具链一键安装（音频降噪 demo 用）
# 用法: sudo bash setup_toolchain.sh
#       sudo SKIP_GUI=1 bash setup_toolchain.sh   # 跳过 Qt（仅 headless 调试）
#
# 说明：本脚本按「全新最小系统」设计，缺什么补什么；依赖按功能分组安装，
#       可选运行期库逐个尝试，个别包在源里不存在时仅告警而不中断整体流程。
#       注意：apt-get 续行内不能插入注释，否则后续行会被当成独立命令执行。
set -e

if [ "$(id -u)" -ne 0 ]; then
    echo "[ERROR] 需要 root 权限：sudo bash scripts/setup_toolchain.sh"
    exit 1
fi

export DEBIAN_FRONTEND=noninteractive

# ---------------------------------------------------------------------------
# 可选依赖：逐个安装，缺失/不可用只告警，不影响整体流程
# ---------------------------------------------------------------------------
install_optional() {
    for pkg in "$@"; do
        if dpkg -s "$pkg" >/dev/null 2>&1; then
            echo "  [OK]   $pkg (已安装)"
            continue
        fi
        if apt-get install -y "$pkg" >/dev/null 2>&1; then
            echo "  [OK]   $pkg"
        else
            echo "  [SKIP] $pkg (当前软件源不可用，忽略)"
        fi
    done
}

echo "==> 1/4 更新软件包索引"
apt-get update || echo "  [WARN] apt-get update 失败（网络受限/需门户认证？），继续尝试安装"

echo "==> 2/4 安装编译工具链"
apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    git \
    ca-certificates

echo "==> 3/4 安装音频采集依赖 (ALSA)"
# libasound2-dev 已依赖 libasound2 运行库，无需单独安装
apt-get install -y \
    libasound2-dev \
    alsa-utils

echo "==> 4/4 安装 Qt6 GUI 依赖"
if [ "${SKIP_GUI:-0}" != "1" ]; then
    # 编译期必需：Qt6 Widgets 开发包 + OpenGL 头文件
    apt-get install -y \
        qt6-base-dev \
        qt6-base-dev-tools \
        libgl1-mesa-dev

    # 运行期可选库：xcb 平台插件、eglfs/linuxfb、图形驱动、字体等
    install_optional \
        qt6-qpa-plugins \
        libgl1 \
        libegl1 \
        libgles2 \
        libgbm1 \
        libdrm2 \
        libglib2.0-0 \
        libxkbcommon-x11-0 \
        libxcb-cursor0 \
        libxcb-icccm4 \
        libxcb-image0 \
        libxcb-keysyms1 \
        libxcb-render-util0 \
        libxcb-xinerama0 \
        libxcb-shape0 \
        libxcb-xfixes0 \
        libxcb-randr0 \
        libxcb-xkb1 \
        libfontconfig1 \
        fonts-dejavu-core
else
    echo "  [SKIP] 已设置 SKIP_GUI=1，跳过 Qt6 安装"
fi

echo
echo "==> 校验关键工具"
for cmd in g++ cmake ninja pkg-config git; do
    if command -v "$cmd" >/dev/null 2>&1; then
        echo "  [OK]   $cmd"
    else
        echo "  [FAIL] $cmd 未安装"
    fi
done

if [ "${SKIP_GUI:-0}" != "1" ]; then
    for m in Qt6Widgets alsa; do
        if pkg-config --exists "$m"; then
            echo "  [OK]   $m $(pkg-config --modversion "$m")"
        else
            echo "  [FAIL] $m 未找到 (.pc)"
        fi
    done
fi

echo
echo "[OK] 工具链安装完成。"
echo "     下一步: bash scripts/build.sh && bash scripts/run.sh"
echo "     音频设备: aplay -l / arecord -l"
