#!/bin/bash
# run.sh — 一键运行脚本
# 用法: bash scripts/run.sh   （算法包路径已固定，摄像头自动检索，无需传参）
# 职责：
#   1) 组装 SDK 运行期 LD_LIBRARY_PATH（libPeopleKptDet 及其自带 OpenCV/RNN 等）
#   2) 注入算法授权码与 license 落点
#   3) 设置模型路径，运行编译产物 posture_demo（唯一可执行文件）
set -e
cd "$(dirname "$0")/.."

# ---- 配置 ----
# 算法包目录：默认取仓库根目录下的 PeoplePostureDet_RK3576_Ubuntu 子目录；
# 如需覆盖可 export POSTURE_SDK_ROOT=<新路径>（会连带影响模型/授权路径）
SDK_ROOT="${POSTURE_SDK_ROOT:-$PWD/PeoplePostureDet_RK3576_Ubuntu}"
# 授权码：必须通过环境变量 PEOPLEKPT_AUTH_CODE 注入，不写入脚本，防止提交到仓库泄露
if [ -z "${PEOPLEKPT_AUTH_CODE:-}" ]; then
    echo "[ERR] 未设置授权码 PEOPLEKPT_AUTH_CODE"
    echo "      请先执行: export PEOPLEKPT_AUTH_CODE=<您的授权码>  再运行本脚本"
    exit 1
fi
AUTH_CODE="$PEOPLEKPT_AUTH_CODE"
# 模型路径 ：
MODEL="${POSTURE_MODEL:-$SDK_ROOT/models/RK3576_Ubuntu_peoplekpt.engine}"
# license 落点：
LICENSE_FILE="${PPD_LICENSE_FILE:-$SDK_ROOT/authorization.lic}"

# 尽早校验关键路径，路径被覆盖后也能立刻知道是否写错
[ -d "$SDK_ROOT" ] && [ -f "$MODEL" ] || { echo "[ERR] SDK 或模型缺失"; \
    echo "      SDK_ROOT=$SDK_ROOT"; echo "      MODEL=$MODEL"; \
    echo "      可用 export POSTURE_SDK_ROOT=<路径> 重定向"; exit 1; }

BIN="./build/posture_demo"
[ -x "$BIN" ] || { echo "未找到 $BIN，请先运行 scripts/build.sh"; exit 1; }

# 运行期库路径：算法包自带单库 OpenCV(libopencv_world.so)，经 libPeopleKptDet.so 依赖，
# 必须在路径里；同时系统 OpenCV(编译期用)也要在。
export LD_LIBRARY_PATH="$SDK_ROOT/lib:$SDK_ROOT/3rd_party/modelinfer:$SDK_ROOT/3rd_party/rknn:$SDK_ROOT/3rd_party/jsoncpp/lib:$SDK_ROOT/3rd_party/opencv/lib:${LD_LIBRARY_PATH:-}"
export PEOPLEKPT_AUTH_CODE="$AUTH_CODE"
export PPD_LICENSE_FILE="$LICENSE_FILE"
# 供 main 读取：不设 CAMERA_DEVICE → 程序内自动检索第一个可用摄像头；
# 传了则用指定设备号。注意：这里不能 unset，否则命令行/环境传入的设备号被吞掉。
export POSTURE_MODEL="$MODEL"

echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
echo "MODEL=$MODEL  AUTH=set  LICENSE=$LICENSE_FILE"
echo "启动坐姿检测 demo，摄像头自动检索 ..."

exec "$BIN"