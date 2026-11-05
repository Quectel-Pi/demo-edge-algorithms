#!/bin/bash
set -e
cd "$(dirname "$0")/.."

AIVOX_TTS_HOME="${AIVOX_TTS_HOME:-$(pwd)/AIVOX-TTS-ZH-LITE-V2.0-30k-vc}"
JOBS="${BUILD_JOBS:-1}"

cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DAIVOX_TTS_HOME="$AIVOX_TTS_HOME"

cmake --build build --target tts_qt_demo -j "$JOBS"

echo "[OK] 编译完成: $(pwd)/build/tts_qt_demo"
echo "     运行:  bash scripts/run.sh"
