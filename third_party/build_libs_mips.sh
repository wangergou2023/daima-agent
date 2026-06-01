#!/bin/bash

set -e

# 设置变量
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_PREFIX="${SCRIPT_DIR}/build_libs_mips"

# 交叉编译设置 - 使用 uclibc 工具链
CROSS_PREFIX="mips-linux-uclibc-gnu-"
CC="$(which ${CROSS_PREFIX}gcc 2>/dev/null || echo ${CROSS_PREFIX}gcc)"
CXX="$(which ${CROSS_PREFIX}g++ 2>/dev/null || echo ${CROSS_PREFIX}g++)"
AR="$(which ${CROSS_PREFIX}ar 2>/dev/null || echo ${CROSS_PREFIX}ar)"
RANLIB="$(which ${CROSS_PREFIX}ranlib 2>/dev/null || echo ${CROSS_PREFIX}ranlib)"

# 检查交叉编译器
if ! command -v "${CC}" &> /dev/null; then
    echo "错误: 找不到交叉编译器 ${CC}"
    echo "请确保 mips-linux-gnu-gcc 已添加到 PATH"
    exit 1
fi

echo "使用交叉编译器: ${CC}"
"${CC}" --version | head -1

# 创建安装目录
mkdir -p "${INSTALL_PREFIX}"

echo "======================================="
echo "安装路径: ${INSTALL_PREFIX}"
echo "交叉编译前缀: ${CROSS_PREFIX}"
echo "======================================="

# 编译 cjson - 静态库
echo ""
echo "======================================="
echo "开始交叉编译 cjson (静态库)..."
echo "======================================="
cd "${SCRIPT_DIR}/cjson"

# 清理之前的构建
rm -rf build_mips_static
mkdir -p build_mips_static
cd build_mips_static

# 配置静态库
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    -DBUILD_SHARED_LIBS=OFF \
    -DENABLE_CJSON_TEST=OFF \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=mips \
    -DCMAKE_C_COMPILER="${CC}" \
    -DCMAKE_CXX_COMPILER="${CXX}" \
    -DCMAKE_AR="${AR}" \
    -DCMAKE_RANLIB="${RANLIB}"

# 编译并安装
make -j$(nproc)
make install

echo "cjson 静态库交叉编译完成!"

# 编译 OpenSSL
echo ""
echo "======================================="
echo "开始交叉编译 OpenSSL (共享+静态)..."
echo "======================================="
cd "${SCRIPT_DIR}/openssl"

# 清理之前的构建
if [ -f Makefile ]; then
    make clean || true
fi

# OpenSSL 交叉编译工具处理（避免前缀重复拼接）
OPENSSL_CROSS_PREFIX="${CROSS_PREFIX}"
OPENSSL_CC="gcc"
OPENSSL_CXX="g++"
OPENSSL_AR="ar"
OPENSSL_RANLIB="ranlib"
OPENSSL_OBJCOPY="objcopy"

