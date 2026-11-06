#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
SDK="${1:-$HOME/RK_HD_NPU_v1.4}"
# Accept the whole RK_HD_NPU package or its rknn-2.3.2-linux directory.
if [ -f "$SDK/include/HelmetEngine.h" ] && [ -f "$SDK/lib/libhelmetdet.so" ]; then
    HELMET="$SDK"
else
    HELMET="$(find "$SDK" -type d -name 'rknn-2.3.2-linux' 2>/dev/null | head -n 1 || true)"
fi
MODEL="${HELMET:-}/model/rknn/RK3576/helmet_q_3576_enc.rknn"
[ -n "${HELMET:-}" ] && [ -f "$HELMET/include/HelmetEngine.h" ] && [ -f "$MODEL" ] || {
    echo "Usage: sh setup_from_sdk.sh <RK_HD_NPU_v1.4 package or its rknn-2.3.2-linux directory>" >&2
    exit 1
}
mkdir -p "$ROOT/include" "$ROOT/lib" "$ROOT/models" "$ROOT/images"
cp "$HELMET/include/HelmetEngine.h" "$ROOT/include/"
cp -R "$HELMET/include/opencv2" "$ROOT/include/"
find "$HELMET/lib" -name '*.so*' -exec cp -P {} "$ROOT/lib/" \;
cp "$MODEL" "$ROOT/models/"
cp "$HELMET/image/"*.jpg "$ROOT/images/"
echo "RK3576 helmet detection dependencies copied from $HELMET to $ROOT"
