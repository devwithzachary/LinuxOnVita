# PS Vita Linux Toolkit & Docker Build Environment

[![PS Vita](https://img.shields.io/badge/Platform-PlayStation%20Vita-blue.svg)](https://en.wikipedia.org/wiki/PlayStation_Vita)
[![Kernel](https://img.shields.io/badge/Kernel-Linux%206.12-orange.svg)](https://kernel.org)
[![Architecture](https://img.shields.io/badge/Architecture-ARMv7--A%20(Cortex--A9)-green.svg)](https://developer.arm.com/Processors/Cortex-A9)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A complete toolkit, automated downloader, and containerized Docker build environment for compiling and running **Linux 6.12** on hacked PlayStation Vita consoles ([HENlo](https://vita.hacks.guide/using-henlo) / HENkaku / Ensō 3.60 & 3.65).

Now featuring **on-device Marvell 88W8787 Wi-Fi auto-connect**, **SSH server access**, an **on-screen framebuffer touch keyboard**, **gamepad button navigation**, and **Alpine Linux (`apk`) package manager support**.

---

## Key Features in Linux 6.12

- 📶 **On-Device Wi-Fi & SSH:** Powered by Marvell Avastar 88W8787 (`mwifiex_sdio`) with custom Syscon power sequencing (`pwrseq_vita_wlan.c`). Automatically joins your Wi-Fi and starts OpenSSH.
- ⚡ **High-Res Clocksource (144 MHz):** Enabled the ARM Global Timer, eliminating the 140-second kernel CRNG entropy delay. **SSH is accessible ~12 seconds after boot.**
- ⌨️ **On-Screen Touch Keyboard (`fbkeyboard`):** High-performance framebuffer virtual keyboard on the OLED/LCD display with direct `/dev/uinput` keystroke injection.
- 🎮 **Physical Button Navigation (`vita-input-mapper`):** D-Pad arrows, Cross (Enter), Square (Space), Circle (Backspace), Triangle (Tab), and L-Trigger (Ctrl+C).
- 📦 **Alpine Linux Userland (`apk`):** Run `alpine-chroot` to access the lightweight Alpine Linux distribution and install packages over the live Wi-Fi connection with `apk add`.
- 💾 **eMMC Storage Access:** Auto-detects the Vita's 12 SCE partitions (`/dev/mmcblk*p1`–`p12`), allowing you to mount internal storage (`os0`, `vs0`, `ur0`).
- 🔄 **Clean Hardware Reboot & Poweroff:** Clean cold reset back to VitaOS or full hardware poweroff via Syscon.

---

## Hardware Support Matrix

| Hardware Component | Status | Implementation Details |
| :--- | :--- | :--- |
| **CPU** | **Working** | Quad-core ARM Cortex-A9 MPCore (SMP active across all 4 cores) |
| **Display / Framebuffer** | **Working** | 960x544 OLED (Vita 1000) and LCD (Vita 2000) |
| **Wi-Fi (Marvell SD8787)** | **Working** | `mwifiex_sdio` on SDIF2 with custom Ernie power sequencing |
| **Front Touchscreen** | **Working** | `vita-syscon-ts.c` multi-touch digitizer mapped via `evdev` |
| **Buttons & D-Pad** | **Working** | `vita-buttons.c` mapped via `vita-input-mapper` daemon |
| **Internal eMMC Storage** | **Working** | Auto-detected partitions (`/dev/vita/{os0,ur0,vs0,...}`) |
| **Official Memory Card** | **Working** | Used by baremetal loader to load kernel & DTB |
| **UART0 Serial Console** | **Working** | 115200 baud serial debug console |
| **Bluetooth** | Working | Marvell SD8787 via `btmrvl` |
| **Audio** | Working | Vita audio codec via ALSA & PipeWire |
| **3D GPU Acceleration** | Not Working | SGX543MP4+ lacks open-source drivers (software rendering) |
| **USB Gadget (`g_ether`)** | Not Working | SoC UDC controller unmapped (use Wi-Fi + SSH instead) |

---

## Button & Touch Controls

When booted on the Vita without a keyboard:

| Vita Control | Synthesized Key / Action | Purpose |
| :--- | :--- | :--- |
| **Touch Screen (Bottom Area)** | Virtual QWERTY Keyboard | Type commands directly on screen |
| **D-Pad Up / Down** | `Arrow Up` / `Arrow Down` | Browse command history in shell |
| **D-Pad Left / Right** | `Arrow Left` / `Arrow Right`| Move text cursor |
| **Cross (✕)** | `Enter` | Execute command |
| **Circle (○)** | `Backspace` | Delete character |
| **Square (□)** | `Space` | Space bar |
| **Triangle (△)** | `Tab` | Command / filename auto-completion |
| **L Trigger** | `Ctrl + C` | Cancel current command (SIGINT) |
| **R Trigger** | `Page Up` | Scroll terminal buffer |
| **[Hide] on touch keyboard** | Toggle Keyboard | Hide keyboard overlay to see full screen |

---

## Quick Setup: Wi-Fi Configuration

Before building or booting Linux, configure your Wi-Fi network credentials:

1. Copy the example configuration:
   ```bash
   cp configs/wifi.conf.example configs/wifi.conf
   ```
2. Edit `configs/wifi.conf`:
   ```bash
   SSID="YourWiFiNetworkName"
   PSK="YourWiFiPassword"
   ```
3. When Linux boots on the Vita, it connects automatically. Once connected, open a terminal on your computer and SSH into the Vita:
   ```bash
   ssh root@vita.local
   # Or find the IP printed on the Vita screen:
   ssh root@<VITA_IP>
   ```

---

## Installing Packages with Alpine Linux

Vita Linux includes an Alpine Linux mini-rootfs integration with the `apk` package manager:

1. In the console or over SSH, run:
   ```bash
   alpine-chroot
   ```
2. You will enter the Alpine Linux environment. Update the repository index and install software:
   ```bash
   apk update
   apk add fastfetch htop python3 nano git curl
   ```
3. Run `fastfetch` to see your system specs and PS Vita hardware information!

---

## Building from Source with Docker

This repository provides a containerized Ubuntu build environment with the ARMv7-eabihf toolchain and VitaSDK.

### Commands

```bash
# 1. Build the Docker environment image
./build.sh image

# 2. Build the RootFS (compiles input tools, applies Wi-Fi config, builds Buildroot rootfs)
./build.sh rootfs

# 3. Build the Linux 6.12 Kernel & Device Trees (outputs to output/ux0/linux/)
./build.sh kernel

# 4. Build everything end-to-end
./build.sh all

# 5. Open an interactive shell inside the build container
./build.sh shell
```

### Build Artifacts Output

After compilation, files are placed in `output/`:
```
output/
├── vpk/
│   └── vita-linux-bootstrapper.vpk    # LiveArea launcher bubble
└── ux0/
    ├── app/
    │   └── VITALINUX/                 # Pre-extracted LiveArea bubble folder
    └── linux/
        ├── baremetal-loader.skprx     # taiHEN kernel payload
        ├── payload.bin                # Bare-metal screen/storage init
        ├── zImage                     # Linux 6.12 kernel + embedded rootfs
        ├── vita.dtb                   # Device tree (Vita 1000 OLED fallback)
        ├── vita1000.dtb               # Device tree for OLED model
        ├── vita2000.dtb               # Device tree for Slim LCD model
        └── pstv.dtb                   # Device tree for PlayStation TV
```

---

## Deploying to the PS Vita

1. Launch **VitaShell** on your Vita and press **SELECT** (USB or FTP mode).
2. Copy `output/ux0/linux/*` to `ux0:linux/` on your memory card.
3. *(If using SD2Vita)*: Also copy `zImage` and `vita.dtb` to `uma0:linux/` (or your official memory card).
4. If you haven't installed the bubble yet:
   - Copy `output/ux0/app/VITALINUX` to `ux0:app/VITALINUX`
   - In VitaShell, press **TRIANGLE** on `ux0:` and select **Refresh LiveArea**.
5. Launch **Vita Linux Bootstrapper** from the LiveArea and press **X** to boot!

---

## Credits & Upstream Sources

This toolkit builds upon the research of the PS Vita homebrew and Linux reverse-engineering community:

- **[incognitojam](https://github.com/incognitojam)**:
  - [vita-linux-port](https://github.com/incognitojam/vita-linux-port) - Linux 6.12 port, Marvell Wi-Fi custom power sequencing (`pwrseq_vita_wlan.c`), High-Res timer fix, SCE eMMC partition driver, and Buildroot integration.
  - [linux_vita (6.12)](https://github.com/incognitojam/linux_vita) - Modernized Linux 6.12 kernel tree for PS Vita.
- **[xerpi (Sergi Granell)](https://github.com/xerpi)**:
  - Original Linux on PS Vita pioneer: [linux_vita](https://github.com/xerpi/linux_vita), [vita-baremetal-loader](https://github.com/xerpi/vita-baremetal-loader), [vita-libbaremetal](https://github.com/xerpi/vita-libbaremetal).
- **[DvaMishkiLapa](https://github.com/DvaMishkiLapa)**:
  - [vita_plugin_linux_loader](https://github.com/DvaMishkiLapa/vita_plugin_linux_loader) - Enhanced Bootstrapper VPK with file integrity checks.
- **[Team Molecule & taiHEN](https://github.com/henkaku)** - HENkaku jailbreak and taiHEN kernel framework.
- **[VitaSDK](https://vitasdk.org/)** - Open-source PlayStation Vita software development kit.
- **[Bootlin](https://toolchains.bootlin.com/)** - Precompiled ARMv7 cross-compilation toolchains.

---

## License

This project and its orchestration scripts are licensed under the [MIT License](LICENSE).
Upstream source trees (Linux kernel, Buildroot, VitaSDK) retain their respective licenses (GPLv2, MIT, BSD).
