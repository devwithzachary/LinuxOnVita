# LinuxOnVita - PlayStation Vita Bootstrapper & Installer

LinuxOnVita is an all-in-one bootstrapper and payload installer application for launching the Linux kernel on the PlayStation Vita.

## Features

- **All-in-One Packaging:** Bundles the complete Linux kernel (`zImage`), Device Tree blobs (`*.dtb`), and baremetal loaders directly inside the VPK.
- **Dynamic Storage Mount Selection:** Detects all active storage partitions (`ux0:`, `ur0:`, `uma0:`, `imc0:`, `xmc0:`) and displays their total capacity and free space in real-time.
- **In-Memory Kernel Pre-Staging:** Pre-stages the Linux kernel (`zImage`) and Device Tree into physical DRAM before jumping to the loader, allowing Linux to boot directly from SD2Vita (`ux0:`) or internal storage (`ur0:`) without an official Sony memory card!
- **Persistent Preferences:** Automatically saves and restores your chosen storage mount across reboots in `ur0:data/LinuxOnVita/mount.cfg`.
- **On-Device Installation:** Checks file presence on your chosen storage partition (`<mount>/linux/`) and installs all bundled files with a single button press.
- **Dual Firmware Support:** Automatically tries the standard baremetal loader and falls back to firmware 3.60 offsets if needed.
- **Safe Wi-Fi Preservation:** Does not overwrite existing `wpa_supplicant.conf` files if wireless credentials are already configured.

## Controls

- **CROSS (X):** Boot Linux (Normal) / Install Linux files to target mount (on first run)
- **SQUARE ([ ]):** Boot Linux in Debug Mode (runs diagnostic logging)
- **TRIANGLE (/\\):** Reinstall or update Linux files to the selected storage mount
- **SELECT:** View persistent boot debug log on screen
- **D-PAD UP / DOWN:** Navigate and select target storage mount (`ux0:`, `ur0:`, `uma0:`, `imc0:`, `xmc0:`)
- **L / R Shoulders:** Previous / Next target storage mount
- **START:** Exit to LiveArea

## Hardware Storage Note

- **No Sony Memory Card Required:** With in-memory kernel pre-staging, Linux boots directly from SD2Vita (`ux0:`) or internal storage (`ur0:`).
- **Sony Memory Card Runtime Note:** If an official Sony memory card is inserted, Linux cannot detect or access it at runtime because Linux does not have a proprietary MSIF driver. All runtime files and packages reside on SD2Vita (`/mnt/ux0/`) or internal storage (`/mnt/ur0/`).

## Credits & Copyright

LinuxOnVita is licensed under the GNU General Public License v3.0 (GPL-3.0).

- **xerpi:** Original PlayStation Vita kernel plugin loader and Linux port.
- **CreepNT:** Linux theming, file checks, and user interface improvements.
- **DvaMishkiLapa:** Standalone bootstrapper maintenance and packaging.
- **Zachary Powell (DevWithZachary):** All-in-one VPK packaging, storage partition selector, on-device installer, and branding.
- **Team Molecule, TheFloW, motoharu, and HENkaku community:** Vita homebrew toolchain, kernel exploits, and reverse engineering.
