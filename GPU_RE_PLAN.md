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
- **Pervasive Wake Alone (`GATE=0x000F000F`, `RESET=0x00000000`):** Reading `0xE8400020` in Linux while `VDDG` is off triggers an AXI slave abort (`imprecise external abort 0x1406`) cleanly without crashing SSH/Linux.
- **TrustZone `SMC 0x114` (`smc_bus_freq_op`) Works Natively in Linux 6.12:**
  - Both VitaOS and Linux return `bus_lvl = 0x6` (`222 MHz`) and `gpu_xbar_lvl = 0x3` (`111 MHz`).
- **Elmo PMIC Power Status (`Syscon 0x0883`), `0x0891`, & Stock `DD4/VDDG` Wire Encoding (`Syscon 0x088E`):**
  - Live VitaOS `0x0883`: `f9 97 40 00 11 c0 14 00`; Linux boot `0x0883`: `f9 97 40 00 11 40 14 00`.
  - Full 72-packet `kscePowerRequestStandby()` spy capture (`SPY[slot=000..071]`) + `SceSyscon@0x00C1DB70` (`0x04EC7579`) disassembly proved `byte[0] = 0xF9` (`DD4/VDDG` external buck rail) remains active across standby, while `byte[5]` (`0xC0` -> `0x40`) is PS button LED state toggled by `Syscon 0x0891(0x40, 0)` at `SPY[slot=068]`.
  - Live VitaOS `ScePower` hook + raw SPI packet trace (`gpu_trace_power.txt`) proved `sceSysconCtrlVoltageForDriver(type=4, vid)` (`SceSyscon@0x00C1DAAD`) sends inverted DAC code `raw = 0x8C - vid`:
    - `vid = 0x5C` (`111 MHz`) -> `tx = [8E 08 03 | 04 30 32]` (`data = 0x3004`, returns `res = 0x00` in Linux)
    - `vid = 0x60` (step) -> `tx = [8E 08 03 | 04 2C 36]` (`data = 0x2C04`)
    - `vid = 0x68` (`222 MHz`) -> `tx = [8E 08 03 | 04 24 3E]` (`data = 0x2404`)
- **Complete VitaOS GPU Power-Domain Mechanism (Disassembled from `SceGpuEs4`, `ScePower`, `SceLowio`):**
  - Neither `ScePower` nor `SceGpuEs4` calls `SceSyscon` `0x0884`/`0x0885` from non-secure kernel mode.
  - Instead, `SceGpuEs4` `PVRSRV` power callbacks toggle the on-die SGX543 power domain and isolation cells via `SceLowio` TrustZone **`SMC 0x110`** (`0x00C08000`):
    - **`SysDevicePostPowerState` (`0x0207D4A8`, leaving D3 -> D0):** calls `ScePervasiveForDriver::0x84EAB5BC` (`SceLowio@0x00C083F4`) -> `smc(0x110, r0=0, r1=1)`.
    - **`SysDevicePrePowerState` (`0x0207D488`, entering D3):** calls `ScePervasiveForDriver::0xE8656ADF` (`SceLowio@0x00C08400`) -> `smc(0x110, r0=0, r1=0)`.
  - Pervasive GPU clock/reset helpers in `SceLowio`:
    - `0x00C09064` (`0x6AF0196F`): writes `(mp_div << 16) | core_div` (`0x00030003` at 111 MHz) to `PERVASIVE_BASECLK + 0x10` (`0xE3103010`), bracketed by `SceGrab` bus pause `smc(0x11B, 0xF00, 0/1)`.
    - `0x00C08AFC` (`0x39E51AE2`): sets `0x000F000F` in `PERVASIVE_GATE + 0x10` (`0xE3102010`).
    - `0x00C08514` (`0x8A85E36B`): clears bit 0 in `PERVASIVE_RESET + 0x10` (`0xE3101010`).

