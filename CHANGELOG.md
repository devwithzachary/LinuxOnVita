# Changelog

All notable changes to LinuxOnVita are documented here.
This project follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) conventions.

---

## [v1.2.0] - Unreleased

### Added
- **SD2Vita (Game Card Slot) Power & High-Speed Support:** Enabled hardware power and high-speed bus negotiation for SD2Vita adapters in the Game Card slot (`SDIF1`):
  - Added Syscon command `0x888` power-on invocation during boot so the physical Game Card slot receives 3.3V VCC.
  - Configured pervasive pad voltage register (`0xE3100124`) for 3.3V signalling on SDIF1.
  - Added `broken-cd` and `cap-sd-highspeed` to the `sdif1` Device Tree node in `vita.dtsi`.
  - Added automatic MMC rescan trigger upon Syscon power stabilization so the microSD card is immediately recognized as `mmcblk1`.
- **Automatic SD2Vita Auto-Mounting & Symlinks:** Updated `S05vita` and `S00mount` to automatically detect `/dev/mmcblk1p1` or `/dev/mmcblk1`, create the `/dev/vita/ux0` device symlink, and mount the card to `/mnt/ux0`.
- **Framebuffer DOOM Controller Exit Confirmation & Quick Exit:**
  - Resolved an issue in `fbdoom` where selecting "Quit Game" from the in-game menu prompted for 'Y' without accepting any PS Vita gamepad buttons.
  - Mapped **Cross (✕)** (`KEY_ENTER`) and **Triangle (△)** (`KEY_TAB`) to confirm menu prompts (`key_menu_confirm`), allowing natural exit confirmation on PS Vita hardware.
  - Mapped **Circle (○)** (`KEY_BACKSPACE`) to abort/cancel menu prompts (`key_menu_abort`).
  - Added universal instant exit controller shortcut: pressing **`START + SELECT`** simultaneously now cleanly exits DOOM from any gameplay screen back to the Linux shell.
  - Ensured `I_Quit()` and `I_Error()` restore terminal modes via `kbd_shutdown()` and cleanly exit the process.
  - Updated in-game exit and confirmation prompts to explicitly reference Cross and Circle buttons.
- **Display Brightness & Model-Aware Backlight Control:**
  - Resolved a critical bug in `vita-baremetal-linux-loader` where `sysroot_model_is_vita()` was evaluated before `sysroot_model_is_vita2k()`, causing all consoles (including PS Vita 2000 Slim) to load `vita1000.dtb` which had I2C bus 1 disabled.
  - Corrected DTB loading order so PS TV loads `pstv.dtb`, PS Vita 2000 loads `vita2000.dtb`, and PS Vita 1000 loads `vita1000.dtb`.
  - Enabled I2C bus 1 in `vita1000.dts` and fallback `vita.dtb` so `/dev/i2c-1` is universally available.
  - Overhauled `vita-brightness` to perform automatic hardware model detection:
    - On **PS Vita 1000 (OLED)**: Displays clear hardware notices explaining that the emissive OLED display is locked to 100% full brightness (Level 15 Gamma) at boot and does not have an adjustable LCD backlight, preventing misleading success messages.
    - On **PS Vita 2000 (Slim / LCD)**: Dynamically controls the hardware PWM backlight via `/dev/i2c-1` at address `0x64`, scaling across the hardware duty cycle range (`0x1F` to `0xFF`), and verifies command success.
- **Alpine Linux Chroot Isolation & Command Execution:**
  - Clarified environment isolation in documentation and interactive banners, explaining that packages installed via `apk` are confined to the chroot container (`/mnt/alpine`) and are not accessible from the base BusyBox shell.
  - Added visual environment prompt (`alpine@vita:~#` in cyan) inside the chroot to clearly distinguish between host and chroot sessions.
  - Added support for running one-shot commands inside Alpine directly from the base shell via `alpine-chroot run <command>`, `alpine-chroot -- <command>`, or `alpine-chroot -c "<command>"`.
- **Compressed ZRAM & Memory Safety Management (`vita-swap`):**
  - Enabled kernel `CONFIG_ZRAM=y`, `CONFIG_ZSMALLOC=y`, `CONFIG_SWAP=y`, and `CONFIG_ZRAM_BACKEND_LZ4=y` with default LZ4 compression in `configs/kernel/vita_defconfig` and build orchestration.
  - Automatically initializes a high-priority (100) 256MB compressed ZRAM swap pool on boot in `S00mount`, expanding usable RAM to ~768MB - 1GB with near-zero latency to eliminate Out-Of-Memory (OOM) crashes during heavy package installations or compilation.
  - Added support for physical swapfiles on SD card or internal storage (`/mnt/ux0/swapfile`), automatically mounted at boot-time with secondary priority (10).
  - Created `vita-swap` CLI utility for inspecting memory telemetry, creating formatted swapfiles with contiguous blocks, resizing ZRAM on the fly, and persisting auto-mount configurations in `/etc/vita-swap.conf` and `/etc/vita-zram.conf`.
