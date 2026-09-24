#!/usr/bin/env bash
set -euo pipefail

# build_release.sh: Builds the full LinuxOnVita system and packages it into a
# distributable zip file suitable for uploading to GitHub Releases.
#
# Wi-Fi credentials are intentionally NEVER included in the release zip.
# Users must add their own wpa_supplicant.conf to ux0:linux/ after flashing.

export FORCE_UNSAFE_CONFIGURE=1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="/build"
OUTPUT_DIR="${BUILD_DIR}/output"
RELEASE_DIR="${OUTPUT_DIR}/release"

# Determine version: use git tag if available, otherwise date-based
VERSION=$(git -C "${BUILD_DIR}" describe --tags --exact-match 2>/dev/null \
    || git -C "${BUILD_DIR}" describe --tags --abbrev=4 2>/dev/null \
    || date +"%Y%m%d")

RELEASE_NAME="LinuxOnVita-release-${VERSION}"
RELEASE_ZIP="${OUTPUT_DIR}/${RELEASE_NAME}.zip"

echo "========================================================="
echo " LinuxOnVita Public Release Build"
echo " Version: ${VERSION}"
echo " Output:  ${RELEASE_ZIP}"
echo "========================================================="
echo ""

# Step 1: Full build
echo "[1/3] Building all components..."
"${SCRIPT_DIR}/build_all.sh"

# Step 2: Assemble clean release directory
echo ""
echo "[2/3] Assembling release package..."
rm -rf "${RELEASE_DIR}"
mkdir -p "${RELEASE_DIR}/ux0/linux" "${RELEASE_DIR}/ux0/app"

# Copy kernel, DTBs, and loaders
cp "${OUTPUT_DIR}/ux0/linux/"*.skprx   "${RELEASE_DIR}/ux0/linux/" 2>/dev/null || true
cp "${OUTPUT_DIR}/ux0/linux/"*.bin     "${RELEASE_DIR}/ux0/linux/" 2>/dev/null || true
cp "${OUTPUT_DIR}/ux0/linux/"zImage    "${RELEASE_DIR}/ux0/linux/" 2>/dev/null || true
cp "${OUTPUT_DIR}/ux0/linux/"*.dtb     "${RELEASE_DIR}/ux0/linux/" 2>/dev/null || true

# Copy LiveArea launcher app folder (unpacked)
cp -r "${OUTPUT_DIR}/ux0/app/LNXONVITA" "${RELEASE_DIR}/ux0/app/" 2>/dev/null || true

# Copy standalone LinuxOnVita.vpk into the release package for easy 1-click VitaShell installation
if [ -f "${OUTPUT_DIR}/vpk/LinuxOnVita.vpk" ]; then
    cp "${OUTPUT_DIR}/vpk/LinuxOnVita.vpk" "${RELEASE_DIR}/LinuxOnVita.vpk"
fi

# Include the wpa_supplicant template so users know what to edit
cat > "${RELEASE_DIR}/ux0/linux/wpa_supplicant.conf" << 'EOF'
# LinuxOnVita Wi-Fi Configuration
# ================================
# Edit this file with your Wi-Fi credentials before booting Linux.
# Copy it to ux0:linux/wpa_supplicant.conf on your Vita memory card.
#
# Tip: You can also drop configs/wifi.conf in the repository before building
# to have credentials baked in automatically (it is gitignored).

network={
    ssid="YourWiFiNetworkName"
    psk="YourWiFiPassword"
    key_mgmt=WPA-PSK
}
EOF

# Include an install instructions text file
cat > "${RELEASE_DIR}/INSTALL.txt" << EOF
LinuxOnVita ${VERSION} - Installation Instructions
==========================================================

1. Copy 'LinuxOnVita.vpk' to your PS Vita (e.g. 'ux0:LinuxOnVita.vpk')
   using VitaShell (via USB or FTP mode).

2. In VitaShell, navigate to 'LinuxOnVita.vpk', press CROSS (X) to install,
   and confirm prompts (including extended permissions).

3. Return to the LiveArea home screen, tap the 'LinuxOnVita' bubble, and open it.

4. In the LinuxOnVita app:
   - The app displays all detected storage mounts and their capacities.
   - Use D-PAD UP/DOWN or L/R to select your official Sony Memory Card
     (e.g. 'xmc0:' or 'uma0:' for SD2Vita setups, or 'ux0:' for standard consoles).
   - Press CROSS (X) to install all bundled Linux files to your memory card.
   - If files were previously placed on 'ux0:', press TRIANGLE to copy them.

5. Press CROSS (X) to boot into Linux!

NOTE FOR SD2VITA USERS:
An official physical Sony memory card is strictly required on all consoles
(both 1000 and 2000 models). The baremetal loader initializes storage using
Sony's proprietary memory card interface (MSIF). It cannot boot Linux from
SD2Vita or internal storage. If your SD2Vita adapter is mounted as ux0:,
select your official Sony memory card (typically mounted as 'xmc0:' or
'uma0:' in VitaShell) inside the app, and install directly to it.

Full documentation: https://github.com/devwithzachary/LinuxOnVita
EOF

# Step 3: Package release archive
# Ensure 'zip' is available (may not be in older image builds; Dockerfile now includes it)
if ! command -v zip >/dev/null 2>&1; then
    echo "[3/3] 'zip' not found - installing..."
    apt-get install -y --no-install-recommends zip >/dev/null 2>&1 || true
fi

if command -v zip >/dev/null 2>&1; then
    echo "[3/3] Creating release zip: ${RELEASE_ZIP}"
    rm -f "${RELEASE_ZIP}"
    cd "${RELEASE_DIR}"
    zip -r "${RELEASE_ZIP}" . -x "*.DS_Store"

    echo ""
    echo "========================================================="
    echo " Release package ready!"
    echo " File: ${RELEASE_ZIP}"
    SIZE=$(du -sh "${RELEASE_ZIP}" | cut -f1)
    echo " Size: ${SIZE}"
    echo ""
    echo " Contents:"
    unzip -l "${RELEASE_ZIP}" | tail -n +4 | head -n -2
    echo "========================================================="
else
    # Fallback: tar.gz (always available)
    RELEASE_TAR="${OUTPUT_DIR}/${RELEASE_NAME}.tar.gz"
    echo "[3/3] 'zip' unavailable - falling back to tar.gz: ${RELEASE_TAR}"
    cd "${RELEASE_DIR}"
    tar -czf "${RELEASE_TAR}" .
    echo ""
    echo "========================================================="
    echo " Release package ready (tar.gz fallback)!"
    echo " File: ${RELEASE_TAR}"
    SIZE=$(du -sh "${RELEASE_TAR}" | cut -f1)
    echo " Size: ${SIZE}"
    echo " Note: Rebuild the Docker image ('./build.sh image') to get .zip output next time."
    echo "========================================================="
fi