### 6.4 Phase 2C On-Device `SMC 0x110` Verification & `SceGrab` (`SMC 0x11B`) Discovery
- **Verified on Physical PS Vita (`vita_sgx_wake.ko` with `read_sgx=0`):**
  - `Syscon 0x088E` (`0x3004`) returned `res = 0x00`.
  - `TrustZone SMC 0x110(domain=0, state=1)` returned `0x00000000` and atomically inside TrustZone transitioned:
    - `PERVASIVE_MISC + 0x34` (`0xE3100034` isolation clamp): `0x00000001 -> 0x00000000`
    - `PERVASIVE_GATE + 0x10` (`0xE3102010` 4-core clock gate): `0x00000000 -> 0x000F000F`
    - `PERVASIVE_RESET + 0x10` (`0xE3101010` GPU reset): `0x00000001 -> 0x00000000`
  - `GPU_REGBUS` (`0xED800000` SonicsSX Target Agent) became cleanly readable in Linux: `+0x00 = 0x01006437`, `+0x18 = 0x00130001`, `+0x1C = 0x00010214`.
  - `rmmod vita_sgx_wake` cleanly called `SMC 0x110(domain=0, state=0) -> 0x00000000`.
- **Why `0xE8400020` (`SceSGX543Reg`) Still Faulted & Missing Post-Soft-Reset Steps:**
  - `baremetal-loader.skprx` hooks `ksceSysconResetDevice` at the end of `kscePowerRequestStandby()` (after all VitaOS suspend callbacks ran) and rewrites `SUSPEND` -> `SOFT_RESET` into `payload.bin` -> `zImage`, bypassing both `ScePower` module boot (`0x018AB99C`) and `ScePower` resume (`0x018ABB84`).
  - Disassembly of `ScePower` (`0x018ABB0A..0x018ABB18` / `0x018ABCE2..0x018ABCF0`) and `SceGpuEs4` (`0x020765B8`) identified the remaining post-reset steps before `0xE8400020` core reads:
    1. Set GPU XBAR clock via `SMC 0x114(r0=2, r1=2)` (`ScePervasiveForDriver::0x7D02215D`).
    2. Unpause the `0xF00` GPU AXI/XBAR Grab bridge via `SMC 0x11B(r0=0xF00, r1=0)` -> `PERVASIVE_BASECLK + 0x10 = 0x00030003` -> `SMC 0x11B(r0=0xF00, r1=1)` (`SceGrabForDriver::0xB22EE8BC`).
    3. `SceGpuEs4` `SGXInitialise` (`0x020765B8` / `0x0207659A`) also pulses Hydra Master soft reset (`+0x4080 = 0x7FF -> 0`) and enables internal SGX clock gating (`+0x0000 = 0x002AAAAA`, `+0x0004 = 0x0A8A8AAA`) before reading core registers.

### 6.5 Phase 2D Complete SGX543MP4+ Bring-Up Verified in Linux 6.12
- **Verified live on physical PS Vita 1000 in Linux 6.12 (`vita_sgx_wake.ko` v4):**
  - Confirmed `XBAR_SMC` (`SMC 0x114(3, 0)`) boots as `0x0` (disabled) after `baremetal-loader`'s `SOFT_RESET`.
  - Complete 5-step non-secure Linux wake sequence verified with zero bus aborts:
    1. `Syscon 0x088E` (`tx_len = 3`, `data = 0x3004`): sets `VDDG` 111 MHz voltage (`res = 0x00`).
    2. `SMC 0x114(r0 = 2, r1 = 2)`: enables 111 MHz GPU XBAR clock (`XBAR_SMC` transitions `0x0 -> 0x2`).
    3. `SMC 0x110(r0 = 0, r1 = 1)`: powers on SGX543 domain (`MISC+0x34 = 0`, `GATE+0x10 = 0x000F000F`, `RESET+0x10 = 0`, `GPU_REGBUS 0xED800000 = 0x01006437`).
    4. `SMC 0x11B(0xF00, 0)` -> `PERVASIVE_BASECLK+0x10 = 0x00030003` -> `SMC 0x11B(0xF00, 1)`: unpauses `0xF00` GPU AXI/XBAR Grab bridge.
    5. `SGXStart` (`+0x4000 = 3`, `+0x4020 = 0x2A9`, `+0x4004 = 0xAA`) + `SGXReset` (`+0x4080 = 0x7FF`, `+0x4C00 = 0`, `+0x4D00 = 0x4C800`, `+0x4D04 = 0x04000000`, `+0x4080 = 0`, `+0x0000 = 0x002AAAAA`, `+0x0004 = 0x0A8A8AAA`):
       - `EUR_CR_CORE_ID (+0x0020) = 0x01174403` (PowerVR SGX543MP4+)
       - `EUR_CR_CORE_REVISION (+0x0024) = 0x00020106` (Revision `2.1.6`)
       - `EUR_CR_CLKGATECTL (+0x0000) = 0x002AAAAA / 0x0A8A8AAA`, `MP(+0x0100) = 0x0000000F`, `MASTER(+0x4000) = 0x00000003`
  - Clean power-down (`SMC 0x110(0, 0)`) verified on `rmmod`.

