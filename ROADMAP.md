# LinuxOnVita Roadmap

This roadmap outlines upcoming milestones, target releases, and prioritized features for LinuxOnVita. Features are organized strictly by practical utility, handheld ergonomics, and user impact, ensuring that critical hardware protection, usability, and power management arrive in the earliest releases.

> [!NOTE]
> Roadmap milestones and priorities are refined through hands-on device testing, hardware reverse-engineering findings, and community feedback.

---

## Release Milestones Overview

| Version | Milestone Theme | Primary User Value |
| :--- | :--- | :--- |
| **[v1.4.0]** | **Core Architecture, Storage Independence & Quad-Core SMP** | 4-Core SMP performance, Sony card independence, persistent read-only storage safety, `CONFIG_TUN` mesh networking, and CLI keyboard reshow button. |
| **[v1.5.0]** | **Handheld Ergonomics, Screen Protection & First-Boot Setup** | First-boot onboarding wizard (`vita-setup`), display sleep and auto-dimming (`vita-powerd`), readable console fonts (`vita-font`), handheld TUI control center (`vita-config`), and gamepad input mapping. |
| **[v1.6.0]** | **Wireless Freedom, Web Management & Performance Controls** | Web-based wireless file transfer portal (`http://vita.local:8080`), CPU overclocking/underclocking (`vita-cpu`: 111-444 MHz), and Bluetooth peripheral pairing (`bluez`). |
| **[v1.7.0]** | **Pocket Sysadmin & Handheld Developer Workstation** | Network Swiss Army knife (`vita-nettools` with Tailscale / ZeroTier / nmap), developer environment (`vita-dev` with Python/C/Git), and remote desktop (VNC/RDP). |
| **[v1.8.0]** | **Direct Framebuffer Gaming & Fantasy Consoles** | TIC-80 and Pico-8 (Fake-08) fantasy consoles, lightweight SDL framebuffer emulators, ScummVM adventure gaming, and x86 user-mode emulation. |
| **[v1.9.0+]** | **Advanced Hardware Research & Kernel Power States** | Suspend-to-RAM deep sleep states, DSP hardware video decoding, and SGX543 3D acceleration research. |

---

## Detailed Milestone Specifications

### [v1.4.0] - Core Bootloader Architecture, Storage Independence & SMP Performance (Current)

**Focus Areas:** Full Quad-Core SMP enablement, Sony memory card independence, storage protection, and virtual network tunneling.

* **Full Quad-Core SMP Enablement:**
  * All 4 ARM Cortex-A9 cores online in full Symmetric Multi-Processing (SMP) mode.
  * Corrected scratchpad stack pointer initialization (`0x1F007E00`) and secondary core mailbox handling.
  * Eliminated secondary core initialization stalls, delivering 1152 BogoMIPS total compute power.
* **Sony Memory Card Independence (DRAM Pre-Staging):**
  * Boot Linux directly from SD2Vita (`ux0:`) or internal eMMC (`ur0:`) without requiring a proprietary Sony memory card.
  * In-memory kernel (`zImage`) and Device Tree (`*.dtb`) pre-staging within `baremetal-loader.skprx` during the VitaOS stage.
  * Bypassed MSIF memory card hardware authentication loops, speeding up baremetal boot times by 2 to 3 seconds.
* **Storage Protection & Persistence:**
  * Default read-only mounting for `/mnt/ux0` and `/mnt/ur0` to prevent filesystem corruption and VitaOS database rebuilds.
  * Elevated write-protection in `vita-wifi` to save wireless credentials cleanly.
  * Disk space pre-allocation verification with 32 MB safety margins in `alpine-chroot` and `alpine-desktop`.
* **Direct-to-Desktop Boot Preference:**
  * Autostart IceWM on boot (`vita-autostart`) with a 3-second failsafe console countdown.
* **Console Touch Keyboard Reshow Button:**
  * Prominent, high-contrast `[KEYBOARD]` button on the bottom-right corner when `fbkeyboard` is hidden.
  * Virtual console geometry isolated to 32 rows (`120x32`), protecting the bottom 32 pixels from terminal text scrolling.
