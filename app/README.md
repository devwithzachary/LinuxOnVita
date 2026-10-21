# LinuxOnVita - PlayStation Vita Bootstrapper & Installer

LinuxOnVita is an all-in-one bootstrapper and payload installer application for launching the Linux kernel on the PlayStation Vita.

## Features

- **Touchscreen Graphical Interface:** Modern handheld GUI rendered on `/dev/fb0` (CDRAM) with double-buffered page flipping and display VSync synchronization for tear-free 60 FPS presentation.
- **Full Touchscreen & Gamepad Dual Controls:** Tap partition cards and action buttons directly with your fingers, or navigate using D-Pad and physical gamepad shortcuts.
- **Streamlined Storage Partition Browser:** Displays active, mounted storage partitions (`ux0:`, `ur0:`) by default in spacious, easy-to-read cards, with an expandable `[+] SHOW ALL STORAGE DRIVES` toggle card for system partitions (`uma0:`, `imc0:`, `xmc0:`).
- **Smart Recommendation Badges:** Clear visual indicators highlighting `ux0:` with `[RECOMMENDED (SD2VITA)]` (primary choice) and `ur0:` with `[RECOMMENDED (INTERNAL)]` (secondary choice).
- **In-Memory Kernel Pre-Staging:** Pre-stages the Linux kernel (`zImage`) and Device Tree into physical DRAM before jumping to the loader, allowing Linux to boot directly from SD2Vita (`ux0:`) or internal storage (`ur0:`) without an official Sony memory card!
- **Modal Progress Dialogs:** Real-time visual percentage progress bars for file installation and partition mounting with tap-to-dismiss feedback.
- **Graphical Boot Debug Log Viewer:** Full-screen presentation with touch scrolling controls, color-coded status syntax highlighting, and persistent saving to `ux0:data/LinuxOnVita/boot_debug.log`.
- **All-in-One Packaging:** Bundles the complete Linux kernel (`zImage`), Device Tree blobs (`*.dtb`), and baremetal loaders directly inside the VPK.
- **Persistent Preferences:** Automatically saves and restores your chosen storage mount across reboots in `ur0:data/LinuxOnVita/mount.cfg`.
- **Dual Firmware Support:** Automatically tries the standard baremetal loader and falls back to firmware 3.60 offsets if needed.
- **Safe Wi-Fi Preservation:** Does not overwrite existing `wpa_supplicant.conf` files if wireless credentials are already configured.

## Controls

### Touchscreen Controls
- **Tap Partition Card:** Select target storage drive (`ux0:`, `ur0:`, etc.)
- **Tap [BOOT LINUX]:** Boot Linux (or install files on first run)
- **Tap [DEBUG BOOT]:** Launch Linux in Debug Mode with diagnostic pre-flight checks
- **Tap [REINSTALL]:** Reinstall or update Linux boot files to the selected storage drive
- **Tap [BOOT LOG]:** Open the full-screen graphical boot debug log viewer
- **Tap [EXIT]:** Exit cleanly back to VitaOS LiveArea
- **Tap [UP] / [DN] in Log Viewer:** Scroll log contents up and down

### Physical Gamepad Shortcuts
- **CROSS (✕):** Boot Linux (Normal) / Install Linux files to target mount (on first run)
- **SQUARE (□):** Boot Linux in Debug Mode (runs diagnostic logging)
- **TRIANGLE (△):** Reinstall or update Linux files to the selected storage mount
- **SELECT:** View persistent boot debug log on screen
- **D-PAD UP / DOWN / LEFT / RIGHT:** Navigate and select target storage mount
- **START:** Exit to LiveArea

## Hardware Storage Note

- **No Sony Memory Card Required:** With in-memory kernel pre-staging, Linux boots directly from SD2Vita (`ux0:`) or internal storage (`ur0:`).
- **Sony Memory Card Runtime Note:** If an official Sony memory card is inserted, Linux cannot detect or access it at runtime because Linux does not have a proprietary MSIF driver. All runtime files and packages reside on SD2Vita (`/mnt/ux0/`) or internal storage (`/mnt/ur0/`).

## Credits & Copyright

LinuxOnVita is licensed under the GNU General Public License v3.0 (GPL-3.0).

- **xerpi:** Original PlayStation Vita kernel plugin loader and Linux port.
- **CreepNT:** Linux theming, file checks, and user interface improvements.
- **DvaMishkiLapa:** Standalone bootstrapper maintenance and packaging.
- **Zachary Powell (DevWithZachary):** All-in-one VPK packaging, storage partition selector, on-device installer, touchscreen GUI, and branding.
- **Team Molecule, TheFloW, motoharu, and HENkaku community:** Vita homebrew toolchain, kernel exploits, and reverse engineering.
