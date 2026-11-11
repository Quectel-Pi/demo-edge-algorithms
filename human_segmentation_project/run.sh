#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
cd "$ROOT"
umask 077
mkdir -p license outputs
export HUMAN_SEG_AUTH_CODE="${HUMAN_SEG_AUTH_CODE:-REPLACE_WITH_ALGORITHM_AUTH_CODE}"
export HUMAN_SEG_LICENSE_FILE="${HUMAN_SEG_LICENSE_FILE:-$ROOT/license/human_segmentation.lic}"
export LD_LIBRARY_PATH="$ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
if [ -z "${QT_QPA_PLATFORM:-}" ]; then
    case " $* " in
        *" --headless "*|*" --self-test "*) export QT_QPA_PLATFORM=offscreen ;;
        *)
            if [ -S "$XDG_RUNTIME_DIR/wayland-0" ] && [ -f /usr/lib/aarch64-linux-gnu/qt5/plugins/platforms/libqwayland-generic.so ]; then
                export QT_QPA_PLATFORM=wayland
                export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
            else
                export QT_QPA_PLATFORM=xcb
                export DISPLAY="${DISPLAY:-:0}"
                authority="$(ls -t "$XDG_RUNTIME_DIR"/.mutter-Xwaylandauth.* 2>/dev/null | head -n 1 || true)"
                if [ -n "$authority" ]; then export XAUTHORITY="$authority"; fi
            fi ;;
    esac
fi
exec ./human_segmentation_cam_demo "$@"