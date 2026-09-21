#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=========================================="
echo " Starting Full PS Vita Linux Compilation "
echo "=========================================="

"${SCRIPT_DIR}/build_rootfs.sh"
"${SCRIPT_DIR}/build_kernel.sh"
"${SCRIPT_DIR}/build_loaders.sh"

echo ""
echo "=========================================="
echo " All components built successfully!       "
echo " Files are located in /build/output       "
echo "=========================================="
