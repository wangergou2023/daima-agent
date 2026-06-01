#!/bin/bash

# 任一命令失败即退出，避免生成半成品
set -e

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# 解析参数：默认 system（使用宿主机依赖）
BUILD_TYPE="${1:-system}"

case "$BUILD_TYPE" in
    system|""|local)
        # 本地构建：使用系统库 & 本机编译器
        echo "=== Building for x86_64 (system libs) ==="
        BUILD_DIR="$SCRIPT_DIR/build-host"
        mkdir -p "$BUILD_DIR"
        # 清理缓存，避免切换平台/选项时混淆
        rm -rf "$BUILD_DIR/CMakeCache.txt" "$BUILD_DIR/CMakeFiles"
        (cd "$BUILD_DIR" && cmake ..)
        cmake --build "$BUILD_DIR"
        echo "=== Build complete: $BUILD_DIR/daima ==="
        ;;

    mips)
        # 交叉编译：使用 MIPS uclibc 工具链 + 启用 Vision
        echo "=== Building for MIPS (cross-compile) ==="
        BUILD_DIR="$SCRIPT_DIR/build-mips"
        mkdir -p "$BUILD_DIR"
        rm -rf "$BUILD_DIR/CMakeCache.txt" "$BUILD_DIR/CMakeFiles"
        (cd "$BUILD_DIR" && cmake -DBUILD_FOR_MIPS=ON -DDAIMA_ENABLE_VISION=ON ..)
        cmake --build "$BUILD_DIR"
        echo "=== Build complete: $BUILD_DIR/daima ==="
        # 可选查看产物类型（静默失败不影响构建）
        file "$BUILD_DIR/daima" 2>/dev/null || true
        ;;

    clean)
        # 删除构建产物目录
        echo "=== Cleaning build directories ==="
        rm -rf build-host build-mips
        echo "=== Clean complete ==="
        ;;

    *)
        # 参数不合法时提示用法
        echo "Usage: $0 [system|mips|clean]"
        echo ""
        echo "Options:"
        echo "  system  - Build for x86_64 using system installed libraries (default)"
        echo "  mips    - Cross-compile for MIPS using mips-linux-uclibc-gnu-gcc"
        echo "  clean   - Remove all build directories"
        exit 1
        ;;
esac
