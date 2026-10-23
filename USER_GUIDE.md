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
   - [Interactive First-Boot Setup Wizard (`vita-setup`)](#1-interactive-first-boot-setup-wizard-vita-setup)
   - [On-Device Wi-Fi Manager (`vita-wifi`)](#2-on-device-wi-fi-manager-vita-wifi)
   - [Screen Brightness Control (`vita-brightness`)](#3-screen-brightness-control-vita-brightness)
   - [Memory Expansion & Swap (`vita-swap`)](#4-memory-expansion--swap-vita-swap)
   - [Framebuffer DOOM (`vita-doom`)](#5-framebuffer-doom-vita-doom)
   - [Hardware & System Diagnostics (`vita-diagnostics`)](#6-hardware--system-diagnostics-vita-diagnostics--vita-diag)
   - [Remote SSH & Wireless Terminal](#7-remote-ssh--wireless-terminal)
   - [Rebooting & Powering Down](#8-rebooting--powering-down)
   - [Virtual Network Tunnels & Mesh VPNs (`CONFIG_TUN` & `vita-tun`)](#9-virtual-network-tunnels--mesh-vpns-config_tun-wireguard--vita-tun)
   - [Hardware Power & CPU Frequency Control (`vita-cpu` / `vita-power`)](#10-hardware-power--cpu-frequency-control-vita-cpu--vita-power)
   - [Display Sleep, Auto-Dimming & Power Management (`vita-powerd`)](#11-display-sleep-auto-dimming--power-management-vita-powerd)
   - [PlayStation Button LED Control (`vita-led`)](#12-playstation-button-led-control-vita-led)
5. [Alpine Linux Package Ecosystem (`alpine-chroot`)](#5-alpine-linux-package-ecosystem-alpine-chroot)
   - [Understanding the Isolated Environment](#understanding-the-isolated-environment)
   - [Entering & Exiting Alpine](#entering--exiting-alpine)
   - [Running Commands from the Base Host](#running-commands-from-the-base-host)
   - [Real-World Packages & Use Cases](#real-world-packages--use-cases)
6. [Turnkey Graphical Desktop (`alpine-desktop`)](#6-turnkey-graphical-desktop-alpine-desktop)
   - [Concept & Architecture](#concept--architecture)
   - [First-Time Automated Setup](#first-time-automated-setup)
   - [Handheld Desktop Navigation & Controls](#handheld-desktop-navigation--controls)
   - [Pre-Installed Graphical Applications](#pre-installed-graphical-applications)
   - [Exiting Back to Console](#exiting-back-to-console)
7. [Managing Files & Storage](#7-managing-files--storage)
8. [Frequently Asked Questions & Troubleshooting](#8-frequently-asked-questions--troubleshooting)

---

## 1. Prerequisites & Requirements

Before getting started, make sure your PlayStation Vita meets these requirements:

* **Custom Firmware:** Any PS Vita (1000 OLED, 2000 Slim, or PlayStation TV) running system software **3.60 or 3.65** with HENkaku / Ensō installed (see [vita.hacks.guide](https://vita.hacks.guide)).
* **Enable Unsafe Homebrew (Critical):**
  - On your Vita, open **Settings** -> **HENkaku Settings**.
  - Check the box for **Enable Unsafe Homebrew [✓]**. If unchecked, the loader will exit with error `0x8002D003`.
* **Supported Storage (SD2Vita or Internal eMMC):**
  - **No Sony memory card required!** Linux can be installed directly to your **SD2Vita** (`ux0:`) or to **internal system storage (`ur0:`)**.
  - An official Sony memory card is completely optional. If one is present, note that Linux cannot detect or access Sony memory cards at runtime because Linux has no proprietary MSIF driver.
* **VitaShell:** Needed to transfer and install the `LinuxOnVita.vpk` package.

> [!IMPORTANT]
> **Reference Device & Plugin Compatibility Disclaimer**
>
> LinuxOnVita is developed and tested on a **PS Vita 1000 (OLED) running firmware 3.65 Ensō with no additional plugins or mods installed**. It is not tested on heavily modded consoles, and behaviour on other configurations is not guaranteed.
>
> If your Vita has **USB streaming, storage remapping, or other kernel-level taiHEN plugins installed**, you may experience a **boot loop when returning to VitaOS** after running Linux. This is a known compatibility issue (see [issue #8](https://github.com/devwithzachary/LinuxOnVita/issues/8) and [issue #14](https://github.com/devwithzachary/LinuxOnVita/issues/14)).
>
> **Known problematic plugin categories:**
> - USB streaming plugins (e.g. **VitaUSBStream** and related plugins)
> - Storage remapping plugins (e.g. **YAMT / Yet Another Mount Tool**, **StorageMgr**)
> - Any plugin listed under the `*KERNEL*` section of `config.txt` that hooks USB or partition management
>
> **Why does this happen?** The baremetal loader reconfigures the MSIF memory card controller and USB subsystem at a hardware level during Linux boot. On the cold reboot back to VitaOS, these kernel plugins can encounter unexpected hardware state and crash, causing a boot loop.
>
> **If you experience a boot loop after using LinuxOnVita**, see the [recovery procedure](#boot-loop-recovery-after-returning-to-vitaos) in the Troubleshooting section below.

> [!WARNING]
> **Important MicroSD Card Backup Notice for SD2Vita Users**
>
> **Always back up your SD2Vita microSD card before testing baremetal Linux or experimental homebrew.**
>
> Baremetal OS execution takes direct low-level control of the PS Vita SoC, bypassing VitaOS storage drivers and power safeguards. If an experimental kernel boot hangs and forces a 30-second hard power-off (holding the Power button), lower-end or budget microSD cards (particularly budget cards lacking power-loss protection capacitors) can suffer FTL (Flash Translation Layer) controller lockups where the internal controller panics and all sectors return `0xFF`.
>
> LinuxOnVita minimizes storage risk by mounting partitions **read-only by default**, executing synchronous device-level filesystem flushes (`sceIoSync` / `ksceIoSync`) and hardware settling delays prior to Syscon soft-reset, and pre-staging boot files directly in DRAM. However, hardware hangs can happen with early software. Always maintain a full backup of your microSD card image or vital data.

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
2. The modern touchscreen interface loads smoothly on screen with real-time partition cards and storage badges:
   - **SD2Vita Users (`ux0:`):** Tap `ux0:` (highlighted with the green `[RECOMMENDED (SD2VITA)]` badge).
   - **Internal Storage Users (`ur0:`):** Tap `ur0:` (highlighted with the cyan `[RECOMMENDED (INTERNAL)]` badge, ideal for Slim 2000 or PSTV consoles without an SD2Vita).
   - *Targeting Secondary Drives:* By default, only active mounted drives are shown. Tap `[+] SHOW ALL STORAGE DRIVES` to expand unmounted partitions (`uma0:`, `imc0:`, `xmc0:`).
   - *Physical Gamepad Navigation:* You can also use **D-Pad Up / Down / Left / Right** to cycle through cards. Your target selection is automatically remembered for future boots.
3. Tap **[ BOOT LINUX ]** (or press **CROSS ✕**) to install the bundled Linux files to your selected partition with a graphical progress modal.
4. Tap **[ BOOT LINUX ]** (or press **CROSS ✕**) to boot!

> [!NOTE]
> **The VPK app install location and the in-app partition selector are independent choices.**
> You can install the `LinuxOnVita.vpk` app to any partition VitaOS allows (e.g. `ux0:app/`, `ur0:app/`). The partition selector inside the app specifies where the **Linux boot files** (kernel, loaders, device tree) are written (`ux0:linux/` or `ur0:linux/`).

The screen will blank briefly, take over the hardware framebuffer, and boot directly to the Linux terminal prompt (`root@vita:~#`).

### Updating to a New Version
When upgrading your console from a previous release of LinuxOnVita:
1. Transfer and install the new `LinuxOnVita.vpk` in VitaShell.
2. Launch the **LinuxOnVita** bubble from the LiveArea.
3. **Reinstall / Update Boot Files:** Press **TRIANGLE (△)** to update the Linux setup (`<mount>/linux/`). Because boot files (`zImage`, Device Tree blobs, and baremetal loaders) already exist on your storage partition from the previous version, pressing Cross (✕) would continue booting the older kernel and files. Pressing **Triangle (△)** refreshes and overwrites your storage partition with the new release files bundled inside the VPK.
4. Press **CROSS (✕)** to boot into your updated Linux system!

### Booting in Debug Mode (Troubleshooting Boot Issues)
If Linux fails to boot, freezes, or the screen goes dark without reaching the shell prompt:
1. Open the **LinuxOnVita** application bubble.
2. Select your target storage partition (`ux0:` or `ur0:`).
3. Press **SQUARE (□)** to launch **Debug Mode**.
4. Debug Mode runs an automated 5-step diagnostic pre-flight sequence directly on-screen:
   - **Hardware & Firmware Check:** Queries system software version (`3.65`, `3.60`, etc.) and detects your exact hardware model (PS Vita OLED 1000, PS Vita Slim 2000, or PlayStation TV Dolce).
   - **Hardware & Storage Interface:** Confirms system model and verifies storage availability for in-memory kernel pre-staging.
   - **Storage Partition Status:** Scans partition mount points, total capacity, and available free space.
   - **Kernel & DTB Integrity:** Checks file existence and exact byte sizes for `zImage` and model-specific Device Tree blobs (`vita2000.dtb`, `vita1000.dtb`, `pstv.dtb`, or `vita.dtb`).
   - **Loader Pre-Flight:** Verifies `payload.bin` and `baremetal-loader.skprx`, auto-syncing mirrors to `ux0:linux/` to ensure all loader fallback paths succeed.
5. If errors are detected, the boot sequence halts safely with an explanation so you can fix missing files without crashing.
6. If all checks pass, press **CROSS (✕)** to trigger the standby handover into Linux, or press **CIRCLE (○)** to cancel.
7. **Viewing Saved Logs:** All diagnostic steps, return codes, and kernel-space handover events are flushed synchronously to `ux0:data/LinuxOnVita/boot_debug.log` (with fallback to `ur0:data/LinuxOnVita/boot_debug.log`). You can view the log directly on your Vita by pressing **SELECT** in the LinuxOnVita main menu, or open it in VitaShell to attach to GitHub issues.

---

## 3. Navigating the Handheld Console

LinuxOnVita transforms your handheld console into a self-contained computer without requiring any external cables or keyboards.

### On-Screen Touch Keyboard (`fbkeyboard`)
* **Show / Hide:** Tap **[Hide]** in the bottom-right corner to dismiss the keyboard for an unobstructed view of your terminal. When hidden, a prominent, high-contrast **[KEYBOARD]** button remains pinned at the bottom-right corner; tap it at any time to instantly restore the on-screen keyboard. Terminal text is constrained to rows 1-24 (`120x24`), ensuring scrolling output never overwrites or erases the keyboard or reshow button.
* **Hardware PS Button Toggle:** Press the physical PlayStation button on the console to toggle keyboard visibility on and off instantly.
* **Caps Lock Toggle:** Double-tap the **Shift** key to engage Caps Lock mode. When active, the key illuminates in high-visibility amber with a **[CAPS]** label. Tap Shift once again to return to standard typing.
* **Key Repeat on Long-Press:** Holding down **Backspace**, **Spacebar**, or any **Arrow key** automatically repeats after a 400ms delay at fast 80ms intervals.
* **Letter / Number Switching:** Tap **[?123]** to access numbers and symbols, or **[ABC]** to return to letters.
* **Special Keys:** Includes dedicated buttons for **Tab**, **Esc**, **Ctrl**, **Enter**, and **Ctrl+C**.

### Physical Gamepad Navigation (`vita-input-mapper`)
You can control the terminal shell and desktop entirely with the Vita's physical gamepad buttons:

| Button | Action | Shell Equivalent |
| :--- | :--- | :--- |
| **D-Pad Up / Down** | History navigation | Arrow Up / Arrow Down |
| **D-Pad Left / Right** | Cursor movement | Arrow Left / Arrow Right |
| **Cross (✕)** | Execute command | `Enter` |
| **Circle (○)** | Delete character | `Backspace` |
| **Square (□)** | Insert space | `Spacebar` |
| **Triangle (△)** | Auto-complete path/command | `Tab` |
| **Select** | Escape / exit edit mode / cancel | `Escape` (`KEY_ESC` for `vi`, `nano`, menus) |
| **Start** | Execute command / Super key | `Enter` (Terminal) / `Super` (Desktop) |
| **L-Trigger** | Cancel command / process | `Ctrl + C` (`SIGINT`) |
| **R-Trigger** | Scroll terminal buffer | `Page Up` |
| **PS Button** | Toggle touch keyboard | Toggles `fbkeyboard` visibility on / off |
| **Volume Up (+)** | Step display brightness up | Increases brightness by +10% |
| **Volume Down (-)** | Step display brightness down | Decreases brightness by -10% |

---

## 4. Built-In Applications & Tools

LinuxOnVita comes pre-installed with dedicated console management utilities tailored specifically for the PS Vita hardware.

### 1. Interactive First-Boot Setup Wizard (`vita-setup`)
The first time you boot LinuxOnVita, the interactive **First-Boot Setup Wizard** automatically welcomes you directly on the console screen (`/dev/tty1`). It guides you through a seamless 6-step initial configuration so your console is immediately online and optimized for your storage:

```bash
vita-setup               # Launch full 6-step setup wizard anytime
vita-setup --wifi        # Quick access to Wi-Fi setup
vita-setup --storage     # Quick access to storage auto-mount protection
vita-setup --memory      # Quick access to ZRAM & swapfile configuration
vita-setup --alpine      # Create or manage Alpine Linux container (alpine.img)
vita-setup --desktop     # Install IceWM desktop packages & set boot autostart
vita-setup --status      # Check current onboarding and subsystem status
vita-setup --reset       # Reset completion flag to re-run on next boot
```

#### What the Wizard Configures:
1. **Hardware Diagnostics & Health Baseline:** Verifies 4 active ARMv7 CPU cores, frequency operating points, detected display panel (OLED PCH-1000 vs LCD PCH-2000 vs PSTV), storage partition mounts (`ux0:` / `ur0:`), total RAM, active ZRAM compression pool, and Wi-Fi radio status.
2. **Wi-Fi Wireless Networking:** Scans nearby wireless networks, connects with password entry, tests DHCP lease acquisition, and displays your active SSH remote login IP (`ssh root@<IP>`).
3. **Storage Protection Modes:** Choose whether storage partitions mount in Safe Read-Only mode (`ro` - recommended to safeguard VitaOS filesystem data) or Read-Write mode (`rw` - direct access).
4. **Memory Optimization:** Select between 256 MB or 512 MB in-RAM compressed ZRAM pools, and optionally create a 512 MB or 1 GB SD card swapfile (`/mnt/ux0/swapfile`) to eliminate Out-Of-Memory (OOM) crashes.
5. **Alpine Linux Storage Container:** Guided creation of your persistent ext4 `alpine.img` container on SD2Vita (`/mnt/ux0`) or internal storage (`/mnt/ur0`) with automatic free space verification.
6. **Graphical Desktop, Boot Mode & LED Preferences:** Check for IceWM desktop packages, prompt to download and install packages over Wi-Fi, configure whether the console boots directly to the IceWM desktop or to the fast text console shell, and choose the front blue PlayStation Button LED mode (heartbeat, disk activity, solid on, off).

> [!TIP]
> You can safely exit or skip the wizard at any time by pressing **s**. You can re-run `vita-setup` whenever you want to reconfigure your console.

---

### 2. On-Device Wi-Fi Manager (`vita-wifi`)
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

### 3. Screen Brightness Control (`vita-brightness`)
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

### 4. Memory Expansion & Swap (`vita-swap`)
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

### 5. Framebuffer DOOM (`vita-doom`)
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

### 6. Hardware & System Diagnostics (`vita-diagnostics` / `vita-diag`)
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

### 7. Remote SSH & Wireless Terminal
Once connected to Wi-Fi, LinuxOnVita automatically starts an OpenSSH server and broadcasts mDNS hostname `vita.local`.

From any computer, tablet, or phone on the same Wi-Fi network:
```bash
ssh root@vita.local
```
*(If mDNS resolution is unavailable on your router, replace `vita.local` with the IP address shown on the login screen or via `vita-wifi status`: `ssh root@192.168.x.x`)*.

Logging in via SSH gives you a full-sized desktop terminal, making it comfortable to transfer files with `scp` or `rsync` and write code using your computer keyboard.

---

### 8. Rebooting & Powering Down
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

### 9. Virtual Network Tunnels & Mesh VPNs (`CONFIG_TUN`, WireGuard & `vita-tun`)

LinuxOnVita includes built-in kernel support for virtual network tunnels and VPN protocols without requiring external out-of-tree kernel modules.

#### Included Kernel Drivers:
* **Universal TUN/TAP Driver (`CONFIG_TUN=y`):** Provides virtual point-to-point network devices required by VPN clients and mesh networking daemons.
* **Native WireGuard Protocol (`CONFIG_WIREGUARD=y`):** High-speed, modern kernel-space cryptography and tunneling.
* **Packet Routing & Filtering:** Full policy routing (`CONFIG_IP_ADVANCED_ROUTER=y`, `CONFIG_IP_MULTIPLE_TABLES=y`) and Netfilter NAT masquerading (`CONFIG_IP_NF_IPTABLES=y`, `CONFIG_IP_NF_NAT=y`, `CONFIG_IP_NF_TARGET_MASQUERADE=y`).
* **Runtime Config Inspection:** Query compiled kernel symbols at any time via `/proc/config.gz`:
  ```bash
  zcat /proc/config.gz | grep -E "CONFIG_TUN|CONFIG_WIREGUARD"
  ```

#### Automatic Device Provisioning:
On boot, the system automatically creates the character device node at `/dev/net/tun` (`major 10, minor 200`) with accessible `0666` permissions, allowing non-root network daemons to establish tunnels seamlessly.

#### Using `vita-tun` (Interface Management):
To quickly test or manage persistent TUN devices directly from the base shell:
```bash
# Create a persistent TUN interface:
vita-tun add tun0

# Inspect interface state:
ip link show tun0

# Remove the interface:
vita-tun del tun0

# Verify driver and /dev/net/tun availability:
vita-tun status
```

#### Mesh Networking inside Alpine Linux:
Inside the Alpine container (`alpine-chroot`), you can install and run modern mesh networking packages:
* **Tailscale:** Connect your PS Vita directly into your secure personal tailnet:
  ```bash
  alpine-chroot apk add tailscale
  alpine-chroot tailscaled &
  alpine-chroot tailscale up
  ```
* **WireGuard Tools:** Manage encrypted tunnels using standard `wg` and `wg-quick`:
  ```bash
  alpine-chroot apk add wireguard-tools
  ```

---

### 10. Hardware Power & CPU Frequency Control (`vita-cpu` / `vita-power`)
LinuxOnVita includes hardware power management and dynamic CPU frequency scaling for the PlayStation Vita's quad-core ARM Cortex-A9 MPCore processor. You can switch between maximum performance for compilation and desktop responsiveness, balanced standard operation, dynamic load-based scaling, or deep powersave mode for background tasks and long battery life.

#### Hardware Operating Points:
* **444 MHz (Max Performance):** Locks all cores to peak frequency. Ideal for code compilation, web browsing in Badwolf, desktop responsiveness, and software emulation.
* **333 MHz (Balanced Standard):** Standard baseline balancing compute throughput and battery longevity.
* **222 MHz (Intermediate):** Moderate power-efficient baseline.
* **166 MHz (Moderate Powersave):** Reduced energy profile for low-intensity workflows.
* **111 MHz (Deep Powersave):** Minimum clock frequency. Conserves maximum battery during background file downloads, music playback, or long terminal reading sessions.

#### Command-Line Usage:
```bash
# View active CPU frequency, active governor, and supported operating points:
vita-cpu status
# or using the power alias:
vita-power status

# Switch to 444 MHz maximum performance mode:
vita-cpu max

# Switch to 333 MHz standard balanced profile:
vita-cpu balanced

# Enable dynamic load-based frequency scaling (111 - 444 MHz):
vita-cpu ondemand

# Switch to 111 MHz deep powersave mode:
vita-cpu powersave

# Set an explicit operating point in MHz (111, 166, 222, 333, 444):
vita-cpu set 222

# Launch the interactive touch GUI dialog:
vita-cpu gui
```

#### IceWM Desktop Taskbar Widget & GUI:
When running the graphical desktop (`alpine-desktop`):
* **Taskbar Tray Applet (`vita-cpu-widget`):** A live notification widget docks into the IceWM taskbar system tray. Hovering over the icon displays the live frequency and active governor tooltip.
* **1-Tap Profile Switching:** Left-clicking or tapping the tray widget opens the graphical touch profile selector (`vita-cpu gui`). Right-clicking brings up a direct profile selection menu.
* **Start Menu Integration:** Fast profile shortcuts are accessible directly under **Start Menu -> CPU Power & Frequency**.
* **Desktop Shortcut:** Double-click or tap the **CPU Power** icon on the desktop.

---

### 11. Display Sleep, Auto-Dimming & Power Management (`vita-powerd`)
To protect OLED and LCD panels against image retention, preserve battery longevity, and provide standard handheld power button behavior, LinuxOnVita features a native background power management daemon: `vita-powerd`.

#### Automated Inactivity Timers:
* **3-Minute Auto-Dim (180s):** If no buttons, touchscreen events, or analog stick movements occur for 3 minutes, the screen brightness automatically dims to 20% PWM.
* **5-Minute Auto-Sleep (300s):** If inactivity continues for 5 minutes, the screen blanks completely into sleep mode (`vita-brightness set 0` and console blanking). Background daemons, SSH sessions, downloads, and audio playback continue running without interruption.
* **Instant Wake-Up:** Touching the front screen, pressing any button (D-Pad, Cross, Circle, Square, Triangle, Triggers, Select, Start), or moving an analog stick instantly restores the previous active brightness level.

#### Hardware Power Button Shortcuts:
* **Short Press (< 3 seconds):** Immediately toggles display sleep / wake on demand. Pressing Power puts the screen to sleep instantly; pressing it again wakes it back up.
* **Long Press (Hold for >= 3 seconds):** Initiates a safe, graceful system shutdown (`poweroff`). The system flashes the blue PS button LED, flushes all write buffers, syncs filesystems, and powers down via Syscon.

#### Command-Line Controls:
```bash
# View active daemon status, idle time, and display state:
vita-powerd status

# Immediately blank display and put screen to sleep:
vita-powerd sleep
# or via vita-power:
vita-power sleep

# Immediately wake display and restore active brightness:
vita-powerd wake
# or via vita-power:
vita-power wake

# Dim display to 20%:
vita-powerd dim

# Reload daemon configuration:
vita-powerd reload
```

#### Customizing Timeouts (`/etc/vita-powerd.conf`):
You can customize timeouts and brightness thresholds in `/etc/vita-powerd.conf`:
```ini
DIM_TIMEOUT=180        # Seconds before dimming (0 = disabled)
SLEEP_TIMEOUT=300      # Seconds before sleeping (0 = disabled)
DIM_BRIGHTNESS=20      # Brightness percentage when dimmed
POWER_HOLD_SECS=3      # Seconds to hold Power button for shutdown
```

---

### 12. PlayStation Button LED Control (`vita-led`)
LinuxOnVita gives you complete control over the front blue PlayStation Home button LED via the Linux kernel LED class and trigger subsystem:

```bash
vita-led                 # Display current LED trigger status, brightness, and device node
vita-led heartbeat       # Set subtle rhythmic pulse reflecting CPU load
vita-led disk            # Blink on SD card and Game Card storage read/write activity (mmc0)
vita-led activity        # Modulate LED brightness with instantaneous CPU activity
vita-led on              # Turn blue LED on with steady, solid illumination
vita-led off             # Turn blue LED off completely
vita-led set <trigger>   # Set an arbitrary kernel trigger (timer, oneshot, etc.)
vita-led apply           # Reapply saved LED preferences from configuration file
```

#### Activity Trigger Modes:
* **`heartbeat`:** A smooth double-flash cardiac pulse whose tempo scales dynamically with system load average.
* **`disk` (or `storage`):** Blinks in real-time on hardware MMC read/write operations, functioning as an authentic disk activity indicator.
* **`activity` (or `cpu`):** Dynamically scales LED intensity based on instantaneous CPU core utilization.
* **`on` / `off`:** Static illumination control for night-time or low-distraction use.

#### Automatic Persistence:
LED preferences are saved to `/etc/vita-led.conf` and `/mnt/ux0/linux/led.conf`, restoring automatically across system boots via `/etc/init.d/S40input`.

---

## 5. Alpine Linux Package Ecosystem (`alpine-chroot`)

The base LinuxOnVita operating system runs from a lightweight, memory-resident root filesystem (initramfs). To allow installing any modern Linux program, LinuxOnVita integrates **Alpine Linux v3.24** with its full-featured `apk` package repository.

> [!IMPORTANT]
> **Where Does alpine.img Live? Storage Architecture Explained**
>
> | Device | VitaOS Name | Linux Path | Role |
> | :--- | :--- | :--- | :--- |
> | **SD2Vita / Game Card Adapter** | `ux0:` | `/mnt/ux0/` | Primary runtime storage: where `alpine.img`, swapfiles, DOOM WADs, and your files live. |
> | **Internal eMMC / NAND** | `ur0:` | `/mnt/ur0/` | Internal system storage (available on all consoles, fallback for `alpine.img`). |
> | **Sony Memory Card** | `xmc0:` / `uma0:` | *(not accessible)* | Proprietary MSIF hardware not accessible in Linux; not used or needed. |
>
> **In short:** Linux boot files and your `alpine.img` package container live directly on your **SD2Vita card** (`/mnt/ux0/`) or **internal storage** (`/mnt/ur0/`). An official Sony Memory Card is not required at all.

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

### Customising the alpine.img Size
By default, `alpine-chroot` creates a **1 GB** (`1024 MB`) persistent ext4 image. If you have a large SD2Vita card and need more space for packages, create a bigger image:
```bash
# Create a 4GB alpine image on your SD card
alpine-chroot --size 4G

# Or recreate an existing image at a new size (WARNING: this deletes all installed packages)
alpine-chroot --recreate --size 4G
```
The image is stored at `/mnt/ux0/alpine.img` (or `/mnt/ur0/alpine.img` if using internal storage). Before allocating space, `alpine-chroot` automatically verifies that your storage device has sufficient free space to accommodate the requested image size plus a 32 MB safety buffer, preventing disk exhaustion or corrupted partial images. Use `df -h /mnt/alpine` inside the chroot to see available space.

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

## 6. Turnkey Graphical Desktop (`alpine-desktop`)

LinuxOnVita includes an automated turnkey launcher (`alpine-desktop`) that runs an ultra-lightweight **IceWM** desktop environment directly on the PlayStation Vita framebuffer (`/dev/fb0`), requiring zero 3D GPU acceleration and consuming only ~25 MB RAM idle (saving over 100 MB of RAM compared to heavyweight desktops like XFCE).

### Concept & Architecture
* **X11 Framebuffer Server (`Xfbdev` / `Xorg` fbdev):** Renders the graphical user interface directly into native 960x544 32-bit color screen memory.
* **Ultra-Low Memory Footprint (~25 MB RAM):** IceWM combined with PCManFM desktop management uses minimal RAM, leaving more than 350 MB of free system memory for running applications and multitasking.
* **Shared Storage Container:** Reuses the exact same persistent `alpine.img` ext4 container as `alpine-chroot`, ensuring any files created in the desktop are immediately accessible in chroot and vice versa.
* **Handheld Ergonomics:** Automatically transforms the PS Vita gamepad into a mouse and keyboard controller, accompanied by an on-screen touchscreen keyboard.
* **Lightweight First-Time Download:** Desktop packages are not bundled in the base VPK to keep downloads minimal. `alpine-desktop` downloads and sets up the environment on-device on first run.

### First-Time Automated Setup
To set up and launch the desktop, connect your PS Vita to Wi-Fi, then run:
```bash
alpine-desktop
```
On first launch, `alpine-desktop` automatically:
1. Verifies active Wi-Fi internet connectivity.
2. Updates Alpine package indexes.
3. Installs `icewm`, `pcmanfm`, `xfce4-taskmanager`, `dbus-x11`, `htop`, `mousepad`, `badwolf`, `feh`, `matchbox-keyboard`, `yad`, and `sakura`.
4. Creates pre-configured X11 framebuffer settings (`/etc/X11/xorg.conf.d/10-fbdev.conf`) calibrated for 96 DPI proportional font rendering.
5. Configures desktop shortcuts and application menus for the Welcome Guide, Badwolf web browser, PCManFM file manager, Mousepad text editor, Xfce activity monitor, Sakura terminal, and virtual keyboard.
6. Starts the graphical IceWM desktop session.

Subsequent launches start immediately in seconds with no re-downloading required!

### Handheld Desktop Navigation & Controls
When `alpine-desktop` starts, `vita-input-mapper` automatically switches into **Desktop Controller Mode**:

| Physical Control | Desktop Action | Description |
| :--- | :--- | :--- |
| **Left Analog Stick** | Mouse Cursor | Smooth 360-degree pointer navigation with calibrated half-speed acceleration |
| **R Trigger** or **Cross (✕)** | Left Mouse Click | Primary click, drag and drop, and window focus |
| **L Trigger** or **Circle (○)** | Right Mouse Click | Context menus and desktop background options |
| **Square (□)** | Middle Click | Paste clipboard or close browser tabs |
| **Triangle (△)** | Enter Key | Confirm dialogs and open highlighted files |
| **Right Analog Stick** | Scroll Wheel | Smooth vertical and horizontal document and web page scrolling |
| **Start Button** | Application Menu | Opens the IceWM Applications Menu (Super / Windows key) |
| **Select Button** | Escape (Esc) | Closes context menus or cancels selections |
| **D-Pad (Arrows)** | Arrow Keys | Directional navigation inside menus and text documents |
| **Touchscreen** | Direct Touch | Direct finger tap and drag on the display |
| **On-Screen Keyboard** | Virtual Typing | Tap the Keyboard icon on the desktop or menu to toggle `matchbox-keyboard` |

### Bundled Graphical Applications
* **Welcome Guide (`vita-desktop-welcome`):** Graphical handheld guide featuring tabs for system specifications, physical controls, bundled apps, software installation instructions, and desktop startup preferences.
* **Web Browsing (`badwolf`):** Privacy-oriented, WebKitGTK-based graphical web browser supporting modern HTML5, CSS3, and JavaScript within a handheld memory footprint.
* **Activity & Process Monitor (`xfce4-taskmanager` / `taskmanager`):** Dedicated task manager displaying live CPU and memory utilization graphs, running processes, and task management directly from the desktop.
* **On-Screen Keyboard (`matchbox-keyboard`):** Touch-friendly virtual keyboard. Tap the Keyboard icon on the desktop or Start Menu anytime to show or hide the keyboard via `toggle-keyboard`.
* **File Manager (`pcmanfm`):** Full-featured lightweight file manager with tabbed browsing, desktop wallpaper management, and drag-and-drop support.
* **Text Editing (`mousepad` / `leafpad`):** Clean graphical notepad with syntax highlighting, tabs, search/replace, and line numbers for code and notes.
* **Terminal Emulator & Software Management (`sakura`):** Fast GTK terminal emulator with multi-tab support and direct native CLI package management via `apk` (`apk update`, `apk add <pkg>`, `apk search <pkg>`).
* **Image Viewing (`feh`):** Lightweight image viewer for screenshots, diagrams, and photos.

### Exiting Back to Console
To exit the graphical desktop session:
* Select **Log Out** from the bottom-left IceWM start menu.
* Or press **Ctrl+C** on a physical or virtual keyboard.

`alpine-desktop` will cleanly shut down X11, restore `vita-input-mapper` to standard console terminal mode, unmount bind mounts, and return you to the command prompt.

### Booting Directly into Desktop on Startup
If you use LinuxOnVita primarily as a portable handheld computer, you can configure the system to boot straight into the IceWM graphical desktop environment on power-on:

* **Enable Autostart:**
  ```bash
  vita-autostart enable
  # or
  alpine-desktop --autostart on
  ```
* **Disable Autostart (return to console boot):**
  ```bash
  vita-autostart disable
  # or
  alpine-desktop --autostart off
  ```
* **Check Status:**
  ```bash
  vita-autostart status
  # or
  alpine-desktop --status
  ```
* **From the Graphical Desktop:**
  Open the IceWM Start Menu and select **Settings** -> **Toggle Boot to Desktop** (or launch from the Applications menu).

**Built-in Failsafe & Exit Protection:**
When desktop autostart is active, the system displays a 3-second countdown before launching X11:
```text
========================================================
 LinuxOnVita: Booting directly into Alpine Desktop...   
========================================================
 Starting desktop in 3 seconds...                       
 (Press any key to cancel and stay in console shell)    
========================================================
```
* **Bypass Autostart:** Press any key or button during the 3-second countdown to cancel autostart and enter the `root@vita:~#` shell.
* **Clean Log Out:** Selecting **Log Out** from the IceWM Start Menu cleanly shuts down the desktop and drops you into the console shell. A session-level guard prevents desktop loop respawns, allowing you to work in the terminal for the remainder of your session.

---

## 7. Managing Files & Storage

### Storage Architecture & Devices

LinuxOnVita installs and runs directly from standard storage devices:

**SD2Vita / Game Card Adapter (`ux0:` in VitaOS)**
- Primary storage for Linux boot files (`ux0:linux/`), `alpine.img`, swapfiles, games, and user data.
- Mounted automatically at `/mnt/ux0/` in Linux.
- Recommended for the best performance and storage capacity.

**Internal eMMC / NAND Storage (`ur0:` in VitaOS)**
- Built-in internal system storage present on all PS Vita consoles.
- Mounted automatically at `/mnt/ur0/` in Linux.
- Supports standalone installation without an SD2Vita adapter or memory card, or acts as fallback storage.

**Sony Memory Card Note (`xmc0:` / `uma0:` in VitaOS)**
- An official Sony memory card is **not required**.
- If a Sony memory card is inserted, Linux cannot detect or use it because Linux does not have a proprietary MSIF hardware driver.

```
Storage Device      VitaOS Name      Linux Path         Role
------------------  ---------------  -----------------  ---------------------------
SD2Vita Adapter     ux0:             /mnt/ux0/          Boot files, runtime storage, alpine.img
Internal eMMC       ur0:             /mnt/ur0/          Internal boot files or fallback storage
Sony Memory Card    xmc0: / uma0:    (not accessible)   Not detected or accessible in Linux
```

LinuxOnVita automatically detects and mounts your runtime storage partitions:

| Path in Linux | Partition Type | Description |
| :--- | :--- | :--- |
| `/mnt/ux0/` | SD2Vita adapter | Main storage: boot files, alpine.img, games, DOOM WADs, swapfiles |
| `/mnt/ur0/` | Internal eMMC | Internal storage fallback or standalone internal installation |
| `/mnt/alpine/` | Ext4 loopback (inside alpine.img) | Alpine Linux persistent container filesystem |

### Sharing Files Between VitaOS and Linux
Files placed in `ux0:` inside VitaOS or VitaShell are accessible directly under `/mnt/ux0/` in Linux.
* Example: A file saved to `ux0:data/test.txt` in VitaShell is at `/mnt/ux0/data/test.txt` in Linux.
* Files can be read directly without modifying VitaOS filesystem state.

### Storage Protection & Safe Remounting
To safeguard VitaOS database integrity and protect SD card flash controllers against sudden power interruptions, LinuxOnVita mounts `/mnt/ux0/` (SD2Vita) and `/mnt/ur0/` (internal memory) in **read-only (`ro`) mode by default**:
1. **Zero Dirty Flags:** Mounting read-only prevents the exFAT/FAT `VolumeDirty` bit from ever being set, preventing forced database rebuilds upon returning to VitaOS.
2. **Flash Controller Safety:** High-capacity SD cards (including 512GB and 1TB models) maintain complex Flash Translation Layer (FTL) tables in RAM. Read-only mounting guarantees no write transactions are in-flight during a reboot or crash.
3. **Safe Write Access:** If you need to copy or edit files on `/mnt/ux0/`, temporarily enable write access:
   ```bash
   vita-storage rw ux0       # Temporarily enable read-write access
   # ... copy your files ...
   vita-storage ro ux0       # Safely sync and lock back to read-only
   ```
4. **Clean Rebooting:** Always use the `vita-reboot` command to return to VitaOS. It automatically flushes block device buffers, unmounts storage volumes, and enforces a settling delay before resetting the console hardware.

---

## 8. Frequently Asked Questions & Troubleshooting

### Do I need an official Sony memory card?
No! Starting with version 1.4.0, LinuxOnVita features in-memory DRAM pre-staging. The app loads the Linux kernel (`zImage`) and Device Tree into physical DRAM before jumping to the baremetal loader, allowing Linux to boot directly from SD2Vita (`ux0:`) or internal system storage (`ur0:`).

An official Sony memory card is completely optional. If you do have a Sony card inserted, note that Linux cannot detect or access it at runtime because Linux does not have a proprietary MSIF driver.

### How do I exit DOOM back to the command prompt?
Press **START + SELECT** simultaneously from any gameplay or menu screen. DOOM will exit cleanly and restore your terminal cursor and keyboard.

### Why does `vita-brightness` say OLED is fixed at 100%?
The PS Vita 1000 uses an emissive OLED panel (Samsung AMS387VB01) without a backlight. Its brightness is governed by hardware gamma tables during bootloader initialization. LinuxOnVita locks it to 100% maximum brightness (Hardware Level 15 Gamma) at boot so games and text are crisp and vibrant. Dynamic backlight dimming is available on PS Vita 2000 (Slim / LCD) models.

### What should I do if a large package installation runs out of memory?
Run `vita-swap create 512M` to create an extra 512 MB swapfile on your SD card. Combined with the built-in 256 MB ZRAM swap, your console will have over 1.2 GB of total virtual memory, eliminating Out-Of-Memory crashes.

### How do I return to VitaOS?
Simply type `reboot` and press Enter. Your console will perform a cold restart straight into official VitaOS.

### Why does df -h only show 1GB inside the Alpine environment?

When inside the Alpine Linux chroot (`alpine-chroot enter`), `df -h` reports the filesystem size of `/` as **1GB**.

This is because `alpine.img` is a fixed-size ext4 loopback image container stored on your SD2Vita card (`/mnt/ux0/alpine.img`) or internal storage (`/mnt/ur0/alpine.img`). By default, it is created with a size of 1GB so that initial setup is fast and takes minimal space.

If you have plenty of free space on your host storage and want more room for packages, you can resize or recreate the alpine container to a larger size:

```bash
# Check free space on your host storage first
df -h /mnt/ux0

# Recreate alpine.img at 4GB (WARNING: deletes all installed packages in the container)
alpine-chroot --recreate --size 4G
```

To summarise storage paths:

| Storage Path | Location | Description |
| :--- | :--- | :--- |
| `/mnt/ux0/` | SD2Vita microSD | Host storage for kernel, files, swapfiles, and alpine.img |
| `/mnt/ur0/` | Internal eMMC | Internal storage fallback or standalone install |
| `/` (inside chroot) | `/mnt/ux0/alpine.img` | Alpine Linux container (1GB default, resizable) |

### Can I use VPNs and mesh networking (Tailscale / ZeroTier / WireGuard)?
Yes! In version 1.4.0, LinuxOnVita builds `CONFIG_TUN` (Universal TUN/TAP device driver) and `CONFIG_WIREGUARD` directly into the kernel. The device node `/dev/net/tun` is created automatically on boot. Inside Alpine Linux (`alpine-chroot`), you can install Tailscale (`apk add tailscale`) or WireGuard tools (`apk add wireguard-tools`) to connect your Vita to your mesh networks. You can also verify TUN functionality using the built-in `vita-tun` tool (`vita-tun add tun0`, `vita-tun del tun0`).

---

### Boot Loop Recovery After Returning to VitaOS

If your Vita enters a boot loop or crashes at the loading circle after rebooting from Linux, a kernel plugin is likely conflicting with hardware state left by the baremetal loader (see [issue #8](https://github.com/devwithzachary/LinuxOnVita/issues/8) and [issue #14](https://github.com/devwithzachary/LinuxOnVita/issues/14)).

This has been confirmed with **VitaUSBStream** and is suspected with **YAMT / StorageMgr** and similar storage or USB kernel plugins.

**Recovery steps:**
1. **Bypass plugins on boot:** Hold the **L trigger** while powering on your Vita. This tells Ensō to skip loading taiHEN plugins, booting you into a clean LiveArea.
2. **Open VitaShell** from the LiveArea.
3. **Edit your taiHEN config:** Navigate to `ur0:tai/config.txt` (or `ux0:tai/config.txt` if you have a `tai` folder there). Open the file.
4. **Comment out or remove the offending plugin:** Find the line under `*KERNEL*` referencing the problematic plugin (e.g. `ur0:tai/VitaUSBStream.skprx`) and either delete it or prefix it with `#` to disable it.
5. **Save the file** and reboot normally. Your Vita should boot cleanly.
6. **Test LinuxOnVita again.** With the conflicting plugin removed, reboot back to Linux and verify the return reboot to VitaOS also works without a loop.

> [!NOTE]
> If holding **L** does not bypass the loop, try booting into Safe Mode (hold **Power + R** or **Power + PS Button** depending on your model) and use the **Rebuild Database** option. As a last resort, **Restore PS Vita System** in Safe Mode will recover the console but will remove your jailbreak and data.

**Long-term:** If you rely on VitaUSBStream or a storage plugin, note this as a current incompatibility. You may need to re-enable it after each LinuxOnVita session. Future releases may investigate a clean hardware teardown on exit to mitigate this.