### 6.6 Phase 3 Display Engine (`SceIftu` & `SceDsi`) Disassembly Findings
- **Disassembled from `SceLowio` (`0x00C0BAA4..0x00C0E788`) and `SceDisplay` (`0x00C2D980..0x00C2EC80`):**
  - **VBlank Hardware Interrupts (`SceDsi` ISR at `0x00C0BAA4`):**
    - `SceDsi0` (`0xE5050000`, OLED/LCD): VitaOS intr `0xD5` (`213`) -> `GIC_SPI 181`.
    - `SceDsi1` (`0xE5060000`, PSTV HDMI): VitaOS intr `0xD2` (`210`) -> `GIC_SPI 178`.
    - `DSI + 0x50`: W1C interrupt status (`bit 1 = 0x2` is VBlank Start; acked via `val = readl(+0x50); writel(val, +0x50); readl(+0x50);`).
    - `DSI + 0x54`: Interrupt enable mask (`bit 1 = 0x2` enables VBlank Start; already set to `0x2` by `libbaremetal` `dsi_start_display`).
    - `DSI + 0x4C`: Current scanline register (`((readl(+0x4C) >> 16) & 0x1FFF) - 1`).
  - **IFTU Per-Plane Interrupts (`0x00C10FC4` table):**
    - `SceIftu0a` (`0xE5020000`): intr `204` (`GIC_SPI 172`), `SceIftu0b` (`0xE5021000`): intr `205` (`GIC_SPI 173`).
    - `SceIftu1a` (`0xE5030000`): intr `206` (`GIC_SPI 174`), `SceIftu1b` (`0xE5031000`): intr `207` (`GIC_SPI 175`).
  - **Tear-Free Double-Buffered Plane Config Flip (`sceIftuSetPlaneConfig` at `0x00C0DFEC`):**
    - Each plane has two hardware shadow config banks: `config 0` at `plane_regs + 0x200` and `config 1` at `plane_regs + 0x300` (both pre-initialized by `libbaremetal` `display_iftu_setup`).
    - `plane_regs + 0x04`: `bit 1` reports the currently active scanout config bank (`0` or `1`); `bit 0` reports whether a config switch is pending.
    - Key `config` bank offsets: `+0x00 = FB_PADDR` (CDRAM physical address), `+0x20 = STRIDE0` (bytes per row, or `0` for packed width*bpp), `+0x40 = SRC_PIXELFMT` (`0x10` = `A8B8G8R8` / `DRM_FORMAT_XBGR8888`), `+0x44 = SRC_WIDTH`, `+0x48 = SRC_HEIGHT`, `+0x4C = CONTROL` (`0` = enabled, `1` = disabled).
    - Writing `next_cfg` (`0` or `1`) to `IFTU_CREG + 0x10` (`PLANE0_CONFIG_SELECT`) latches the new config bank tear-free at the next hardware VBlank.

### 6.7 Phase 3 Live Hardware Verification (`vita_iftu_probe.ko`) & `vita-drm` Implementation
- **Verified live on physical PS Vita 1000 in Linux 6.12 (`vita_iftu_probe.ko`):**
  - `GIC_SPI 181` (`virq 32`) delivered clean hardware VBlank interrupts at scanline `1` (`storm = 0`, W1C ack at `DSI0 + 0x50` with `DSI0 + 0x54 = 0x2`).
  - Hardware double-buffered CDRAM page-flipping between `0x20000000` (`CFG0`, `P0+04 = 0x00000101`) and `0x20400000` (`CFG1`, `P0+04 = 0x00000002`) latched tear-free on VBlank via `IFTU_CREG0 + 0x10`, visually confirmed on OLED with an R/G/B test banner.
  - Confirmed `SceIftu` pixelformat codes (`SCE_IFTU_PIXELFORMAT_BGR565 = 0x01`, `RGB565 = 0x02`, `BGRX8888 = 0x10`, `RGBX8888 = 0x20`) and row padding register (`CFG + 0x54 = leftover_stride`).
