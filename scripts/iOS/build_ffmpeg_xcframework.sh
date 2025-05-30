#!/bin/bash

# 设置错误时退出
set -e

# 配置变量
FFMPEG_VERSION="7.1"
OPENSSL_VERSION="3.1.4"
DEPLOYMENT_TARGET="15.0"
SCRIPT_DIR="$(pwd)"
BUILD_DIR="${SCRIPT_DIR}/ffmpeg-build"
SOURCE_DIR="${BUILD_DIR}/ffmpeg-${FFMPEG_VERSION}"
OPENSSL_SOURCE_DIR="${BUILD_DIR}/openssl-${OPENSSL_VERSION}"
OUTPUT_DIR="${SCRIPT_DIR}/output"
FRAMEWORK_NAME="FFmpeg"
XCFRAMEWORK_PATH="${OUTPUT_DIR}/${FRAMEWORK_NAME}.xcframework"

# 创建必要的目录
mkdir -p "${BUILD_DIR}"
mkdir -p "${OUTPUT_DIR}"

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
    local platform=$2
    local sdk=$3
    local build_dir="${BUILD_DIR}/openssl-${arch}-${platform}"
    
    # 检查OpenSSL是否已经编译
    if [ -d "${build_dir}/lib" ] && [ -f "${build_dir}/lib/libssl.a" ] && [ -f "${build_dir}/lib/libcrypto.a" ]; then
        echo "OpenSSL 已经为 ${arch} (${platform}) 编译，跳过编译步骤..."
        return 0
    fi
    
    echo "为 ${arch} (${platform}) 构建OpenSSL..."
    
    # 获取SDK路径
    local sdk_path=$(xcrun --sdk ${sdk} --show-sdk-path)
    
    # 创建并进入构建目录
    mkdir -p "${build_dir}"
    
    # 复制源码到构建目录以避免污染源码
    cp -R "${OPENSSL_SOURCE_DIR}/" "${build_dir}/src"
    cd "${build_dir}/src"
    
    # 设置编译器和标志
    if [ "${platform}" = "simulator" ]; then
        if [ "${arch}" = "arm64" ]; then
            ./Configure darwin64-arm64-cc no-shared no-dso no-hw no-engine \
                --prefix="${build_dir}" \
                -mios-simulator-version-min=${DEPLOYMENT_TARGET} \
                -isysroot ${sdk_path}
        else
            ./Configure darwin64-x86_64-cc no-shared no-dso no-hw no-engine \
                --prefix="${build_dir}" \
                -mios-simulator-version-min=${DEPLOYMENT_TARGET} \
                -isysroot ${sdk_path}
        fi
    else
        ./Configure ios64-cross no-shared no-dso no-hw no-engine \
            --prefix="${build_dir}" \
            -mios-version-min=${DEPLOYMENT_TARGET} \
            -isysroot ${sdk_path}
    fi
    
    # 编译和安装
    make clean
    make -j$(sysctl -n hw.ncpu)
    make install_sw
    
    # 返回到脚本目录
    cd "${SCRIPT_DIR}"
}

