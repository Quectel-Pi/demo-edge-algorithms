#!/bin/bash
# setup_toolchain.sh — Debian/Ubuntu (aarch64) 板端工具链一键安装
# 用法: sudo bash scripts/setup_toolchain.sh
# 说明：SKIP_GUI=1 时不装 Qt（仅命令行/headless 调试用）。

set -e

# ------- 编译工具链 -------
# 1) build-essential : g++ 编译器与 make、libc 头文件
# 2) cmake          : CMake 构建系统（本项目构建入口）
# 3) ninja-build    : 更快的小巧构建后端（build.sh 默认使用）
# 4) pkg-config     : 帮助 Qt 探测工具链
# 5) git            : （可选）拉取 sherpa-onnx 源码以补齐缺失头文件
# ------- GUI 依赖 -------
# 6) qt6-base-dev   : Qt6 核心(Widgets)开发库 —— 界面
# 7) libgl1-mesa-dev: 提供 EGL/GL 链接符号（Qt GUI 需要）
# ------- 音频采集 -------
# 8) libasound2-dev : ALSA 音频开发库 —— 板载麦克风采集（sherpa_onnx::Alsa 需要）

apt-get update

apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    git \
    libasound2-dev

if [ "$SKIP_GUI" != "1" ]; then
    apt-get install -y \
        qt6-base-dev \
        libgl1-mesa-dev
fi

echo "[OK] 工具链安装完成。"
echo "     验证: g++ --version; cmake --version; ninja --version; pkg-config --modversion Qt6Widgets alsa"