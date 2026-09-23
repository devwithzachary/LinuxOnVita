# LinuxOnVita - Complete Handheld Linux Toolkit for PS Vita

[![PS Vita](https://img.shields.io/badge/Platform-PlayStation%20Vita-blue.svg)](https://en.wikipedia.org/wiki/PlayStation_Vita)
[![Kernel](https://img.shields.io/badge/Kernel-Linux%206.12-orange.svg)](https://kernel.org)
[![Architecture](https://img.shields.io/badge/Architecture-ARMv7--A%20(Cortex--A9)-green.svg)](https://developer.arm.com/Processors/Cortex-A9)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A complete distribution, automated build environment, and handheld userland for running **Linux 6.12** on hacked PlayStation Vita consoles ([HENlo](https://vita.hacks.guide/using-henlo) / HENkaku / Ensō 3.60 & 3.65).

While the original upstream projects proved that modern Linux can boot on the PS Vita SoC, they functioned primarily as developer proof-of-concepts requiring specialized hardware (such as soldered UART debug cables). LinuxOnVita bridges the gap into a **fully standalone, interactive handheld Linux device** with out-of-the-box touch typing, physical gamepad navigation, automated Wi-Fi, battery monitoring, package management (`apk`), and native framebuffer gaming.

---

## 🌟 What Makes This Project Different: Upstream vs. LinuxOnVita

If you build the raw upstream projects by default, you get a bare-bones Linux kernel that boots to a terminal you cannot interact with without soldering serial wires. Below is a breakdown of what this distribution adds:

| Feature Area | Upstream Default Experience (Raw Proof-of-Concept) | **LinuxOnVita (This Distribution)** |
| :--- | :--- | :--- |
| **Interactive Terminal Input** | ❌ **None.** Requires a custom soldered UART cable or USB-UART interface to type commands. | ✅ **Built-in Touch Keyboard (`fbkeyboard`):** Rendered directly on `/dev/fb0` with dual ABC / ?123 layout, customizable keys, and VT isolation. |
| **Physical Button Navigation** | ❌ Raw evdev only; buttons do nothing in the console shell. | ✅ **`vita-input-mapper`:** D-Pad, Cross (Enter), Circle (Backspace), Square (Space), Triangle (Tab), and L-Trigger (Ctrl+C) synthesized via `/dev/uinput`. |
| **Button Driver Correctness** | ⚠️ **Broken Mask:** Legacy XOR mask `0x4037fcf9` inverted D-Pad Right, Down, Select, and L-Trigger (causing D-Pad Right to be permanently stuck ON). | ✅ **Fixed Mask (`0x4037ffff`):** All 12 hardware buttons are decoded with uniform active-low logic. Analog sticks are initialized via Syscon `0x180`. |
| **Screen Brightness** | ⚠️ **Dim:** Baremetal OLED driver hardcoded to gamma level 0 (dimmest); LCD driver hardcoded to 50% PWM. | ✅ **100% Full Brightness:** Patched baremetal loaders to maximum brightness curve on boot, plus a runtime `vita-brightness` CLI. |
| **Wi-Fi & SSH Access** | ⚠️ Manual CLI setup or hardcoded supplicant configs required. | ✅ **Zero-Config Auto-Connect:** Drop credentials into `configs/wifi.conf`. Auto-joins WPA2 Wi-Fi on boot and starts OpenSSH with mDNS (`vita.local`). |
| **Boot Delay / SSH Ready Time** | ⚠️ **~2.5 minute delay:** Linux kernel blocked waiting on CRNG random entropy due to missing hardware timer. | ✅ **~12 seconds to SSH:** Enabled the 144 MHz ARM Global Timer clocksource, completely eliminating entropy stalls. |
| **Package Management** | ❌ **Read-only / Ephemeral:** Buildroot initramfs has no package manager (`apt`/`apk`); any downloaded binaries vanish on reboot. | ✅ **Alpine Linux (`alpine-chroot`):** Run `alpine-chroot` to create a persistent ext4 userland on SD/internal storage with live `apk add` package management. |
| **Storage & SD2Vita** | ⚠️ Only official Sony memory cards or internal eMMC mounted manually. | ✅ **Auto-Mounting:** Automatic mounting of `/mnt/ux0` (SD2Vita / memory card), `/mnt/ur0` (internal storage), and `/mnt/uma0` on boot. |
| **Battery & Power Telemetry** | ❌ Unknown; battery fuel gauge is isolated on Ernie's private I2C bus. | ✅ **Hardware Battery Telemetry:** Captured during bootloader handover, printed on login banner, and accessible via `vita-battery` CLI. |
| **Gaming Demonstration** | ❌ None out of the box. | ✅ **Framebuffer DOOM (`vita-doom`):** Native pure-C `fbdoom` with twin-stick and button controls, analog deadzones, and input isolation. |
| **Build System & Toolchains** | ⚠️ Complex multi-step host compilation across separate repos and cross-compilers. | ✅ **One-Command Docker Build:** `./build.sh all` handles toolchains, kernel patches, Buildroot overlays, and VPK packaging automatically. |

---

## 📦 Pre-Built Releases (Quick Install)

Don't want to compile from source? Download the latest pre-built release zip (`LinuxOnVita-release-*.zip`) from the [GitHub Releases page](https://github.com/devwithzachary/LinuxOnVita/releases).

The release zip is an all-in-one package that contains:
* **`VitaLinux.vpk`:** Standalone homebrew installer package (easiest way to install the LiveArea bubble via VitaShell).
* **`ux0/linux/`:** Linux 6.12 kernel (`zImage`), Device Tree blobs (`*.dtb`), baremetal loaders (`baremetal-loader.skprx`, `payload.bin`), and sample Wi-Fi config.
* **`ux0/app/VITALINUX/`:** Pre-extracted app folder (for users who prefer manual folder deployment).
* **`INSTALL.txt`:** Quick setup instructions.

> [!IMPORTANT]
> **Pre-built releases do NOT include Wi-Fi credentials.** The release zip is intentionally built without a `wpa_supplicant.conf` so you must add your own network details before the Vita can connect to Wi-Fi or SSH.
> After extracting the release, edit `ux0:linux/wpa_supplicant.conf` directly on your memory card (via VitaShell FTP or USB):
> ```
> network={
>     ssid="YourWiFiNetworkName"
>     psk="YourWiFiPassword"
> }
> ```
> Without this step the system **will still boot into Linux** but Wi-Fi and SSH will not be available.

To install, simply follow [Step 4](#step-4-transfer-files-to-your-ps-vita) and [Step 5](#step-5-install--launch-linux) below.

---

## 🚀 Complete Step-by-Step Guide: From Clone to Running Linux (Build from Source)

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

### Step 2: Configure Your Wi-Fi Credentials

> [!IMPORTANT]
> **Wi-Fi must be configured before building.** Pre-built release downloads do **not** include Wi-Fi credentials - you must add them yourself before flashing. Without this step the Vita will boot into Linux but will **not** connect to your network and SSH will not be available.

If you want your Vita to automatically join your Wi-Fi network and start SSH on boot:
```bash
cp configs/wifi.conf.example configs/wifi.conf
```
Edit `configs/wifi.conf` with your network details:
```bash
SSID="YourWiFiNetworkName"
PSK="YourWiFiPassword"
```
*(Note: `configs/wifi.conf` is ignored by git so your credentials remain private).*

---

### Step 3: Build Linux with Docker
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
> `./build.sh release` packages everything into `output/LinuxOnVita-release-<version>.zip` ready to share or upload to GitHub Releases. Wi-Fi credentials are **never** included in the release zip.

---

### Step 4: Transfer Files to Your PS Vita
1. Connect your PS Vita to your computer via USB cable (or FTP).
2. Open **VitaShell** and press **SELECT** to enable USB/FTP storage.
3. Choose your preferred installation method below:

#### Option A: Install via `VitaLinux.vpk` (Recommended & Easiest!)
Copy the following files to your memory card (`ux0:`):

| Source on Computer | Destination on PS Vita (`ux0:`) | Description |
| :--- | :--- | :--- |
| `VitaLinux.vpk` (from release zip or `output/vpk/`) | `ux0:VitaLinux.vpk` | Standard Vita homebrew installer package |
| `ux0/linux/*` (from release zip or `output/ux0/linux/`) | `ux0:linux/` | Kernel (`zImage`), Device Trees (`*.dtb`), and Loaders |

#### Option B: Manual Folder Copy
Copy the pre-extracted application folder directly:

| Source on Computer | Destination on PS Vita (`ux0:`) | Description |
| :--- | :--- | :--- |
| `output/ux0/app/VITALINUX/` | `ux0:app/VITALINUX/` | LiveArea launcher bubble folder |
| `output/ux0/linux/*` | `ux0:linux/` | Kernel (`zImage`), Device Trees (`*.dtb`), and Loaders |

Your Vita's file structure should look like this:
```
ux0:
├── VitaLinux.vpk               (if using Option A to install)
├── app/
│   └── VITALINUX/             (installed by VPK, or copied manually)
│       ├── eboot.bin
│       ├── sce_sys/
│       │   ├── icon0.png
│       │   └── param.sfo
└── linux/
    ├── baremetal-loader.skprx
    ├── payload.bin
    ├── zImage
    ├── vita.dtb
    ├── vita1000.dtb
    ├── vita2000.dtb
    ├── pstv.dtb
    └── wpa_supplicant.conf     (edit with your Wi-Fi details)
```

> [!IMPORTANT]
> **For SD2Vita Users:**
> The early baremetal payload initializes storage using Sony's proprietary memory card interface (MSIF).
> - If you use an **SD2Vita** adapter as `ux0:`, you **must also copy** `zImage` and `vita.dtb` to your official Sony memory card (which mounts as `uma0:linux/` in VitaShell).
> - If using a PS Vita 2000 Slim without a Sony memory card, the internal 1GB storage is used.

---

### Step 5: Install & Launch Linux

#### If you used Option A (`VitaLinux.vpk`):
1. Disconnect USB / exit VitaShell server mode.
2. In VitaShell, navigate to `ux0:VitaLinux.vpk`.
3. Press **CROSS (✕)** to install the package.
4. When prompted that the package requires extended/unsafe permissions, press **CROSS (✕)** to accept.
5. Once installation finishes, press the **PS Button** to return to the LiveArea home screen.
6. Tap the new **VitaLinux** bubble and select **Start**.
7. Press **CROSS (✕)** to initiate the baremetal loader handover.

#### If you used Option B (Manual Folder Copy):
1. Disconnect USB / exit VitaShell server mode.
2. In VitaShell, navigate to the partition list (`ux0:`, `ur0:`, etc.).
3. Highlight `ux0:`, press **TRIANGLE (△)** to open the menu, and select **Refresh LiveArea**.
   *(Note: If the bubble does not appear, see [Troubleshooting](#livearea-bubble-does-not-appear-after-refresh-livearea-refreshed-0-items) or simply install `VitaLinux.vpk`).*
4. Press the **PS Button** to return to the LiveArea home screen.
5. Tap the new **VitaLinux** bubble and select **Start**.
6. Press **CROSS (✕)** to initiate the baremetal loader handover.

The screen will blank briefly, take over the display, and boot straight into the Linux terminal!

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

### 4. Hardware Battery Check (`vita-battery`)
Inspect your Vita's hardware battery telemetry at any time:
```bash
vita-battery
```
```
======================================================
               PlayStation Vita Battery               
======================================================
 Battery Level:   87% [================>   ]
 Power State:     Discharging (Battery Power)
 Battery Voltage: 3920 mV
 Battery Health:  Good
======================================================
```
*(Also supports `--percent` and `--short` flags for status scripts).*

### 5. Playing Framebuffer DOOM (`vita-doom`)
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

### 6. Installing Packages with Alpine Linux (`alpine-chroot`)
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

### 7. Remote SSH Access
Within ~12 seconds of boot, your Vita connects to Wi-Fi and starts OpenSSH. Connect from your computer:
```bash
ssh root@vita.local
```
*(If mDNS does not resolve on your network, use the IP address shown on the login banner: `ssh root@<VITA_IP>`)*.

### 8. Returning to VitaOS or Powering Down
```bash
reboot      # Clean cold reset straight back to official Sony OS
poweroff    # Full hardware poweroff via Syscon
```

---

## 🛠 Hardware Support Matrix

| Hardware Component | Status | Implementation Details |
| :--- | :--- | :--- |
| **CPU** | **Working** | Quad-core ARM Cortex-A9 MPCore (SMP active across all 4 cores) |
| **Display / Framebuffer** | **Working** | 960x544 OLED (Vita 1000) and LCD (Vita 2000) at 100% full brightness |
| **Wi-Fi (Marvell SD8787)** | **Working** | `mwifiex_sdio` on SDIF2 with custom Ernie power sequencing |
| **Touchscreen** | **Working** | `vita-syscon-ts.c` multi-touch digitizer mapped via `evdev` |
| **Buttons & D-Pad** | **Working** | Patched `vita-buttons.c` driver with uniform active-low mask and `/dev/uinput` mapping |
| **Analog Sticks** | **Working** | Syscon `0x180` analog sampling enabled with deadzone filtering |
| **Storage (eMMC)** | **Working** | Auto-detected SCE partitions (`/dev/mmcblk*p1`–`p12`) |
| **Storage (SD2Vita / Sony)** | **Working** | Automatically mounted at `/mnt/ux0`, `/mnt/ur0`, and `/mnt/uma0` |
| **Battery Fuel Gauge** | **Working** | Handoff telemetry reporting percentage, voltage, and charging state |
| **UART0 Serial Console** | **Working** | 115200 baud serial debug console |
| **Bluetooth** | Working | Marvell SD8787 via `btmrvl` |
| **Audio** | Working | Vita audio codec via ALSA & PipeWire |
| **3D GPU Acceleration** | Not Working | SGX543MP4+ lacks open-source Linux drivers (software rendering) |

---

## ❓ Troubleshooting

### LiveArea bubble does not appear after "Refresh LiveArea" ("Refreshed 0 items")
- **Cause 1 (Nested Directory):** If you extracted the release zip on your PC/Mac and dragged the `ux0` folder straight into `ux0:`, the files ended up at `ux0:ux0/app/VITALINUX/`. VitaShell only scans the top-level `ux0:app/` folder.
- **Cause 2 (VitaShell Homebrew Detection):** VitaShell's "Refresh LiveArea" was originally designed for NoNpDrm game dumps that contain license files (`work.bin`). Raw homebrew folders without license files can be skipped depending on your VitaShell or HENkaku version.
- **Fix:** Install via **`VitaLinux.vpk`**! In VitaShell, highlight `VitaLinux.vpk` and press **CROSS (✕)** to install it directly. The installer registers the application directly into the PS Vita's system database and creates the bubble reliably every time.

### "The file is corrupt" error when launching the LiveArea bubble
- **Cause:** Sony OS reports this if `eboot.bin` does not physically exist inside `ux0:app/VITALINUX/` on the current `ux0:` partition.
- **Fix:** Install **`VitaLinux.vpk`** via VitaShell, or copy the entire `output/ux0/app/VITALINUX` folder into `ux0:app/VITALINUX` and run **Refresh LiveArea** from VitaShell.

### Error `0x8002D003` when launching the bootstrapper
- **Cause:** Unsafe homebrew is disabled in HENkaku settings.
- **Fix:** Go to Vita **Settings** → **HENkaku Settings** → check **Enable Unsafe Homebrew**.

### "Memory card not inserted" on screen
- **Cause:** The baremetal loader could not locate an official Sony memory card.
- **Fix:** If you use an SD2Vita adapter, copy `zImage` and `vita.dtb` to your official memory card (which mounts as `uma0:linux/` in VitaShell).

### Screen freezes at `Uncompressing Linux... done, booting the kernel`
- **Cause:** The Device Tree Blob (`vita.dtb`) does not match your specific console model.
- **Fix:** Copy `vita1000.dtb` (for OLED 1000) or `vita2000.dtb` (for Slim 2000) to `ux0:linux/vita.dtb`.

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

## 🤝 Contributing & AI Policy

Contributions, bug reports, and feature requests are welcome! Feel free to check out the [issues page](https://github.com/devwithzachary/LinuxOnVita/issues) or submit a pull request.

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

This project and its orchestration scripts are licensed under the [MIT License](LICENSE).
Upstream source trees (Linux kernel, Buildroot, VitaSDK) retain their respective licenses (GPLv2, MIT, BSD).
