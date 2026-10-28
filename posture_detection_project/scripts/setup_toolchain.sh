#!/bin/bash
# setup_toolchain.sh — Debian 13 (aarch64) 板端工具链一键安装
# 用法: sudo bash setup_toolchain.sh
# 说明：以非中文注释列出每项用途；SKIP_GUI=1 时不装 Qt（仅命令行/headless 调试用）。

set -e

# ------- 编译工具链 -------
# 1) build-essential : g++ 编译器与 make、libc 头文件
# 2) cmake          : CMake 构建系统（本项目构建入口）
# 3) ninja-build    : 更快的小巧构建后端（可选，也可用 make）
# 4) pkg-config     : 帮助 Qt/OpenCV 探测工具链
# 5) git            : （可选）拉取/同步源码
# ------- 运行/开发依赖 -------
# 6) qt6-base-dev   : Qt6 核心(Widgets)开发库 —— 界面
# 7) libgl1-mesa-dev: 提供 EGL/GL 链接符号（Qt GUI 需要）
# 8) libopencv-dev  : 系统 OpenCV(含 videoio) —— USB 摄像头采集
# 9) 算法包自带 OpenCV/RNN 运行期依赖（libinference/libPeopleKptDet 需要它们即可加载）
#    libjpeg62-turbo libpng16-16 libtiff6 libwebp7 libgdal32 libgdal-dev libgomp1
#    注：具体版本名随 Debian 13 发布演进，缺哪个会以"共享库不存在"报错，用 dpkg -l 核对。

apt-get update

# ---- 编译工具链 ----
apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    git

# ---- 界面/采集 与 算法包运行期依赖 ----
# 说明：算法包 libPeopleKptDet.so 自带 OpenCV(.so)，但仍依赖系统若干图像格式/数学库。
#       若个别包在目标 Debian 版本中不存在，会以"N 有未满足的依赖关系"报错，可删除对应行，
#       或用 dpkg -l 核对实际可用版本名再安装。
if [ "$SKIP_GUI" != "1" ]; then
    apt-get install -y \
        qt6-base-dev \
        libgl1-mesa-dev \
        libopencv-dev

    apt-get install -y \
        libjpeg62-turbo \
        libpng16-16 \
        libtiff6 \
        libwebp7 \
        libgdal32 \
        libgdal-dev \
        libgomp1 || echo "[WARN] 部分运行期库安装失败，可跳过或用 dpkg -l 核对后手动补装"

    # 常见 Qt 运行期依赖：字体渲染 / XKB / GL 运行库（libopencv-dev 已带，此处补齐 GUI 所需）
    apt-get install -y \
        libfontconfig1 \
        fontconfig \
        libxkbcommon0 \
        libgl1 || echo "[WARN] 部分 Qt 运行期依赖安装失败，可按需手动补装"
fi

echo "[OK] 工具链安装完成。"
echo "     验证: g++ --version; cmake --version; pkg-config --modversion Qt6Widgets; pkg-config --modversion opencv4"