if [[ "${CC}" == */* ]]; then
    CC_DIR="$(dirname "${CC}")"
    CC_BASE="$(basename "${CC}")"
    if [[ "${CC_BASE}" == "${CROSS_PREFIX}"* ]]; then
        OPENSSL_CROSS_PREFIX="${CC_DIR}/${CROSS_PREFIX}"
    else
        # 工具链前缀不匹配，直接用绝对路径，避免再拼前缀
        OPENSSL_CROSS_PREFIX=""
        OPENSSL_CC="${CC}"
        OPENSSL_CXX="${CXX}"
        OPENSSL_AR="${AR}"
        OPENSSL_RANLIB="${RANLIB}"
        OPENSSL_OBJCOPY="${CC_DIR}/objcopy"
    fi
fi

OPENSSL_CONFIGURE_ARGS="linux-mips32 no-shared --prefix=${INSTALL_PREFIX} --openssldir=${INSTALL_PREFIX}/ssl --libdir=lib no-apps no-async no-tests no-unit-test no-fuzz-afl no-fuzz-libfuzzer"
if [ -n "${OPENSSL_CROSS_PREFIX}" ]; then
    OPENSSL_CONFIGURE_ARGS="${OPENSSL_CONFIGURE_ARGS} --cross-compile-prefix=${OPENSSL_CROSS_PREFIX}"
fi

echo "OpenSSL 交叉编译前缀: ${OPENSSL_CROSS_PREFIX:-<none>}"
echo "OpenSSL CC: ${OPENSSL_CC}"

OPENSSL_EXTRA_CFLAGS=""
# 不需要 IPv6，直接关闭，避免缺失宏导致编译失败
OPENSSL_EXTRA_CFLAGS="${OPENSSL_EXTRA_CFLAGS} -DOPENSSL_USE_IPV6=0"

# 配置并编译（同时生成静态与动态库）
CC="${OPENSSL_CC}" CXX="${OPENSSL_CXX}" AR="${OPENSSL_AR}" RANLIB="${OPENSSL_RANLIB}" OBJCOPY="${OPENSSL_OBJCOPY}" CFLAGS="-fPIC ${OPENSSL_EXTRA_CFLAGS}" \
    ./Configure ${OPENSSL_CONFIGURE_ARGS}

make -j$(nproc) build_libs
make install_sw
make install_ssldirs

echo "OpenSSL 交叉编译完成!"

# 编译 curl - 静态库
echo ""
echo "======================================="
echo "开始交叉编译 curl (静态库)..."
echo "======================================="
cd "${SCRIPT_DIR}/curl"

# 清理之前的构建
rm -rf build_mips_static
mkdir -p build_mips_static
cd build_mips_static

# 配置静态库
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_TESTING=OFF \
    -DBUILD_CURL_EXE=OFF \
    -DCURL_ENABLE_SSL=ON \
    -DENABLE_IPV6=OFF \
    -DCURL_USE_LIBPSL=OFF \
    -DCURL_USE_LIBSSH2=OFF \
    -DCURL_USE_LIBSSH=OFF \
    -DCMAKE_USE_LIBSSH2=OFF \
    -DCURL_ENABLE_LDAP=OFF \
    -DCURL_ENABLE_LDAPS=OFF \
    -DHTTP_ONLY=OFF \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=mips \
    -DCMAKE_C_COMPILER="${CC}" \
    -DCMAKE_CXX_COMPILER="${CXX}" \
    -DCMAKE_AR="${AR}" \
    -DCMAKE_RANLIB="${RANLIB}" \
    -DCMAKE_FIND_ROOT_PATH="${INSTALL_PREFIX}" \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
    -DOPENSSL_ROOT_DIR="${INSTALL_PREFIX}" \
    -DOPENSSL_INCLUDE_DIR="${INSTALL_PREFIX}/include" \
    -DOPENSSL_USE_STATIC_LIBS=ON \
    -DHAVE_STRUCT_TIMEVAL=ON

# 编译并安装
make -j$(nproc)
make install

echo "curl 静态库交叉编译完成!"

echo ""
echo "======================================="
echo "所有库交叉编译完成!"
echo "安装路径: ${INSTALL_PREFIX}"
echo "======================================="

echo ""
echo "======================================="
echo "清理构建目录..."
echo "======================================="
rm -rf "${SCRIPT_DIR}/cjson/build_mips_static"
rm -rf "${SCRIPT_DIR}/curl/build_mips_static"
if [ -f "${SCRIPT_DIR}/openssl/Makefile" ]; then
    (cd "${SCRIPT_DIR}/openssl" && make clean || true)
fi
echo "清理完成!"
echo ""
echo "目录结构:"
ls -la "${INSTALL_PREFIX}"
echo ""
echo "库文件:"
find "${INSTALL_PREFIX}" -name "*.a" -o -name "*.so*" 2>/dev/null || echo "No libraries found"
echo ""
echo "头文件目录:"
ls -la "${INSTALL_PREFIX}/include" 2>/dev/null || echo "No include directory"
echo ""
echo "检查库文件类型:"
find "${INSTALL_PREFIX}" -name "*.so*" -exec file {} \; 2>/dev/null
