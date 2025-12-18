ARG ARCH=armv7hf
ARG REPO=axisecp
ARG SDK=acap-native-sdk
ARG UBUNTU_VERSION=24.04
ARG VERSION=12.7.0

FROM ${REPO}/${SDK}:${VERSION}-${ARCH}-ubuntu${UBUNTU_VERSION}

# Set general arguments
ARG ARCH
ARG SDK_LIB_PATH_BASE=/opt/axis/acapsdk/sysroots/${ARCH}/usr
ARG BUILD_DIR=/opt/build

#-------------------------------------------------------------------------------
# Prepare build environment
#-------------------------------------------------------------------------------

# Install build dependencies for cross compiling OpenCV and GStreamer headers
# Note: libfcgi is available in the ACAP SDK sysroot
RUN DEBIAN_FRONTEND=noninteractive \
    apt-get update && apt-get install -y -f --no-install-recommends \
    cmake \
    meson \
    ninja-build && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

#-------------------------------------------------------------------------------
# Build OpenCV libraries
#-------------------------------------------------------------------------------

ARG OPENCV_VERSION=4.10.0
ARG OPENCV_DIR=${BUILD_DIR}/opencv
ARG OPENCV_SRC_DIR=${OPENCV_DIR}/opencv-${OPENCV_VERSION}
ARG OPENCV_BUILD_DIR=${OPENCV_DIR}/build

WORKDIR ${OPENCV_DIR}
SHELL ["/bin/bash", "-o", "pipefail", "-c"]
RUN curl -fsSL https://github.com/opencv/opencv/archive/$OPENCV_VERSION.tar.gz | tar -xz

WORKDIR ${OPENCV_BUILD_DIR}
ENV COMMON_CMAKE_FLAGS="-S $OPENCV_SRC_DIR \
        -B $OPENCV_BUILD_DIR \
        -D CMAKE_INSTALL_PREFIX=$SDK_LIB_PATH_BASE \
        -D CMAKE_BUILD_TYPE=RELEASE \
        -D WITH_OPENEXR=OFF \
        -D WITH_GTK=OFF \
        -D WITH_V4L=OFF \
        -D WITH_FFMPEG=OFF \
        -D WITH_GSTREAMER=OFF \
        -D WITH_GSTREAMER_0_10=OFF \
        -D BUILD_LIST=core,imgproc,imgcodecs \
        -D BUILD_EXAMPLES=OFF \
        -D BUILD_OPENCV_APPS=OFF \
        -D BUILD_DOCS=OFF \
        -D BUILD_JPEG=ON \
        -D BUILD_PNG=OFF \
        -D WITH_JASPER=OFF \
        -D BUILD_PROTOBUF=OFF \
        -D OPENCV_GENERATE_PKGCONFIG=ON "

# hadolint ignore=SC2086
RUN if [ "$ARCH" = armv7hf ]; then \
        . /opt/axis/acapsdk/environment-setup* && \
        cmake \
        -D CMAKE_CXX_COMPILER=${CXX%-g++*}-g++ \
        -D CMAKE_CXX_FLAGS="${CXX#*-g++}" \
        -D CMAKE_C_COMPILER=${CC%-gcc*}-gcc \
        -D CMAKE_C_FLAGS="${CC#*-gcc}" \
        -D CMAKE_TOOLCHAIN_FILE=${OPENCV_SRC_DIR}/platforms/linux/arm-gnueabi.toolchain.cmake \
        -D CPU_BASELINE=NEON,VFPV3 \
        -D ENABLE_NEON=ON \
        -D ENABLE_VFPV3=ON \
        $COMMON_CMAKE_FLAGS && \
        make -j "$(nproc)" install ; \
    elif [ "$ARCH" = aarch64 ]; then \
        . /opt/axis/acapsdk/environment-setup* && \
        cmake \
        -D CMAKE_CXX_COMPILER=${CXX%-g++*}-g++ \
        -D CMAKE_CXX_FLAGS="${CXX#*-g++}" \
        -D CMAKE_C_COMPILER=${CC%-gcc*}-gcc \
        -D CMAKE_C_FLAGS="${CC#*-gcc}" \
        -D CMAKE_TOOLCHAIN_FILE=${OPENCV_SRC_DIR}/platforms/linux/aarch64-gnu.toolchain.cmake \
        $COMMON_CMAKE_FLAGS && \
        make -j "$(nproc)" install ; \
    else \
        printf "Error: '%s' is not a valid value for the ARCH variable\n", "$ARCH"; \
        exit 1; \
    fi

