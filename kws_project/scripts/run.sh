#!/bin/bash
# run.sh — launch the keyword spotting demo with the AIVOX KWS runtime.
set -e
cd "$(dirname "$0")/.."

BIN="./build/kws_demo"
[ -x "$BIN" ] || { echo "未找到 $BIN，请先运行 scripts/build.sh"; exit 1; }

# ---- Algorithm package / runtime libraries ----
SDK_ROOT="${KWS_SDK_ROOT:-$(pwd)/aivox-scr-xiaoyuantongxue-zipformer-960k-zh/SDK}"
export LD_LIBRARY_PATH="$SDK_ROOT/lib/linux_arm_v8:${LD_LIBRARY_PATH:-}"

# ---- License: default code is the one shipped for Quectel PI M2 KWS ----
AUTH_CODE="${KWS_AUTH_CODE:-DD07D3A7-8BD0-41CB-8ADE-21BB2421E5AD}"
LICENSE_DIR="${KWS_LICENSE_DIR:-/data}"
if ! mkdir -p "$LICENSE_DIR" 2>/dev/null || ! touch "$LICENSE_DIR/.kws_wtest" 2>/dev/null; then
    LICENSE_DIR="$(pwd)/license"
    mkdir -p "$LICENSE_DIR"
fi
rm -f "$LICENSE_DIR/.kws_wtest" 2>/dev/null || true

# ---- Device identity: /dev/ttyGS0 must contain the board SN ----
# 授权库（at=3）通过读取 /dev/ttyGS0 获取设备 SN，从而绕开 /dev/mem。
# 该节点的 bind mount 在重启后会丢失；若节点仍是真实的 tty，读取会一直阻塞
# （现象：打印 quec_decoder_get_sn_exp: /dev/ttyGS0 后卡住），因此必须校验内容，
# 而不能只判断节点是否可读。
SN="${KWS_DEVICE_SN:-}"
if [ -z "$SN" ] && [ -r /proc/device-tree/serial-number ]; then
    SN="$(tr -d '\000\n\r' < /proc/device-tree/serial-number 2>/dev/null)"
fi

sn_node_ok() {
    [ -n "$SN" ] && [ -r /dev/ttyGS0 ] || return 1
    local got
    got="$(timeout 1 head -c 64 /dev/ttyGS0 2>/dev/null | tr -d '\000\n\r' || true)"
    [ "$got" = "$SN" ]
}

if [ -n "$SN" ] && ! sn_node_ok; then
    printf '%s' "$SN" > /tmp/kws_sn.txt 2>/dev/null || true
    if [ "$(id -u)" -eq 0 ]; then
        systemctl stop port_bridge 2>/dev/null || true
        [ -e /dev/ttyGS0 ] || : > /dev/ttyGS0 2>/dev/null || true
        mount --bind /tmp/kws_sn.txt /dev/ttyGS0 2>/dev/null || true
    elif sudo -n true 2>/dev/null; then
        sudo -n bash -c 'systemctl stop port_bridge 2>/dev/null || true; [ -e /dev/ttyGS0 ] || : > /dev/ttyGS0; mount --bind /tmp/kws_sn.txt /dev/ttyGS0' 2>/dev/null || true
    fi
    if sn_node_ok; then
        echo "SN node /dev/ttyGS0: rebind ok (SN=$SN)"
    else
        echo "SN node /dev/ttyGS0: NOT ready —— 授权初始化会卡住，请先手动执行:"
        echo "  sudo systemctl stop port_bridge"
        echo "  printf '$SN' > /tmp/kws_sn.txt"
        echo "  sudo mount --bind /tmp/kws_sn.txt /dev/ttyGS0"
        echo "  timeout 1 head -c 64 /dev/ttyGS0   # 应输出 SN 后立刻返回"
    fi
elif [ -n "$SN" ]; then
    echo "SN node /dev/ttyGS0: ok (SN=$SN)"
fi

# ---- Demo defaults (all overridable from the environment) ----
THRESHOLD="${KWS_THRESHOLD:-0.30}"
GAIN_DB="${KWS_GAIN_DB:-30}"
WAKE_LABEL="${KWS_WAKE_LABEL:-}"        # 留空则使用 keywords.txt 中 @ 后的文本
BOARD_MODULE="${KWS_BOARD_MODULE:-3}"   # 3 = RK3576 (Quectel Pi M2)
AT_INDEX="${KWS_AT:-3}"                 # 3 = 从 /dev/ttyGS0 读设备 SN（避免 /dev/mem）
DEV_NODE="${KWS_DEV_NODE:-}"            # only used when BOARD_MODULE=0
TOKENS="${KWS_TOKENS:-$(pwd)/models/tokens.txt}"
KEYWORDS="${KWS_KEYWORDS:-$(pwd)/models/keywords.txt}"
MODELS_DIR="$(pwd)/models"

# ---- Model files: make sure the current user can rewrite keywords.txt ----
# UI 里的 Edit 需要写回 keywords.txt。若文件曾被 root 创建/拷贝，属主会变成 root，
# 普通用户就没有写权限。
# 目标属主规则：非 root 运行时 = 当前用户；root 运行时 = SUDO_USER，否则上级目录
# 的属主（/home/q/kws_demo -> q），这样即使用 root 启动，q 之后也能改关键词。
CURRENT_USER="$(id -un)"
if [ "$(id -u)" -eq 0 ]; then
    if [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != "root" ]; then
        TARGET_OWNER="$SUDO_USER"
    else
        TARGET_OWNER="$(stat -c '%U' .. 2>/dev/null || true)"
        [ -n "$TARGET_OWNER" ] || TARGET_OWNER="$(stat -c '%U' . 2>/dev/null || true)"
        [ -n "$TARGET_OWNER" ] || TARGET_OWNER="root"
    fi
