# LinuxOnVita - Complete Handheld Linux Toolkit for PS Vita

[![PS Vita](https://img.shields.io/badge/Platform-PlayStation%20Vita-blue.svg)](https://en.wikipedia.org/wiki/PlayStation_Vita)
[![Kernel](https://img.shields.io/badge/Kernel-Linux%206.12-orange.svg)](https://kernel.org)
[![Architecture](https://img.shields.io/badge/Architecture-ARMv7--A%20(Cortex--A9)-green.svg)](https://developer.arm.com/Processors/Cortex-A9)
[![Discord](https://img.shields.io/badge/Discord-Join%20Community-5865F2.svg?logo=discord&logoColor=white)](https://discord.gg/BrzdmHu8Am)
[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-blue.svg)](LICENSE)

A complete distribution, automated build environment, and handheld userland for running **Linux 6.12** on hacked PlayStation Vita consoles ([HENlo](https://vita.hacks.guide/using-henlo) / HENkaku / Ensō 3.60 & 3.65).

While the original upstream projects proved that modern Linux can boot on the PS Vita SoC, they functioned primarily as developer proof-of-concepts requiring specialized hardware (such as soldered UART debug cables). LinuxOnVita bridges the gap into a **fully standalone, interactive handheld Linux device** with out-of-the-box touch typing, physical gamepad navigation, automated Wi-Fi, package management (`apk`), and native framebuffer gaming.

<p align="center">
  <img src="screenshot.png" alt="PlayStation Vita Linux 6.12 Running with Framebuffer Touch Keyboard" width="720">
</p>

---

## ✨ Features

- **Built-in Touch Keyboard (`fbkeyboard`):** Type directly on the console screen with an on-framebuffer virtual keyboard featuring dual ABC / ?123 layouts, special keys, and a hide toggle.
- **Physical Gamepad Navigation (`vita-input-mapper`):** Complete console shell navigation using the D-Pad, face buttons, and shoulder triggers synthesized through `/dev/uinput`.
- **Fixed Controller Logic:** Corrected inverted button mask issues and initialized analog sticks via Syscon for smooth twin-stick controls in games.
- **Full Display Brightness:** Patched baremetal bootloaders to initialize displays at 100% brightness on both OLED and LCD models, plus runtime adjustment via `vita-brightness`.
- **Interactive On-Device Wi-Fi Manager (`vita-wifi`):** Scan, connect, and switch wireless networks directly on the console with an ASCII signal table. Saves credentials permanently to your memory card without needing to edit config files on a PC. Includes power toggle commands (`vita-wifi off / on`) to extend battery life.
- **Fast Boot & OpenSSH:** Boots straight into the Linux terminal. Wi-Fi connects automatically with OpenSSH and mDNS (`vita.local`), ready in ~12 seconds. Headless pre-configuration is also supported via `configs/wifi.conf` or `wpa_supplicant.conf`.
- **Alpine Linux Chroot (`alpine-chroot`):** Run a persistent Alpine Linux userland on storage with live `apk add` package management.
- **Storage Auto-Mounting:** Automatically mounts SD2Vita (`/mnt/ux0`), internal memory (`/mnt/ur0`), and secondary storage (`/mnt/uma0`) on boot.
- **Framebuffer DOOM (`vita-doom`):** Pre-installed pure-C `fbdoom` engine with twin-stick controls, analog deadzones, and input isolation.
- **All-in-One Installer App (`LinuxOnVita.vpk`):** Graphical bootstrapper with real-time partition detection, memory card mount selector, and 1-click on-device installer.
- **Automated Docker Build:** Build the entire stack (kernel, rootfs, loaders, and VPK) with a single command (`./build.sh all`).

---

> [!IMPORTANT]
> **Hardware Requirement: Official Sony Memory Card Strictly Required**
> An official physical Sony memory card is required on **all** PS Vita models (both 1000 OLED and 2000 Slim) to boot Linux.
> The baremetal loader communicates directly with Sony's proprietary memory card interface (MSIF) and cannot boot Linux from SD2Vita adapters (game card slot) or internal eMMC storage (`imc0:`).
> - **Standard Consoles (No SD2Vita):** Your Sony Memory Card mounts as `ux0:`.
> - **SD2Vita Users:** Your SD2Vita mounts as `ux0:`, and your physical Sony Memory Card typically mounts as `xmc0:` (VitaShell/YAMT default) or `uma0:` (StorageMgr). The **LinuxOnVita** app detects all partitions and lets you install directly to your Sony Memory Card.

---

## 📦 Pre-Built Releases (Quick Install)

Don't want to compile from source? Download the latest release from the [GitHub Releases page](https://github.com/devwithzachary/LinuxOnVita/releases).

Release download assets:
* **`LinuxOnVita.vpk` (Standalone VPK - Recommended for most users):** The all-in-one bootstrapper and on-device installer. Bundles the complete Linux kernel, device tree blobs, and baremetal loaders directly inside the app. Users who just want to run Linux only need to download and install this single VPK!
* **`LinuxOnVita-release-*.zip` (All-in-One Archive):** Contains `LinuxOnVita.vpk`, extracted boot files (`ux0/linux/`), unpacked LiveArea app (`ux0/app/LNXONVITA/`), and `INSTALL.txt`.

> [!TIP]
> **Wi-Fi Setup for Pre-Built Releases**
> Pre-built releases connect to Wi-Fi directly on the console using `vita-wifi`. Boot into Linux, open the terminal, and run `vita-wifi` to scan for nearby networks and connect directly on your Vita. Your network credentials are automatically saved to your memory card for future boots!
>
> *(Optional: For headless setups, you can still drop a pre-configured `wpa_supplicant.conf` into your memory card's `linux/` directory via VitaShell).*

To install, simply follow [Step 3](#step-3-transfer-linuxonvitavpk-to-your-ps-vita) and [Step 4](#step-4-install--launch-linux) below, then connect using [Step 5](#step-5-connect-to-wi-fi-on-device-vita-wifi).

---

## 🚀 Complete Step-by-Step Guide

Follow these steps to build and install Linux on your PlayStation Vita:

### Step 0: Prerequisites on Your PS Vita
1. **Custom Firmware:** PS Vita (1000 OLED, 2000 Slim, or PSTV) running **3.60 or 3.65** with HENkaku / Ensō (see [vita.hacks.guide](https://vita.hacks.guide)).
2. **Enable Unsafe Homebrew (Critical!):**
   - On your PS Vita, open **Settings** → **HENkaku Settings**.
   - Ensure **Enable Unsafe Homebrew** is **checked [✓]**. If unchecked, the loader plugin will fail with error `0x8002D003`.
3. **VitaShell Installed:** Required for USB/FTP file transfer and refreshing the LiveArea.

---

### Step 1: Clone the Repository
Clone this repository to your computer (macOS or Linux):
```bash
git clone https://github.com/devwithzachary/LinuxOnVita.git
cd LinuxOnVita
```

---

### Step 2: Build Linux with Docker
Run the automated Docker build system:
```bash
# 1. Build the Docker environment image (one-time setup)
./build.sh image

# 2. Build the complete system (RootFS, Linux 6.12 Kernel, and Loaders)
./build.sh all

# 3. (Optional) Create a distributable release zip for sharing
./build.sh release
```
*(You can also build individual components: `./build.sh rootfs`, `./build.sh kernel`, or `./build.sh loaders`.)*

> [!NOTE]
> `./build.sh release` packages everything into `output/LinuxOnVita-release-<version>.zip` and produces standalone `output/LinuxOnVita.vpk` ready to share or upload to GitHub Releases. Wi-Fi credentials are **never** included in release assets.

> [!TIP]
> **Optional: Pre-configure Wi-Fi for Headless Boot**
> You do **not** need to configure Wi-Fi before building: you can scan and connect directly on the console after boot using `vita-wifi`.
> However, if you want your Vita to join your network immediately on first boot without any manual interaction:
> ```bash
> cp configs/wifi.conf.example configs/wifi.conf
> # Edit SSID="YourWiFiNetworkName" and PSK="YourWiFiPassword"
> ```
> *(Note: `configs/wifi.conf` is ignored by git so your credentials remain private).*

---

### Step 3: Transfer LinuxOnVita.vpk to Your PS Vita
1. Connect your PS Vita to your computer via USB cable (or FTP).
2. Open **VitaShell** and press **SELECT** to enable USB/FTP storage.
3. Copy **`LinuxOnVita.vpk`** (from GitHub Releases, the release zip, or `output/`) to your memory card root (`ux0:LinuxOnVita.vpk`).

> [!IMPORTANT]
> **Hardware Requirement: Official Sony Memory Card Required**
> The early baremetal payload initializes storage using Sony's proprietary memory card interface (MSIF).
> - **All models (1000 and 2000):** An official physical Sony memory card is **strictly required** to boot Linux.
> - **For SD2Vita Users:** If your SD2Vita adapter is configured as `ux0:`, your official Sony memory card is typically mounted as `xmc0:` (VitaShell/YAMT default) or `uma0:` (StorageMgr). Inside the **LinuxOnVita** app, select your Sony card mount using the D-Pad, and press **CROSS (X)** to install directly to it.
> - **Internal Storage Note:** The baremetal loader runs directly on bare metal hardware without VitaOS and cannot read from internal eMMC storage (`imc0:`) or SD2Vita adapters. Booting without an official Sony memory card is currently not supported.

---

### Step 4: Install & Launch Linux
1. Disconnect USB / exit VitaShell server mode.
2. In VitaShell, navigate to `ux0:LinuxOnVita.vpk`.
3. Press **CROSS (✕)** to install the package, and accept extended permissions when prompted.
4. Once installation finishes, press the **PS Button** to return to the LiveArea home screen.
5. Tap the new **LinuxOnVita** bubble and select **Start**.
6. **Mount Selection & Setup:**
   - The app scans all available storage partitions (`xmc0:`, `ux0:`, `uma0:`, `imc0:`) and displays their size and free space.
   - Use **D-PAD UP / DOWN** or **L / R** to select the mount corresponding to your official Sony Memory Card (e.g. `xmc0:` for SD2Vita users, or `ux0:` for standard consoles). Your choice is saved automatically.
   - Press **CROSS (✕)** to install the bundled Linux files directly to your Sony Memory Card (`<mount>/linux/`).
   - If you already have boot files on `ux0:`, you can also press **TRIANGLE (/\\)** to copy them to your selected memory card mount.
7. Once files are verified on your Sony Memory Card, press **CROSS (✕)** to boot into Linux!

The screen will blank briefly, take over the display, and boot straight into the Linux terminal!

---

### Step 5: Connect to Wi-Fi On-Device (`vita-wifi`)
Once booted into Linux, connect to your wireless network directly from the handheld:

1. Tap the lower third of the screen to toggle the virtual touch keyboard (or navigate with physical buttons).
2. Scan for nearby wireless networks:
   ```bash
   vita-wifi scan
   ```
   Or launch the interactive menu:
   ```bash
   vita-wifi
   ```
3. Enter the network number from the ASCII table (or connect directly with `vita-wifi connect <SSID>`) and enter your Wi-Fi password when prompted.
4. `vita-wifi` verifies the connection, obtains a DHCP lease, and automatically saves your configuration to `<mount>/linux/wpa_supplicant.conf` on your memory card so future boots connect automatically without re-entering your password.
5. OpenSSH and mDNS (`vita.local`) are ready immediately:
   ```bash
   ssh root@vita.local
   # or ssh root@<VITA_IP>
   ```

> [!NOTE]
> **Backwards Compatibility / Headless Alternative:**
> If you prefer headless setup, you can still drop a pre-configured `wpa_supplicant.conf` into `<mount>:linux/` (e.g. `ux0:linux/wpa_supplicant.conf` or `xmc0:linux/wpa_supplicant.conf`) via VitaShell before booting. LinuxOnVita automatically detects existing configuration files on boot and connects seamlessly.

---

## 🎮 On-Device Experience & Utilities

### 1. Typing with the On-Screen Touch Keyboard
* Tap the bottom third of the display to type on the compact framebuffer virtual keyboard.
* Tap **[?123]** to switch to numbers and symbols, or **[ABC]** to return to letters.
* Tap **[Hide]** to dismiss the keyboard for a full unobstructed screen view; tap the bottom bar to bring it back.

### 2. Navigating with Physical Buttons
Navigate your terminal history and line editing entirely with the Vita's physical buttons:
* **D-Pad Up / Down:** Command history (Arrow Up / Down)
* **D-Pad Left / Right:** Cursor movement (Arrow Left / Right)
* **Cross (✕):** Enter (execute command)
* **Circle (○):** Backspace
* **Square (□):** Space
* **Triangle (△):** Tab (command / path auto-complete)
* **L-Trigger:** `Ctrl + C` (SIGINT / cancel process)
* **R-Trigger:** Page Up

### 3. Screen Brightness Control (`vita-brightness`)
Displays automatically initialize to 100% full brightness on boot. You can inspect and adjust brightness dynamically:
```bash
vita-brightness get        # View current brightness percentage
vita-brightness set 80     # Adjust brightness (0-100%)
vita-brightness max        # Jump straight to 100% full bright
```

### 4. Playing Framebuffer DOOM (`vita-doom`)
A pure-C standalone framebuffer DOOM engine (`fbdoom`) is pre-installed.
1. Place any standard DOOM WAD (e.g. `DOOM1.WAD`, `DOOM.WAD`, `DOOM2.WAD`) into `ux0:doom/` (available in Linux at `/mnt/ux0/doom/`).
2. Run `vita-doom` from the terminal:
```bash
vita-doom
```
**Controls:**
* **Left Stick & D-Pad:** Move forward/backward, turn left/right
* **Right Stick:** Strafe left / right
* **R-Trigger:** Shoot / Fire (`KEY_FIRE`)
* **Square (□):** Open doors / use switches (`KEY_USE`)
* **Cross (✕) / Select:** Enter / menu select
* **Circle (○):** Back / cancel
* **L-Trigger:** Run / speed (`KEY_RSHIFT`)
* **Triangle (△):** Automap (`KEY_TAB`)
* **Start:** Pause / Menu (`KEY_ESCAPE`)

*(Note: Background input daemons and the touch keyboard are automatically suspended while DOOM is running and resumed when you exit).*

### 5. Installing Packages with Alpine Linux (`alpine-chroot`)
Vita Linux includes an integrated Alpine Linux environment. Run `alpine-chroot` to launch a persistent Alpine rootfs:
```bash
# Initialize/enter Alpine Linux (stored persistently as an ext4 image)
alpine-chroot

# Update repositories and install any software
apk update
apk add fastfetch htop python3 git curl nano gcc musl-dev

# View system information
fastfetch
```

### 6. Managing Wi-Fi On-Device (`vita-wifi`)
Manage wireless connections directly on your PS Vita without needing to edit config files on a PC:
* **Interactive TUI Menu:** Run `vita-wifi` without arguments for an interactive menu.
* **Scan for Networks:** `vita-wifi scan` displays an ASCII table of nearby SSIDs, signal strength bars, and security types. Enter the network number to connect immediately.
* **Connect to Network:** `vita-wifi connect <SSID>` prompts for your passphrase, saves persistently to your memory card (`<mount>/linux/wpa_supplicant.conf`), and acquires a DHCP lease.
* **View Signal & Status:** `vita-wifi status` displays your active SSID, BSSID, RSSI dBm level, MAC, and assigned IP address.
* **Power Savings:** `vita-wifi off` and `vita-wifi on` toggle the wireless radio to extend battery runtime.

### 7. Remote SSH Access
Within ~12 seconds of boot (or after connecting via `vita-wifi`), your Vita starts OpenSSH. Connect from your computer:
```bash
ssh root@vita.local
```
*(If mDNS does not resolve on your network, use the IP address shown on the login banner or via `vita-wifi status`: `ssh root@<VITA_IP>`)*.

### 8. Returning to VitaOS or Powering Down
```bash
reboot      # Clean cold reset straight back to official Sony OS
poweroff    # Full hardware poweroff via Syscon
```

---

## 🛠 Hardware Support

| Hardware Component | Status | Implementation Details |
| :--- | :--- | :--- |
| **CPU** | **Working** | Quad-core ARM Cortex-A9 MPCore (SMP active across all 4 cores) |
| **Display / Framebuffer** | **Working** | 960x544 OLED (Vita 1000) and LCD (Vita 2000) at 100% full brightness |
| **Wi-Fi (Marvell SD8787)** | **Working** | `mwifiex_sdio` on SDIF2 with custom Ernie power sequencing |
| **Touchscreen** | **Working** | `vita-syscon-ts.c` multi-touch digitizer mapped via `evdev` |
| **Buttons & D-Pad** | **Working** | Patched `vita-buttons.c` driver with uniform active-low mask and `/dev/uinput` mapping |
| **Analog Sticks** | **Working** | Syscon `0x180` analog sampling enabled with deadzone filtering |
| **Storage (eMMC)** | **Working** | Auto-detected SCE partitions (`/dev/mmcblk*p1`-`p12`) |
| **Storage (SD2Vita / Sony)** | **Working** | Automatically mounted at `/mnt/ux0`, `/mnt/ur0`, and `/mnt/uma0` |
| **Battery Fuel Gauge** | **Not yet supported** | Isolated on Syscon (Ernie) private I2C bus; runtime streaming unsupported |
| **UART0 Serial Console** | **Working** | 115200 baud serial debug console |
| **Bluetooth** | **Not yet supported** | Marvell SD8787 Bluetooth driver not enabled in kernel config |
| **Audio** | **Not yet supported** | Vita audio codec lacks an active Linux ALSA driver |
| **3D GPU Acceleration** | **Not supported** | PowerVR SGX543MP4+ lacks open-source Linux drivers (software rendering via simplefb) |

---

## ❓ Troubleshooting

### LiveArea bubble does not appear after "Refresh LiveArea" ("Refreshed 0 items")
- **Cause 1 (Nested Directory):** If you extracted the release zip on your PC/Mac and dragged the `ux0` folder straight into `ux0:`, the files ended up at `ux0:ux0/app/LNXONVITA/`. VitaShell only scans the top-level `ux0:app/` folder.
- **Cause 2 (VitaShell Homebrew Detection):** VitaShell's "Refresh LiveArea" was originally designed for NoNpDrm game dumps that contain license files (`work.bin`). Raw homebrew folders without license files can be skipped depending on your VitaShell or HENkaku version.
- **Fix:** Install via **`LinuxOnVita.vpk`**! In VitaShell, highlight `LinuxOnVita.vpk` and press **CROSS (✕)** to install it directly. The installer registers the application directly into the PS Vita's system database and creates the bubble reliably every time.

### "The file is corrupt" error when launching the LiveArea bubble
- **Cause:** Sony OS reports this if `eboot.bin` does not physically exist inside `ux0:app/LNXONVITA/` on the current `ux0:` partition.
- **Fix:** Reinstall **`LinuxOnVita.vpk`** via VitaShell.

### Error `0x8002D003` when launching the bootstrapper
- **Cause:** Unsafe homebrew is disabled in HENkaku settings.
- **Fix:** Go to Vita **Settings** → **HENkaku Settings** → check **Enable Unsafe Homebrew**.

### "Memory card not inserted" on screen
- **Cause:** The baremetal loader could not locate an official Sony memory card in the memory card slot.
- **Fix:**
  - An official Sony memory card is required on all consoles (both 1000 OLED and 2000 Slim). The baremetal loader only includes drivers for Sony's proprietary memory card interface (MSIF). It cannot read `zImage` or `vita.dtb` from internal eMMC storage (`imc0:`) or SD2Vita adapters.
  - If you use an SD2Vita adapter as `ux0:`, insert an official Sony memory card, open LinuxOnVita, select your Sony card mount (`xmc0:` or `uma0:`), install or copy the boot files, and then launch Linux.

### Screen freezes at `Uncompressing Linux... done, booting the kernel`
- **Cause:** The Device Tree Blob (`vita.dtb`) does not match your specific console model.
- **Fix:** Copy `vita1000.dtb` (for OLED 1000) or `vita2000.dtb` (for Slim 2000) to `<target_mount>:linux/vita.dtb` (on your official Sony Memory Card).

---

## 👏 Credits & Upstream Sources

This toolkit builds upon the groundbreaking research and development of the PS Vita homebrew and Linux reverse-engineering community:

- **[incognitojam](https://github.com/incognitojam)**:
  - [vita-linux-port](https://github.com/incognitojam/vita-linux-port) - Modern Linux 6.12 port, Marvell Wi-Fi custom power sequencing (`pwrseq_vita_wlan.c`), High-Res timer fix, SCE eMMC partition driver, and Buildroot integration.
  - [linux_vita (6.12)](https://github.com/incognitojam/linux_vita) - Modernized Linux 6.12 kernel tree for PS Vita.
- **[xerpi (Sergi Granell)](https://github.com/xerpi)**:
  - Original Linux on PS Vita pioneer: [linux_vita](https://github.com/xerpi/linux_vita), [vita-baremetal-loader](https://github.com/xerpi/vita-baremetal-loader), [vita-libbaremetal](https://github.com/xerpi/vita-libbaremetal).
- **[DvaMishkiLapa](https://github.com/DvaMishkiLapa)**:
  - [vita_plugin_linux_loader](https://github.com/DvaMishkiLapa/vita_plugin_linux_loader) - Bootstrapper VPK with file verification.
- **[Team Molecule & taiHEN](https://github.com/henkaku)** - HENkaku jailbreak and taiHEN kernel framework.
- **[VitaSDK](https://vitasdk.org/)** - Open-source PlayStation Vita software development kit.
- **[Bootlin](https://toolchains.bootlin.com/)** - Precompiled ARMv7 cross-compilation toolchains.

---

## 🤝 Community, Contributing & AI Policy

Have questions, need help troubleshooting, or want to share photos and clips of Linux running on your Vita? Join our **[Discord Community](https://discord.gg/BrzdmHu8Am)**!

Contributions, bug reports, and feature requests are welcome! Feel free to join the discussion on Discord, check out the [issues page](https://github.com/devwithzachary/LinuxOnVita/issues), or submit a pull request.

Please review our **[AI Usage Policy](AI.md)** for guidelines on using AI coding assistants when contributing to this project. All code submitted must be thoroughly reviewed and personally owned by the human author; automated bot PRs are not accepted.

---

## ☕ Support the Project

If LinuxOnVita has been useful to you, consider supporting continued development:

<div align="center">
  <a href="https://www.patreon.com/DevWithZachary"><img src="https://img.shields.io/badge/Patreon-Become%20a%20Patron-F96854.svg?logo=patreon&logoColor=white&style=for-the-badge" alt="Patreon" /></a>
  &nbsp;
  <a href="https://buymeacoffee.com/linuxonandroid"><img src="https://img.shields.io/badge/Buy%20Me%20a%20Coffee-Tip%20the%20Dev-FFDD00.svg?logo=buymeacoffee&logoColor=black&style=for-the-badge" alt="Buy Me a Coffee" /></a>
</div>

---

## 📄 License

LinuxOnVita is licensed under the [GNU General Public License v3.0](LICENSE) (GPL-3.0).
Upstream source trees (Linux kernel, Buildroot, VitaSDK) retain their respective licenses (GPLv2, MIT, BSD).

