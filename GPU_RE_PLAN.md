# PS Vita GPU (PowerVR SGX543MP4+) Reverse Engineering & Driver Plan

> **Working Document:** This plan outlines a safe, phased approach to reverse engineering the PlayStation Vita's display controller and PowerVR SGX543MP4+ GPU for Linux 6.12, with strict hardware-safety rules to protect a single test console.

---

## 1. Hardware Safety Rules (Single-Console Protection)

Because only one physical PS Vita is available for testing, every step must follow these non-negotiable safety boundaries:

1. **Zero Non-Volatile Writes:**
   - Never write to eMMC boot partitions (`mmcblk0boot*`), Syscon NVS, IdStorage, or VitaOS system partitions.
   - Keep `/mnt/ux0` and `/mnt/ur0` mounted read-only (`ro`) during all kernel/driver experiments.
   - All Linux GPU experiments run strictly in volatile DRAM (`zImage` or manually loaded `.ko` kernel modules). A hard power-off (holding Power for 15-30 seconds) always returns the console cleanly to stock VitaOS.
2. **No Blind Register Writes to Power, Clocks, or PMIC:**
   - Never poke unknown Syscon commands, unknown I2C PMIC registers, or unverified PLL multipliers/voltage regulators.
   - Overvolting or setting out-of-spec PLL clocks (> 222 MHz GPU or unverified core voltages) can stress silicon or trigger PMIC brownouts. Only apply clock/power-gating sequences that have been **observed directly from stock VitaOS** (`ScePervasive`, `ScePower`, `SceGxm`).
3. **Display Panel Protection (OLED & LCD):**
   - Never write unverified DSI timings or OLED gamma tables.
   - Continue letting `baremetal-loader.skprx` / VitaOS initialize the panel (`960x544` framebuffer) and keep `vita-powerd` active to prevent OLED burn-in during long debug sessions.
4. **Manual Module Loading Only (No Boot-Loop Risk):**
   - Never auto-load experimental GPU kernel modules via `/etc/init.d/` or `modprobe.d` until proven stable across multiple runs.
   - Always load modules manually over SSH (`insmod vita_gpu_probe.ko`) so a kernel panic or bus hang only requires a simple power cycle to recover.
5. **Bus Hang Awareness (Unclocked Peripherals):**
   - On ARM SoCs, reading an MMIO register belonging to a powered-off or unclocked IP block typically causes an imprecise external abort or bus lockup. This is **non-destructive** (resolved by reboot), but we will always verify Pervasive clock/power status before reading SGX MMIO ranges.

---

## 2. Current Baseline in This Repository

From inspecting `sources/vita-linux-port/linux_vita/arch/arm/boot/dts/vita.dtsi` and `configs/kernel/vita_defconfig`:

- **Kernel:** Linux 6.12 LTS (`vita-linux-port`).
- **Memory Map (`vita.dtsi`):** System RAM starts at `0x40200000` (size `0x1FE00000`, ~510 MB).
- **Display:** Currently driven via `simplefb` (software framebuffer at `960x544`, 32bpp), initialized by VitaOS / `baremetal-loader.skprx`. No DRM/KMS driver or VSync interrupt handling exists yet.
- **GPU:** No Device Tree node, clock/power-domain driver, or kernel module exists for the PowerVR SGX543MP4+ yet.

---

## 3. Architectural Reality Check: Why SGX543 is Unique

The PS Vita uses an **Imagination PowerVR SGX543MP4+** (4-core Series5XT). Unlike modern GPUs with open Mesa drivers (`etnaviv`, `lima`, `panfrost`), PowerVR SGX5xx has two major layers:

1. **Display Controller (`SceDisplay`) vs. 3D Core (`SGX543`):**
   - On the Vita, display scanout (reading a framebuffer from RAM and sending it to the OLED/LCD/HDMI) is separate from the 3D rendering core.
   - Building a **DRM/KMS Display Driver** first is completely independent of 3D acceleration and immediately unlocks hardware VSync, tear-free page flipping, and plane scaling.
2. **SGX543 3D Core Complexity:**
   - **Kernel Driver (`pvrsrvkm`):** Imagination's reference Linux kernel driver is **GPLv2** and already supports SGX543/544 register layouts (MMU/BIF, TA/3D kicks, interrupts).
   - **Microkernel (USSE Firmware):** SGX5xx relies on a tiny firmware program running on an internal USSE microcontroller inside the GPU to schedule Tile Accelerator (TA) and 3D render tasks.
   - **Userspace Driver (`libGLESv2` / `libEGL`):** Imagination never open-sourced the SGX543 userspace DDK or shader compiler.