#-------------------------------------------------------------------------------
# Build GStreamer RTSP Server (headers only - library exists on device)
#-------------------------------------------------------------------------------

ARG GST_VERSION=1.22.12
ARG GST_DIR=${BUILD_DIR}/gstreamer
ARG GST_RTSP_SRC_DIR=${GST_DIR}/gst-rtsp-server-${GST_VERSION}
ARG GST_RTSP_BUILD_DIR=${GST_DIR}/rtsp-build

WORKDIR ${GST_DIR}
RUN curl -fsSL https://gstreamer.freedesktop.org/src/gst-rtsp-server/gst-rtsp-server-${GST_VERSION}.tar.xz | tar -xJ

WORKDIR ${GST_RTSP_BUILD_DIR}

# Build gst-rtsp-server to install headers into SDK sysroot (library won't be bundled)
# hadolint ignore=SC2086,SC2129
RUN . /opt/axis/acapsdk/environment-setup* && \
    CROSS_FILE=/tmp/cross.txt && \
    C_COMPILER=$(echo $CC | awk '{print $1}') && \
    CXX_COMPILER=$(echo $CXX | awk '{print $1}') && \
    C_ARGS=$(echo $CC | cut -d' ' -f2-) && \
    SYSROOT=$(echo $CC | grep -oP -- '--sysroot=\K[^ ]+') && \
    printf '[binaries]\n' > $CROSS_FILE && \
    printf "c = '%s'\n" "$C_COMPILER" >> $CROSS_FILE && \
    printf "cpp = '%s'\n" "$CXX_COMPILER" >> $CROSS_FILE && \
    printf "ar = '%s'\n" "$AR" >> $CROSS_FILE && \
    printf "strip = '%s'\n" "$STRIP" >> $CROSS_FILE && \
    printf "pkgconfig = 'pkg-config'\n\n" >> $CROSS_FILE && \
    printf '[built-in options]\n' >> $CROSS_FILE && \
    printf "c_args = ['%s']\n" "$(echo $C_ARGS | sed "s/ /', '/g")" >> $CROSS_FILE && \
    printf "c_link_args = ['%s']\n\n" "$(echo $C_ARGS | sed "s/ /', '/g")" >> $CROSS_FILE && \
    printf '[host_machine]\n' >> $CROSS_FILE && \
    printf "system = 'linux'\n" >> $CROSS_FILE && \
    if [ "$ARCH" = armv7hf ]; then \
        printf "cpu_family = 'arm'\n" >> $CROSS_FILE && \
        printf "cpu = 'armv7hf'\n" >> $CROSS_FILE ; \
    else \
        printf "cpu_family = 'aarch64'\n" >> $CROSS_FILE && \
        printf "cpu = 'aarch64'\n" >> $CROSS_FILE ; \
    fi && \
    printf "endian = 'little'\n" >> $CROSS_FILE && \
    PKG_CONFIG_SYSROOT_DIR=$SYSROOT \
    PKG_CONFIG_PATH=$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig \
    meson setup ${GST_RTSP_SRC_DIR} ${GST_RTSP_BUILD_DIR} \
        --cross-file $CROSS_FILE \
        --prefix=${SDK_LIB_PATH_BASE} \
        -Dexamples=disabled \
        -Dtests=disabled \
        -Ddoc=disabled \
        -Dintrospection=disabled && \
    ninja -C ${GST_RTSP_BUILD_DIR} && \
    ninja -C ${GST_RTSP_BUILD_DIR} install

#-------------------------------------------------------------------------------
# Copy the built library files to application directory
#-------------------------------------------------------------------------------

WORKDIR /opt/app
COPY ./app .
RUN mkdir -p lib && \
    cp -P ${OPENCV_BUILD_DIR}/lib/lib*.so* ./lib/

#-------------------------------------------------------------------------------
# Finally build the ACAP application
#-------------------------------------------------------------------------------

RUN . /opt/axis/acapsdk/environment-setup* && acap-build .
