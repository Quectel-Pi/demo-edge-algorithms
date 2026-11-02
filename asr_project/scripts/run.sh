#!/bin/bash
# run.sh — 一键运行脚本
# 用法: bash scripts/run.sh   （算法包路径已固定，模型路径用程序默认值，无需传参）
#
# 职责：
#   1) 组装算法包运行期 LD_LIBRARY_PATH（libsherpa-onnx-cxx-api / libonnxruntime 等）
#   2) 运行编译产物 asr_qt_demo（唯一可执行文件），额外参数透传给程序
set -e
cd "$(dirname "$0")/.."

# ---- 配置（算法包已内嵌在本工程目录下；如需覆盖可 export SHERPA_ONNX_HOME）----
SHERPA_ONNX_HOME="${SHERPA_ONNX_HOME:-$(pwd)/sherpa-onnx-asr-conformer-dewatermark}"
ARCH="${ARCH:-linux_arm_v8}"

BIN="./build/asr_qt_demo"
[ -x "$BIN" ] || { echo "未找到 $BIN，请先运行 scripts/build.sh"; exit 1; }

# 运行期库路径：算法包自带的 sherpa-onnx / onnxruntime 动态库
export LD_LIBRARY_PATH="$SHERPA_ONNX_HOME/lib/$ARCH:${LD_LIBRARY_PATH:-}"

echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
echo "启动实时语音识别 demo，音频输入开关默认关闭，点击 Start 开始识别 ..."

exec "$BIN" "$@"