- **Implemented Full Linux 6.12 Atomic KMS Driver (`drivers/gpu/drm/vita/vita_drm.c`):**
  - Supports PS Vita 1000 OLED (`960x544`), PS Vita 2000 LCD (`960x544`), and PlayStation TV HDMI (`1280x720`, `dst_x = 26, dst_y = 15`).
  - Backed by a dedicated 28 MB CDRAM `shared-dma-pool` (`0x20400000 .. 0x21FFFFFF`), preserving early `simple-framebuffer` (`0x20000000`) until VBlank handoff and leaving 96 MB of CDRAM (`0x22000000 .. 0x27FFFFFF`) free for Phase 4/5 SGX543 VRAM.
  - Exposes both `/dev/dri/card0` (atomic KMS, hardware VBlank, `XRGB8888`/`ARGB8888`/`XBGR8888`/`ABGR8888`/`RGB565`/`BGR565`) and `/dev/fb0` (`drm_fbdev_dma_setup`, including DPMS blanking via `/sys/class/graphics/fb0/blank`), plus `/usr/bin/vita-drm-test` for hardware VSync validation.
- **Verified `vita-drm` Kernel Driver Live on Physical PS Vita 1000 (`v1.7.0-BETA1`):**
  - Reserved 28 MB CDRAM pool `vram@20400000` (`0x20400000 .. 0x21FFFFFF`) bound cleanly to `e5020000.display` (`vita-drm 1.0.0` on `minor 0`, `card0-DSI-1`, and `fb0: vita-drmdrmfb`).
  - `vita-drm-test` completed 120 tear-free hardware VSync CDRAM page flips in `3.071 s` (`39.07 FPS`, frame interval `min=25.50 ms`, `avg=25.59 ms`, `max=25.69 ms`, `0.19 ms` jitter) across consecutive runs, and `/sys/class/graphics/fb0/blank` DPMS blank/unblank verified cleanly.
- **Phase 4 `SceGpuEs4` Disassembly Findings (`kdump_SceGpuEs4.bin`, DDK `1.7.17.5029`):**
  - **Hydra MP4+ 128 KB Register Banking (`0xE8400000 .. 0xE841FFFF`):**
- **`+0x001C` (`EUR_CR_POWER`):** `0x00000000` (both USSE pipes enabled per core; note `+0x0080` is `EUR_CR_SOFT_RESET`).
    - `0x00000 .. 0x03FFF`: Broadcast / core-common bank (`+0x0000` `CLKGATECTL`, `+0x001C` `POWER`, `+0x0080` `SOFT_RESET`, `+0x0110..+0x0134` `EVENT_*`, `+0x0144` `TIMER`, `+0x0ACC` `EVENT_TIMER`, `+0x0C00..+0x0CD0` `BIF_*`).
    - `0x04000 .. 0x07FFF`: Hydra Master bank (`+0x4000` `MASTER_CORE`, `+0x4004` `MASTER_CLKGATECTL`, `+0x4020` `MASTER_CLKGATECTL2`, `+0x4080` `MASTER_SOFT_RESET`, `+0x412C..+0x4134` `MASTER_EVENT_*`, `+0x4174..+0x41FC` `MASTER_PTLA_*`, `+0x4C00..+0x4CD0` `MASTER_BIF_*`, `+0x4D00..+0x4D34` `MASTER_SLC_*`).
    - `0x08000 + core * 0x4000` (`core = 0..3`): Per-core SGX543 banks (`0x8000`, `0xC000`, `0x10000`, `0x14000`), including Core 0 EDM kicks `+0x8AC0` (`EUR_CR_EVENT_KICK2`) and `+0x8AC8` (`EUR_CR_EVENT_KICK`) and per-core BIF fault registers `+0x8C04`/`+0x8C08`.
  - **SGX543 Interrupt Handling (`SceGpuLisr` `0x0207715A`, `GIC_SPI 20` / VitaOS intr `0x34 = 52`):**
    - Hydra Master events (`EUR_CR_MASTER_EVENT_STATUS` `+0x412C`, `HOST_ENABLE` `+0x4130`, `HOST_CLEAR` `+0x4134`) and Core events (`+0x012C` / `+0x0130` / `+0x0134`) assert `GIC_SPI 20` when enabled together with `MASTER_INTERRUPT` (`0x80000000U`), including `TIMER` (`0x20000000U`), `TWOD_COMPLETE` (`0x08000000U`), and `SW_EVENT` (`0x00004000U`).
    - Cleared via `writel(status | 0x80000000U, +0x4134)` and `writel(status | 0x80000000U, +0x0134)`.
  - **SGX543 BIF 2-Level MMU (`SGXResetInitBIF` `0x02076628`, `sgxmmu.h`):**
    - Standard 32-bit SGX MMU (`ADDR_MASK = 0xFFFFF000U`, `ALIGNSHIFT = 0`): 4 KB Page Directory (`1024` x 32-bit PDEs, `4 MB` per PDE, `pd_idx = vaddr >> 22`, 4 KB PT PDE = `pt_phys | 0x1U`) and 4 KB Page Table (`1024` x 32-bit PTEs, `pt_idx = (vaddr >> 12) & 0x3FF`, RW coherent PTE = `page_phys | 0x9U` where `bit 0 = VALID`, `bit 3 = CACHECONSISTENT`).
    - Master BIF `EUR_CR_MASTER_BIF_BANK0` (`+0x4C78`) selects directory list indices for `TA[7:4]`, `3D[15:12]`, and `PTLA[19:16]`; `EUR_CR_MASTER_BIF_CTRL_INVAL` (`+0x4C34 = 0x8`) and `EUR_CR_BIF_CTRL_INVAL` (`+0x0C34 = 0x8`) invalidate TLB/directory caches (`FLUSH_COMPLETE = 0x00080000` in `+0x4C04`/`+0x0C04`).

