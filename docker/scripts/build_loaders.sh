#!/usr/bin/env bash
set -euo pipefail

echo "=== Building PS Vita Baremetal Loaders and VPK ==="

BUILD_DIR="/build"
SRC_DIR="${BUILD_DIR}/sources"
OUTPUT_DIR="${BUILD_DIR}/output"
LINUX_OUT="${OUTPUT_DIR}/ux0/linux"
VPK_OUT="${OUTPUT_DIR}/vpk"

mkdir -p "${SRC_DIR}" "${LINUX_OUT}" "${VPK_OUT}"

export VITASDK=/usr/local/vitasdk
export PATH="${VITASDK}/bin:${PATH}"

cd "${SRC_DIR}"

# Ensure taihen headers and library are installed
if [ ! -f "${VITASDK}/arm-vita-eabi/include/taihen.h" ]; then
    echo "Installing taihen via vdpm..."
    echo "y" | vdpm taihen
fi

# 1. Build vita-baremetal-loader (Kernel plugin)
echo "[1/4] Building vita-baremetal-loader..."
if [ ! -d "vita-baremetal-loader" ]; then
    git clone https://github.com/xerpi/vita-baremetal-loader.git
fi
cd vita-baremetal-loader
git checkout . 2>/dev/null || true
if [ -f "${BUILD_DIR}/patches/vita-baremetal-loader/0001-loader-features.patch" ]; then
    git apply "${BUILD_DIR}/patches/vita-baremetal-loader/0001-loader-features.patch" || true
fi
make clean || true
make CFLAGS="-std=gnu17 -Wl,-q -Wall -O0 -nostartfiles -mcpu=cortex-a9 -mthumb-interwork"
if [ -f "baremetal-loader_363.skprx" ]; then
    cp baremetal-loader_363.skprx "${LINUX_OUT}/baremetal-loader.skprx"
    cp baremetal-loader.skprx "${LINUX_OUT}/baremetal-loader_360.skprx" 2>/dev/null || true
elif [ -f "baremetal-loader.skprx" ]; then
    cp baremetal-loader.skprx "${LINUX_OUT}/baremetal-loader.skprx"
fi
cd "${SRC_DIR}"

# 2. Build and install libbaremetal
echo "[2/4] Building and installing libbaremetal..."
if [ ! -d "vita-libbaremetal" ]; then
    git clone https://github.com/xerpi/vita-libbaremetal.git
fi
cd vita-libbaremetal
git checkout . 2>/dev/null || true
if [ -f "${BUILD_DIR}/patches/vita-libbaremetal/0001-max-brightness.patch" ]; then
    git apply "${BUILD_DIR}/patches/vita-libbaremetal/0001-max-brightness.patch" || true
fi
cd libbaremetal
make clean || true
make install
cd "${SRC_DIR}"

# 3. Build vita-baremetal-linux-loader (Payload)
echo "[3/4] Building vita-baremetal-linux-loader..."
if [ ! -d "vita-baremetal-linux-loader" ]; then
    git clone https://github.com/xerpi/vita-baremetal-linux-loader.git
fi
cd vita-baremetal-linux-loader
git checkout . 2>/dev/null || true
make clean || true
make CFLAGS="-std=gnu17 -Iinclude -IFatFs -mcpu=cortex-a9 -mthumb-interwork -O0 -g3 -Wall -Wno-unused-const-variable -ffreestanding"
cp vita-baremetal-linux-loader.bin "${LINUX_OUT}/payload.bin"
cd "${SRC_DIR}"

# Ensure clean wpa_supplicant.conf template is in LINUX_OUT for bundling into VPK
cat << 'EOF' > "${LINUX_OUT}/wpa_supplicant.conf"
# LinuxOnVita Wi-Fi Configuration
# ================================
# Edit this file with your Wi-Fi credentials before booting Linux,
# or connect on-device using 'vita-wifi'.

ctrl_interface=/var/run/wpa_supplicant
update_config=1

network={
    ssid="YourWiFiNetworkName"
    psk="YourWiFiPassword"
    key_mgmt=WPA-PSK
}
EOF

# Ensure kernel is built if zImage is missing
if [ ! -f "${LINUX_OUT}/zImage" ]; then
    echo "Notice: ${LINUX_OUT}/zImage not found. Building kernel first..."
    "${BUILD_DIR}/docker/scripts/build_kernel.sh"
fi

# 4. Build LinuxOnVita (All-in-one Bootstrapper & Installer VPK)
echo "[4/4] Building LinuxOnVita VPK..."
APP_DIR_SRC="${BUILD_DIR}/app"
cd "${APP_DIR_SRC}"
rm -rf build && mkdir -p build && cd build
cmake -DLINUX_FILES_DIR="${LINUX_OUT}" ..
make
cp *.vpk "${VPK_OUT}/LinuxOnVita.vpk"
cp *.vpk "${OUTPUT_DIR}/LinuxOnVita.vpk"

# Pre-extract VPK for direct folder deployment (ux0:app/LNXONVITA)
APP_DIR="${OUTPUT_DIR}/ux0/app/LNXONVITA"
rm -rf "${APP_DIR}"
mkdir -p "${APP_DIR}"
unzip -qo "${VPK_OUT}/LinuxOnVita.vpk" -d "${APP_DIR}"

# Also mirror payload to ux0/baremetal for compatibility
BAREMETAL_OUT="${OUTPUT_DIR}/ux0/baremetal"
mkdir -p "${BAREMETAL_OUT}"
cp "${LINUX_OUT}/payload.bin" "${BAREMETAL_OUT}/payload.bin"
cp "${LINUX_OUT}/baremetal-loader.skprx" "${BAREMETAL_OUT}/baremetal-loader.skprx"

cd "${SRC_DIR}"

echo "All loaders and VPK built successfully!"
ls -lh "${LINUX_OUT}/baremetal-loader.skprx" "${LINUX_OUT}/payload.bin" "${OUTPUT_DIR}/LinuxOnVita.vpk"

