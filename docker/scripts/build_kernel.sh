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
    
    # Apply tracked kernel defconfig if present in repository
    if [ -f "${BUILD_DIR}/configs/kernel/vita_defconfig" ]; then
        echo "Using tracked configs/kernel/vita_defconfig..."
        cp "${BUILD_DIR}/configs/kernel/vita_defconfig" "${PORT_DIR}/linux_vita/arch/arm/configs/vita_defconfig"
    fi

    # Apply tracked kernel patches (DTS SD2Vita support, etc.)
    if [ -d "${BUILD_DIR}/patches/kernel" ]; then
        for p in "${BUILD_DIR}/patches/kernel/"*.patch; do
            if [ -f "$p" ] && git -C "${PORT_DIR}/linux_vita" apply --check "$p" >/dev/null 2>&1; then
                echo "Applying kernel patch: $p"
                git -C "${PORT_DIR}/linux_vita" apply "$p" || true
            fi
        done
    fi

    echo "Applying vita_defconfig..."
    make config CROSS_COMPILE="${CROSS_COMPILE}"
    "${PORT_DIR}/linux_vita/scripts/config" --file "${PORT_DIR}/linux_vita/.config" --enable CONFIG_INPUT_MISC
    "${PORT_DIR}/linux_vita/scripts/config" --file "${PORT_DIR}/linux_vita/.config" --enable CONFIG_INPUT_UINPUT
    "${PORT_DIR}/linux_vita/scripts/config" --file "${PORT_DIR}/linux_vita/.config" --enable CONFIG_EXT4_FS
    "${PORT_DIR}/linux_vita/scripts/config" --file "${PORT_DIR}/linux_vita/.config" --enable CONFIG_EXT4_FS_POSIX_ACL
    "${PORT_DIR}/linux_vita/scripts/config" --file "${PORT_DIR}/linux_vita/.config" --enable CONFIG_EXT4_FS_SECURITY
    "${PORT_DIR}/linux_vita/scripts/config" --file "${PORT_DIR}/linux_vita/.config" --enable CONFIG_SWAP
    "${PORT_DIR}/linux_vita/scripts/config" --file "${PORT_DIR}/linux_vita/.config" --enable CONFIG_ZRAM
    "${PORT_DIR}/linux_vita/scripts/config" --file "${PORT_DIR}/linux_vita/.config" --enable CONFIG_ZRAM_BACKEND_LZ4
    "${PORT_DIR}/linux_vita/scripts/config" --file "${PORT_DIR}/linux_vita/.config" --enable CONFIG_ZRAM_DEF_COMP_LZ4
    "${PORT_DIR}/linux_vita/scripts/config" --file "${PORT_DIR}/linux_vita/.config" --set-str CONFIG_ZRAM_DEF_COMP "lz4"
    "${PORT_DIR}/linux_vita/scripts/config" --file "${PORT_DIR}/linux_vita/.config" --enable CONFIG_ZSMALLOC
    "${PORT_DIR}/linux_vita/scripts/config" --file "${PORT_DIR}/linux_vita/.config" --enable CONFIG_LZ4_COMPRESS
    "${PORT_DIR}/linux_vita/scripts/config" --file "${PORT_DIR}/linux_vita/.config" --enable CONFIG_LZ4_DECOMPRESS
    
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

    # Apply tracked kernel defconfig if present in repository
    if [ -f "${BUILD_DIR}/configs/kernel/vita_defconfig" ]; then
        echo "Using tracked configs/kernel/vita_defconfig..."
        cp "${BUILD_DIR}/configs/kernel/vita_defconfig" "arch/arm/configs/vita_defconfig"
    fi

    # Apply tracked kernel patches (DTS SD2Vita support, button mask, etc.)
    if [ -d "${BUILD_DIR}/patches/kernel" ]; then
        for p in "${BUILD_DIR}/patches/kernel/"*.patch; do
            if [ -f "$p" ] && git apply --check "$p" >/dev/null 2>&1; then
                echo "Applying kernel patch: $p"
                git apply "$p" || true
            fi
        done
    fi

    make ARCH=arm vita_defconfig
    ./scripts/config --file .config --enable CONFIG_INPUT_MISC
    ./scripts/config --file .config --enable CONFIG_INPUT_UINPUT
    ./scripts/config --file .config --enable CONFIG_EXT4_FS
    ./scripts/config --file .config --enable CONFIG_EXT4_FS_POSIX_ACL
    ./scripts/config --file .config --enable CONFIG_EXT4_FS_SECURITY
    ./scripts/config --file .config --enable CONFIG_SWAP
    ./scripts/config --file .config --enable CONFIG_ZRAM
    ./scripts/config --file .config --enable CONFIG_ZRAM_BACKEND_LZ4
    ./scripts/config --file .config --enable CONFIG_ZRAM_DEF_COMP_LZ4
    ./scripts/config --file .config --set-str CONFIG_ZRAM_DEF_COMP "lz4"
    ./scripts/config --file .config --enable CONFIG_ZSMALLOC
    ./scripts/config --file .config --enable CONFIG_LZ4_COMPRESS
    ./scripts/config --file .config --enable CONFIG_LZ4_DECOMPRESS
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