* **Virtual Network Tunnels (`CONFIG_TUN` & WireGuard):**
  * Built-in `CONFIG_TUN=y` and `CONFIG_WIREGUARD=y` kernel support for Tailscale, ZeroTier, and WireGuard mesh networking.
  * Netfilter NAT masquerading, packet filtering, and automatic `/dev/net/tun` device provisioning.

---

### [v1.5.0] - Handheld Ergonomics, Screen Protection & First-Boot Setup

**Priority:** CRITICAL (Highest)
**Focus Areas:** Battery life preservation, OLED burn-in protection, readable console fonts, guided onboarding, and gamepad ergonomics.

* **Display Sleep, Auto-Dimming & Power Management (`vita-powerd`):**
  * **OLED & LCD Burn-In Prevention:** Background event monitor tracking `/dev/input/event*` for touch, stick, and button activity.
  * **Automated Inactivity Timeout:**
    * Inactivity for 3 minutes: Dims brightness to 20% PWM.
    * Inactivity for 5 minutes: Blanks the screen completely (`vita-brightness set 0`) while keeping background tasks running.
    * Touching the screen or pressing any button instantly wakes the display back to previous brightness.
  * **Power Button Integration (`KEY_POWER`):**
    * Short press: Immediately toggles display sleep/wake.
    * Long press (3 seconds): Initiates a clean, graceful system shutdown (`poweroff`).
  * **Battery Health Monitoring:** Background monitor blinks the blue PlayStation LED when battery drops below 15% and performs a safe emergency shutdown when reaching critical levels.

* **Interactive First-Boot Setup Wizard (`vita-setup`):**
  * Launches automatically on initial boot (and re-runnable at any time from CLI).
  * **Wi-Fi Configuration:** Interactive network scan and password entry to get online immediately.
  * **Storage Auto-Mount Preferences:** Select which partitions (`ux0:`, `ur0:`, `uma0:`) auto-mount at boot and configure default protection modes.
  * **Permanent Alpine Storage:** Guided `alpine.img` ext4 creation with storage destination selection, interactive size choice, and automated free space checks.
  * **Desktop Environment Installation:** Interactive prompt to download and install the IceWM graphical desktop.
  * **Desktop Autostart:** Configure whether the system boots directly to IceWM desktop or to the console shell.
  * **Memory Optimization:** Automatic configuration of ZRAM RAM compression and swapfile sizing tailored to the available storage.
  * **Diagnostic Baseline:** Automated hardware verification confirming screen, network, and storage health.

* **Handheld Control Center TUI (`vita-config`):**
  * Unified interactive console menu utility (inspired by `raspi-config` on Raspberry Pi):
    * **Network:** Scan and connect to Wi-Fi networks.
    * **Display:** Adjust brightness slider and set screen blanking timeouts.
    * **Audio:** Adjust volume levels and toggle speaker mute.
    * **Bluetooth:** Scan, pair, and manage Bluetooth peripherals.
    * **Input:** Switch gamepad mapper modes (Console Keypad vs Analog Mouse vs Custom).
    * **Storage:** View partition free space, check disk health, and resize Alpine container image.
    * **Console:** Change console font size on the fly.
    * **Power:** Reboot directly to VitaOS LiveArea or perform full shutdown.

* **High-DPI Console Font Switcher (`vita-font`):**
  * Resolves the tiny 8x16 font (120x34) issue on the 5-inch 960x544 display.
  * Bundle clean bitmap fonts into rootfs (Terminus 12x24 and 16x32).
  * Simple CLI tool: `vita-font [small | medium | large]` allowing instant switching between high density (120x34) and comfortable handheld reading (80x22 or 60x17).

