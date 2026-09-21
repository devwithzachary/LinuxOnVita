#!/usr/bin/env bash
set -euo pipefail

echo "=== Building Linux Kernel for PS Vita ==="

BUILD_DIR="/build"
SRC_DIR="${BUILD_DIR}/sources"
OUTPUT_DIR="${BUILD_DIR}/output"
LINUX_OUT="${OUTPUT_DIR}/ux0/linux"

mkdir -p "${SRC_DIR}" "${LINUX_OUT}"

ROOTFS_XZ="${OUTPUT_DIR}/rootfs.cpio.xz"
if [ ! -f "${ROOTFS_XZ}" ]; then
    echo "Notice: ${ROOTFS_XZ} not found. Checking if buildroot produced one..."
    if [ -f "${SRC_DIR}/buildroot/output/images/rootfs.cpio.xz" ]; then
        cp "${SRC_DIR}/buildroot/output/images/rootfs.cpio.xz" "${ROOTFS_XZ}"
    else
        echo "Error: rootfs.cpio.xz not found! Run build_rootfs.sh first."
        exit 1
    fi
fi

cd "${SRC_DIR}"
if [ ! -d "linux_vita" ]; then
    echo "Cloning xerpi/linux_vita..."
    git clone https://github.com/xerpi/linux_vita.git
fi

cd linux_vita

echo "Copying rootfs.cpio.xz into kernel tree..."
cp "${ROOTFS_XZ}" ./

echo "Configuring kernel (vita_defconfig)..."
make ARCH=arm vita_defconfig

echo "Compiling zImage..."
make ARCH=arm CROSS_COMPILE=arm-linux- -j"$(nproc)"

echo "Compiling Device Tree Blobs (DTBs)..."
make ARCH=arm CROSS_COMPILE=arm-linux- vita1000.dtb vita2000.dtb pstv.dtb

echo "Copying compiled kernel and DTBs to output directory..."
cp arch/arm/boot/zImage "${LINUX_OUT}/zImage"
cp arch/arm/boot/dts/vita1000.dtb "${LINUX_OUT}/vita1000.dtb"
cp arch/arm/boot/dts/vita2000.dtb "${LINUX_OUT}/vita2000.dtb"
cp arch/arm/boot/dts/pstv.dtb "${LINUX_OUT}/pstv.dtb"
# Provide fallback vita.dtb (defaulting to vita1000)
cp arch/arm/boot/dts/vita1000.dtb "${LINUX_OUT}/vita.dtb"

echo "Kernel build completed successfully!"
ls -lh "${LINUX_OUT}/zImage" "${LINUX_OUT}"/*.dtb