- **Comprehensive User Guide (`USER_GUIDE.md`):**
  - Authored an in-depth end-user manual covering console navigation, touchscreen keyboard usage, physical gamepad shortcuts, built-in system tools (`vita-wifi`, `vita-brightness`, `vita-swap`, `vita-doom`), remote SSH workflows, and real-world software installations in Alpine Linux (from micro web servers and network utilities to native C compilation).
  - Streamlined `README.md` to focus on distribution overview, hardware support matrix, quick installation, and source build instructions.

---

## [v1.1.0] - 2026-09-24

### Added
- **All-in-One Application (`app/LinuxOnVita`):** Rebuilt the bootstrapper into a native, tracked project under `app/` (Title ID `LNXONVITA`, named `LinuxOnVita.vpk`).
- **Dynamic Storage Partition Detection & Sizing:** Integrated `sceIoDevctl` to query and display all active storage partitions (`xmc0:`, `ux0:`, `uma0:`, `imc0:`) with total capacity and free space in real-time.
- **Custom Memory Card Mount Selection:** Users can select their official Sony Memory Card partition (`xmc0:`, `ux0:`, `uma0:`, `imc0:`) using D-Pad Up/Down or L/R shoulder buttons. This resolves issues on consoles where SD2Vita is `ux0:` and the Sony card is mapped to `xmc0:` or `uma0:`.
- **Persistent Mount Preference:** Automatically saves and restores the user's selected mount across reboots in `ur0:data/LinuxOnVita/mount.cfg` (with `ux0:` fallback).
- **Multi-Path Baremetal Loader (`xmc0:` & `ur0:`):** Updated `baremetal-loader.skprx` and `0001-loader-features.patch` to search `xmc0:linux/payload.bin` and `ur0:linux/payload.bin` so the kernel plugin reliably locates the payload on secondary mounts.
- **On-Device Partition Mounting (`_vshIoMount`):** Added native VSH bridge mounting support inside `LinuxOnVita`. Automatically mounts `xmc0:` on launch if unmounted, and allows users to manually mount unmounted storage partitions (`xmc0:`, `uma0:`, `imc0:`) directly with [X] without needing to launch VitaShell first.
- **UI Screen Buffering & Formatting:** Refined all text strings to fit within 56 characters per line to eliminate line wraps on the Vita's 60-column debug display, and introduced clean screen wipes during boot and installation routines.
- **Bundled Linux Payload & 1-Click Installer:** The single `LinuxOnVita.vpk` bundles all built Linux files (`zImage`, `vita.dtb`, `payload.bin`, `baremetal-loader.skprx`, and configuration templates) directly inside `app0:data/`. Users can install or update all files directly to their chosen memory card partition (`<mount>/linux/`).
- **Boot File Copy Helper:** Added a 1-click copy action (TRIANGLE) in the application UI that copies `zImage` and Device Tree files to the selected memory card partition.
- **Reinstall & Update Support:** Added a reinstall/update shortcut (SQUARE) to refresh or repair memory card files directly from the bundled payload at any time.
- **Interactive On-Device Wi-Fi Manager (`vita-wifi`):** Introduced a lightweight command-line and interactive TUI Wi-Fi management utility running on `/usr/bin/vita-wifi`:
  - `vita-wifi scan`: Scans wireless channels and renders an ASCII table of available SSIDs, signal levels (`[====]`), and security types (WPA2, Open, etc.), with interactive numbered selection.
  - `vita-wifi connect <SSID>`: Prompts for passphrase, creates/updates network blocks, syncs persistently across boot mounts (`/mnt/ux0/linux/`, `/mnt/xmc0/linux/`, `/mnt/uma0/linux/`), and automatically restarts `wpa_supplicant` and `udhcpc`.
  - `vita-wifi status`: Displays live network telemetry including current SSID, BSSID, RSSI dBm signal level, assigned IP address, and MAC address.
  - `vita-wifi off / on`: Toggles wireless power dynamically via `rfkill` / `ip link` to preserve battery.
  - Interactive menu interface launched by running `vita-wifi` without arguments.
- **Dynamic Wi-Fi Service Daemon (`S45wifi`):** Enhanced init script to auto-create control interface sockets (`/var/run/wpa_supplicant`), load multi-mount configurations (`ux0:`, `xmc0:`, `uma0:`), and start an idle fallback supplicant if no network profile exists, allowing immediate zero-configuration onboarding with `vita-wifi`.
- **Buildroot Source Compilation of `wpa_cli` and `wpa_passphrase`:** Configured Buildroot (`BR2_PACKAGE_WPA_SUPPLICANT_CLI=y`, `BR2_PACKAGE_WPA_SUPPLICANT_PASSPHRASE=y`, and `BR2_PACKAGE_WPA_SUPPLICANT_CTRL_IFACE=y`) and added `patches/vita-linux-port/0002-enable-wpa-cli.patch` to compile both utilities natively from official upstream source code during the build process, eliminating pre-built binary blobs from the repository.