# 构建函数
build_ffmpeg() {
    local arch=$1
    local platform=$2
    local sdk=$3
    local build_dir="${BUILD_DIR}/${arch}-${platform}"
    local openssl_dir="${BUILD_DIR}/openssl-${arch}-${platform}"
    
    echo "为 ${arch}-${platform} 构建FFmpeg..."
    
    # 获取SDK路径
    local sdk_path=$(xcrun --sdk ${sdk} --show-sdk-path)
    
    # 设置编译器标志 - 移除了 -fembed-bitcode
    if [ "${platform}" = "simulator" ]; then
        export CFLAGS="-arch ${arch} -isysroot ${sdk_path} -mios-simulator-version-min=${DEPLOYMENT_TARGET}"
        export LDFLAGS="-arch ${arch} -isysroot ${sdk_path} -mios-simulator-version-min=${DEPLOYMENT_TARGET}"
        export CPPFLAGS="${CFLAGS}"
    else
        export CFLAGS="-arch ${arch} -isysroot ${sdk_path} -mios-version-min=${DEPLOYMENT_TARGET}"
        export LDFLAGS="-arch ${arch} -isysroot ${sdk_path} -mios-version-min=${DEPLOYMENT_TARGET}"
        export CPPFLAGS="${CFLAGS}"
    fi
    
    export CC="$(xcrun -find -sdk ${sdk} clang)"
    export CXX="$(xcrun -find -sdk ${sdk} clang++)"
    
    # 添加OpenSSL头文件和库路径
    export CFLAGS="${CFLAGS} -I${openssl_dir}/include"
    export LDFLAGS="${LDFLAGS} -L${openssl_dir}/lib"
    
    # 创建并进入构建目录
    mkdir -p "${build_dir}"
    cd "${SOURCE_DIR}"
    
    # 配置FFmpeg，只包含必要的组件，专注于流复制功能
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
        --enable-avdevice \
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
        --disable-avdevice \
        --disable-doc \
        --disable-x86asm

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
    local platform=$2
    local build_dir="${BUILD_DIR}/${arch}-${platform}"
    local openssl_dir="${BUILD_DIR}/openssl-${arch}-${platform}"
    local lib_dir="${OUTPUT_DIR}/${platform}-${arch}"
    
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

# 构建各个架构的OpenSSL
build_openssl "arm64" "iphoneos" "iphoneos"
build_openssl "arm64" "simulator" "iphonesimulator"
build_openssl "x86_64" "simulator" "iphonesimulator"

# 构建各个架构的FFmpeg
build_ffmpeg "arm64" "iphoneos" "iphoneos"
create_library "arm64" "iphoneos"

build_ffmpeg "arm64" "simulator" "iphonesimulator"
create_library "arm64" "simulator"

build_ffmpeg "x86_64" "simulator" "iphonesimulator"
create_library "x86_64" "simulator"

# 创建XCFramework
echo "创建XCFramework..."

# 如果已存在，先删除旧的XCFramework
if [ -d "${XCFRAMEWORK_PATH}" ]; then
    rm -rf "${XCFRAMEWORK_PATH}"
fi

# 创建合并模拟器架构的通用库
echo "合并模拟器架构..."
simulator_universal_dir="${OUTPUT_DIR}/simulator-universal"
mkdir -p "${simulator_universal_dir}/lib"
mkdir -p "${simulator_universal_dir}/include"

# 复制头文件（从任一模拟器架构复制即可）
cp -R "${OUTPUT_DIR}/simulator-arm64/include/" "${simulator_universal_dir}/include/"

# 合并模拟器的不同架构库
lipo -create \
    "${OUTPUT_DIR}/simulator-arm64/lib/libffmpeg.a" \
    "${OUTPUT_DIR}/simulator-x86_64/lib/libffmpeg.a" \
    -output "${simulator_universal_dir}/lib/libffmpeg.a"

# 使用静态库格式创建XCFramework
xcrun xcodebuild -create-xcframework \
    -library "${OUTPUT_DIR}/iphoneos-arm64/lib/libffmpeg.a" -headers "${OUTPUT_DIR}/iphoneos-arm64/include" \
    -library "${simulator_universal_dir}/lib/libffmpeg.a" -headers "${simulator_universal_dir}/include" \
    -output "${XCFRAMEWORK_PATH}"

echo "清理临时文件..."
# 保留源码和最终XCFramework，删除中间文件
for platform_arch in "iphoneos-arm64" "simulator-arm64" "simulator-x86_64" "simulator-universal"; do
    rm -rf "${OUTPUT_DIR}/${platform_arch}"
done

echo "完成！XCFramework已创建在: ${XCFRAMEWORK_PATH}"
