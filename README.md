<p align="center">
  <img src="https://raw.githubusercontent.com/yuaotian/antigravity-proxy/main/img/antigravity_logo.png" alt="Antigravity Logo" width="120"/>
</p>

<h1 align="center">Antigravity-Proxy-Mac</h1>

<p align="center">
  <b>🍎 专为 Antigravity 编辑器打造的 macOS ARM 代理注入工具</b><br>
  无需 TUN 模式，精准代理 Antigravity 相关进程
</p>

<p align="center">
  <img src="https://img.shields.io/badge/platform-macOS%20ARM%20%7C%20Apple%20Silicon-lightgrey.svg" alt="Platform"/>
  <img src="https://img.shields.io/badge/tested-Apple%20Silicon%20%7C%20macOS-success.svg" alt="Tested"/>
  <img src="https://img.shields.io/badge/C-17-00599C.svg" alt="C17"/>
  <img src="https://img.shields.io/badge/hook-DYLD__INSERT__LIBRARIES-orange.svg" alt="DYLD Hook"/>
  <img src="https://img.shields.io/badge/proxy-SOCKS5%20%7C%20HTTP-blue.svg" alt="Proxy"/>
</p>

---

> **本项目受 [yuaotian/antigravity-proxy](https://github.com/yuaotian/antigravity-proxy)（Windows 版）的灵感启发，专为 macOS Apple Silicon (ARM64) 平台独立重构与实现。**
> 核心代理拦截思想相通，底层基于 macOS BSD Socket 与 dyld 插桩机制原生实现。

---

## 📖 目录

- [原理对比：Windows vs macOS](#-原理对比windows-vs-macos)
- [快速开始](#-快速开始)
- [手动编译与安装](#️-手动编译与安装)
- [使用方法](#-使用方法)
- [配置说明](#-配置说明)
- [日志查看](#-日志查看)
- [故障排查](#-故障排查)
- [常见问题](#-常见问题)
- [鸣谢与致谢](#-鸣谢与致谢)
- [许可证](#-许可证)

---

## 🔬 原理对比：Windows vs macOS

| 机制 | Windows 版 | macOS ARM 版（本项目） |
|------|-----------|----------------------|
| 注入方式 | DLL 劫持（`version.dll`/`dbghelp.dll` 放到程序目录） | `DYLD_INSERT_LIBRARIES` 环境变量注入 `.dylib` |
| Hook 库 | MinHook | dyld interpose（系统内置） |
| Hook 目标 | Winsock `connect` / `ConnectEx` | BSD socket `connect()` |
| 代理协议 | SOCKS5 / HTTP CONNECT | SOCKS5 / HTTP CONNECT |
| 日志位置 | `<安装目录>\logs\proxy-YYYYMMDD.log` | `~/Library/Logs/antigravity-proxy/proxy-YYYYMMDD.log` |
| 配置文件 | `config.json`（程序目录旁） | `config.json`（脚本目录或 `~/Library/Application Support/antigravity-proxy/`） |

**核心思路完全一致：在不修改 Antigravity 程序本身的情况下，拦截其网络连接并重定向到本地代理。**

---

## ⚡ 快速开始

> [!TIP]
> **实机测试验证**：
> 本项目已在作者的 **Apple Silicon (ARM64) macOS 真实设备** 下完成编译、注入与全流程功能实测，Antigravity IDE 正常启动且所有 Agent 对话流量均可稳定重定向至本地代理，确认 100% 测试通过。

### 前提条件

- macOS 11+ (Big Sur) on Apple Silicon (M1/M2/M3/M4/M5 系列)
- 已安装 Xcode Command Line Tools：`xcode-select --install`
- 已启动本地代理软件（Clash/Mihomo/V2Ray 等），默认端口 `7890`

### 一键安装

```bash
git clone https://github.com/telnet402/antigravity-proxy-mac.git
cd antigravity-proxy-mac
chmod +x install.sh
./install.sh
```

安装过程会：
1. 自动编译 arm64 dylib
2. 安装到 `~/Library/Application Support/antigravity-proxy/`
3. 创建 `agy-proxy` 快捷命令
4. 引导配置代理参数

### 启动 Antigravity 与 Antigravity IDE

```bash
# 启动 Antigravity IDE（默认并自动注入代理）
agy-proxy
# 或显式指定
agy-proxy ide

# 启动轻量版 Antigravity 应用
agy-proxy app

# 支持直接传递自定义文件或目录
agy-proxy /path/to/project
```

### 为什么之前单纯靠 DYLD 注入会失效？
1. **苹果 Hardened Runtime 安全防御**：macOS 官方签名的应用（如 Antigravity IDE / Electron）默认开启了 Hardened Runtime，系统 dyld 会自动屏蔽所有未授权的 `DYLD_*` 环境变量注入。
2. **多层复合架构**：Antigravity IDE 本身是 Electron / VS Code 内核，后台通过 Node 派生 `language_server_macos_arm` 进行 gRPC / HTTPS 通信。

本项目现已采用 **“多重保障” 综合方案**：
- **Electron 核心层**：通过 `--proxy-server` 原生参数彻底接管渲染进程与网络层
- **IDE 设置层**：自动同步更新 `settings.json` 的 `http.proxy` 与 `http.proxySupport`
- **后台服务层**：全套导出 `ALL_PROXY`、`GRPC_PROXY`、`HTTP_PROXY`，穿透 `language_server`
- **底层 Hook 层**：保留 `antigravity_proxy.dylib` 供无 Hardened Runtime 限制的底层进程使用

---

## 🛠️ 手动编译与安装

### 编译 arm64 版本

```bash
make arm64
# 输出：output/antigravity_proxy_arm64.dylib
```

### 编译 Universal Binary（arm64 + x86_64）

```bash
make universal
# 输出：output/antigravity_proxy.dylib
```

### 编译后安装

```bash
make install
```

---

## 📝 使用方法

### 基本用法

```bash
# 自动查找 Antigravity.app 并启动
./launch.sh

# 指定 Antigravity 路径
./launch.sh /Applications/Antigravity.app/Contents/MacOS/Antigravity

# 通过环境变量覆盖代理配置（无需修改 config.json）
AGY_PROXY_TYPE=socks5 AGY_PROXY_HOST=127.0.0.1 AGY_PROXY_PORT=7891 ./launch.sh
```

### 通过 agy CLI 使用

如果你使用 Antigravity CLI（`agy` 命令），同样支持注入：

```bash
DYLD_INSERT_LIBRARIES="${HOME}/Library/Application Support/antigravity-proxy/antigravity_proxy.dylib" \
DYLD_FORCE_FLAT_NAMESPACE=1 \
agy chat
```

或者在 `~/.zshrc` / `~/.bashrc` 中添加别名：

```bash
# 添加到 ~/.zshrc
export AGY_PROXY_LIB="${HOME}/Library/Application Support/antigravity-proxy/antigravity_proxy.dylib"
alias agy-proxy='DYLD_INSERT_LIBRARIES="${AGY_PROXY_LIB}" DYLD_FORCE_FLAT_NAMESPACE=1 agy'
```

之后直接用 `agy-proxy chat` 替代 `agy chat`。

---

## ⚙️ 配置说明

配置文件路径（按优先级）：
1. 脚本同级目录 `./config.json`
2. `~/Library/Application Support/antigravity-proxy/config.json`

```json
{
  "proxy": {
    "type": "socks5",      // "socks5" 或 "http"
    "host": "127.0.0.1",   // 代理服务器地址
    "port": 7890           // 代理端口
  },
  "enabled": true,
  "log_level": 1,          // 0=静默 1=普通 2=详细
  "log_dir": "",           // 留空使用默认 ~/Library/Logs/antigravity-proxy/
  "proxy_rules": {
    "bypass_local": true   // 本地地址直连（不走代理）
  }
}
```

### 环境变量覆盖

| 环境变量 | 说明 | 示例 |
|---------|------|------|
| `AGY_PROXY_TYPE` | 代理类型 | `socks5` / `http` |
| `AGY_PROXY_HOST` | 代理地址 | `127.0.0.1` |
| `AGY_PROXY_PORT` | 代理端口 | `7890` |

---

## 📋 日志查看

```bash
# 查看今天的日志
tail -f ~/Library/Logs/antigravity-proxy/proxy-$(date +%Y%m%d).log

# 查看所有日志
ls ~/Library/Logs/antigravity-proxy/
```

### 关键日志含义

| 日志内容 | 含义 | 状态 |
|---------|------|------|
| `Antigravity-Proxy-Mac v1.0.0 已加载` | dylib 注入成功 | ✅ 正常 |
| `SOCKS5: 隧道建立成功, 目标=...` | 代理连接成功 | ✅ 正常 |
| `本地地址直连` | 本地流量不走代理 | ✅ 正常 |
| `连接代理服务器失败` | 代理软件未启动 | ❌ 需排查 |
| `SOCKS5: 握手失败` | SOCKS5 协议错误 | ❌ 需排查 |

---

## 🔧 故障排查

### 问题 1：`dylib` 无法加载 / `Library not loaded`

```
dyld: Library not loaded: antigravity_proxy.dylib
```

**原因：** dylib 路径不正确，或 macOS Gatekeeper 拦截。

**解决：**
```bash
# 方法 1：移除隔离属性
xattr -d com.apple.quarantine output/antigravity_proxy.dylib

# 方法 2：使用绝对路径
export DYLD_INSERT_LIBRARIES="$(pwd)/output/antigravity_proxy_arm64.dylib"
```

### 问题 2：`DYLD_INSERT_LIBRARIES` 被忽略

**原因：** Antigravity 开启了 Hardened Runtime 或 SIP 保护。

**解决：**
- 确认 Antigravity 不是从 App Store 下载的（部分 App Store 版启用了完整 sandbox）
- 尝试方案 B（仅环境变量代理）：

```bash
# 方案 B：仅靠环境变量（不注入 dylib）
# 适用于 Antigravity 尊重系统代理的情况
ALL_PROXY=socks5://127.0.0.1:7890 \
HTTPS_PROXY=http://127.0.0.1:7890 \
HTTP_PROXY=http://127.0.0.1:7890 \
/Applications/Antigravity.app/Contents/MacOS/Antigravity
```

### 问题 3：编译失败

```bash
# 检查 clang 是否支持 arm64
clang -arch arm64 -v /dev/null

# 确认 SDK 版本
xcrun --show-sdk-path
```

### 问题 4：对话报错 `User location is not supported`

> **这不是 dylib 的问题，是代理出口 IP 的问题。**

与 Windows 版相同：
- 代理已生效，但出口 IP 所在的国家/ASN 被 Antigravity agent 服务端拒绝
- 解决：更换非机房/住宅 IP 的代理节点

---

## ❓ 常见问题

**Q: 和 Windows 版的 `version.dll` 有什么区别？**

A: 功能完全等价，只是实现方式适配了 macOS：
- Windows：把 `version.dll` 放到程序目录，程序启动时系统自动加载
- macOS：通过 `DYLD_INSERT_LIBRARIES` 环境变量，在程序启动前注入 dylib

**Q: 需要 root/sudo 权限吗？**

A: 不需要。`DYLD_INSERT_LIBRARIES` 是普通用户权限即可使用的标准 dyld 功能。

**Q: 会影响其他应用吗？**

A: 不会。`DYLD_INSERT_LIBRARIES` 只对通过 `launch.sh` 启动的进程生效，不会全局影响。

**Q: 支持 Rosetta 2（Intel 应用在 ARM Mac 上运行）吗？**

A: 支持。使用 `make universal` 编译 Universal Binary 即可同时覆盖 arm64 和 x86_64。

---

## 🙏 鸣谢与致谢

本项目在实现过程中参考并吸取了开源社区的优秀思路，特此致谢：

- **[yuaotian/antigravity-proxy](https://github.com/yuaotian/antigravity-proxy)**：原作者出色的 Windows 版网络注入思路为本项目带来了核心灵感。
- **[cJSON](https://github.com/DaveGamble/cJSON)**：提供超轻量、纯 C 语言的 JSON 配置文件解析支持。
- **macOS 底层安全与 Hook 社区**：感谢探索 `dyld interpose` 机制与 macOS 平台进程网络拦截的技术先驱们。

---

## 📄 许可证

本项目基于 [BSD-2-Clause](LICENSE) 许可证开源。

