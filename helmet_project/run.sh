#!/bin/sh
# Run the desktop helmet detection demo.
# Usage: ./run.sh [options]   (e.g. --headless --image images/input.jpg; see --help)
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
cd "$ROOT"
umask 077
mkdir -p license outputs
# Set a valid code here or export HELMET_AUTH_CODE before launching.
export HELMET_AUTH_CODE="${HELMET_AUTH_CODE:-REPLACE_WITH_ALGORITHM_AUTH_CODE}"
# Must match the application the authorization code belongs to.
export HELMET_APP_ID="${HELMET_APP_ID:-145}"
export HELMET_LICENSE_FILE="${HELMET_LICENSE_FILE:-$ROOT/license/helmet_license.ini}"
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
                # Stale files from earlier sessions may remain; use the newest.
                authority="$(ls -t "$XDG_RUNTIME_DIR"/.mutter-Xwaylandauth.* 2>/dev/null | head -n 1 || true)"
                if [ -n "$authority" ]; then export XAUTHORITY="$authority"; fi
            fi ;;
    esac
fi
exec ./helmet_cam_demo "$@"