### Viable Paths Forward

| Path | Effort | Pros | Cons |
| :--- | :--- | :--- | :--- |
| **A. DRM/KMS Display Driver (`vita-drm`)** | Low-Medium | Immediate VSync, tear-free desktop & emulators, DMA-BUF ready; zero closed blobs needed | 2D scanout only (no 3D acceleration) |
| **B. VitaOS Observation (`taiHEN`) + Clean-Room Kernel/Userspace** | High | 100% open-source, tailored to Vita's exact SGX543MP4+ revisions & Sony's `SceGxm` init sequence | Requires RE of USSE microkernel / shader ISA (partially documented in `Vita3K`) |
| **C. GPL `pvrsrvkm` + Compatible Linux `armhf` SGX543 DDK** | Medium | Full OpenGL ES 2.0 if a matching `armhf` DDK binary + microkernel version aligns | Closed userspace blob, strict kernel-to-DDK version lock, ABI (`armhf`) matching |

**Recommended Strategy:** Execute **Phase 0 -> Phase 3** first (safe observation + clock/power bringup + DRM/KMS display driver). Once the SGX543 core is powered, clocked, and responding in Linux, we can evaluate **Path B vs. Path C** with real hardware telemetry in hand.

---

## 4. Phased Reverse Engineering Roadmap

### Phase 0: Literature & Existing Knowledge Audit (Zero Hardware Risk)
Before running a single line of new code on the Vita, consolidate existing public documentation and open-source code:
- **Vita-Specific Knowledge:**
  - HENkaku Wiki / `vita-headers` / `vitasdk`: Pervasive register offsets, Display controller (`SceDisplay`) registers, `SceGxm` memory/sync structures, and `SceSysmem` physical memory flags.
  - `Vita3K`: Existing documentation of the Vita's SGX543 USSE shader ISA, texture formats, and `SceGxm` command buffers.
- **Linux / PowerVR Knowledge:**
  - Reference GPL `pvrsrvkm` sources (e.g., TI OMAP / `pvr-omap4` / Imagination Series5XT headers) for `EUR_CR_*` register definitions, SGX543MP4+ multi-core (`MP`) register banking, and BIF (Bus Interface) MMU page table format.
- **Deliverable:** A register & memory map reference sheet (`docs/gpu_registers.md` or appendix) before touching hardware.

### Phase 1: Safe Read-Only Observation in VitaOS (`taiHEN` Logger)
Rather than guessing how to wake up the GPU in Linux, observe **Sony's own drivers** initializing it safely in VitaOS:
- Write a minimal, read-only `taiHEN` kernel plugin (loaded manually in VitaOS, never in `ur0:tai/config.txt` during early tests) that snapshots:
  1. **Pervasive Clock, Reset & Power Domain Registers** before and after `SceGxmInitialize()`.
  2. **Display Controller Registers** during frame swaps (`sceDisplaySetFrameBuf`).
  3. **SGX543MMIO Registers** (`EUR_CR_CORE_ID`, `EUR_CR_CORE_REVISION`, `EUR_CR_CLKGATECTL`, `EUR_CR_BIF_*`) while a simple Vita homebrew renders a single frame.
- **Safety:** Purely `readl` / `ksceKernelMemcpyKernelToUser` logging to `ux0:data/gpu_trace.txt`. Zero register writes.

### Phase 2: Linux Clock, Reset & Power-Domain Bringup + Read-Only Probe
Once Phase 1 gives us the exact, verified VitaOS clock/reset/power sequence:
1. Add the SGX543 and Pervasive GPU clock/reset bindings to `vita.dtsi` (without enabling any auto-loaded driver).
2. Create a standalone, manually-loaded Linux probe module (`vita_sgx_probe.ko`):
   - Enable the verified GPU clock gate and deassert GPU reset using the exact VitaOS sequence.
   - `ioremap` the SGX543 MMIO region.
   - Read and print `EUR_CR_CORE_ID` and `EUR_CR_CORE_REVISION` to `dmesg`.
   - Cleanly gate the clock back off on `rmmod`.
3. **Success Milestone:** `dmesg` prints the authentic PowerVR SGX543MP4+ Core ID and Revision cleanly without any bus abort or thermal spike.

