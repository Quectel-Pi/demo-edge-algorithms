#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
SDK="${1:-$HOME/RK_human_segmentation_NPU_v1.2}"
if [ ! -f "$SDK/include/human_segmentation.h" ]; then
    SDK="$(find "$SDK" -type d -path '*/RKNN-2.3.2-linux/rk3576/human_segmentation' -print -quit)"
fi
[ -n "$SDK" ] && [ -f "$SDK/lib/aarch64_Linux/libhuman_segmentation.so" ] || {
    echo 'RK3576 Linux SDK not found; pass the package root or its human_segmentation directory.' >&2
    exit 1
}
mkdir -p "$ROOT/include" "$ROOT/lib" "$ROOT/models" "$ROOT/images"
cp -f "$SDK/include/human_segmentation.h" "$ROOT/include/"
cp -R "$SDK/3rd_party/opencv_4.2.0/include/opencv2" "$ROOT/include/"
find "$SDK/lib/aarch64_Linux" "$SDK/3rd_party" -type f -name '*.so*' -exec cp -f '{}' "$ROOT/lib/" \;
cp -f "$SDK/models/sim.engine" "$SDK/models/model_seg.json" "$ROOT/models/"
find "$SDK/images" -maxdepth 1 -type f \( -iname '*.jpg' -o -iname '*.png' -o -iname '*.jpeg' -o -iname '*.bmp' \) -exec cp -f '{}' "$ROOT/images/" \;
if [ ! -f "$ROOT/images/input.jpg" ]; then
    sample="$(find "$ROOT/images" -maxdepth 1 -type f -iname '*.jpg' -print -quit)"
    if [ -n "$sample" ]; then cp -f "$sample" "$ROOT/images/input.jpg"; fi
fi
echo 'RK3576 SDK dependencies copied.'