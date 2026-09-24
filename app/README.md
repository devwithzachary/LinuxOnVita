# LinuxOnVita - PlayStation Vita Bootstrapper & Installer

LinuxOnVita is an all-in-one bootstrapper and payload installer application for launching the Linux kernel on the PlayStation Vita.

## Features

- **All-in-One Packaging:** Bundles the complete Linux kernel (`zImage`), Device Tree blobs (`*.dtb`), and baremetal loaders directly inside the VPK.
- **Dynamic Storage Mount Selection:** Detects all active storage partitions (`xmc0:`, `ux0:`, `uma0:`, `imc0:`) and displays their total capacity and free space in real-time.
- **Sony Memory Card Targeting:** Allows users with SD2Vita adapters (where SD is `ux0:` and the Sony card is `xmc0:` or `uma0:`) to select their exact memory card mount point.
- **Persistent Preferences:** Automatically saves and restores your chosen memory card mount across reboots in `ur0:data/LinuxOnVita/mount.cfg`.
- **On-Device Installation:** Checks file presence on your chosen memory card partition (`<mount>/linux/`) and installs all bundled files with a single button press.
- **Boot File Copy Helper:** Enables copying kernel and Device Tree boot files from `ux0:` to your target memory card directly from the UI.
- **Dual Firmware Support:** Automatically tries the standard baremetal loader and falls back to firmware 3.60 offsets if needed.
- **Safe Wi-Fi Preservation:** Does not overwrite existing `wpa_supplicant.conf` files if wireless credentials are already configured.

## Controls

- **CROSS (X):** Boot Linux (when files are verified) / Install Linux files to target mount (on first run)
- **SQUARE ([ ]):** Reinstall or update Linux files to the selected memory card mount
- **TRIANGLE (/\\):** Copy boot files from `ux0:` to target memory card mount
- **D-PAD UP / DOWN:** Navigate and select target memory card mount (`xmc0:`, `ux0:`, `uma0:`, `imc0:`)
- **L / R Shoulders:** Previous / Next target memory card mount
- **START:** Exit to LiveArea

## Hardware Storage Note

The baremetal loader (MSIF) communicates directly with the proprietary Sony Memory Card controller. It cannot boot Linux from SD2Vita adapters (game card slot) or internal eMMC storage (`imc0:`).
- For standard setups: the Sony Memory Card is mounted as `ux0:`.
- For SD2Vita setups: the Sony Memory Card is typically mounted as `xmc0:` (VitaShell/YAMT default) or `uma0:` (StorageMgr).

## Credits & Copyright

LinuxOnVita is licensed under the GNU General Public License v3.0 (GPL-3.0).

- **xerpi:** Original PlayStation Vita kernel plugin loader and Linux port.
- **CreepNT:** Linux theming, file checks, and user interface improvements.
- **DvaMishkiLapa:** Standalone bootstrapper maintenance and packaging.
- **Zachary Powell (DevWithZachary):** All-in-one VPK packaging, storage partition selector, on-device installer, and branding.
- **Team Molecule, TheFloW, motoharu, and HENkaku community:** Vita homebrew toolchain, kernel exploits, and reverse engineering.