### Phase 3: Standalone DRM/KMS Display Driver (`vita-drm`)
Independent of 3D rendering, build a minimal Linux DRM/KMS driver for the Vita's display engine:
- Map the display controller MMIO and VSync interrupt (`IRQ`).
- Support `960x544` primary plane scanout, hardware VSync (`drm_crtc_handle_vblank`), and atomic page flipping (double/triple buffering).
- Support `CMA` / `GEM` framebuffer allocation (`drm_gem_dma_helper`) so X11 (`modesetting`), Wayland, SDL2, and RetroArch/emulators get tear-free, zero-copy VSync scanout.
- **Safety:** Reuse the exact panel/DSI initialization already performed by `baremetal-loader.skprx`; only update framebuffer base address / stride / VSync registers.

### Phase 4: SGX543 Interrupts, BIF MMU & Minimal Render Test
With the GPU clocked (Phase 2) and display scanout working (Phase 3):
1. Wire the SGX543 hardware IRQ in `vita.dtsi` and verify interrupt delivery in Linux.
2. Program the SGX543 **BIF (Bus Interface) MMU** page tables to map a single physical CMA buffer (e.g., a `256x256` RGBA texture/render target in RAM).
3. Test the smallest possible hardware operation:
   - Clear / render a single solid-colored triangle or hardware register self-test using the observed VitaOS command stream / microkernel setup, then verify the output pixels in RAM.

### Phase 5: Userspace 3D Stack Evaluation
Once hardware command submission works in Phase 4, decide between:
- **Clean-room minimal Gallium/Mesa or `libgxm`-style shim** (long-term maintainable, 100% open source, no ABI mismatches).
- **GPL `pvrsrvkm` + compatible `armhf` SGX543 userspace DDK** (faster path to full OpenGL ES 2.0 if a binary-compatible DDK for Linux 6.12 `armhf` is viable).

---

## 5. Phase 0 Audit Findings (Verified MMIO & Repo Baseline)

### 5.1 Pervasive Block (Clocks, Resets & Gates)
Confirmed in `sources/vita-libbaremetal/libbaremetal/src/pervasive.c` and `patches/kernel/0004-vita-cpufreq-driver.patch`:
- `PERVASIVE_MISC_BASE_ADDR`: `0xE3100000`
- `PERVASIVE_RESET_BASE_ADDR`: `0xE3101000`
- `PERVASIVE_GATE_BASE_ADDR`: `0xE3102000`
- `PERVASIVE_BASECLK_BASE_ADDR`: `0xE3103000` (HENkaku Wiki documents GPU `corefreq`/`mpfreq` at `0xE3103010`, and `scePervasiveGpuResetDisableForDriver` for GPU reset control)
- `PERVASIVE2_BASE_ADDR`: `0xE3110000`

### 5.2 Display Engine (IFTU & MIPI DSI)
Confirmed in `sources/vita-libbaremetal/libbaremetal/src/iftu.c`, `dsi.c`, and `display.c`:
- `IFTUREG_BASE_ADDR`: `0xE5020000` (Plane registers at `0xE5020000 + bus * 0x10000 + plane * 0x1000`)
- `IFTUCREG_BASE_ADDR`: `0xE5022000` (Control registers at `0xE5022000 + bus * 0x10000`)
- `DSI_BASE_ADDR`: `0xE5050000` (`DSI_REGS(i)` at `0xE5050000 + i * 0x10000`)
- `FB_ADDR`: `0x20000000` (CDRAM framebuffer initialized at `960x544` 32bpp by `baremetal-loader.skprx`)

### 5.3 PowerVR SGX543MP4+ Core & Interrupts
Confirmed via HENkaku Vita Development Wiki (`SceGpuEs4` & `SceKernelIntrMgr`):
- `SceSGX543Reg`: Physical MMIO range `0xE8400000 - 0xE841FFFF` (128 KB)
- VitaOS kernel driver: `SceGpuEs4` (shares architecture with Imagination GPL `pvrsrvkm`)
- **Hardware Interrupts (`GIC_SPI = code - 32`):**
  - `SceGpuLisr` (SGX543 GPU IRQ): VitaOS code `52` -> `GIC_SPI 20`
  - `SceDisplayVbStartMainLCD` (OLED/LCD VBlank): VitaOS code `213` -> `GIC_SPI 181`
  - `SceDisplayVbStartHDMI` (PSTV HDMI VBlank): VitaOS code `210` -> `GIC_SPI 178`

---

## 6. Phase 1 & Phase 2A Trace Findings (Verified on Physical PS Vita)

Captured via read-only `GpuProbe.vpk` (`gpu_trace_before.txt` / `gpu_trace_after.txt`) in VitaOS and `vita_sgx_probe.ko` (`linux_pervasive_trace.txt`) in Linux:

### 6.1 Pervasive GPU Clock, Gate & Reset State Diff

