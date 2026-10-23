# Antigravity Proxy Mac — Makefile
# 构建 arm64 dylib（支持 macOS 11+ Apple Silicon）

CC       = clang

# arm64 (Apple Silicon)
CFLAGS   = -Wall -Wextra -O2 -fPIC \
           -arch arm64 \
           -mmacosx-version-min=11.0 \
           -DTARGET_OS_MAC=1
LDFLAGS  = -dynamiclib \
           -arch arm64 \
           -mmacosx-version-min=11.0 \
           -framework CoreFoundation \
           -lpthread

# x86_64 (Rosetta 2 兼容)
CFLAGS_X64  = -Wall -Wextra -O2 -fPIC \
              -arch x86_64 \
              -mmacosx-version-min=10.15 \
              -DTARGET_OS_MAC=1
LDFLAGS_X64 = -dynamiclib \
              -arch x86_64 \
              -mmacosx-version-min=10.15 \
              -framework CoreFoundation \
              -lpthread

TARGET_ARM64  = output/antigravity_proxy_arm64.dylib
TARGET_X64    = output/antigravity_proxy_x64.dylib
TARGET_UNIV   = output/antigravity_proxy.dylib

SRC = proxy_hook.c

.PHONY: all arm64 x64 universal clean install

all: universal

arm64: $(TARGET_ARM64)

x64: $(TARGET_X64)

# Universal binary（同时支持 arm64 和 x86_64）
universal: $(TARGET_ARM64) $(TARGET_X64)
	@echo "→ 创建 Universal Binary..."
	lipo -create $(TARGET_ARM64) $(TARGET_X64) -output $(TARGET_UNIV)
	@echo "✅ Universal dylib: $(TARGET_UNIV)"
	@echo ""
	@file $(TARGET_UNIV)

$(TARGET_ARM64): $(SRC) | output
	@echo "→ 编译 arm64 dylib..."
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
	@echo "✅ arm64 dylib: $@"

$(TARGET_X64): $(SRC) | output
	@echo "→ 编译 x86_64 dylib..."
	$(CC) $(CFLAGS_X64) $(LDFLAGS_X64) -o $@ $<
	@echo "✅ x86_64 dylib: $@"

output:
	mkdir -p output

clean:
	rm -rf output

install: universal
	@echo "→ 安装 dylib 到 ~/Library/Application Support/antigravity-proxy/"
	mkdir -p "$(HOME)/Library/Application Support/antigravity-proxy"
	cp $(TARGET_UNIV) "$(HOME)/Library/Application Support/antigravity-proxy/"
	cp config.json "$(HOME)/Library/Application Support/antigravity-proxy/"
	@echo "✅ 安装完成，请使用 launch.sh 启动 Antigravity"
