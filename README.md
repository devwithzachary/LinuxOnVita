# PS Vita Linux Toolkit & Docker Build Environment

[![PS Vita](https://img.shields.io/badge/Platform-PlayStation%20Vita-blue.svg)](https://en.wikipedia.org/wiki/PlayStation_Vita)
[![Kernel](https://img.shields.io/badge/Kernel-Linux%206.12-orange.svg)](https://kernel.org)
[![Architecture](https://img.shields.io/badge/Architecture-ARMv7--A%20(Cortex--A9)-green.svg)](https://developer.arm.com/Processors/Cortex-A9)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A complete toolkit, automated downloader, and containerized Docker build environment for compiling, configuring, and running **Linux 6.12** on hacked PlayStation Vita consoles ([HENlo](https://vita.hacks.guide/using-henlo) / HENkaku / Ensō 3.60 & 3.65).

Now featuring **on-device Marvell 88W8787 Wi-Fi auto-connect**, **SSH server access**, an **on-screen framebuffer touch keyboard**, **physical gamepad navigation**, and **Alpine Linux (`apk`) package manager support**.

---

## Key Features

- 📶 **On-Device Wi-Fi & SSH:** Powered by Marvell Avastar 88W8787 (`mwifiex_sdio`) with custom Syscon power sequencing (`pwrseq_vita_wlan.c`). Automatically joins your Wi-Fi network and starts OpenSSH.
- ⚡ **High-Res Clocksource (144 MHz):** Enabled the ARM Global Timer, eliminating the 140-second kernel CRNG entropy delay. **SSH is accessible ~12 seconds after boot.**
- ⌨️ **On-Screen Touch Keyboard (`fbkeyboard`):** High-performance framebuffer virtual keyboard rendered directly on the OLED/LCD display with `/dev/uinput` keystroke injection.
- 🎮 **Physical Button Navigation (`vita-input-mapper`):** D-Pad arrows, Cross (Enter), Square (Space), Circle (Backspace), Triangle (Tab), and L-Trigger (Ctrl+C).
- 📦 **Alpine Linux Userland (`apk`):** Run `alpine-chroot` to access the lightweight Alpine Linux distribution and install packages over the live Wi-Fi connection with `apk add`.
- 💾 **eMMC Storage Access:** Auto-detects the Vita's 12 SCE partitions (`/dev/mmcblk*p1`–`p12`), allowing you to mount internal storage (`os0`, `vs0`, `ur0`).
- 🔄 **Clean Hardware Reboot & Poweroff:** Clean cold reset back to VitaOS or full hardware poweroff via Syscon.

---

## 🚀 Complete Step-by-Step Guide: From Clone to Running Linux

Follow these steps to get from a newly cloned repository to a booting, interactive Linux system on your PS Vita.

### Step 0: Prerequisites on Your PS Vita

Before starting, ensure your PS Vita meets these requirements:
1. **Custom Firmware:** PS Vita (1000 OLED, 2000 Slim, or PSTV) running **3.60 or 3.65** with HENkaku / Ensō (follow [vita.hacks.guide](https://vita.hacks.guide) if not already hacked).
2. **Enable Unsafe Homebrew (Critical!):**
   - On your PS Vita, open **Settings** → **HENkaku Settings**.
   - Ensure **Enable Unsafe Homebrew** is **checked [✓]**. If unchecked, the kernel plugin will fail to load with error `0x8002D003`.
3. **VitaShell Installed:** Required for USB/FTP file transfer and refreshing the LiveArea.

---

### Step 1: Clone the Repository

Clone this repository to your computer (macOS or Linux):

```bash
git clone https://github.com/devwithzachary/vita-linux.git
cd vita-linux
```

---

### Step 2: Configure Your Wi-Fi Credentials (Optional but Recommended)

If you want your Vita to automatically connect to your home Wi-Fi network and start SSH on boot:

```bash
cp configs/wifi.conf.example configs/wifi.conf
```

Open `configs/wifi.conf` in your favorite text editor and enter your network details:

```bash
SSID="YourWiFiNetworkName"
PSK="YourWiFiPassword"
```

*(Note: `configs/wifi.conf` is gitignored so your password will never be committed to git).*

---

### Step 3: Choose Your Build Path

You have two choices to obtain the boot files:

#### Option A: Fast-Track (Prebuilt Binaries — Takes 1 Minute)
If you want to test booting Linux immediately without running Docker or compiling from source:

```bash
./build.sh prebuilts
```

This script automatically downloads the verified binaries and extracts the LiveArea app folder into `output/`.

#### Option B: Build Modern Linux 6.12 from Source (Custom Wi-Fi + Touch Keyboard + Alpine)
If you want the full modern setup with your custom Wi-Fi network, on-screen touch keyboard, and Alpine Linux userland:

```bash
# 1. Build the Docker environment image
./build.sh image

# 2. Build the rootfs (compiles input tools, injects Wi-Fi config, builds rootfs)
./build.sh rootfs

# 3. Build the Linux 6.12 kernel (embeds rootfs into zImage and compiles DTBs)
./build.sh kernel
```

*(Or simply run `./build.sh all` to build everything end-to-end).*

---

### Step 4: Transfer Files to Your PS Vita

1. Connect your PS Vita to your computer via USB cable (or FTP).
2. Launch **VitaShell** on your Vita and press **SELECT** to start the USB/FTP server.
3. On your computer, open the Vita's memory card (`ux0:`).
4. Copy the following folders from `output/` to your Vita:

| Source on Computer | Destination on PS Vita (`ux0:`) | Description |
| :--- | :--- | :--- |
| `output/ux0/app/VITALINUX/` | `ux0:app/VITALINUX/` | LiveArea launcher bubble folder |
| `output/ux0/linux/*` | `ux0:linux/` | Kernel (`zImage`), Device Trees (`*.dtb`), and Loaders |

Your Vita's file structure should look like this:
```
ux0:
├── app/
│   └── VITALINUX/
│       ├── eboot.bin
│       ├── sce_sys/
│       │   ├── icon0.png
│       │   ├── param.sfo
│       │   └── ...
└── linux/
    ├── baremetal-loader.skprx
    ├── payload.bin
    ├── zImage
    ├── vita.dtb
    ├── vita1000.dtb
    ├── vita2000.dtb
    └── pstv.dtb
```

> [!IMPORTANT]
> **Crucial Note for SD2Vita Users:**
> The early baremetal payload initializes storage using Sony's proprietary memory card interface (MSIF).
> - If you use an **SD2Vita** adapter as `ux0:`, you **must also copy** `zImage` and `vita.dtb` to your official Sony memory card (which typically mounts as `uma0:linux/` in VitaShell).
> - If using a PS Vita 2000 Slim without a memory card, the 1GB internal storage is used.

---

### Step 5: Refresh LiveArea & Launch VitaLinux

1. Disconnect the USB connection (or press **Cancel** in VitaShell).
2. In VitaShell, navigate to the main partition list (where `ux0:`, `ur0:`, etc. are listed).
3. Highlight `ux0:`, press **TRIANGLE (△)** to open the Options menu.
4. Select **Refresh LiveArea**.
5. VitaShell will scan the directory and report:
   ```
   Refreshed 1 items.
   ```
6. Press the **PS Button** to exit VitaShell and return to the LiveArea home screen.
7. You will see a new **VitaLinux** bubble!
8. Tap the bubble and select **Start**.
9. The bootstrapper will verify that all required files exist on `ux0:linux/`.
10. Press **CROSS (✕)** to trigger the baremetal payload.

The screen will blank briefly, take over the display, and boot straight into the Linux terminal!

---

### Step 6: Using Linux on Your PS Vita

Once booted, Linux will automatically log you into the root shell (`root@vita:~#`).

#### 1. Typing with the On-Screen Touch Keyboard
- Tap anywhere on the bottom third of the Vita's touchscreen to use the virtual QWERTY keyboard.
- Tap **[Hide]** to dismiss the keyboard and see the full screen. Tap the bottom edge to bring it back.

#### 2. Navigating with Physical Vita Buttons
You can navigate the console without touching the screen:
- **D-Pad Up / Down:** Browse command history.
- **D-Pad Left / Right:** Move cursor left/right.
- **Cross (✕):** Enter (execute command).
- **Circle (○):** Backspace.
- **Square (□):** Space.
- **Triangle (△):** Tab (auto-completion).
- **L Trigger:** `Ctrl + C` (Cancel command / SIGINT).

#### 3. SSH into the Vita from Your Computer
Within ~12 seconds of boot, the Vita connects to your Wi-Fi network and starts OpenSSH.
From your Mac or Linux terminal on the same Wi-Fi network, run:

```bash
ssh root@vita.local
```

*(If `vita.local` does not resolve on your router, use the IP address printed on the Vita screen welcome banner: `ssh root@<VITA_IP>`)*.

#### 4. Installing Software with Alpine Linux (`apk`)
Vita Linux includes an integrated Alpine Linux environment with full package management:

```bash
# Enter the Alpine environment
alpine-chroot

# Update package repositories and install tools
apk update
apk add fastfetch htop python3 nano git curl

# Run fastfetch to see Vita hardware info
fastfetch
```

*(Packages installed via `alpine-chroot` are stored persistently in internal storage at `/mnt/ur0/alpine`)*.

#### 5. Returning to VitaOS
To safely reset the console back to the official Sony OS without holding power buttons:
```bash
reboot
```

Or to power down completely:
```bash
poweroff
```

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

## Troubleshooting

### "The file is corrupt" error when launching the LiveArea bubble
- **Cause:** Sony OS throws this error if `eboot.bin` does not physically exist inside `ux0:app/VITALINUX/` on the currently mounted `ux0:` partition.
- **Fix:** Ensure you copied the entire `output/ux0/app/VITALINUX` directory into `ux0:app/VITALINUX` and then ran **Refresh LiveArea** from VitaShell.

### Error `0x8002D003` when launching the bootstrapper
- **Cause:** Unsafe homebrew is disabled in your HENkaku settings.
- **Fix:** Go to Vita **Settings** → **HENkaku Settings** → check **Enable Unsafe Homebrew**.

### "Memory card not inserted" on screen
- **Cause:** The baremetal loader could not find an official Sony memory card.
- **Fix:** If you use SD2Vita, copy `zImage` and `vita.dtb` to your official memory card (which mounts as `uma0:linux/` in VitaShell).

### Screen freezes at `Uncompressing Linux... done, booting the kernel`
- **Cause:** The Device Tree Blob (`vita.dtb`) does not match your specific console model, or L2 cache data was stale.
- **Fix:** Ensure `output/ux0/linux/vita1000.dtb` (for OLED 1000) or `output/ux0/linux/vita2000.dtb` (for Slim 2000) is copied as `ux0:linux/vita.dtb`.

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