### 6.8 Phase 4 Live Hardware Verification (`vita_sgx_phase4.ko` on PS Vita 1000)
- **Verified 100% live on physical PS Vita 1000 OLED in Linux 6.12 (`tools/vita-sgx-probe/vita_sgx_phase4.c`):**
  1. **Step 4A - `GIC_SPI 20` Hardware Interrupt & `EUR_CR_TIMER` (`irq_test`):**
     - Mapped `/soc/gpu@e8400000` `GIC_SPI 20` (`IRQ_TYPE_LEVEL_HIGH`) to Linux `virq = 33`.
     - Confirmed `EUR_CR_TIMER` (`+0x0144` / `+0x8144`) increments by `+9` ticks per `1 ms` at `111 MHz` (`111 MHz / 12288 = 9.03 ticks/ms`).
     - Armed `EUR_CR_EVENT_TIMER` (`+0x0ACC = 0x01001000`); `GIC_SPI 20` fired cleanly in `137 us` (`timer_irqs = 1`, `last_m_stat = 0xA0000000`, `last_c_stat = 0xA0000000`, `storm = 0`).
  2. **Step 4B - 2-Level BIF MMU Virtual-to-Physical Translation (`mmu_test`):**
     - Allocated coherent 4 KB Page Directory (`PD_PA = 0x5F044000`) and 4 KB Page Tables mapping SGX VA `0x50000000` (`PD[0x140]`) -> `64 KB` coherent system DRAM (`0x5F050000`) and SGX VA `0x50400000` (`PD[0x141]`) -> `256 KB` physical CDRAM (`0x22000000`).
     - Programmed `EUR_CR_MASTER_BIF_DIR_LIST_BASE0` (`+0x4C84`) and `EUR_CR_BIF_DIR_LIST_BASE0` (`+0x0C84`), invalidated BIF TLBs (`+0x4C34 = 8`, `+0x0C34 = 8`), and verified `MASTER_BIF_STAT = 0x00080000` (`FLUSH_COMPLETE`), `BIF_FAULT = 0x00000000`.
  3. **Step 4C - Hydra Master PTLA 2D Hardware Fill, Blit & Live OLED Scanout (`blit_test` & `oled_test`):**
     - Submitted 36-word PTLA 2D hardware colour fill stream (`EUR_CR_MASTER_PTLA_SLAVE_CMD_0` `+0x4180`, including mandatory dummy `SRC_SURF_BH` + `EURASIA_PTLA_FLUSH_BH` `0x80000000U`) drawing four `64x64` `ARGB8888` quadrants (`0xFFFF0000`, `0xFF00FF00`, `0xFF0000FF`, `0xFFFFFFFF`) to SGX VA `0x50000000` (DRAM): completed in `150 us` with `TWOD_COMPLETE` (`0x88000000`) IRQ on `GIC_SPI 20` and `0` BIF faults.
     - Submitted 10-word PTLA 2D strided blit copying `128x128` (`64 KB`) from SGX VA `0x50000000` (DRAM) -> SGX VA `0x50400000` (`0x22000000` CDRAM): completed in `164 us` with `TWOD_COMPLETE` IRQ on `GIC_SPI 20`, `SLC_CTRL_FLUSH_INV` (`+0x4D34 = 0x10`), `0` BIF faults, and **`16384/16384` exact pixel matches** in both DRAM and CDRAM.
     - Mapped active `960x544` `vita-drm` CDRAM framebuffer (`0x20400000`, `stride = 3840`) at SGX VA `0x50800000` (`PD[0x142]`) and executed `oled_test` (`255 us`, `twod_irqs = 5`, `BIF_FAULT = 0`): visually confirmed the GPU-rendered `128x128` RGBW badge live at `(796, 28)` on the PS Vita 1000 OLED screen, followed by clean `rmmod` power-down.

