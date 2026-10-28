#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
SDK="${1:-$HOME/RK_OCR_NPU_1.2}"
# Accept the whole RK_OCR_NPU SDK package or a deep_ocr directory; resolve the RK3576 Linux deep_ocr.
if [ -f "$SDK/include/deep_ocr.h" ] && [ -f "$SDK/models/sim.engine" ]; then
    DEEP_OCR="$SDK"
else
    DEEP_OCR="$(find "$SDK" -type d -path '*RKNN-2.3.2-linux/rk3576/deep_ocr' 2>/dev/null | head -n 1 || true)"
fi
[ -n "${DEEP_OCR:-}" ] && [ -f "$DEEP_OCR/include/deep_ocr.h" ] && [ -f "$DEEP_OCR/models/sim.engine" ] || {
    echo "Usage: sh setup_from_sdk.sh <RK_OCR_NPU_1.2 package or RK3576 Linux deep_ocr directory>" >&2
    exit 1
}
mkdir -p "$ROOT/include" "$ROOT/lib" "$ROOT/models" "$ROOT/images"
cp "$DEEP_OCR/include/deep_ocr.h" "$ROOT/include/"
cp -R "$DEEP_OCR/3rd_party/opencv_4.2.0/include/opencv2" "$ROOT/include/"
for directory in "$DEEP_OCR/lib" "$DEEP_OCR/3rd_party"; do
    find "$directory" -name '*.so*' -exec cp -P {} "$ROOT/lib/" \;
done
cp "$DEEP_OCR/models/sim.engine" "$ROOT/models/"
cp "$DEEP_OCR/models/model_ocr.json" "$ROOT/models/"
cp "$DEEP_OCR/images/"*.png "$ROOT/images/"
echo "RK3576 OCR dependencies copied from $DEEP_OCR to $ROOT"