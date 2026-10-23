#!/bin/sh
set -eu
ROOT="$(cd "$(dirname "$0")" && pwd)"
SDK="${1:-$HOME/facial_attributes_detection_RK3576}"
[ -f "$SDK/lib/libfacial_attributes_detection.so" ] || {
    echo "Usage: sh setup_from_sdk.sh <RK3576 Linux facial attributes SDK directory>" >&2
    exit 1
}
mkdir -p "$ROOT/include" "$ROOT/lib" "$ROOT/models" "$ROOT/images"
cp "$SDK/include/facial_attributes_detection.h" "$ROOT/include/"
cp -R "$SDK/3rd_party/opencv/include/opencv2" "$ROOT/include/"
for directory in "$SDK/lib" "$SDK/3rd_party/modelinfer" "$SDK/3rd_party/rknn" "$SDK/3rd_party/opencv/lib"; do
    find "$directory" -maxdepth 1 -name '*.so*' -exec cp -P {} "$ROOT/lib/" \;
done
cp "$SDK/models/RK3576_Ubuntu_widerface_last.engine" "$ROOT/models/"
cp "$SDK/models/RK3576_Ubuntu_FER_best.engine" "$ROOT/models/"
cp "$SDK/images/input.jpg" "$ROOT/images/"
echo "RK3576 dependencies copied to $ROOT"