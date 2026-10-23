#!/bin/sh
set -eu
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
umask 077
mkdir -p "$ROOT/license" "$ROOT/outputs"
export FAD_AUTH_CODE="${FAD_AUTH_CODE:-REPLACE_WITH_ALGORITHM_AUTH_CODE}"
export LD_LIBRARY_PATH="$ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export FAD_LICENSE_FILE="${FAD_LICENSE_FILE:-$ROOT/license/fad_license.txt}"
if [ -d "/run/user/$(id -u)" ]; then
    export XDG_RUNTIME_DIR="/run/user/$(id -u)"
else
    export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/runtime-$(id -u)}"
    mkdir -p "$XDG_RUNTIME_DIR"
    chmod 700 "$XDG_RUNTIME_DIR"
fi
if [ -z "${QT_QPA_PLATFORM:-}" ]; then
    if [ -S "$XDG_RUNTIME_DIR/wayland-0" ] && [ -f /usr/lib/aarch64-linux-gnu/qt5/plugins/platforms/libqwayland-generic.so ]; then
        export QT_QPA_PLATFORM=wayland
        export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
    else
        export QT_QPA_PLATFORM=xcb
        export DISPLAY="${DISPLAY:-:0}"
        for authority in "$XDG_RUNTIME_DIR"/.mutter-Xwaylandauth.*; do
            if [ -f "$authority" ]; then export XAUTHORITY="$authority"; break; fi
        done
    fi
fi
exec ./facial_attributes_cam_demo "$@"