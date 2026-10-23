#!/usr/bin/env bash
# =============================================================
# antigravity-proxy-mac — 统一代理启动器
# 支持 Antigravity 与 Antigravity IDE (双向支持)
# 采用“Chromium 参数 + 环境变量 + 配置同步 + DYLD Hook”多重保障
# =============================================================

set -euo pipefail

# ---------- 颜色输出 ----------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${BLUE}[信息]${NC} $*"; }
ok()    { echo -e "${GREEN}[成功]${NC} $*"; }
warn()  { echo -e "${YELLOW}[警告]${NC} $*"; }
error() { echo -e "${RED}[错误]${NC} $*" >&2; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---------- 候选目标应用路径 ----------
IDE_APP_DIR="/Applications/Antigravity IDE.app"
IDE_EXEC="/Applications/Antigravity IDE.app/Contents/MacOS/Electron"
IDE_USER_SETTINGS="${HOME}/Library/Application Support/Antigravity IDE/User/settings.json"

APP_APP_DIR="/Applications/Antigravity.app"
APP_EXEC="/Applications/Antigravity.app/Contents/MacOS/Antigravity"

# ---------- 配置文件搜索路径 ----------
CONFIG_PATHS=(
    "${SCRIPT_DIR}/config.json"
    "${HOME}/Library/Application Support/antigravity-proxy/config.json"
)

# ---------- dylib 搜索路径 ----------
DYLIB_PATHS=(
    "${SCRIPT_DIR}/output/antigravity_proxy.dylib"
    "${SCRIPT_DIR}/output/antigravity_proxy_arm64.dylib"
    "${HOME}/Library/Application Support/antigravity-proxy/antigravity_proxy.dylib"
)

# ---------- 解析目标应用 ----------
TARGET_MODE="ide" # 默认优先启动 Antigravity IDE
CUSTOM_PATH=""

if [[ $# -ge 1 ]]; then
    case "$1" in
        ide|IDE|"antigravity-ide")
            TARGET_MODE="ide"
            shift
            ;;
        app|APP|"antigravity")
            TARGET_MODE="app"
            shift
            ;;
        *)
            if [[ -e "$1" ]]; then
                CUSTOM_PATH="$1"
                shift
            fi
            ;;
    esac
fi

TARGET_EXEC=""
TARGET_NAME=""

if [[ -n "$CUSTOM_PATH" ]]; then
    if [[ -d "$CUSTOM_PATH" && "$CUSTOM_PATH" == *.app ]]; then
        if [[ -f "${CUSTOM_PATH}/Contents/MacOS/Electron" ]]; then
            TARGET_EXEC="${CUSTOM_PATH}/Contents/MacOS/Electron"
        elif [[ -f "${CUSTOM_PATH}/Contents/MacOS/Antigravity" ]]; then
            TARGET_EXEC="${CUSTOM_PATH}/Contents/MacOS/Antigravity"
        fi
    elif [[ -f "$CUSTOM_PATH" ]]; then
        TARGET_EXEC="$CUSTOM_PATH"
    fi
    TARGET_NAME="自定义目标"
elif [[ "$TARGET_MODE" == "ide" ]]; then
    if [[ -f "$IDE_EXEC" ]]; then
        TARGET_EXEC="$IDE_EXEC"
        TARGET_NAME="Antigravity IDE"
    elif [[ -f "$APP_EXEC" ]]; then
        TARGET_EXEC="$APP_EXEC"
        TARGET_NAME="Antigravity"
    fi
else
    if [[ -f "$APP_EXEC" ]]; then
        TARGET_EXEC="$APP_EXEC"
        TARGET_NAME="Antigravity"
    elif [[ -f "$IDE_EXEC" ]]; then
        TARGET_EXEC="$IDE_EXEC"
        TARGET_NAME="Antigravity IDE"
    fi
fi

if [[ -z "$TARGET_EXEC" || ! -f "$TARGET_EXEC" ]]; then
    error "未找到可执行文件！请确认已在 /Applications 中安装 Antigravity 或 Antigravity IDE。"
    exit 1
fi

ok "目标应用: ${TARGET_NAME} (${TARGET_EXEC})"

# ---------- 读取代理配置 ----------
PROXY_TYPE="${AGY_PROXY_TYPE:-socks5}"
PROXY_HOST="${AGY_PROXY_HOST:-127.0.0.1}"
PROXY_PORT="${AGY_PROXY_PORT:-7890}"

CONFIG_FILE=""
for p in "${CONFIG_PATHS[@]}"; do
    if [[ -f "$p" ]]; then
        CONFIG_FILE="$p"
        break
    fi
done