---

## 7. Current Session Status & Active Phase (Phase 5)

### Status Summary (2026-11-27)
- **Phase 0, Phase 1, & Phase 2 (Power/Clock/Hydra Bring-Up):** **100% COMPLETE & VERIFIED ON HARDWARE.**
- **Phase 3 (`vita-drm` Atomic KMS Display Driver):** **100% COMPLETE & VERIFIED ON HARDWARE.**
- **Phase 4 (SGX543 `GIC_SPI 20` IRQ, 2-Level BIF MMU & PTLA Hardware Render/Blit):** **100% COMPLETE & VERIFIED ON HARDWARE.** First-ever GPU-accelerated pixels rendered through the 2-level BIF MMU into both DRAM and CDRAM and displayed live on the PS Vita OLED panel under Linux 6.12.
- **Active Phase (Phase 5 - Kernel 2D Integration & 3D OpenGL ES Stack Architecture):**
  1. **Track 5A (100% Open-Source GPLv2 Kernel + Desktop 2D Acceleration, Zero Blobs):**
     - Integrate SGX543 power/clock management, 2-level BIF MMU, and Hydra Master **PTLA 2D blitter/scaler/YUV engine** into `vita-drm` / `drivers/gpu/drm/vita/` (enabling `/soc/gpu@e8400000` in `vita.dtsi`), unlocking cached-DRAM -> CDRAM DMA page-flips, hardware 2D rectangle fills/copies/scaling/rotation, YUV420 video blits, and Xorg EXA 2D desktop acceleration (`xf86-video-vita`).
  2. **Track 5B (Full 3D OpenGL ES 1.1 / 2.0 & EGL Stack Evaluation):**
     - Cross-audit `openpvrsgx-devgroup/linux_openpvrsgx` Dual MIT/GPLv2 `1.7.862890` (`services4/srvkm` + `srvinit` USSE microkernel) together with `GrapheneCt/PVR_PSP2` (full Imagination DDK 1.7 `eurasiacon` OpenGL ES 1/2, EGL, `glslcompiler`, `usc2` USSE compiler, and `pds`/`usegen` C source targeting PS Vita's exact `SGX543MP4+` rev `2.1.6`) to determine the cleanest buildable path to native Linux `armhf` `libEGL.so` + `libGLESv2.so` on Linux 6.12.

---

## 8. Post-GPU Follow-Up Items
- **SD2Vita (`ux0`) Dynamic MMC Device Discovery (`S05vita` & `vita-storage`):** **VERIFIED ON HARDWARE** in `v1.7.0-BETA1` (`ux0` mounted read-only at `/mnt/ux0` on `/dev/mmcblk1`).
- **`vita-reboot` / `vita-reboot-gui` in CLI:** **IMPLEMENTED** in `v1.7.0-BETA1` (`vita-reboot` checks for `/tmp/.X11-unix/X0` before invoking `vita-reboot-gui`).
- **Wi-Fi Boot DHCP Race Fix (`S45wifi`, `vita-wifi-daemon`, `vita-wifi`):** **IMPLEMENTED** in `v1.7.0-BETA2` (waits for `wpa_state=COMPLETED`, disables `mwifiex` 802.11 power-save `ps 0`, uses `-B` broadcast fallback, and prevents overlapping `udhcpc` daemons).

