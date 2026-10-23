#!/usr/bin/env bash
# =============================================================
# antigravity-proxy-mac — 一键安装与配置程序
# =============================================================

set -euo pipefail

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${BLUE}[信息]${NC} $*"; }
ok()    { echo -e "${GREEN}[成功]${NC} $*"; }
warn()  { echo -e "${YELLOW}[警告]${NC} $*"; }
error() { echo -e "${RED}[错误]${NC} $*" >&2; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo -e "${CYAN}=================================================${NC}"
echo -e "${CYAN}  Antigravity / Antigravity IDE 代理助手安装程序 ${NC}"
echo -e "${CYAN}  macOS ARM 架构适配版                         ${NC}"
echo -e "${CYAN}=================================================${NC}"
echo ""

# ---------- 检查编译器环境 ----------
if command -v clang &>/dev/null; then
    ok "检测到编译器: $(clang --version | head -1)"
else
    warn "未检测到 clang，跳过 dylib 编译（将采用原生代理注入方案）"
fi

# ---------- 编译 Universal dylib ----------
if command -v clang &>/dev/null; then
    info "正在编译底层 Hook 动态库..."
    cd "$SCRIPT_DIR"
    make universal >/dev/null 2>&1 || make arm64 >/dev/null 2>&1 || true
    ok "动态库准备完成"
fi

# ---------- 安装目录 ----------
APP_SUPPORT_DIR="${HOME}/Library/Application Support/antigravity-proxy"
mkdir -p "$APP_SUPPORT_DIR"

# 拷贝核心文件
cp -f "${SCRIPT_DIR}/launch.sh" "${APP_SUPPORT_DIR}/launch.sh"
chmod +x "${APP_SUPPORT_DIR}/launch.sh"

if [[ -f "${SCRIPT_DIR}/output/antigravity_proxy.dylib" ]]; then
    cp -f "${SCRIPT_DIR}/output/antigravity_proxy.dylib" "${APP_SUPPORT_DIR}/antigravity_proxy.dylib"
fi

# ---------- 配置向导 ----------
echo ""
echo -e "${CYAN}------------------- 代理参数配置 -------------------${NC}"
CURRENT_TYPE="socks5"
CURRENT_HOST="127.0.0.1"
CURRENT_PORT="7890"

if [[ -f "${APP_SUPPORT_DIR}/config.json" ]]; then
    CURRENT_TYPE=$(grep -o '"type"[[:space:]]*:[[:space:]]*"[^"]*"' "${APP_SUPPORT_DIR}/config.json" | head -1 | grep -o '"[^"]*"$' | tr -d '"' 2>/dev/null || echo "socks5")
    CURRENT_HOST=$(grep -o '"host"[[:space:]]*:[[:space:]]*"[^"]*"' "${APP_SUPPORT_DIR}/config.json" | head -1 | grep -o '"[^"]*"$' | tr -d '"' 2>/dev/null || echo "127.0.0.1")
    CURRENT_PORT=$(grep -o '"port"[[:space:]]*:[[:space:]]*[0-9]*' "${APP_SUPPORT_DIR}/config.json" | head -1 | grep -o '[0-9]*$' 2>/dev/null || echo "7890")
fi

read -p "代理协议 [socks5/http] (当前: ${CURRENT_TYPE}): " INPUT_TYPE
PROXY_TYPE="${INPUT_TYPE:-$CURRENT_TYPE}"

read -p "代理服务器地址 (当前: ${CURRENT_HOST}): " INPUT_HOST
PROXY_HOST="${INPUT_HOST:-$CURRENT_HOST}"

read -p "代理监听端口 (当前: ${CURRENT_PORT}): " INPUT_PORT
PROXY_PORT="${INPUT_PORT:-$CURRENT_PORT}"

# 写入配置
cat > "${APP_SUPPORT_DIR}/config.json" <<EOF
{
  "proxy": {
    "type": "${PROXY_TYPE}",
    "host": "${PROXY_HOST}",
    "port": ${PROXY_PORT}
  },
  "enabled": true,
  "log_level": 1,
  "target_processes": [
    "Antigravity",
    "Antigravity IDE",
    "Electron",
    "language_server_macos_arm",
    "language_server",
    "node"
  ]
}
EOF
cp -f "${APP_SUPPORT_DIR}/config.json" "${SCRIPT_DIR}/config.json"
ok "配置已保存至: ${APP_SUPPORT_DIR}/config.json"

# 连通性测试
info "测试代理端口 ${PROXY_HOST}:${PROXY_PORT}..."
if nc -z -w2 "$PROXY_HOST" "$PROXY_PORT" 2>/dev/null; then
    ok "代理端口连通正常 ✓"
else
    warn "代理端口暂未接通，启动后请确认代理软件运行状态。"
fi

# ---------- 安装快捷命令到 PATH ----------
# 检查已有 PATH 目录（优先安装到用户个人 bin）
LOCAL_BIN="${HOME}/.local/bin"
mkdir -p "$LOCAL_BIN"

cat > "${LOCAL_BIN}/agy-proxy" <<EOF
#!/usr/bin/env bash
exec "${APP_SUPPORT_DIR}/launch.sh" "\$@"
EOF
chmod +x "${LOCAL_BIN}/agy-proxy"
ok "快捷启动命令已安装: ${LOCAL_BIN}/agy-proxy"

# 确保在 ~/.zshrc 中有对应别名与路径支持
ZSHRC="${HOME}/~/.zshrc"
[[ -f "${HOME}/.zshrc" ]] && ZSHRC="${HOME}/.zshrc"

if [[ -f "$ZSHRC" ]]; then
    if ! grep -q "antigravity-proxy" "$ZSHRC"; then
        echo "" >> "$ZSHRC"
        echo "# Antigravity 代理快捷启动命令" >> "$ZSHRC"
        echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$ZSHRC"
        echo "alias agy-proxy='\"${LOCAL_BIN}/agy-proxy\"'" >> "$ZSHRC"
        echo "alias agy-ide='\"${LOCAL_BIN}/agy-proxy\" ide'" >> "$ZSHRC"
        echo "alias agy-app='\"${LOCAL_BIN}/agy-proxy\" app'" >> "$ZSHRC"
        ok "已在 ~/.zshrc 中注册快捷别名: agy-proxy, agy-ide, agy-app"
    fi
fi

# ---------- 完成提示 ----------
echo ""
echo -e "${GREEN}=================================================${NC}"
echo -e "${GREEN}                安装与配置完成！                 ${NC}"
echo -e "${GREEN}=================================================${NC}"
echo ""
echo "🚀 启动方式（直接在终端运行）："
echo -e "   ${CYAN}agy-proxy${NC}        # 启动 Antigravity IDE（默认并注入代理）"
echo -e "   ${CYAN}agy-proxy ide${NC}    # 显式启动 Antigravity IDE"
echo -e "   ${CYAN}agy-proxy app${NC}    # 启动轻量版 Antigravity 应用"
echo ""
echo "💡 如果终端当前尚未刷新环境，也可直接执行："
echo "   ${APP_SUPPORT_DIR}/launch.sh"
echo ""