if [[ -n "$CONFIG_FILE" ]]; then
    _type=$(grep -o '"type"[[:space:]]*:[[:space:]]*"[^"]*"' "$CONFIG_FILE" | head -1 | grep -o '"[^"]*"$' | tr -d '"' 2>/dev/null || true)
    _host=$(grep -o '"host"[[:space:]]*:[[:space:]]*"[^"]*"' "$CONFIG_FILE" | head -1 | grep -o '"[^"]*"$' | tr -d '"' 2>/dev/null || true)
    _port=$(grep -o '"port"[[:space:]]*:[[:space:]]*[0-9]*' "$CONFIG_FILE" | head -1 | grep -o '[0-9]*$' 2>/dev/null || true)
    [[ -n "$_type" ]] && PROXY_TYPE="$_type"
    [[ -n "$_host" ]] && PROXY_HOST="$_host"
    [[ -n "$_port" ]] && PROXY_PORT="$_port"
fi

PROXY_URL="${PROXY_TYPE}://${PROXY_HOST}:${PROXY_PORT}"
HTTP_PROXY_URL="http://${PROXY_HOST}:${PROXY_PORT}"
info "代理地址: ${PROXY_URL}"

# ---------- 检查端口连通性 ----------
if nc -z -w2 "$PROXY_HOST" "$PROXY_PORT" 2>/dev/null; then
    ok "代理端口连通性测试通过 ✓"
else
    warn "代理端口 ${PROXY_HOST}:${PROXY_PORT} 当前不可达！请确认代理软件已启动且开启了对应端口。"
fi

# ---------- 同步更新 Antigravity IDE 的 settings.json ----------
if [[ -f "$IDE_USER_SETTINGS" ]]; then
    info "检测到 Antigravity IDE 设置文件，同步注入代理规则..."
    # 使用 Python 脚本安全更新 JSON 字段，保留用户原有的所有配置项
    python3 - <<EOF 2>/dev/null || true
import json, os

path = os.path.expanduser("$IDE_USER_SETTINGS")
try:
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
except Exception:
    data = {}

# 注入 VS Code 核心代理项
data["http.proxy"] = "$PROXY_URL"
data["http.proxySupport"] = "override"
data["http.proxyStrictSSL"] = False

with open(path, "w", encoding="utf-8") as f:
    json.dump(data, f, indent=4, ensure_ascii=False)
EOF
    ok "已成功配置 IDE 设置: http.proxy -> $PROXY_URL"
fi

# ---------- 全面注入网络与语言服务器环境变量 ----------
export ALL_PROXY="$PROXY_URL"
export HTTP_PROXY="$HTTP_PROXY_URL"
export HTTPS_PROXY="$HTTP_PROXY_URL"
export http_proxy="$HTTP_PROXY_URL"
export https_proxy="$HTTP_PROXY_URL"
export all_proxy="$PROXY_URL"
export GRPC_PROXY="$PROXY_URL"
export grpc_proxy="$PROXY_URL"
export NO_PROXY="localhost,127.0.0.1,::1"
export no_proxy="localhost,127.0.0.1,::1"

# 解决 Node.js / gRPC 的证书检验与代理穿透
export NODE_TLS_REJECT_UNAUTHORIZED="0"

# ---------- 查找 Hook dylib ----------
DYLIB=""
for p in "${DYLIB_PATHS[@]}"; do
    if [[ -f "$p" ]]; then
        DYLIB="$p"
        break
    fi
done

if [[ -n "$DYLIB" ]]; then
    export DYLD_INSERT_LIBRARIES="$DYLIB"
    export DYLD_FORCE_FLAT_NAMESPACE=1
    export AGY_PROXY_TYPE="$PROXY_TYPE"
    export AGY_PROXY_HOST="$PROXY_HOST"
    export AGY_PROXY_PORT="$PROXY_PORT"
fi

# ---------- 启动应用 ----------
echo ""
echo -e "${CYAN}======================================================${NC}"
echo -e "${GREEN} 正在为 ${TARGET_NAME} 注入代理并启动...${NC}"
echo -e "${CYAN}======================================================${NC}"
echo ""

APP_BUNDLE_PATH=""
if [[ "$TARGET_NAME" == "Antigravity IDE" && -d "$IDE_APP_DIR" ]]; then
    APP_BUNDLE_PATH="$IDE_APP_DIR"
elif [[ "$TARGET_NAME" == "Antigravity" && -d "$APP_APP_DIR" ]]; then
    APP_BUNDLE_PATH="$APP_APP_DIR"
fi

# 检查当前是否在后台运行或者用户指定了命令行模式
if [[ -n "$APP_BUNDLE_PATH" && "${AGY_CLI_MODE:-0}" != "1" ]]; then
    # 使用 macOS 原生 open 命令启动，保证 Dock 栏、窗口聚焦及独立后台运行
    open -n -a "$APP_BUNDLE_PATH" --args \
        --proxy-server="$PROXY_URL" \
        --proxy-bypass-list="localhost;127.0.0.1;::1" \
        "$@"
    ok "已成功在后台启动 ${TARGET_NAME}（代理已接管）"
else
    # 命令行直接 exec 启动
    exec "$TARGET_EXEC" \
        --proxy-server="$PROXY_URL" \
        --proxy-bypass-list="localhost;127.0.0.1;::1" \
        "$@"
fi
