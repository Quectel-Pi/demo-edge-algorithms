#!/bin/bash
set -e
cd "$(dirname "$0")/.."

AIVOX_TTS_HOME="${AIVOX_TTS_HOME:-$(pwd)/AIVOX-TTS-ZH-LITE-V2.0-30k-vc}"
BIN="./build/tts_qt_demo"
[ -x "$BIN" ] || { echo "未找到 $BIN，请先运行 scripts/build.sh"; exit 1; }

export LD_LIBRARY_PATH="$AIVOX_TTS_HOME/lib:${LD_LIBRARY_PATH:-}"

export TTS_AUTH_CODE="${TTS_AUTH_CODE:-7F21A688-3C73-4E20-BA3B-CCD7273D41FF}"

export TTS_AUDIO_HW_RATE="${TTS_AUDIO_HW_RATE:-32000}"

echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
echo "授权: license 目录=${TTS_LICENSE_DIR:-/data}，授权码=${TTS_AUTH_CODE:+已设置}"
echo "音频: 设备=${TTS_AUDIO_DEV:-自动探测}，硬件采样率=${TTS_AUDIO_HW_RATE:-按设备报告}"
echo "启动 TTS demo，输入文本后点击 SPEAK 开始合成播放 ..."

exec "$BIN" "$@"
