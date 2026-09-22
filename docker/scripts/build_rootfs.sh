#!/usr/bin/env bash
set -euo pipefail

echo "=== Building RootFS for PS Vita Linux 6.12 ==="

BUILD_DIR="/build"
SRC_DIR="${BUILD_DIR}/sources"
OUTPUT_DIR="${BUILD_DIR}/output"
ROOTFS_OVERLAY="${BUILD_DIR}/rootfs-overlay"
TOOLS_DIR="${BUILD_DIR}/tools"
PORT_DIR="${SRC_DIR}/vita-linux-port"

mkdir -p "${SRC_DIR}" "${OUTPUT_DIR}/ux0/linux" "${ROOTFS_OVERLAY}/usr/bin"

# 1. Compile native input tools (fbkeyboard & vita-input-mapper)
echo "=== Compiling native input tools for ARMv7 ==="
if [ -d "${TOOLS_DIR}" ]; then
    make -C "${TOOLS_DIR}" clean
    make -C "${TOOLS_DIR}" CROSS_COMPILE=arm-linux-
    cp "${TOOLS_DIR}/fbkeyboard/fbkeyboard" "${ROOTFS_OVERLAY}/usr/bin/"
    cp "${TOOLS_DIR}/vita-input-mapper/vita-input-mapper" "${ROOTFS_OVERLAY}/usr/bin/"
    echo "Input tools compiled and staged to ${ROOTFS_OVERLAY}/usr/bin/"
fi

# Ensure Marvell Wi-Fi firmware is present in overlay
FIRMWARE_BIN="${ROOTFS_OVERLAY}/lib/firmware/mrvl/sd8787_uapsta.bin"
if [ ! -f "${FIRMWARE_BIN}" ]; then
    echo "Downloading Marvell 88W8787 firmware..."
    mkdir -p "${ROOTFS_OVERLAY}/lib/firmware/mrvl"
    curl -fsSL -o "${FIRMWARE_BIN}" \
        https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/mrvl/sd8787_uapsta.bin
fi

# Ensure Alpine Linux mini-rootfs is present in overlay
ALPINE_TAR="${ROOTFS_OVERLAY}/usr/share/alpine/alpine-minirootfs-armv7.tar.gz"
if [ ! -f "${ALPINE_TAR}" ]; then
    echo "Downloading Alpine Linux ARMv7 mini-rootfs..."
    mkdir -p "${ROOTFS_OVERLAY}/usr/share/alpine"
    curl -fsSL -o "${ALPINE_TAR}" \
        https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/armv7/alpine-minirootfs-3.20.0-armv7.tar.gz
fi

# 2. Clone or update vita-linux-port
cd "${SRC_DIR}"
if [ ! -d "vita-linux-port" ]; then
    echo "Cloning incognitojam/vita-linux-port..."
    git clone --recurse-submodules https://github.com/incognitojam/vita-linux-port.git
else
    echo "vita-linux-port already exists. Updating submodules..."
    cd vita-linux-port
    git submodule update --init --recursive
    cd ..
fi

cd "${PORT_DIR}"

# 3. Setup local overlay with user Wi-Fi credentials
LOCAL_OVERLAY="${PORT_DIR}/buildroot-vita/board/vita/local"
mkdir -p "${LOCAL_OVERLAY}/etc" "${LOCAL_OVERLAY}/etc/ssh" "${LOCAL_OVERLAY}/root/.ssh"

WIFI_CONF="${BUILD_DIR}/configs/wifi.conf"
WPA_TARGET="${LOCAL_OVERLAY}/etc/wpa_supplicant.conf"
if [ -f "${WIFI_CONF}" ]; then
    echo "Applying Wi-Fi credentials from configs/wifi.conf..."
    # shellcheck source=/dev/null
    source "${WIFI_CONF}"
    cat << EOF > "${WPA_TARGET}"
ctrl_interface=/var/run/wpa_supplicant
update_config=1

network={
    ssid="${SSID}"
    psk="${PSK}"
}
EOF
    echo "Wi-Fi network '${SSID}' configured."
else
    echo "Notice: configs/wifi.conf not found. Using placeholder credentials."
    cat << 'EOF' > "${WPA_TARGET}"
ctrl_interface=/var/run/wpa_supplicant
update_config=1

network={
    ssid="YourNetworkName"
    psk="YourPassword"
}
EOF
fi
cp "${WPA_TARGET}" "${ROOTFS_OVERLAY}/etc/wpa_supplicant.conf" 2>/dev/null || true

# 4. Inject rootfs-overlay into buildroot-vita overlay
echo "Copying rootfs-overlay files into buildroot-vita overlay..."
cp -r "${ROOTFS_OVERLAY}/." "${PORT_DIR}/buildroot-vita/board/vita/overlay/"

# 5. Build rootfs via Buildroot
echo "Building rootfs via Buildroot (this may take several minutes on first build)..."
make rootfs

# 6. Copy output rootfs.cpio.zst
if [ -f "${PORT_DIR}/linux_vita/rootfs.cpio.zst" ]; then
    cp "${PORT_DIR}/linux_vita/rootfs.cpio.zst" "${OUTPUT_DIR}/rootfs.cpio.zst"
    echo "=== RootFS build completed successfully: ${OUTPUT_DIR}/rootfs.cpio.zst ($ (du -h "${OUTPUT_DIR}/rootfs.cpio.zst" | cut -f1)) ==="
elif [ -f "${PORT_DIR}/buildroot/output/images/rootfs.cpio.zst" ]; then
    cp "${PORT_DIR}/buildroot/output/images/rootfs.cpio.zst" "${OUTPUT_DIR}/rootfs.cpio.zst"
    echo "=== RootFS build completed successfully: ${OUTPUT_DIR}/rootfs.cpio.zst ==="
else
    echo "Error: rootfs.cpio.zst was not generated!"
    exit 1
fi
