#!/bin/bash
# build-arm-vicos.sh — 用 vicos-sdk (glibc Clang) 交叉编译 daima-agent
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SDK="$HOME/.anki/vicos-sdk/dist/5.3.0-r07"
CC="$SDK/prebuilt/bin/arm-oe-linux-gnueabi-clang"
CXX="$SDK/prebuilt/bin/arm-oe-linux-gnueabi-clang++"
SYSROOT="$SDK/sysroot"
TARGET="arm-oe-linux-gnueabi"

echo "=== Building daima-agent (vicos-sdk glibc) ==="
echo "Compiler: $($CC --version | head -1)"

BUILD_DIR="$SCRIPT_DIR/build-arm"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake .. \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_C_FLAGS="--target=$TARGET --sysroot=$SYSROOT" \
    -DCMAKE_CXX_FLAGS="--target=$TARGET --sysroot=$SYSROOT" \
    -DCMAKE_EXE_LINKER_FLAGS="--sysroot=$SYSROOT -lm" \
    -DCMAKE_SYSROOT="$SYSROOT" \
    -DCMAKE_FIND_ROOT_PATH="$SYSROOT" \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=arm \
    -DCMAKE_BUILD_TYPE=Release \
    -DLOCAL_THIRD_PARTY="" \
    -DBUILD_FOR_MIPS=OFF

cmake --build . -j$(sysctl -n hw.ncpu)

# Strip
$SDK/prebuilt/bin/arm-oe-linux-gnueabi-strip daima

echo "=== Build complete ==="
file daima
ls -lh daima