* **Gamepad Input Mapping & Keyboard Polish:**
  * **Key Repeat on Long-Press:** Holding Backspace, Space, or Arrow keys in `fbkeyboard` repeats after 400ms at 80ms intervals.
  * **Caps Lock Toggle:** Double-tapping Shift in `fbkeyboard` locks uppercase mode with an illuminated indicator.
  * **Select Button (`BTN_SELECT`):** Mapped to `KEY_ESC` in `vita-input-mapper` (essential for `vi`, `nano`, command cancellation, and curses menus).
  * **Volume Buttons (`KEY_VOLUMEUP` / `KEY_VOLUMEDOWN`):** Step display brightness up or down by 10% (or control ALSA audio volume).
  * **PS Button (`BTN_MODE`):** Configurable to toggle `fbkeyboard` visibility or launch the quick overlay menu.
  * **PlayStation LED Daemon (`vita-led`):** Configurable activity triggers for the blue PS button LED (disk I/O activity, heartbeat pulse, or low battery warning).

---

### [v1.6.0] - Wireless Freedom, Web Management & Performance Controls

**Priority:** HIGH
**Focus Areas:** Cable-free file management, CPU frequency governors, Bluetooth peripherals, and desktop network management.

* **Web-Based Management Portal & Wireless File Transfer (`vita-webui`):**
  * Lightweight local web server running on port 80/8080 accessible via `http://vita.local:8080` or `http://<VITA_IP>:8080`.
  * **Drag-and-Drop Web File Manager:** Upload and download files, ROMs, scripts, wallpapers, and documents between any phone, tablet, or PC and the Vita storage (`/mnt/ux0/`).
  * **Live System Telemetry Dashboard:** Real-time visual monitoring of CPU frequency, per-core load, battery percentage, RAM/ZRAM usage, and Wi-Fi signal.
  * **In-Browser Web Terminal (`ttyd`):** Access the Vita command prompt from a laptop or tablet browser without needing SSH keys configured.
  * **Zero-Conf Discovery:** Automatic local network discovery via mDNS / Bonjour.

* **Hardware Power & CPU Frequency Control (`vita-cpu` / `vita-power`):**
  * Dynamic CPU frequency scaling across hardware operating points:
    * **444 MHz:** High-performance mode for code compilation, desktop responsiveness, and software emulation.
    * **333 MHz:** Standard baseline balancing speed and battery longevity.
    * **111 MHz / 166 MHz:** Powersave mode for background downloads, music playback, or long terminal reading sessions.
  * CLI utility (`vita-cpu [status | max | powersave | ondemand]`) and IceWM desktop taskbar widget.

* **Bluetooth Subsystem & Peripheral Pairing (`bluez` / `vita-bluetooth`):**
  * Kernel Bluetooth driver bringup with userspace `bluez` stack.
  * Interactive CLI and GUI pairing utilities for external Bluetooth keyboards, mice, and wireless gamepads.
  * Audio profile support for Bluetooth headphones and portable speakers.

* **Desktop Wi-Fi Manager (GUI):**
  * Intuitive network manager applet for the IceWM desktop system tray.
  * Graphical network scanning, password prompt dialogs, and active signal strength indicators.
  * Automatic background reconnection daemon across network dropouts and roaming.

---

### [v1.7.0] - Pocket Sysadmin & Handheld Developer Workstation

**Priority:** MEDIUM-HIGH
**Focus Areas:** Network diagnostics, portable coding environment, remote administration, and homelab tools.

* **Pocket Sysadmin & Network Swiss Army Knife (`vita-nettools`):**
  * Curated network auditing and diagnostic suite taking full advantage of the Vita's built-in battery, Wi-Fi, and `CONFIG_TUN` support:
    * `nmap` (network mapping, host discovery, and port auditing)
    * `iperf3` (Wi-Fi speed and throughput benchmarking)
    * `tcpdump` / `tshark` (packet capture and network protocol analysis)
    * `wavemon` (real-time wireless signal and noise monitor)
    * `arp-scan` (instant local subnet device discovery)
    * `wakeonlan` (wake PCs on the LAN directly from the handheld)
  * Handheld TUI launcher (`vita-netmenu`) designed for rapid D-Pad navigation.
  * Mesh VPN integrations: pre-packaged helper scripts for Tailscale (`tailscale up`) and ZeroTier (`zerotier-cli join`).

