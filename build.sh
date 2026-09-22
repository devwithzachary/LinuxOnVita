#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_NAME="psvita-linux-builder"

mkdir -p "${ROOT_DIR}/output" "${ROOT_DIR}/sources" "${ROOT_DIR}/rootfs-overlay"

chmod +x "${ROOT_DIR}/docker/scripts/"*.sh 2>/dev/null || true
chmod +x "${ROOT_DIR}/scripts/"*.sh 2>/dev/null || true

usage() {
    echo "Usage: $0 <command>"
    echo ""
    echo "Commands:"
    echo "  prebuilts    - Fetch and stage verified prebuilt binaries (fastest, no build needed)"
    echo "  image        - Build the Docker build environment image (${IMAGE_NAME})"
    echo "  all          - Build RootFS, Kernel, and Loaders inside Docker"
    echo "  rootfs       - Build only Buildroot rootfs inside Docker"
    echo "  kernel       - Build only Linux kernel inside Docker"
    echo "  loaders      - Build VitaSDK loaders and bootstrapper VPK inside Docker"
    echo "  shell        - Open an interactive shell inside the build container"
    echo "  clean        - Clean build artifacts"
    exit 1
}

if [ $# -lt 1 ]; then
    usage
fi

build_docker_image() {
    echo "=== Building Docker image: ${IMAGE_NAME} ==="
    docker build -t "${IMAGE_NAME}" -f "${ROOT_DIR}/docker/Dockerfile" "${ROOT_DIR}"
}

ensure_image() {
    if ! docker image inspect "${IMAGE_NAME}" >/dev/null 2>&1; then
        echo "Docker image '${IMAGE_NAME}' not found. Building it first..."
        build_docker_image
    fi
}

run_in_docker() {
    ensure_image
    local tty_flags="-i"
    if [ -t 0 ] && [ -t 1 ]; then
        tty_flags="-it"
    fi
    docker run --rm ${tty_flags} \
        -v "${ROOT_DIR}:/build" \
        -w /build \
        "${IMAGE_NAME}" \
        "$@"
}

case "$1" in
    prebuilts|fetch-prebuilts)
        "${ROOT_DIR}/scripts/fetch_prebuilts.sh"
        ;;
    image|build-image)
        build_docker_image
        ;;
    shell)
        ensure_image
        run_in_docker /bin/bash
        ;;
    rootfs)
        run_in_docker /build/docker/scripts/build_rootfs.sh
        ;;
    kernel)
        run_in_docker /build/docker/scripts/build_kernel.sh
        ;;
    loaders)
        run_in_docker /build/docker/scripts/build_loaders.sh
        ;;
    all)
        run_in_docker /build/docker/scripts/build_all.sh
        ;;
    clean)
        echo "Cleaning output..."
        rm -rf "${ROOT_DIR}/output"
        mkdir -p "${ROOT_DIR}/output"
        echo "Done."
        ;;
    *)
        usage
        ;;
esac