### Changed
- **Dual Release Packaging:** Updated release packaging to distribute both the standalone `LinuxOnVita.vpk` (for quick 1-click on-device installs) and `LinuxOnVita-release-*.zip` (full archive with manual payloads and installation guides) as separate release assets.
- **Streamlined Wi-Fi Setup Flow:** Updated the main documentation and quick start guides to highlight `vita-wifi` as the primary on-device connection workflow, eliminating the manual `wpa_supplicant.conf` or `wifi.conf` editing step while keeping it documented as an optional headless alternative.
- **Official Sony Memory Card Requirement:** Clarified that an official physical Sony memory card is strictly required on all hardware models (both 1000 and 2000 consoles) to boot Linux. Corrected misleading documentation that previously suggested PS Vita 2000 Slim could boot using internal 1GB storage without an official memory card.
- **SD2Vita Setup Guide:** Expanded troubleshooting and setup guidance explaining that SD2Vita users must target their official Sony memory card (typically `xmc0:` or `uma0:`) because the baremetal payload only interfaces with MSIF hardware and cannot read from SD2Vita.
- **License Unification (GPL-3.0):** Unified the project under the GNU General Public License v3.0 (GPL-3.0). This aligns the repository license with upstream component requirements, specifically the native `app/` bootstrapper (derived from GPL-3.0 `vita_plugin_linux_loader`) and `fbdoom` (GPL-2.0+).
- **Dynamic Wireless Interface Support (`mlan0` / `wlan0`):** Enhanced all wireless networking scripts, `vita-wifi`, and `network/interfaces` to auto-detect both `mlan0` (native Marvell `mwifiex` hardware driver interface on PS Vita) and standard `wlan0`, ensuring consistent detection and connectivity across any network stack.

### Fixed
- **`wpa_cli` control interface (`CONFIG_CTRL_IFACE`):** Added `BR2_PACKAGE_WPA_SUPPLICANT_CTRL_IFACE=y` to the vita_defconfig. Without this flag the build produced a non-functional `wpa_cli` reporting `CONFIG_CTRL_IFACE not defined - wpa_cli disabled`. With the fix, `wpa_cli` can open its Unix domain socket at `/var/run/wpa_supplicant` and exchange commands with the running `wpa_supplicant` daemon, enabling `vita-wifi scan`, `vita-wifi connect`, and `vita-wifi status` to function correctly.
- **`udhcpc` DHCP client daemon backgrounding:** Fixed conflicting flags (`-b -n -t 5`) in `vita-wifi` where `-n` overrode `-b` and caused the DHCP client to terminate immediately if not answered within 5 seconds, rather than staying active in the background.
- **`vita-wifi` scan table deduplication:** Fixed an AWK array indexing bug where multiple access points or dual-band routers broadcasting the same SSID caused duplicate rows in the ASCII scan table and `/tmp/vita_wifi_scan.list`.
- **`S45wifi` unconfigured template handling:** Updated candidate config scanning in `S45wifi` to verify that found files contain actual network credentials rather than unedited template placeholders (`YourWiFiNetworkName`, `YourNetworkName`), allowing the clean zero-config idle supplicant stack to initialize immediately on unconfigured systems.
- **I/O read error handling in `app/main.c` (`copy_file`):** Added explicit validation of negative return codes from `sceIoRead` to ensure storage read failures are accurately reported as critical errors instead of reporting false `[OK]` status.
- **`build_kernel.sh` fallback branch parity:** Added missing defconfig copying, kernel patch application (`patches/kernel/*.patch`), and EXT4 filesystem configuration to the standalone fallback branch of `build_kernel.sh`.
- **Wi-Fi privacy guarantee in release builds:** Isolated local Wi-Fi configurations during `build_release.sh` (`IS_RELEASE_BUILD=1`) and ensured `build_loaders.sh` always bundles a clean generic template, preventing developer credentials from being packaged into `rootfs.cpio.zst` or `LinuxOnVita.vpk`.

### Removed
- **Static Battery Telemetry (`vita-battery`):** Removed the static bootloader handoff and `vita-battery` command. Because the hardware fuel gauge (TI bq27520) is isolated on the Syscon companion microcontroller's private I2C bus and live streaming is not yet reverse-engineered, the static snapshot did not reflect runtime battery state or charging changes and caused confusion.
- **Dead Buildroot Config (`configs/buildroot.config`):** Removed the 3,840-line unused static Buildroot `.config` file that was superseded by `vita-linux-port/buildroot-vita/configs/vita_defconfig`.
- **Legacy Bootstrapper Directory (`sources/vita_plugin_linux_loader/`):** Removed the untracked clone of the legacy bootstrapper that was replaced by `app/`.
- **Redundant `rootfs.cpio.zst` in release packaging:** Removed separate copying and instruction steps for `rootfs.cpio.zst` from `build_release.sh` since the rootfs is linked directly into `zImage`.

