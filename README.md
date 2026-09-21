# PS Vita Linux Toolkit & Docker Build Environment

[![PS Vita](https://img.shields.io/badge/Platform-PlayStation%20Vita-blue.svg)](https://en.wikipedia.org/wiki/PlayStation_Vita)
[![Architecture](https://img.shields.io/badge/Architecture-ARMv7--A%20(Cortex--A9)-green.svg)](https://developer.arm.com/Processors/Cortex-A9)
[![License](https://img.shields.io/badge/License-GPL%20v2%20%2F%20MIT-orange.svg)](#credits--upstream-sources)

A complete toolkit, automated downloader, and containerized Docker build environment for compiling and running Linux on hacked PlayStation Vita consoles ([HENlo](https://vita.hacks.guide/using-henlo) / HENkaku / Ensō).

This repository bridges the gap between [xerpi's](https://github.com/xerpi) foundational Linux port and modern developer setups, providing:
1. **Fast-Track (5 Minutes):** An automated script that fetches verified, working prebuilt releases (`zImage`, `vita.dtb`, `payload.bin`, `baremetal-loader.skprx`, and `vita-linux-bootstrapper.vpk`) ready for instant transfer.
2. **Reproducible Docker Environment:** A containerized build system that avoids the friction of cross-compiling ARMv7 Linux kernels, Buildroot root filesystems, and VitaSDK plugins natively on macOS or non-Linux hosts.

---

## Table of Contents

- [1. Architecture: How Linux Boots on the Vita](#1-architecture-how-linux-boots-on-the-vita)
- [2. Hardware Support & Status](#2-hardware-support--status)
- [3. Critical Gotchas (Read Before Booting!)](#3-critical-gotchas-read-before-booting)
- [4. Quick Start: Using Prebuilt Binaries (5 Minutes)](#4-quick-start-using-prebuilt-binaries-5-minutes)
- [5. Building from Source with Docker](#5-building-from-source-with-docker)
  - [Build Commands](#build-commands)
  - [Customizing the RootFS Overlay](#customizing-the-rootfs-overlay)
- [6. Troubleshooting](#6-troubleshooting)
- [7. Credits & Upstream Sources](#7-credits--upstream-sources)

---

## 1. Architecture: How Linux Boots on the Vita

The PlayStation Vita runs Sony's proprietary microkernel-based operating system. You cannot run Linux in a container *within* the Sony OS. Instead, the Linux boot chain takes over the hardware bare-metal:

```
[ HENlo / HENkaku / Ensō ]
          │
          ▼
[ Vita Linux Bootstrapper VPK ]  (User-facing launcher & file validator)
          │
          ▼
[ taiHEN Kernel Plugin: baremetal-loader.skprx ]  (Disables Sony OS MMU & interrupts)
          │
          ▼
[ Baremetal Payload: payload.bin ]  (Initializes OLED/LCD screen & storage)
          │
          ▼
[ Linux Kernel: zImage + Device Tree: vita.dtb ]  (Boots Linux on ARM Cortex-A9 bare-metal)
          │
          ▼
[ Embedded Initramfs RootFS: Buildroot / BusyBox console ]
```

---

## 2. Hardware Support & Status

| Hardware Component | Status | Notes |
| :--- | :--- | :--- |
| **CPU** | **Working** | Quad-core ARM Cortex-A9 MPCore SMP boots up to 444MHz |
| **Display / Framebuffer** | **Working** | Framebuffer console (`fbcon`) renders on OLED (Vita 1000) and LCD (Vita 2000) |
| **UART0 Serial Console** | **Working** | 115200 baud; accessible via test pads on motherboard or multi-use port |
| **Official Memory Card (MSIF)** | **Working** | Used by baremetal loader to fetch kernel and DTB |
| **SD2Vita (GCD/SDIF)** | Kernel WIP | Game card slot storage is not initialized in the baremetal loader |
| **Buttons / Gamepad** | Basic | Handled via syscon driver |
| **Touchscreen** | WIP | Drivers present in experimental branches |
| **3D GPU Acceleration** | **Not Working** | PowerVR SGX543MP4+ has no open-source driver; software rendering only |
| **WiFi / Bluetooth** | Experimental | In development, not ready out-of-the-box |

---

## 3. Critical Gotchas (Read Before Booting!)

> [!WARNING]
> **SD2Vita vs. Official Sony Memory Card:**
> The baremetal payload (`payload.bin`) currently only contains a driver for the official Sony Memory Card interface (MSIF). It **does not** initialize the SD2Vita adapter (gamecard slot) at baremetal boot.
> - If you use an **SD2Vita** as `ux0:`, you **must** copy `zImage` and `vita.dtb` to your official Sony memory card (which usually mounts as `uma0:` in VitaShell).
> - On PS Vita 2000 (Slim) or PSTV without a memory card, the internal storage is used.

> [!IMPORTANT]
> **Enable Unsafe Homebrew:**
> On your PS Vita, navigate to **Settings** → **HENkaku Settings** → ensure **Enable Unsafe Homebrew** is checked. If this is disabled, taiHEN will reject the kernel plugin with error `0x8002D003`.

---

## 4. Quick Start: Using Prebuilt Binaries (5 Minutes)

### Step 1: Download & Stage the Files
Run the included prebuilt fetcher script on your host computer:

```bash
./build.sh prebuilts
```

This downloads verified releases ([xerpi's 5.9.0-rc5](https://github.com/xerpi/linux_vita/releases/tag/5.9.0-rc5) kernel/payload and [DvaMishkiLapa's bootstrapper](https://github.com/DvaMishkiLapa/vita_plugin_linux_loader/releases/tag/v0.1.0-alpha)) and stages them into `output/`:

```
output/
├── vpk/
│   └── vita-linux-bootstrapper.vpk
└── ux0/
    └── linux/
        ├── baremetal-loader.skprx
        ├── payload.bin
        ├── zImage
        ├── vita.dtb
        ├── vita1000.dtb
        └── vita2000.dtb
```

### Step 2: Transfer to PS Vita
1. Launch **VitaShell** on your PS Vita and press **SELECT** to start USB or FTP transfer mode.
2. Copy all files from `output/ux0/linux/` to `ux0:linux/` on your Vita.
3. *(If using SD2Vita)*: In VitaShell, also copy `zImage` and `vita.dtb` to `uma0:linux/` (or your official memory card's mount).
4. Copy `output/vpk/vita-linux-bootstrapper.vpk` to `ux0:data/` (or root of `ux0:`).

### Step 3: Install & Launch
1. In VitaShell, browse to `vita-linux-bootstrapper.vpk`, press **X**, and confirm installation.
2. Return to the LiveArea and launch **Vita Linux Bootstrapper**.
3. The app automatically checks that all required files exist on `ux0:linux/`.
4. Press **X** to trigger the baremetal payload.
5. The console will take over the display, mount the memory card, load `zImage`, and jump into Linux!

---

## 5. Building from Source with Docker

Cross-compiling an ARMv7 Linux kernel, Buildroot root filesystem, and VitaSDK plugins on macOS or non-Linux hosts is difficult due to APFS case-insensitivity, BSD vs. GNU coreutils, and toolchain incompatibilities. 

This repository provides an automated Docker environment configured with:
- Ubuntu 22.04 LTS
- [Bootlin](https://toolchains.bootlin.com/) ARMv7-eabihf bleeding-edge toolchain (in `/opt`)
- [VitaSDK](https://vitasdk.org/) (`arm-vita-eabi`) toolchain (in `/usr/local/vitasdk`)
- Xerpi's Buildroot and kernel configurations

### Build Commands

```bash
# 1. Build the Docker environment image (psvita-linux-builder)
./build.sh image

# 2. Enter an interactive container shell (useful for make menuconfig or manual inspection)
./build.sh shell

# 3. Build only the Buildroot RootFS (generates output/rootfs.cpio.xz)
./build.sh rootfs

# 4. Build only the Linux Kernel & Device Trees (generates zImage and .dtb files)
./build.sh kernel

# 5. Build Vita baremetal loaders and the Bootstrapper VPK
./build.sh loaders

# 6. Build everything end-to-end from scratch
./build.sh all
```

### Customizing the RootFS Overlay
Any files or scripts placed in the `rootfs-overlay/` directory on your host will automatically be copied into the root filesystem when running `./build.sh rootfs` or `./build.sh all`. This is ideal for adding custom scripts, shell configurations, or test binaries to your Linux environment.

---

## 6. Troubleshooting

- **Error `0x8002D003` when launching the bootstrapper:**
  Unsafe Homebrew is disabled. Open the Vita **Settings** application, open **HENkaku Settings**, and enable **Enable Unsafe Homebrew**.
- **`Memory card not inserted` error on screen:**
  The baremetal loader could not find an official Sony Memory card. If you are using SD2Vita, you must also copy `zImage` and `vita.dtb` to the official Sony memory card (`uma0:linux/`).
- **Screen freezes at `Uncompressing Linux... done, booting the kernel`:**
  Occasionally the L2 cache contains stale data after soft reset, or the Device Tree Blob (DTB) does not match your specific Vita model. Ensure you have copied both `vita1000.dtb` (for OLED models) or `vita2000.dtb` (for Slim models) as `vita.dtb`.

---

## 7. Credits & Upstream Sources

This toolkit builds upon the work of the PS Vita homebrew and reverse engineering community:

- **[xerpi (Sergi Granell)](https://github.com/xerpi)**:
  - [linux_vita](https://github.com/xerpi/linux_vita) - Linux kernel port for PlayStation Vita
  - [PSVita Linux build instructions Gist](https://gist.github.com/xerpi/5c60ce951caf263fcafffb48562fe50f)
  - [Buildroot .config Gist](https://gist.github.com/xerpi/ef487ec59a8246cb2823d007f5e8dfcb)
  - [vita-baremetal-loader](https://github.com/xerpi/vita-baremetal-loader) - taiHEN kernel plugin for launching baremetal payloads
  - [vita-libbaremetal](https://github.com/xerpi/vita-libbaremetal) - Bare-metal library for PS Vita hardware initialization
  - [vita-baremetal-linux-loader](https://github.com/xerpi/vita-baremetal-linux-loader) - Bare-metal Linux loader payload
  - [vita-linux-loader](https://github.com/xerpi/vita-linux-loader) - Original Linux loader
- **[DvaMishkiLapa](https://github.com/DvaMishkiLapa)**:
  - [vita_plugin_linux_loader](https://github.com/DvaMishkiLapa/vita_plugin_linux_loader) - Enhanced Bootstrapper VPK with file checks
  - [vita-baremetal-loader fork](https://github.com/DvaMishkiLapa/vita-baremetal-loader) - Updated baremetal loader supporting firmware >= 3.63
- **[CreepNT](https://github.com/xerpi/vita-linux-loader/pull/2)** - UI checks and Linux bootstrapper styling
- **[Team Molecule & taiHEN](https://github.com/henkaku)** - HENkaku jailbreak and taiHEN kernel hooking framework
- **[VitaSDK](https://vitasdk.org/)** - Open-source PlayStation Vita software development kit
- **[Bootlin](https://toolchains.bootlin.com/)** - Precompiled ARMv7 cross-compilation toolchains
- **[postmarketOS](https://wiki.postmarketos.org/wiki/Sony_PlayStation_Vita_(sony-psvita))** - PlayStation Vita device documentation
