# PlayStation Vita Linux: Complete User & Applications Guide

Welcome to the comprehensive user guide for **LinuxOnVita**. This guide is designed for console owners who want to get Linux up and running on their PlayStation Vita, understand everything the built-in tools can do, and explore real-world programs and use cases.

---

## 📖 Table of Contents

1. [Prerequisites & Requirements](#1-prerequisites--requirements)
2. [Quick Installation & First Boot](#2-quick-installation--first-boot)
3. [Navigating the Handheld Console](#3-navigating-the-handheld-console)
   - [On-Screen Touch Keyboard](#on-screen-touch-keyboard-fbkeyboard)
   - [Physical Gamepad Navigation](#physical-gamepad-navigation-vita-input-mapper)
4. [Built-In Applications & Tools](#4-built-in-applications--tools)
   - [On-Device Wi-Fi Manager (`vita-wifi`)](#1-on-device-wi-fi-manager-vita-wifi)
   - [Screen Brightness Control (`vita-brightness`)](#2-screen-brightness-control-vita-brightness)
   - [Memory Expansion & Swap (`vita-swap`)](#3-memory-expansion--swap-vita-swap)
   - [Framebuffer DOOM (`vita-doom`)](#4-framebuffer-doom-vita-doom)
   - [Hardware & System Diagnostics (`vita-diagnostics`)](#5-hardware--system-diagnostics-vita-diagnostics--vita-diag)
   - [Remote SSH & Wireless Terminal](#6-remote-ssh--wireless-terminal)
   - [Rebooting & Powering Down](#7-rebooting--powering-down)
5. [Alpine Linux Package Ecosystem (`alpine-chroot`)](#5-alpine-linux-package-ecosystem-alpine-chroot)
   - [Understanding the Isolated Environment](#understanding-the-isolated-environment)
   - [Entering & Exiting Alpine](#entering--exiting-alpine)
   - [Running Commands from the Base Host](#running-commands-from-the-base-host)
   - [Real-World Packages & Use Cases](#real-world-packages--use-cases)
6. [Managing Files & Storage](#6-managing-files--storage)
7. [Frequently Asked Questions & Troubleshooting](#7-frequently-asked-questions--troubleshooting)

---

## 1. Prerequisites & Requirements

Before getting started, make sure your PlayStation Vita meets these requirements:

* **Custom Firmware:** Any PS Vita (1000 OLED, 2000 Slim, or PlayStation TV) running system software **3.60 or 3.65** with HENkaku / Ensō installed (see [vita.hacks.guide](https://vita.hacks.guide)).
* **Enable Unsafe Homebrew (Critical):**
  - On your Vita, open **Settings** -> **HENkaku Settings**.
  - Check the box for **Enable Unsafe Homebrew [✓]**. If unchecked, the loader will exit with error `0x8002D003`.
* **Official Sony Memory Card:**
  - An official physical Sony memory card is **strictly required** on all hardware models.
  - The low-level baremetal loader communicates directly with Sony's proprietary memory card interface (MSIF) and cannot read boot files from internal eMMC storage (`imc0:`) or SD2Vita game-card adapters.
* **VitaShell:** Needed to transfer and install the `LinuxOnVita.vpk` package.

---

## 2. Quick Installation & First Boot

### Step 1: Download the Package
Download **`LinuxOnVita.vpk`** from the [GitHub Releases page](https://github.com/devwithzachary/LinuxOnVita/releases). This single file is an all-in-one package containing the bootstrapper, Linux 6.12 kernel, device tree files, loaders, and configurations.

### Step 2: Transfer to PS Vita
1. Connect your PS Vita to your computer using a USB cable (or FTP).
2. Open **VitaShell** on your console and press **SELECT** to start the connection.
3. Copy `LinuxOnVita.vpk` to your memory card root (e.g., `ux0:LinuxOnVita.vpk`).
4. Press **CIRCLE (○)** to disconnect.

### Step 3: Install via VitaShell
1. In VitaShell, highlight `LinuxOnVita.vpk` and press **CROSS (✕)** to install.
2. Accept the extended system permissions prompt.
3. Once complete, press the **PS Button** to return to the LiveArea home screen.

### Step 4: Configure Storage & Launch Linux
1. Tap the new **LinuxOnVita** bubble and select **Start**.
2. The application scans all storage partitions and displays them on screen:
   - **Standard Consoles (No SD2Vita):** Your Sony Memory Card is already `ux0:`.
   - **SD2Vita Users:** Your SD2Vita adapter is `ux0:`, while your official Sony Memory Card is typically mapped to `xmc0:` or `uma0:`. Use **D-Pad Up / Down** or **L / R** triggers to select your Sony Memory Card partition. Your selection is automatically remembered for future boots.
3. Press **CROSS (✕)** to install the bundled Linux files to your selected memory card.
4. Press **CROSS (✕)** to boot!

The screen will blank briefly, take over the hardware framebuffer, and boot directly to the Linux terminal prompt (`root@vita:~#`).

### Updating to a New Version
When upgrading your console from a previous release of LinuxOnVita:
1. Transfer and install the new `LinuxOnVita.vpk` in VitaShell.
2. Launch the **LinuxOnVita** bubble from the LiveArea.
3. **Reinstall / Update Memory Card Files:** Press **SQUARE (□)** to update the Linux setup (`<mount>/linux/`). Because boot files (`zImage`, Device Tree blobs, and baremetal loaders) already exist on your memory card from the previous version, pressing Cross (✕) would continue booting the older kernel and files. Pressing **Square (□)** refreshes and overwrites your memory card with the new release files bundled inside the VPK.
4. Press **CROSS (✕)** to boot into your updated Linux system!

---

## 3. Navigating the Handheld Console

LinuxOnVita transforms your handheld console into a self-contained computer without requiring any external cables or keyboards.

### On-Screen Touch Keyboard (`fbkeyboard`)
* **Show / Hide:** Tap the lower third of the touchscreen to bring up the on-screen keyboard. Tap **[Hide]** in the bottom right corner to dismiss it for an unobstructed view of your terminal.
* **Letter / Number Switching:** Tap **[?123]** to access numbers and symbols, or **[ABC]** to return to letters.
* **Special Keys:** Includes dedicated buttons for **Tab**, **Esc**, **Ctrl**, and **Enter**.

### Physical Gamepad Navigation (`vita-input-mapper`)
You can control the terminal shell entirely with the Vita's physical gamepad buttons:

| Button | Action | Shell Equivalent |
| :--- | :--- | :--- |
| **D-Pad Up / Down** | History navigation | Arrow Up / Arrow Down |
| **D-Pad Left / Right** | Cursor movement | Arrow Left / Arrow Right |
| **Cross (✕)** | Execute command | `Enter` |
| **Circle (○)** | Delete character | `Backspace` |
| **Square (□)** | Insert space | `Spacebar` |
| **Triangle (△)** | Auto-complete path/command | `Tab` |
| **L-Trigger** | Cancel command / process | `Ctrl + C` (`SIGINT`) |
| **R-Trigger** | Scroll terminal buffer | `Page Up` |

---

## 4. Built-In Applications & Tools

LinuxOnVita comes pre-installed with dedicated console management utilities tailored specifically for the PS Vita hardware.

### 1. On-Device Wi-Fi Manager (`vita-wifi`)
Connect to wireless networks directly on your console without manually creating configuration files on a PC.

#### Interactive Menu:
Run `vita-wifi` with no arguments to launch a simple on-screen menu:
```bash
vita-wifi
```

#### Scanning for Networks:
```bash
vita-wifi scan
```
Displays an ASCII table showing nearby Wi-Fi networks, channel numbers, signal strength indicators (`[====]`), and security types (WPA2, Open). Enter the row number of your network to connect immediately.

#### Connecting to a Network:
```bash
vita-wifi connect "MyHomeNetwork"
```
Prompts for your Wi-Fi password, acquires an IP address via DHCP, and permanently saves credentials to your memory card (`<mount>/linux/wpa_supplicant.conf`). On all future boots, Linux connects to your Wi-Fi automatically in the background.

#### Checking Network Telemetry:
```bash
vita-wifi status
```
Displays your active interface (`mlan0`), SSID, MAC address, assigned IP, BSSID, and RSSI signal level in dBm.

#### Power Savings:
```bash
vita-wifi off    # Turn off the Wi-Fi radio to save battery
vita-wifi on     # Turn the radio back on and reconnect
```

---

### 2. Screen Brightness Control (`vita-brightness`)
Displays automatically boot to 100% full brightness across all models.

```bash
vita-brightness get        # View current brightness and display hardware status
vita-brightness set 50     # Set brightness percentage (0-100%)
vita-brightness max        # Jump directly to 100% full brightness
```

#### Hardware Differences Explained:
* **PS Vita 2000 (Slim / LCD):** Features an I2C-controlled LED backlight. `vita-brightness` dynamically dims or brightens the screen across its full hardware PWM duty cycle. Setting `0` turns the backlight completely off.
* **PS Vita 1000 (Fat / OLED):** Uses an emissive Samsung OLED panel where each pixel produces its own light. The OLED display is configured via SPI 2 hardware gamma tables during bootloader initialization and is fixed to 100% maximum brightness (Hardware Level 15 Gamma). Running `vita-brightness` displays informative hardware status.
* **PlayStation TV (Dolce):** Video output is handled over HDMI. Brightness is controlled using your connected TV or monitor settings.

---

### 3. Memory Expansion & Swap (`vita-swap`)
The PlayStation Vita hardware has 512 MB of physical RAM. While sufficient for basic tasks, running modern software, compiling C programs, installing large packages, or running Python scripts can exhaust memory and trigger the Linux Out-Of-Memory (OOM) killer.

To solve this, LinuxOnVita incorporates two layers of virtual memory management:
1. **Compressed ZRAM (Default in RAM):** Automatically initialized on boot (`/dev/zram0`). Pages written to swap are compressed in RAM using high-speed LZ4 compression. This expands effective memory to ~768 MB - 1 GB with zero storage wear and near-zero latency.
2. **Physical Swapfiles (Storage Swap):** Optional swap files stored on your SD card or memory card (`/mnt/ux0/swapfile`).

#### Inspecting Memory Telemetry:
```bash
vita-swap status
```
Displays physical RAM usage, ZRAM capacity, LZ4 compression ratio, data stored vs memory consumed in RAM, and active storage swapfiles.

#### Creating an SD Card Swapfile:
To add extra memory headroom for heavy workloads:
```bash
# Create a 512MB swapfile on your SD card (default: /mnt/ux0/swapfile)
vita-swap create 512M

# Or create a 1GB swapfile
vita-swap create 1G
```
This formats the file with contiguous blocks, activates it at secondary priority (10), and saves it to `/etc/vita-swap.conf` so it automatically mounts on every boot.

#### Resizing ZRAM:
```bash
# Adjust the in-memory compressed ZRAM pool size (e.g., 512MB)
vita-swap zram 512M
```

#### Toggling or Removing Swapfiles:
```bash
vita-swap off       # Deactivate swapfile temporarily
vita-swap on        # Reactivate swapfile
vita-swap remove    # Deactivate and delete swapfile to free disk space
```

---

### 4. Framebuffer DOOM (`vita-doom`)
LinuxOnVita includes a native, pure-C implementation of classic DOOM running directly on the Linux framebuffer device (`/dev/fb0`).

#### Setup:
1. Place any standard DOOM WAD file (such as `DOOM1.WAD`, `DOOM.WAD`, or `DOOM2.WAD`) into `ux0:doom/` (accessible in Linux at `/mnt/ux0/doom/`).
2. Run `vita-doom` from the terminal:
```bash
vita-doom
```

#### Controls:
* **Left Analog Stick & D-Pad:** Move forward/backward, turn left/right
* **Right Analog Stick:** Strafe left / right
* **R-Trigger:** Fire weapon
* **Square (□):** Open doors and activate switches
* **Cross (✕) / Select:** Enter, select menu option, confirm Yes (Y) on prompts
* **Circle (○):** Cancel, go back, answer No (N) on prompts
* **L-Trigger:** Run / speed boost
* **Triangle (△):** Automap / confirm prompt
* **Start:** Pause / in-game options menu
* **Start + Select:** Instant quick-exit back to terminal

#### Display Modes:
By default, `vita-doom` automatically scales to fill 100% of the PlayStation Vita screen:
* **Fullscreen (Default):** Runs at 960x544 edge-to-edge widescreen with zero black borders using an optimized zero-copy `mmap` blitter.
* **4:3 Aspect Ratio (Pillarbox):**
  ```bash
  vita-doom -aspect
  ```
  Scales to full vertical height (544 lines) while preserving the original 4:3 CRT proportions (725x544 centered with side borders).
* **Integer 2x Scale (640x400):**
  ```bash
  vita-doom -integer
  ```
  Centers an unscaled 2x pixel-doubled canvas (640x400) in the middle of the screen.

*(Note: Background input daemons and the touch keyboard automatically pause while DOOM is playing and resume cleanly when you exit).*

---

### 5. Hardware & System Diagnostics (`vita-diagnostics` / `vita-diag`)
When encountering issues like Wi-Fi failing to connect or an SD card not mounting, `vita-diagnostics` automates the entire debugging and log collection process. It probes hardware devices, verifies drivers and firmware, tests partition filesystems, queries Wi-Fi association state, redacts sensitive passwords for privacy, and saves a comprehensive report to accessible storage.

#### Running Diagnostics:
```bash
vita-diagnostics
# or using the shorthand alias:
vita-diag
```

#### What It Checks:
* **Storage & SD Cards (`--storage`):**
  - Detects all MMC controllers, partition tables, and SD2Vita adapter devices (`/dev/mmcblk*`).
  - Verifies whether `/mnt/ux0` (SD card or memory card) is mounted.
  - If unmounted: safely probe-mounts candidate partitions with read-only checks across `exfat`, `vfat`, and `ext4` to identify why the mount failed, and provides exact commands to mount it.
  - Inspects internal eMMC storage (`/mnt/ur0`).
* **Wi-Fi & Networking (`--wifi`):**
  - Confirms detection of Marvell 88W8787 wireless hardware (`mlan0` / `wlan0`).
  - Verifies presence and integrity of Marvell firmware (`sd8787_uapsta.bin`) and wireless regulatory database.
  - Queries `wpa_supplicant` and analyzes association states:
    - `4WAY_HANDSHAKE`: Alerts if Wi-Fi password may be incorrect.
    - `SCANNING`: Alerts if SSID was not found, reminding users that PS Vita hardware strictly supports 2.4GHz Wi-Fi (channels 1-13) and cannot connect to 5GHz networks.
    - `COMPLETED`: Confirms active authentication and association.
  - Performs live 2.4GHz RF scans and tests network gateway / DNS pings.
  - **Privacy Guarantee:** Automatically redacts all Wi-Fi passwords (`psk="[REDACTED_FOR_PRIVACY]"`) so diagnostic reports can be shared publicly on Discord or GitHub without leaking private credentials.
* **Kernel & System Telemetry:**
  - Captures full kernel ring buffer (`dmesg`), early boot logs (`/tmp/boot.log`), running processes, memory usage, ZRAM compression ratios, and input mapping status.

#### Safe Multi-Target Log Saving:
Even if your SD card fails to mount, logs are **never lost**. `vita-diagnostics` simultaneously saves to multiple locations:
1. `ur0:vita-diagnostics.txt` (Internal eMMC flash, accessible on all PS Vita models even if SD card fails).
2. `ux0:vita-diagnostics.txt` and `ux0:vita-diagnostics.tar.gz` (SD card / Memory Card, if mounted).
3. `/tmp/vita-diagnostics.txt` (Volatile RAM).

#### How to Retrieve and Send Logs:
1. After running `vita-diagnostics`, type `reboot` and press Enter to return to VitaOS.
2. Launch **VitaShell** from the LiveArea home screen.
3. If your SD card was mounted, navigate to **`ux0:`**. If your SD card failed to mount, navigate to **`ur0:`**.
4. Press **SELECT** in VitaShell to connect your console to your computer via USB (or FTP).
5. Copy `vita-diagnostics.txt` (or `vita-diagnostics.tar.gz`) to your computer and attach it to your GitHub issue or share it with DevWithZachary on Discord!

#### Viewing Logs On-Device:
```bash
vita-diagnostics --view    # View the most recent report on the terminal
```

---

### 6. Remote SSH & Wireless Terminal
Once connected to Wi-Fi, LinuxOnVita automatically starts an OpenSSH server and broadcasts mDNS hostname `vita.local`.

From any computer, tablet, or phone on the same Wi-Fi network:
```bash
ssh root@vita.local
```
*(If mDNS resolution is unavailable on your router, replace `vita.local` with the IP address shown on the login screen or via `vita-wifi status`: `ssh root@192.168.x.x`)*.

Logging in via SSH gives you a full-sized desktop terminal, making it comfortable to transfer files with `scp` or `rsync` and write code using your computer keyboard.

---

### 7. Rebooting & Powering Down
* **Reboot straight to Sony VitaOS:**
  ```bash
  reboot
  ```
  Performs a clean cold hardware reset, returning your console directly to official VitaOS / LiveArea.
* **Full Poweroff:**
  ```bash
  poweroff
  ```
  Signals the Syscon power management chip to power down all console rails completely.

---

## 5. Alpine Linux Package Ecosystem (`alpine-chroot`)

The base LinuxOnVita operating system runs from a lightweight, memory-resident root filesystem (initramfs). To allow installing any modern Linux program, LinuxOnVita integrates **Alpine Linux** with its full-featured `apk` package repository.

### Understanding the Isolated Environment
The Alpine Linux environment runs in an **isolated chroot container** (`/mnt/alpine`):
* **Packages reside inside the container:** Programs installed with `apk add` (such as Python, Git, GCC, or text editors) only exist and execute inside the Alpine environment.
* **Base host isolation:** Running `apk` or Alpine-installed binaries directly at the base `root@vita:~#` prompt will report `command not found`. This is intentional to ensure the core boot filesystem remains untouched, ultra-fast, and crash-resilient.

### Entering & Exiting Alpine
To enter the Alpine Linux environment:
```bash
alpine-chroot
```
* The prompt changes to cyan: `alpine@vita:~#`.
* Now you are inside full Alpine Linux! Update package repositories and install software:
```bash
apk update
apk add fastfetch htop
fastfetch
```
* To return to the base Vita shell, type:
```bash
exit
```
Your prompt returns to green `root@vita:~#`.

### Running Commands from the Base Host
If you want to execute an Alpine-installed program directly from the base prompt without opening an interactive shell:
```bash
alpine-chroot run fastfetch
alpine-chroot -- python3 my_script.py
alpine-chroot -c "htop"
```

---

### Real-World Packages & Use Cases

Here are practical, tested things you can do on your PS Vita using Alpine Linux:

#### 1. Hardware & System Monitoring
Display detailed system telemetry and monitor the Vita's quad-core CPU and memory usage in real time.
```bash
alpine-chroot
apk add fastfetch htop btop ncdu

# Display beautiful system info with console logo
fastfetch

# Interactive process and core monitor
htop

# Disk space visualizer for memory cards
ncdu /mnt/ux0
```

#### 2. Text Editing & Note-Taking On-The-Go
Write code, edit configuration files, or jot down notes directly on your handheld.
```bash
alpine-chroot
apk add nano micro vim

# Launch a friendly text editor with mouse and touch support
micro notes.txt
```

#### 3. Portable Python Development & Micro Web Servers
Run Python 3 scripts, test algorithms, or host a local web server from your pocket.
```bash
alpine-chroot
apk add python3 py3-pip

# Check Python version
python3 --version

# Turn your Vita into a portable Wi-Fi file server:
cd /mnt/ux0
python3 -m http.server 8080
```
*Now open `http://vita.local:8080` (or `http://<VITA_IP>:8080`) in a browser on your phone or PC to download or view files stored on your Vita's SD card!*

#### 4. Native C & C++ Compilation On-Console
Compile real C and C++ programs directly on your PS Vita without needing a cross-compiler or desktop computer.
```bash
alpine-chroot
apk add gcc g++ make musl-dev git

# Create a simple C program
cat << 'EOF' > hello.c
#include <stdio.h>
int main() {
    printf("Compiled natively on PlayStation Vita ARMv7 Cortex-A9!\n");
    return 0;
}
EOF

# Compile and run
gcc -O2 hello.c -o hello
./hello
```
*(Tip: Make sure ZRAM swap is active or run `vita-swap create 512M` before compiling large C++ projects to give the compiler ample memory).*

#### 5. Terminal Web Browsing
Read articles, documentation, or news directly on the Vita screen over Wi-Fi without needing a desktop graphical browser.
```bash
alpine-chroot
apk add links w3m

# Browse Wikipedia or documentation
links https://en.wikipedia.org
```

#### 6. Network Diagnostics & Security Tools
Turn your PS Vita into a portable network audit tool.
```bash
alpine-chroot
apk add curl wget jq nmap net-tools bind-tools

# Test an API endpoint
curl -s https://api.github.com/zen

# Scan local Wi-Fi devices
nmap -sn 192.168.1.0/24
```

#### 7. Terminal Games & Visual Diversions
Classic terminal software running on the Vita's high-contrast display.
```bash
alpine-chroot
apk add cmatrix nyancat bsd-games

# Relax with a falling green digital rain animation
cmatrix
```

---

## 6. Managing Files & Storage

LinuxOnVita automatically detects and mounts your physical storage partitions:

| Path in Linux | Partition Type | Description |
| :--- | :--- | :--- |
| `/mnt/ux0/` | SD2Vita or Memory Card | Main storage for games, DOOM WADs, swapfiles, and Alpine images |
| `/mnt/ur0/` | Internal eMMC | Internal memory storage partition |
| `/mnt/alpine/` | Ext4 loopback | Alpine Linux persistent container filesystem |

### Sharing Files Between VitaOS and Linux
Files placed in `ux0:` inside VitaOS or VitaShell are accessible directly under `/mnt/ux0/` in Linux.
* Example: A file saved to `ux0:data/test.txt` in VitaShell is at `/mnt/ux0/data/test.txt` in Linux.
* Files written to `/mnt/ux0/` in Linux are immediately visible in VitaShell when you reboot into VitaOS.

---

## 7. Frequently Asked Questions & Troubleshooting

### Why is an official Sony memory card required?
The baremetal loader (`payload.bin`) executes in the early boot stage before Linux or VitaOS drivers are running. It uses Sony's proprietary memory card hardware controller (MSIF). It cannot read from SD2Vita game card adapters or internal eMMC memory.

### How do I exit DOOM back to the command prompt?
Press **START + SELECT** simultaneously from any gameplay or menu screen. DOOM will exit cleanly and restore your terminal cursor and keyboard.

### Why does `vita-brightness` say OLED is fixed at 100%?
The PS Vita 1000 uses an emissive OLED panel (Samsung AMS387VB01) without a backlight. Its brightness is governed by hardware gamma tables during bootloader initialization. LinuxOnVita locks it to 100% maximum brightness (Hardware Level 15 Gamma) at boot so games and text are crisp and vibrant. Dynamic backlight dimming is available on PS Vita 2000 (Slim / LCD) models.

### What should I do if a large package installation runs out of memory?
Run `vita-swap create 512M` to create an extra 512 MB swapfile on your SD card. Combined with the built-in 256 MB ZRAM swap, your console will have over 1.2 GB of total virtual memory, eliminating Out-Of-Memory crashes.

### How do I return to VitaOS?
Simply type `reboot` and press Enter. Your console will perform a cold restart straight into official VitaOS.
