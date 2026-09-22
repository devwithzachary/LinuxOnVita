#!/usr/bin/env bash
set -euo pipefail

# Directories
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT_DIR}/output"
LINUX_DIR="${OUT_DIR}/ux0/linux"
VPK_DIR="${OUT_DIR}/vpk"
TMP_DIR="${ROOT_DIR}/.tmp_prebuilts"

echo "=== PS Vita Linux: Downloading Prebuilt Binaries ==="

mkdir -p "${LINUX_DIR}" "${VPK_DIR}" "${TMP_DIR}"

# 1. Fetch Xerpi's Linux 5.9.0-rc5 Kernel + Device Tree + Baremetal Loader Payload
echo "[1/3] Downloading Xerpi's kernel and payload (5.9.0-rc5)..."
curl -sSL -o "${TMP_DIR}/linux-vita-5.9.0-rc5.zip" \
    "https://github.com/xerpi/linux_vita/releases/download/5.9.0-rc5/linux-vita-5.9.0-rc5.zip"

unzip -q -o "${TMP_DIR}/linux-vita-5.9.0-rc5.zip" -d "${TMP_DIR}/xerpi_release"

# Copy kernel & DTB
cp "${TMP_DIR}/xerpi_release/linux/zImage" "${LINUX_DIR}/zImage"
cp "${TMP_DIR}/xerpi_release/linux/vita.dtb" "${LINUX_DIR}/vita.dtb"
# Also keep copies for specific models if needed
cp "${TMP_DIR}/xerpi_release/linux/vita.dtb" "${LINUX_DIR}/vita1000.dtb"
cp "${TMP_DIR}/xerpi_release/linux/vita.dtb" "${LINUX_DIR}/vita2000.dtb"

# Copy baremetal payload
cp "${TMP_DIR}/xerpi_release/baremetal/payload.bin" "${LINUX_DIR}/payload.bin"

# 2. Fetch Baremetal Loader SKPRX (FW >= 3.63 compatible, works with 3.65 Enso)
echo "[2/3] Downloading baremetal loader plugin..."
curl -sSL -o "${LINUX_DIR}/baremetal-loader.skprx" \
    "https://github.com/DvaMishkiLapa/vita-baremetal-loader/releases/download/v0.1.0-alpha/baremetal-loader_363_or_newer.skprx"

# Also save the older (<3.63) version just in case user is on 3.60
curl -sSL -o "${LINUX_DIR}/baremetal-loader_older_than_363.skprx" \
    "https://github.com/DvaMishkiLapa/vita-baremetal-loader/releases/download/v0.1.0-alpha/baremetal-loader_older_than_363.skprx"

# 3. Fetch Vita Linux Bootstrapper VPK (User friendly loader with file sanity checks)
echo "[3/3] Downloading Vita Linux Bootstrapper VPK..."
curl -sSL -o "${VPK_DIR}/vita-linux-bootstrapper.vpk" \
    "https://github.com/DvaMishkiLapa/vita_plugin_linux_loader/releases/download/v0.1.0-alpha/vita-linux-bootstrapper.vpk"

# Also unpack directly into ux0/app/VITALINUX for direct folder transfer
mkdir -p "${OUT_DIR}/ux0/app/VITALINUX"
unzip -q -o "${VPK_DIR}/vita-linux-bootstrapper.vpk" -d "${OUT_DIR}/ux0/app/VITALINUX"

# Cleanup
rm -rf "${TMP_DIR}"

echo ""
echo "=== Done! Files prepared in ${OUT_DIR} ==="
echo "Tree:"
find "${OUT_DIR}" -type f
echo ""
echo "Deployment Instructions:"
echo "1. Connect your PS Vita to your PC/Mac via VitaShell (USB or FTP)."
echo "2. Copy all files from '${LINUX_DIR}/' to 'ux0:linux/' on the Vita."
echo "   (IMPORTANT: If using SD2Vita, also copy zImage and vita.dtb to your Sony Memory Card / uma0:)"
echo "3. Copy '${VPK_DIR}/vita-linux-bootstrapper.vpk' to 'ux0:' and install it in VitaShell."
echo "4. In PS Vita Settings -> HENkaku Settings, ensure 'Enable Unsafe Homebrew' is checked."
echo "5. Run 'Vita Linux Bootstrapper' from the LiveArea!"
