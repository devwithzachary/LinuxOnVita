#!/usr/bin/env bash
set -euo pipefail

echo "=== Building RootFS with Buildroot ==="

BUILD_DIR="/build"
SRC_DIR="${BUILD_DIR}/sources"
OUTPUT_DIR="${BUILD_DIR}/output"
CONFIG_FILE="${BUILD_DIR}/configs/buildroot.config"
ROOTFS_OVERLAY="${BUILD_DIR}/rootfs-overlay"

mkdir -p "${SRC_DIR}" "${OUTPUT_DIR}/ux0/linux" "${ROOTFS_OVERLAY}"

cd "${SRC_DIR}"
if [ ! -d "buildroot" ]; then
    echo "Cloning Buildroot..."
    git clone --depth 1 https://github.com/buildroot/buildroot.git
fi

cd buildroot

echo "Applying Buildroot configuration..."
cp "${CONFIG_FILE}" .config

# Ensure rootfs-overlay path exists
mkdir -p "${SRC_DIR}/buildroot/rootfs-overlay"
if [ -d "${ROOTFS_OVERLAY}" ]; then
    cp -r "${ROOTFS_OVERLAY}/." "${SRC_DIR}/buildroot/rootfs-overlay/"
fi

echo "Building Buildroot rootfs (this may take a while)..."
make olddefconfig
make -j"$(nproc)"

echo "Copying rootfs.cpio.xz to outputs..."
cp output/images/rootfs.cpio.xz "${OUTPUT_DIR}/rootfs.cpio.xz"

echo "RootFS build completed successfully: ${OUTPUT_DIR}/rootfs.cpio.xz"
