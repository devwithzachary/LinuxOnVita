FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install host compilation tools and dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    git \
    bc \
    bison \
    flex \
    libssl-dev \
    libncurses5-dev \
    rsync \
    cpio \
    unzip \
    wget \
    curl \
    cmake \
    python3 \
    python3-pip \
    bzip2 \
    xz-utils \
    zstd \
    libelf-dev \
    pkg-config \
    file \
    ca-certificates \
    libglib2.0-dev \
    libpixman-1-dev \
    gcc-arm-linux-gnueabihf \
    g++-arm-linux-gnueabihf \
    && rm -rf /var/lib/apt/lists/*

# Install Bootlin ARMv7-eabihf toolchain (required by xerpi's buildroot configuration)
ENV TOOLCHAIN_DIR=/opt/armv7-eabihf--glibc--bleeding-edge-2023.08-1
RUN mkdir -p /opt && \
    curl -sSL https://toolchains.bootlin.com/downloads/releases/toolchains/armv7-eabihf/tarballs/armv7-eabihf--glibc--bleeding-edge-2023.08-1.tar.bz2 \
    | tar -xj -C /opt

# Install VitaSDK
ENV VITASDK=/usr/local/vitasdk
RUN git clone https://github.com/vitasdk/vdpm /tmp/vdpm && \
    cd /tmp/vdpm && \
    ./bootstrap-vitasdk.sh --install-dir /usr/local/vitasdk && \
    rm -rf /tmp/vdpm

# Set environment PATH
ENV PATH="${TOOLCHAIN_DIR}/bin:${VITASDK}/bin:${PATH}"

WORKDIR /build

CMD ["/bin/bash"]