* **Pocket Developer Workstation (`vita-code` / `vita-dev`):**
  * One-command developer environment setup (`vita-dev setup`):
    * Python 3 + Pip + IPython environment
    * Native C/C++ compilation toolchain: GCC, Make, and build-base
    * Git configured with handheld pager settings and credential caching
    * Neovim and Micro text editors pre-configured with Vita gamepad navigation shortcuts
  * Local HTTP API and web development testing sandbox.

* **Remote Desktop & Pocket Terminal Client (VNC / RDP / SSH Kiosk):**
  * **Handheld Remote Monitor:** Connect to remote desktops, home PCs, or headless Raspberry Pis via `remmina` or lightweight VNC/RDP client.
  * **Desktop Streaming Server (`x11vnc`):** Stream the Vita's IceWM desktop to a laptop or tablet monitor for easy configuration on a full-size display.

---

### [v1.8.0] - Direct Framebuffer Gaming & Fantasy Consoles

**Priority:** MEDIUM
**Focus Areas:** Classic indie gaming, fantasy consoles, point-and-click adventure games, and lightweight emulation without X11 overhead.

* **Direct Framebuffer Fantasy Consoles (Pico-8 & TIC-80):**
  * Fantasy console engines rendering directly to `/dev/fb0` or SDL fbdev for zero-latency indie gaming.
  * **TIC-80:** Native ARMv7 build supporting thousands of open-source cartridges, demoscene productions, and chiptunes.
  * **Pico-8 Support (via Fake-08):** High-speed execution of Pico-8 cartridges with physical Vita gamepad controls.

* **Lightweight Standalone Framebuffer Emulators:**
  * Standalone SDL2 framebuffer emulators running directly from the Linux console without X11 overhead:
    * Game Boy / Game Boy Color (`gnuboy`)
    * Game Boy Advance (`mgba-sdl`)
    * NES (`fceux-sdl`)
  * Full physical button and D-Pad bindings pre-mapped for each emulator.

* **Point-and-Click Adventure Launcher (ScummVM Framebuffer):**
  * ScummVM direct framebuffer runner supporting classic point-and-click adventures (Monkey Island, Day of the Tentacle, Beneath a Steel Sky).
  * Seamless control using Vita touchscreen tapping and analog stick mouse emulation.

* **x86 User-Mode Emulation via QEMU:**
  * Configured QEMU user-mode emulation (`qemu-i386`) inside Alpine Linux.
  * Capability to execute legacy x86 Linux binaries, classic DOSBox games, and retro CLI utilities on ARMv7.

---

### [v1.9.0 and Beyond] - Advanced Hardware Research & Kernel Optimization

**Priority:** LONG-TERM RESEARCH
**Focus Areas:** Low-level power states, DSP media offloading, and 3D graphics reverse engineering.

* **Suspend-to-RAM Deep Sleep Kernel Power States (S3):**
  * Research Syscon power management registers and ARM Cortex-A9 low-power standby modes.
  * Target multi-day standby battery life matching official VitaOS sleep behavior.

* **Hardware Video & Audio Decoding Research:**
  * Explore hardware video decoding via Sony custom DSP / media co-processor interfaces.
  * Low-CPU media playback for video files and streaming audio.

* **SGX543 3D Graphics Hardware Acceleration:**
  * Ongoing reverse-engineering of the PowerVR SGX543MP4+ GPU register interface and memory mapping.
  * Collaboration with open-source graphics communities exploring Lima/Panfrost-style reverse engineering.

* **Community Software Repository:**
  * Dedicated LinuxOnVita binary package repository (`apk`) for one-click installation of community-contributed handheld apps, emulators, and games.

---

## Community & Feedback

LinuxOnVita is an open-source, community-driven project. We invite testing reports, feature discussions, and contributions:

* **Discord Community:** Join discussions, share ideas, and get support on [Discord](https://discord.gg/BrzdmHu8Am).
* **Issue Tracker:** Submit feature requests and bug reports on the [GitHub Issue Tracker](https://github.com/devwithzachary/LinuxOnVita/issues).
* **Contributions:** Pull requests are always welcome. Please review our documentation and contribution guidelines before submitting.
