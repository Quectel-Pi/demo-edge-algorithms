#!/bin/bash
# run.sh — 一键运行脚本
set -e
cd "$(dirname "$0")/.."

# ---- 配置 ----
SDK_ROOT="${AECANS_SDK_ROOT:-/home/q/aec_ans_project/AIVOXAEC_ANS_V1.1.0.2.1_SDK_16k_10ms_Exp009_dewatermark_encrypt}"
AUTH_CODE="${ANS_AUTH_CODE:-F388795C-BD59-42A2-BF84-C3690E8509A6}"
LICENSE_DIR="${ANS_LICENSE_DIR:-/data}"
MODEL="${ANS_MODEL:-$SDK_ROOT/models/Exp009M060_16k_mdl-v1.0_10ms_32ms_simple.mnn}"
CAP_DEV="${ANS_CAPTURE_DEV:-default}"
# 播放设备用 default（让 PulseAudio 或 dmix 自动重采样到硬件对的速率）
PLAY_DEV="${ANS_PLAYBACK_DEV:-default}"
OUTPUT_DIR="${ANS_OUTPUT_DIR:-$(pwd)/output}"

# ---- ALSA mixer：必须按 numid 直接控制（ES8389 simple control 名不匹配） ----
# numid=57 spk switch        → on   (喇叭主开关，关了就不会响)
# numid=60 Speaker Switch    → on   (codec speaker 使能)
# numid=51 DACL Playback Vol → 255  (左声道拉满)
# numid=52 DACR Playback Vol → 255  (右声道拉满)
# numid=59 Headphone Switch  → off  (别让耳机通道抢路由)
# 无需 root：普通用户对 /dev/snd/controlC0 有写权限
echo "[MIXER] configuring ES8389 playback routes..."
amixer -c 0 cset numid=57 1 >/dev/null 2>&1   && echo "  numid=57 spk switch      on"
amixer -c 0 cset numid=60 1 >/dev/null 2>&1   && echo "  numid=60 Speaker Switch  on"
amixer -c 0 cset numid=51 255 >/dev/null 2>&1 && echo "  numid=51 DACL Playback   255"
amixer -c 0 cset numid=52 255 >/dev/null 2>&1 && echo "  numid=52 DACR Playback   255"
amixer -c 0 cset numid=59 0 >/dev/null 2>&1   && echo "  numid=59 Headphone Sw    off"

# ---- output 目录自愈 ----
mkdir -p "$OUTPUT_DIR" 2>/dev/null || true
if ! (touch "$OUTPUT_DIR/.wtest" 2>/dev/null); then
    sudo -n chown -R "$(id -u):$(id -g)" "$OUTPUT_DIR" 2>/dev/null || true
fi
if ! (touch "$OUTPUT_DIR/.wtest" 2>/dev/null); then
    ARCHIVE="${OUTPUT_DIR}.root.$(date +%Y%m%d%H%M%S)"
    if mv "$OUTPUT_DIR" "$ARCHIVE" 2>/dev/null; then
        echo "[INFO] 旧 output 目录不可写，已归档到 $ARCHIVE"
        mkdir -p "$OUTPUT_DIR" 2>/dev/null || true
    fi
fi
if ! (touch "$OUTPUT_DIR/.wtest" 2>/dev/null); then
    echo "[WARN] $OUTPUT_DIR 不可写，保存会失败 — sudo chown -R $(id -u):$(id -g) $OUTPUT_DIR"
else
    rm -f "$OUTPUT_DIR/.wtest" 2>/dev/null || true
    for f in "$OUTPUT_DIR"/*.wav; do
        [ -e "$f" ] || continue
        [ -w "$f" ] || mv "$f" "${f}.root.$(date +%Y%m%d%H%M%S)" 2>/dev/null || true
    done
fi

# ---- SN bind mount ----
SN="${ANS_DEVICE_SN:-}"
if [ -z "$SN" ] && [ -r /proc/device-tree/serial-number ]; then
    SN="$(tr -d '\000\n\r' < /proc/device-tree/serial-number 2>/dev/null)"
fi
if [ -n "$SN" ] && [ ! -f /dev/ttyGS0 ]; then
    printf '%s' "$SN" > /tmp/aivox_sn.txt 2>/dev/null || true
    if [ "$(id -u)" -eq 0 ]; then
        systemctl stop port_bridge 2>/dev/null || true
        mount --bind /tmp/aivox_sn.txt /dev/ttyGS0 2>/dev/null || true
    elif sudo -n true 2>/dev/null; then
        sudo -n bash -c 'systemctl stop port_bridge 2>/dev/null || true; mount --bind /tmp/aivox_sn.txt /dev/ttyGS0' 2>/dev/null || true
    fi
fi

BIN="./build/ans_demo"
[ -x "$BIN" ] || { echo "未找到 $BIN，请先运行 scripts/build.sh"; exit 1; }

export LD_LIBRARY_PATH="$SDK_ROOT/lib/Linux-aarch64_v8:${LD_LIBRARY_PATH:-}"
export ANS_AUTH_CODE="$AUTH_CODE"
export ANS_LICENSE_DIR="$LICENSE_DIR"
export ANS_MODEL="$MODEL"
export ANS_CAPTURE_DEV="$CAP_DEV"
export ANS_PLAYBACK_DEV="$PLAY_DEV"
export ANS_OUTPUT_DIR="$OUTPUT_DIR"

echo "CAP=$CAP_DEV  PLAY=$PLAY_DEV  OUTPUT=$OUTPUT_DIR"
exec "$BIN" "$@"
