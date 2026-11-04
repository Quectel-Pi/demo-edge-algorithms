#!/bin/sh
# Run the desktop mask detection demo.
# Usage: ./run.sh [options]   (e.g. --headless --image images/input.jpg; see --help)
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
cd "$ROOT"
umask 077
mkdir -p license outputs
# Set a valid code here or export MASK_AUTH_CODE before launching.
export MASK_AUTH_CODE="${MASK_AUTH_CODE:-REPLACE_WITH_ALGORITHM_AUTH_CODE}"
export MASK_APP_ID="${MASK_APP_ID:-147}"
export MASK_LICENSE_FILE="${MASK_LICENSE_FILE:-$ROOT/license/mask_license.ini}"
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
                for authority in "$XDG_RUNTIME_DIR"/.mutter-Xwaylandauth.*; do
                    if [ -f "$authority" ]; then export XAUTHORITY="$authority"; break; fi
                done
            fi ;;
    esac
fi
exec ./maskdet_cam_demo "$@"