else
    TARGET_OWNER="$CURRENT_USER"
fi

if [ -d "$MODELS_DIR" ]; then
    OWNER_NOW="$(stat -c '%U' "$MODELS_DIR/keywords.txt" 2>/dev/null || true)"
    NEED_FIX=0
    [ -w "$MODELS_DIR/keywords.txt" ] || NEED_FIX=1
    [ -w "$MODELS_DIR" ] || NEED_FIX=1
    # root 运行但文件仍属 root：提前交给目标用户，避免之后 q 无法保存
    if [ "$(id -u)" -eq 0 ] && [ "$OWNER_NOW" != "$TARGET_OWNER" ]; then
        NEED_FIX=1
    fi

    if [ "$NEED_FIX" -eq 1 ]; then
        echo "MODEL FILES     : fixing owner -> $TARGET_OWNER ..."
        if [ "$(id -u)" -eq 0 ]; then
            chown -R "$TARGET_OWNER:$TARGET_OWNER" "$MODELS_DIR" 2>/dev/null || true
        elif sudo -n true 2>/dev/null; then
            sudo -n chown -R "$TARGET_OWNER:$TARGET_OWNER" "$MODELS_DIR" 2>/dev/null || true
        elif [ -t 0 ]; then
            # 没有免密 sudo 时交互式提示一次密码
            sudo chown -R "$TARGET_OWNER:$TARGET_OWNER" "$MODELS_DIR" 2>/dev/null || true
        fi
        chmod 755 "$MODELS_DIR" 2>/dev/null || true
        chmod 644 "$MODELS_DIR"/*.txt 2>/dev/null || true
    fi

    if [ -w "$MODELS_DIR/keywords.txt" ] && [ -w "$MODELS_DIR" ]; then
        echo "MODEL FILES     = writable (owner=$(stat -c '%U' "$MODELS_DIR/keywords.txt" 2>/dev/null), user=$CURRENT_USER)"
    else
        echo "MODEL FILES     = NOT writable —— 界面 Edit 无法保存，请手动执行:"
        echo "  sudo chown -R $TARGET_OWNER:$TARGET_OWNER $MODELS_DIR"
        echo "  sudo chmod 755 $MODELS_DIR && sudo chmod 644 $MODELS_DIR/*.txt"
    fi
else
    echo "MODEL FILES     = $MODELS_DIR 不存在"
fi

# ---- Capture device: prefer 'default', fall back to the first real card ----
# root 用户通常连不上 q 会话的 PulseAudio/PipeWire，'default' 会报
# "Host is down"；此时直接使用 plughw:<card>,<device> 绕开音频服务。
detect_capture_dev() {
    if [ -n "${KWS_CAPTURE_DEV:-}" ]; then
        printf '%s' "$KWS_CAPTURE_DEV"
        return
    fi
    local probe=/tmp/kws_cap_probe.wav card dev
    if timeout 4 arecord -D default -f S16_LE -r 16000 -c 1 -d 1 "$probe" >/dev/null 2>&1; then
        rm -f "$probe"
        printf '%s' "default"
        return
    fi
    rm -f "$probe"
    card="$(arecord -l 2>/dev/null | sed -n 's/^card \([0-9][0-9]*\):.*/\1/p' | head -1)"
    dev="$(arecord -l 2>/dev/null | sed -n 's/.*device \([0-9][0-9]*\):.*/\1/p' | head -1)"
    if [ -n "$card" ]; then
        printf 'plughw:%s,%s' "$card" "${dev:-0}"
    else
        printf '%s' "default"
    fi
}
CAP_DEV="$(detect_capture_dev)"
if [ -n "${KWS_CAPTURE_DEV:-}" ]; then
    CAPTURE_SOURCE="KWS_CAPTURE_DEV"
elif [ "$CAP_DEV" = "default" ]; then
    CAPTURE_SOURCE="auto (default works)"
else
    CAPTURE_SOURCE="auto (default unavailable)"
fi

export KWS_SDK_ROOT="$SDK_ROOT"
export KWS_AUTH_CODE="$AUTH_CODE"
export KWS_LICENSE_DIR="$LICENSE_DIR"
export KWS_CAPTURE_DEV="$CAP_DEV"
export KWS_THRESHOLD="$THRESHOLD"
export KWS_GAIN_DB="$GAIN_DB"
# 显式设置才覆盖，否则让程序读取 keywords.txt 的 @ 显示名
if [ -n "$WAKE_LABEL" ]; then
    export KWS_WAKE_LABEL="$WAKE_LABEL"
else
    unset KWS_WAKE_LABEL
fi
export KWS_BOARD_MODULE="$BOARD_MODULE"
export KWS_AT="$AT_INDEX"
export KWS_DEV_NODE="$DEV_NODE"
export KWS_TOKENS="$TOKENS"
export KWS_KEYWORDS="$KEYWORDS"

echo "LD_LIBRARY_PATH = $LD_LIBRARY_PATH"
echo "SDK_ROOT        = $SDK_ROOT"
echo "AUTH_CODE       = ${AUTH_CODE:0:8}... (set)"
echo "LICENSE_DIR     = $LICENSE_DIR"
echo "CAPTURE         = $CAP_DEV   ($CAPTURE_SOURCE)"
echo "THRESHOLD       = $THRESHOLD   MIC_GAIN = ${GAIN_DB} dB"
echo "TOKENS          = $TOKENS"
echo "KEYWORDS        = $KEYWORDS"
echo "WAKE LABEL      = ${WAKE_LABEL:-auto (from keywords.txt)}"
echo "BOARD_MODULE    = $BOARD_MODULE   AT = $AT_INDEX   DEV_NODE = ${DEV_NODE:-<none>}"

exec "$BIN" "$@"