| Register | Physical Address | VitaOS (Before GXM) | VitaOS (After GXM) | Linux Boot State | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `PERVASIVE_RESET + 0x10` | `0xE3101010` | `0x00000000` | `0x00000000` | **`0x00000001`** | Held in reset (`bit 0 = 1`) after standby boot; `scePervasiveGpuResetDisableForDriver` clears bit 0 |
| `PERVASIVE_GATE + 0x10` | `0xE3102010` | `0x000F000F` | `0x000F000F` | **`0x00000000`** | 4-core GPU clock gate disabled (`0`) after standby boot |
| `PERVASIVE_BASECLK + 0x10` | `0xE3103010` | `0x00000000` | `0x00030003` | **`0x00030003`** | Stock `corefreq` / `mpfreq` divider already retained in Linux! |

### 6.2 PowerVR SGX543MP4+ & GPU RegBus Live Identification (in VitaOS)
- **`GPU_REGBUS` (`0xED800000`):** `+0x0000 = 0x01006437` (SonicsSX wrapper), `+0x0018 = 0x00130001`, `+0x001C = 0x00010214`
- **`+0x0000` (`EUR_CR_CLKGATECTL`):** `0x002AAAAA`
- **`+0x0004` (`EUR_CR_CLKGATECTL2`):** `0x0A8A8AAA`
- **`+0x0020` (`EUR_CR_CORE_ID`):** `0x01174403` (SGX543 MP4+)
- **`+0x0024` (`EUR_CR_CORE_REVISION`):** `0x00020106` (Revision `2.1.6`)
- **`+0x0100`:** `0x0000000F` (4 active MP cores)
- **`+0x0C38` / `+0x0C50` (BIF MMU Directory Bases):** `0x41234000` / `0x41A3A000`

### 6.3 Phase 2B Power Rail (`DD4`/`VDDG`), TrustZone SMC & Standby Findings
- **Pervasive Wake Alone (`GATE=0x000F000F`, `RESET=0x00000000`):** Reading `0xE8400020` in Linux triggers an AXI slave abort (`imprecise external abort 0x1406`) cleanly without crashing SSH/Linux.
- **TrustZone `SMC 0x114` (`smc_bus_freq_op`) Works Natively in Linux 6.12:**
  - Both VitaOS and Linux return `bus_lvl = 0x6` (`222 MHz`) and `gpu_xbar_lvl = 0x3` (`111 MHz`).
- **Elmo PMIC Power Status (`Syscon 0x0883`) & Stock `DD4/VDDG` Voltage IDs (`Syscon 0x088E`):**
  - Linux `0x0883` returns `f9 97 40 00 11 40 14 00` (byte 0 `0xF9` = `1111_1001`, confirming `DD3/VDDC` and `DD4/VDDG` rails were switched off during `kscePowerRequestStandby()`).
  - Live VitaOS `ScePower` hook (`gpu_trace_power.txt`) captured stock `sceSysconCtrlVoltageForDriver(type=4, vid)` values on Vita 1000 (`0x80000042`): `vid = 0x5C` (`111 MHz`), `0x60` (step), `0x68` (`222 MHz`).
  - Sending `0x088E` (`0x5C04`, `cmd_len = 3`) alone returned `res = 0x03` and left `0x0883` at `f9 97 ...`, confirming a separate Syscon rail-enable command (`0x884`/`0x885`/`0x88B`..`0x88D`) switches the `DD4` buck converter on/off alongside `0x88E`.

---

## 7. Immediate Next Steps (Next Session: Capture `DD4/VDDG` Rail Switch)

1. Use `ksceSysconSetDebugHandlers()` (`SceSysconPacket` TX/RX callback) in `GpuProbe` or `baremetal-loader.skprx` (during `kscePowerRequestStandby()`, logged to scratchpad SRAM `0x1F007C00`) to capture the exact 2-byte Syscon rail-enable packet that switches `DD4/VDDG` (`0xF9` <-> `0xFD`) on and off.
2. Replay that verified `DD4/VDDG` rail-enable packet + `0x88E` (`vid=0x5C`) in `vita_gpu_diag.ko`, confirm `Elmo 0x883` reports `DD4` active, and read `EUR_CR_CORE_ID` (`0x01174403`) in Linux `dmesg`.

---

## 8. Post-GPU Follow-Up Items
- **`vita-reboot` / `vita-reboot-gui` in CLI:** User reported `vita-reboot-gui: failed to open X11 window` when running `reboot` in Linux CLI (likely `$DISPLAY` set without an active X11 server, or `vita-reboot` needs to verify X11 socket `/tmp/.X11-unix/X0` / `x11_open` fallback before aborting). Fix after current GPU debugging session.

