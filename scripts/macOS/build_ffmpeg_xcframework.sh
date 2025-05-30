#!/bin/bash

# 设置错误时退出
set -e

# 配置变量
FFMPEG_VERSION="7.1"
OPENSSL_VERSION="3.1.4"
DEPLOYMENT_TARGET="11.0"  # macOS 最低版本
SCRIPT_DIR="$(pwd)"
BUILD_DIR="${SCRIPT_DIR}/ffmpeg-build"
SOURCE_DIR="${BUILD_DIR}/ffmpeg-${FFMPEG_VERSION}"
OPENSSL_SOURCE_DIR="${BUILD_DIR}/openssl-${OPENSSL_VERSION}"
OUTPUT_DIR="${SCRIPT_DIR}/output"
LIB_NAME="libffmpeg.a"
UNIVERSAL_LIB_PATH="${OUTPUT_DIR}/universal/lib/${LIB_NAME}"

# 创建必要的目录
mkdir -p "${BUILD_DIR}"
mkdir -p "${OUTPUT_DIR}/universal/lib"
mkdir -p "${OUTPUT_DIR}/universal/include"

# 下载并解压FFmpeg源码
if [ ! -d "${SOURCE_DIR}" ]; then
    echo "下载FFmpeg ${FFMPEG_VERSION}..."
    cd "${BUILD_DIR}"
    curl -O "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.bz2"
    tar -xf "ffmpeg-${FFMPEG_VERSION}.tar.bz2"
    rm "ffmpeg-${FFMPEG_VERSION}.tar.bz2"
fi

# 下载并解压OpenSSL源码
if [ ! -d "${OPENSSL_SOURCE_DIR}" ]; then
    echo "下载OpenSSL ${OPENSSL_VERSION}..."
    cd "${BUILD_DIR}"
    curl -L -o "openssl-${OPENSSL_VERSION}.tar.gz" "https://www.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz"
    
    # 检查下载是否成功
    if [ -f "openssl-${OPENSSL_VERSION}.tar.gz" ]; then
        tar -xzf "openssl-${OPENSSL_VERSION}.tar.gz"
        rm "openssl-${OPENSSL_VERSION}.tar.gz"
    else
        echo "OpenSSL 下载失败，请检查网络连接或 OpenSSL 版本"
        exit 1
    fi
fi

# 构建OpenSSL函数
build_openssl() {
    local arch=$1
    local build_dir="${BUILD_DIR}/openssl-${arch}-macos"
    
    # 检查OpenSSL是否已经编译
    if [ -d "${build_dir}/lib" ] && [ -f "${build_dir}/lib/libssl.a" ] && [ -f "${build_dir}/lib/libcrypto.a" ]; then
        echo "OpenSSL 已经为 ${arch} (macos) 编译，跳过编译步骤..."
        return 0
    fi
    
    echo "为 ${arch} (macos) 构建OpenSSL..."
    
    # 获取macOS SDK路径
    local sdk_path=$(xcrun --sdk macosx --show-sdk-path)
    
    # 创建并进入构建目录
    mkdir -p "${build_dir}"
    
    # 复制源码到构建目录以避免污染源码
    cp -R "${OPENSSL_SOURCE_DIR}/" "${build_dir}/src"
    cd "${build_dir}/src"
    
    # 设置编译器和标志
    if [ "${arch}" = "arm64" ]; then
        ./Configure darwin64-arm64-cc no-shared no-dso no-hw no-engine \
            --prefix="${build_dir}" \
            -mmacosx-version-min=${DEPLOYMENT_TARGET} \
            -isysroot ${sdk_path}
    else
        ./Configure darwin64-x86_64-cc no-shared no-dso no-hw no-engine \
            --prefix="${build_dir}" \
            -mmacosx-version-min=${DEPLOYMENT_TARGET} \
            -isysroot ${sdk_path}
    fi
    
    # 编译和安装
    make clean
    make -j$(sysctl -n hw.ncpu)
    make install_sw
    
    # 返回到脚本目录
    cd "${SCRIPT_DIR}"
}

