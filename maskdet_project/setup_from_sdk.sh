#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
SDK="${1:-$HOME/RK_MD_NPU_v1.4}"
# Accept the whole RK_MD_NPU package or its rknn-2.3.2-linux directory.
if [ -f "$SDK/include/MaskEngine.h" ] && [ -f "$SDK/lib/libmaskdet.so" ]; then
    MASKDET="$SDK"
else
    MASKDET="$(find "$SDK" -type d -name 'rknn-2.3.2-linux' 2>/dev/null | head -n 1 || true)"
fi
MODEL="${MASKDET:-}/model/rknn/RK3576/mask_q_3576_enc.rknn"
[ -n "${MASKDET:-}" ] && [ -f "$MASKDET/include/MaskEngine.h" ] && [ -f "$MODEL" ] || {
    echo "Usage: sh setup_from_sdk.sh <RK_MD_NPU_v1.4 package or its rknn-2.3.2-linux directory>" >&2
    exit 1
}
mkdir -p "$ROOT/include" "$ROOT/lib" "$ROOT/models" "$ROOT/images"
cp "$MASKDET/include/MaskEngine.h" "$ROOT/include/"
cp -R "$MASKDET/include/opencv2" "$ROOT/include/"
find "$MASKDET/lib" -name '*.so*' -exec cp -P {} "$ROOT/lib/" \;
cp "$MODEL" "$ROOT/models/"
cp "$MASKDET/image/"*.jpg "$ROOT/images/"
echo "RK3576 mask detection dependencies copied from $MASKDET to $ROOT"