---

## [v1.0.0] - 2026-09-22 - First Public Release

### Added (vs. raw upstream projects)

#### Interactive Input
- **On-screen touch keyboard (`fbkeyboard`):** Full framebuffer virtual keyboard rendered directly on `/dev/fb0` with dual ABC / ?123 layout, Hide/Show toggle, and VT isolation. No UART cable needed.
- **Physical button mapper (`vita-input-mapper`):** D-Pad, Cross (Enter), Circle (Backspace), Square (Space), Triangle (Tab), and L-Trigger (Ctrl+C) mapped via `/dev/uinput` for full terminal navigation.

#### Hardware Fixes
- **Fixed button input mask (`0x4037ffff`):** Corrected the upstream XOR mask `0x4037fcf9` that inverted D-Pad Right, Down, Select, and L-Trigger (causing D-Pad Right to be permanently stuck ON in all applications including DOOM).
- **Analog stick initialization:** Added Syscon command `0x180` during kernel probe and baremetal handover to enable both analog stick axes.

#### Display
- **100% full brightness on boot:** Patched both OLED (Vita 1000) and LCD (Vita 2000) baremetal loaders to initialize at maximum brightness. Upstream defaults to gamma level 0 (OLED) / 50% PWM (LCD).
- **`vita-brightness` CLI:** Runtime brightness control from 0-100% via I²C for LCD models. OLED models hardwired to max.

#### Networking
- **Zero-config Wi-Fi auto-connect:** Drop credentials into `configs/wifi.conf` before building. System auto-joins WPA2 networks and starts OpenSSH on boot.
- **~12 second SSH ready time:** Fixed Linux CRNG entropy stall by enabling the 144 MHz ARM Global Timer clocksource (upstream: ~2.5 minute delay).
- **mDNS hostname:** Accessible as `vita.local` over the local network.

#### Storage & Package Management
- **Auto-mounting:** Automatic mounting of `/mnt/ux0` (SD2Vita / Sony memory card), `/mnt/ur0` (internal eMMC), and `/mnt/uma0` on boot.
- **Alpine Linux chroot (`alpine-chroot`):** Persistent ext4 loopback image on SD card or internal storage with full `apk` package management. Supports configurable size, custom paths, RAM mode, and graceful SIGINT cleanup.

#### Power & Battery
- **Hardware battery telemetry (`vita-battery`):** Percentage, voltage, and charging state captured during bootloader handover and accessible via CLI. Displayed on the SSH login banner.

#### Gaming
- **Framebuffer DOOM (`vita-doom`):** Pure-C `fbdoom` engine with twin-stick and D-Pad controls, right-stick strafe, analog deadzones, and automatic suspension/resumption of background input daemons during gameplay.

#### Build System
- **One-command Docker build:** `./build.sh all` orchestrates all toolchains, kernel patches, Buildroot overlays, and VPK packaging automatically.
- **`./build.sh release`:** Packages a distributable zip for GitHub Releases (no Wi-Fi credentials included).

### Fixed (upstream bugs)
- Button XOR mask causing D-Pad Right to be permanently active (`0x4037fcf9` → `0x4037ffff`)
- CRNG entropy stall delaying boot by ~2.5 minutes (Global Timer clocksource enabled)
- Alpine chroot APK repos now detect tarball version instead of hardcoding v3.20
- Alpine chroot pseudo-filesystems now unmount correctly on SIGINT (Ctrl+C)

### Upstream Projects This Builds Upon
- [vita-linux-port](https://github.com/incognitojam/vita-linux-port) by **incognitojam**: Linux 6.12 port, Wi-Fi power sequencing, eMMC driver, Buildroot integration
- [linux_vita](https://github.com/incognitojam/linux_vita) by **incognitojam**: Modernized Linux 6.12 kernel tree
- [linux_vita](https://github.com/xerpi/linux_vita), [vita-baremetal-loader](https://github.com/xerpi/vita-baremetal-loader), [vita-libbaremetal](https://github.com/xerpi/vita-libbaremetal) by **xerpi**: Original Linux on PS Vita pioneer
- [vita_plugin_linux_loader](https://github.com/DvaMishkiLapa/vita_plugin_linux_loader) by **DvaMishkiLapa**: Bootstrapper VPK

---

*For older history see the [GitHub commit log](https://github.com/devwithzachary/LinuxOnVita/commits/main).*
