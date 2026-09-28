# LinuxOnVita Roadmap

This roadmap outlines upcoming milestones, target releases, and planned features for the LinuxOnVita project. Our goal is to transform the PlayStation Vita into a versatile, reliable, and responsive handheld Linux computer.

> [!NOTE]
> Roadmap milestones and timelines are subject to community feedback, hardware reverse-engineering findings, and ongoing development priorities.

---

## Release Milestones

### [v1.4.0] - Connectivity, Bluetooth & Performance Controls

**Focus Areas:** Wi-Fi enhancements, Bluetooth peripheral support, and CPU frequency management.

* **Wi-Fi Improvements (CLI & GUI):**
  * **Graphical Wi-Fi Tool:** Introduce an intuitive GUI network manager for the IceWM desktop environment with live network scanning, password prompts, and status tray integration.
  * **CLI Enhancements:** Expand `vita-wifi` with auto-reconnection daemons, saved network management (edit, re-prioritize, or forget networks), and signal quality indicators.
  * **Connection Reliability:** Enhanced handling of roaming and sleep-wake network state restoration.

* **Bluetooth Subsystem & Peripheral Pairing:**
  * Bring up Bluetooth kernel drivers and the userland Bluetooth stack (`bluez`).
  * Enable discovery and pairing for external Bluetooth keyboards, mice, and gamepads.
  * Desktop and CLI Bluetooth pairing utilities for quick peripheral connectivity on the go.

* **CPU Frequency & Power Management:**
  * Expose kernel CPU frequency scaling governors (`cpufreq`: performance, powersave, ondemand).
  * Develop handheld CPU clock control tools (`vita-cpufreq`) to balance battery life and compute performance.
  * Battery telemetry reporting (state of charge, current draw, estimated runtime) across CLI and GUI status bars.

---

### [v1.5.0] - Handheld Control Center & Web Portal

**Focus Areas:** Handheld system dashboard and browser-based remote device management.

* **Web-Based Management Portal:**
  * Lightweight on-device web management service accessible from any phone, tablet, or PC on the local network (`http://vita.local:8080`).
  * **Wireless File Transfer:** Drag-and-drop web file manager to easily upload and download files, ROMs, scripts, and documents between your PC and `/mnt/ux0/`.
  * **Remote System Dashboard:** Live monitoring of CPU, RAM, storage, network statistics, and running processes directly from a browser.

* **Handheld CLI Control Center (`vita-control`):**
  * Unified, interactive console menu for quick handheld adjustments without typing long commands.
  * Quick-access toggles for Wi-Fi, Bluetooth, display brightness, audio volume, CPU governors, and swap settings.
  * Integrated launcher for built-in diagnostic tools, desktop sessions, and games.

* **Curated Handheld Software Repository:**
  * Tested list of recommended Alpine Linux packages optimized for the Vita 960x544 screen and memory profile.
  * Quick-installer helper scripts for productivity tools, text editors, lightweight games, and developer utilities.

---

### [v1.6.0] - UX Polish, Community Feedback & Helper Tooling

**Focus Areas:** User feedback integration, ergonomic refinements, and system optimization.

* **Community-Driven Improvements:**
  * Address usability feedback, bug reports, and quality-of-life requests from GitHub issues and the Discord community.
  * Fine-tune analog stick acceleration curves and deadzone profiles for broader controller preferences.

* **Handheld UI & Theme Optimization:**
  * Enhanced themes and icon packs tailored specifically for the 960x544 5-inch OLED and LCD screens.
  * Improved on-screen keyboard layouts, key sizes, and international keyboard mapping options.
  * High-contrast and low-light night mode profiles.

* **Extra Helper Scripts & System Utilities:**
  * Expanded diagnostic and maintenance scripts for storage filesystem integrity checks (`fsck`), log rotation, and container backup/restore.
  * Automated container snapshot and export tools to back up Alpine configurations.

---

### [v1.7.0] - Core Bootloader Architecture & Storage Independence

**Focus Areas:** Low-level baremetal loader improvements and expanding storage boot options.

* **Sony Memory Card Independence:**
  * Explore booting Linux directly without requiring a proprietary Sony PlayStation Vita memory card.
  * Enable pure SD2Vita (Game Card slot) boot configurations for consoles with unpopulated or missing official memory cards.
  * Explore boot paths leveraging internal storage (`ur0:`) for slim (PCH-2000) and PlayStation TV (VTE-1000) models.

* **Baremetal Loader Optimization:**
  * Streamline the payload loader handover sequence to further reduce boot times.
  * Support early boot status displays on framebuffer before kernel serial handoff.
  * Investigate direct kexec and custom kernel parameter passing from the initial loader VPK.

---

### [v1.8.0 and Beyond] - Advanced Capabilities & Architecture Emulation

**Focus Areas:** Broadening handheld capabilities, virtualization, and novel computing workloads.

* **x86 Emulation via QEMU:**
  * Integrate and configure QEMU user-mode emulation (`qemu-arm` / `qemu-i386`) within Alpine Linux.
  * Enable running legacy x86 Linux binaries, CLI utilities, and classic x86 software on the ARMv7 Cortex-A9 processor.
  * Explore running DOSBox and lightweight retro PC applications.

* **Hardware Video & Acceleration Research:**
  * Investigate hardware video decode capabilities (DSP / hardware codecs) for media playback.
  * Continue reverse-engineering SGX543 graphics hardware interfaces.

* **Expanded Handheld Ecosystem:**
  * Native terminal-based development environment (Git, Vim, Nano, Micro, Python, GCC, Make) pre-configured for handheld hacking on the go.
  * Community script repository for one-click installation of specialized handheld Linux apps.

---

## Community & Feedback

LinuxOnVita is an open-source, community-driven project. We welcome suggestions, testing reports, and contributions:

* **Discord Community:** Join discussions, share ideas, and get support on [Discord](https://discord.gg/BrzdmHu8Am).
* **Issue Tracker:** Submit feature requests and bug reports on the [GitHub Issue Tracker](https://github.com/devwithzachary/LinuxOnVita/issues).
* **Contributions:** Pull requests are always welcome. Please review our documentation and contribution guidelines before submitting.
