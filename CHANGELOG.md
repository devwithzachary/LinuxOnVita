# Changelog

All notable changes to LinuxOnVita are documented here.
This project follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) conventions.

---

## [1.1.0] - Unreleased

### Added
- **All-in-One Application (`app/LinuxOnVita`):** Rebuilt the bootstrapper into a native, tracked project under `app/` (Title ID `LNXONVITA`, named `LinuxOnVita.vpk`).
- **Dynamic Storage Partition Detection & Sizing:** Integrated `sceIoDevctl` to query and display all active storage partitions (`xmc0:`, `ux0:`, `uma0:`, `imc0:`) with total capacity and free space in real-time.
- **Custom Memory Card Mount Selection:** Users can select their official Sony Memory Card partition (`xmc0:`, `ux0:`, `uma0:`, `imc0:`) using D-Pad Up/Down or L/R shoulder buttons. This resolves issues on consoles where SD2Vita is `ux0:` and the Sony card is mapped to `xmc0:` or `uma0:`.
- **Persistent Mount Preference:** Automatically saves and restores the user's selected mount across reboots in `ur0:data/LinuxOnVita/mount.cfg` (with `ux0:` fallback).
- **Multi-Path Baremetal Loader (`xmc0:` & `ur0:`):** Updated `baremetal-loader.skprx` and `0001-loader-features.patch` to search `xmc0:linux/payload.bin` and `ur0:linux/payload.bin` so the kernel plugin reliably locates the payload on secondary mounts.
- **Bundled Linux Payload & 1-Click Installer:** The single `LinuxOnVita.vpk` bundles all built Linux files (`zImage`, `vita.dtb`, `payload.bin`, `baremetal-loader.skprx`, and configuration templates) directly inside `app0:data/`. Users can install or update all files directly to their chosen memory card partition (`<mount>/linux/`).
- **Boot File Copy Helper:** Added a 1-click copy action (TRIANGLE) in the application UI that copies `zImage` and Device Tree files to the selected memory card partition.
- **Reinstall & Update Support:** Added a reinstall/update shortcut (SQUARE) to refresh or repair memory card files directly from the bundled payload at any time.

### Documentation & Hardware Requirements
- **Official Sony Memory Card Requirement:** Clarified that an official physical Sony memory card is strictly required on all hardware models (both 1000 and 2000 consoles) to boot Linux. Corrected misleading documentation that previously suggested PS Vita 2000 Slim could boot using internal 1GB storage without an official memory card.
- **SD2Vita Setup Guide:** Expanded troubleshooting and setup guidance explaining that SD2Vita users must target their official Sony memory card (typically `xmc0:` or `uma0:`) because the baremetal payload only interfaces with MSIF hardware and cannot read from SD2Vita.

### Changed
- **License Unification (GPL-3.0):** Unified the project under the GNU General Public License v3.0 (GPL-3.0). This aligns the repository license with upstream component requirements, specifically the native `app/` bootstrapper (derived from GPL-3.0 `vita_plugin_linux_loader`) and `fbdoom` (GPL-2.0+).

### Removed
- **Static Battery Telemetry (`vita-battery`):** Removed the static bootloader handoff and `vita-battery` command. Because the hardware fuel gauge (TI bq27520) is isolated on the Syscon companion microcontroller's private I2C bus and live streaming is not yet reverse-engineered, the static snapshot did not reflect runtime battery state or charging changes and caused confusion.

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
