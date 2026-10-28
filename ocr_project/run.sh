#!/bin/sh
# Run the desktop OCR demo.
# Usage: ./run.sh [options]   (e.g. --headless --image images/111.png; see --help)
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
cd "$ROOT"
umask 077
mkdir -p license outputs
# Set a valid code here or export OCR_AUTH_CODE before launching.
export OCR_AUTH_CODE="${OCR_AUTH_CODE:-REPLACE_WITH_ALGORITHM_AUTH_CODE}"
export OCR_LICENSE_FILE="${OCR_LICENSE_FILE:-$ROOT/license/ocr.lic}"
export LD_LIBRARY_PATH="$ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
if [ -z "${QT_QPA_PLATFORM:-}" ]; then
    case " $* " in
        *" --headless "*) export QT_QPA_PLATFORM=offscreen ;;
        *)
            if [ -S "$XDG_RUNTIME_DIR/wayland-0" ] && [ -f /usr/lib/aarch64-linux-gnu/qt5/plugins/platforms/libqwayland-generic.so ]; then
                export QT_QPA_PLATFORM=wayland
                export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
            else
                export QT_QPA_PLATFORM=xcb
                export DISPLAY="${DISPLAY:-:0}"
                for authority in "$XDG_RUNTIME_DIR"/.mutter-Xwaylandauth.*; do
                    if [ -f "$authority" ]; then export XAUTHORITY="$authority"; break; fi
                done
            fi ;;
    esac
fi
exec ./ocr_cam_demo "$@"