# 构建FFmpeg函数
build_ffmpeg() {
    local arch=$1
    local build_dir="${BUILD_DIR}/${arch}-macos"
    local openssl_dir="${BUILD_DIR}/openssl-${arch}-macos"
    
    echo "为 ${arch}-macos 构建FFmpeg..."
    
    # 获取macOS SDK路径
    local sdk_path=$(xcrun --sdk macosx --show-sdk-path)
    
    # 设置编译器标志
    export CFLAGS="-arch ${arch} -isysroot ${sdk_path} -mmacosx-version-min=${DEPLOYMENT_TARGET}"
    export LDFLAGS="-arch ${arch} -isysroot ${sdk_path} -mmacosx-version-min=${DEPLOYMENT_TARGET}"
    export CPPFLAGS="${CFLAGS}"
    
    export CC="$(xcrun -find -sdk macosx clang)"
    export CXX="$(xcrun -find -sdk macosx clang++)"
    
    # 添加OpenSSL头文件和库路径
    export CFLAGS="${CFLAGS} -I${openssl_dir}/include"
    export LDFLAGS="${LDFLAGS} -L${openssl_dir}/lib"
    
    # 创建并进入构建目录
    mkdir -p "${build_dir}"
    cd "${SOURCE_DIR}"
    
    # 配置FFmpeg，专注于macOS需要的功能
    ./configure \
        --prefix="${build_dir}" \
        --enable-cross-compile \
        --target-os=darwin \
        --arch=${arch} \
        --cc="${CC}" \
        --extra-cflags="${CFLAGS}" \
        --extra-ldflags="${LDFLAGS} -framework CoreVideo -framework VideoToolbox" \
        --enable-static \
        --disable-shared \
        --enable-pic \
        --enable-gpl \
        --enable-version3 \
        --enable-nonfree \
        --enable-openssl \
        --enable-libxml2 \
        --enable-avcodec \
        --enable-avformat \
        --enable-avfilter \
        --enable-swscale \
        --enable-swresample \
        --enable-postproc \
        --enable-protocols \
        --enable-parsers \
        --enable-muxers \
        --enable-demuxers \
        --enable-encoders \
        --enable-decoders \
        --enable-hwaccels \
        --enable-videotoolbox \
        --enable-hwaccel=h264_videotoolbox \
        --enable-hwaccel=hevc_videotoolbox \
        --enable-bsfs \
        --enable-indevs \
        --enable-outdevs \
        --disable-programs \
        --disable-doc
    
    # 编译和安装
    make clean
    make -j$(sysctl -n hw.ncpu)
    make install
    
    # 返回到脚本目录
    cd "${SCRIPT_DIR}"
}

# 创建合并库的函数
create_library() {
    local arch=$1
    local build_dir="${BUILD_DIR}/${arch}-macos"
    local openssl_dir="${BUILD_DIR}/openssl-${arch}-macos"
    local lib_dir="${OUTPUT_DIR}/${arch}-macos"
    
    echo "为 ${arch} 创建合并库..."
    
    # 创建输出目录
    mkdir -p "${lib_dir}/lib"
    mkdir -p "${lib_dir}/include"
    
    # 复制头文件
    cp -R "${build_dir}/include/" "${lib_dir}/include/"
    
    # 创建合并的静态库
    local static_libs=(
        "${build_dir}/lib/libavcodec.a"
        "${build_dir}/lib/libavformat.a"
        "${build_dir}/lib/libavutil.a"
        "${build_dir}/lib/libswscale.a"
        "${build_dir}/lib/libswresample.a"
        "${openssl_dir}/lib/libssl.a"
        "${openssl_dir}/lib/libcrypto.a"
    )
    
    # 使用libtool合并静态库
    libtool -static -o "${lib_dir}/lib/libffmpeg.a" ${static_libs[@]}
}

# 构建两个架构的OpenSSL
build_openssl "arm64"
build_openssl "x86_64"

# 构建两个架构的FFmpeg
build_ffmpeg "arm64"
create_library "arm64"

build_ffmpeg "x86_64"
create_library "x86_64"

# 创建通用库
echo "合并arm64和x86_64架构为通用库..."
# 复制头文件（从arm64复制即可）
cp -R "${OUTPUT_DIR}/arm64-macos/include/" "${OUTPUT_DIR}/universal/include/"

# 使用lipo合并两个架构的库
lipo -create \
    "${OUTPUT_DIR}/arm64-macos/lib/libffmpeg.a" \
    "${OUTPUT_DIR}/x86_64-macos/lib/libffmpeg.a" \
    -output "${UNIVERSAL_LIB_PATH}"

# 清理临时文件
echo "清理临时文件..."
rm -rf "${OUTPUT_DIR}/arm64-macos" "${OUTPUT_DIR}/x86_64-macos"

echo "完成！通用库已创建在: ${UNIVERSAL_LIB_PATH}"
