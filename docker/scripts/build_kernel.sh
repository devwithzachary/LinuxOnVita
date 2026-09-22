#!/usr/bin/env bash
set -euo pipefail

echo "=== Building Linux 6.12 Kernel for PlayStation Vita ==="

BUILD_DIR="/build"
SRC_DIR="${BUILD_DIR}/sources"
OUTPUT_DIR="${BUILD_DIR}/output"
LINUX_OUT="${OUTPUT_DIR}/ux0/linux"
PORT_DIR="${SRC_DIR}/vita-linux-port"

mkdir -p "${SRC_DIR}" "${LINUX_OUT}"

# Ensure RootFS exists
ROOTFS_ZST="${OUTPUT_DIR}/rootfs.cpio.zst"
if [ ! -f "${ROOTFS_ZST}" ]; then
    echo "Notice: ${ROOTFS_ZST} not found. Running build_rootfs.sh first..."
    "${BUILD_DIR}/docker/scripts/build_rootfs.sh"
fi

# Auto-detect working cross compiler
if command -v arm-linux-gnueabihf-gcc >/dev/null 2>&1; then
    CROSS_COMPILE="arm-linux-gnueabihf-"
elif command -v arm-linux-gcc >/dev/null 2>&1; then
    CROSS_COMPILE="arm-linux-"
else
    CROSS_COMPILE="arm-linux-gnueabihf-"
fi
echo "Using cross-compiler prefix: ${CROSS_COMPILE}"

if [ -d "${PORT_DIR}" ]; then
    echo "Building kernel via vita-linux-port orchestration..."
    cd "${PORT_DIR}"
    
    # Ensure rootfs.cpio.zst is in place
    cp "${ROOTFS_ZST}" "${PORT_DIR}/linux_vita/rootfs.cpio.zst"
    
    echo "Applying vita_defconfig..."
    make config CROSS_COMPILE="${CROSS_COMPILE}"
    
    echo "Compiling zImage and DTBs..."
    make build CROSS_COMPILE="${CROSS_COMPILE}"
    
    echo "Copying compiled kernel and DTBs to ${LINUX_OUT}..."
    DTB_DIR="linux_vita/arch/arm/boot/dts"
    if [ -f "linux_vita/arch/arm/boot/dts/sony/vita1000.dtb" ]; then
        DTB_DIR="linux_vita/arch/arm/boot/dts/sony"
    fi
    cp linux_vita/arch/arm/boot/zImage "${LINUX_OUT}/zImage"
    cp "${DTB_DIR}/vita1000.dtb" "${LINUX_OUT}/vita1000.dtb"
    cp "${DTB_DIR}/vita2000.dtb" "${LINUX_OUT}/vita2000.dtb"
    cp "${DTB_DIR}/pstv.dtb" "${LINUX_OUT}/pstv.dtb"
    cp "${DTB_DIR}/vita1000.dtb" "${LINUX_OUT}/vita.dtb"
else
    echo "Building standalone kernel from linux_vita..."
    cd "${SRC_DIR}"
    if [ ! -d "linux_vita" ]; then
        git clone --branch vita-port-6.12.0 --depth 1 https://github.com/incognitojam/linux_vita.git
    fi
    cd linux_vita
    cp "${ROOTFS_ZST}" ./rootfs.cpio.zst
    make ARCH=arm vita_defconfig
    make ARCH=arm CROSS_COMPILE="${CROSS_COMPILE}" -j"$(nproc)" zImage
    make ARCH=arm CROSS_COMPILE="${CROSS_COMPILE}" sony/vita1000.dtb sony/vita2000.dtb sony/pstv.dtb
    
    cp arch/arm/boot/zImage "${LINUX_OUT}/zImage"
    cp arch/arm/boot/dts/sony/vita1000.dtb "${LINUX_OUT}/vita1000.dtb"
    cp arch/arm/boot/dts/sony/vita2000.dtb "${LINUX_OUT}/vita2000.dtb"
    cp arch/arm/boot/dts/sony/pstv.dtb "${LINUX_OUT}/pstv.dtb"
    cp arch/arm/boot/dts/sony/vita1000.dtb "${LINUX_OUT}/vita.dtb"
fi

echo "=== Linux 6.12 Kernel Build Completed Successfully! ==="
ls -lh "${LINUX_OUT}/zImage" "${LINUX_OUT}"/*.dtb
