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

---

## 7. Current Session Handoff & Immediate Resume Checklist

### Status Summary (End of Day - 2026-11-24)
- **Phase 0 & Phase 1:** Complete. Stock VitaOS Pervasive, IFTU, SGX543MP4+, Syscon `0x088E` voltage scaling, and full 72-packet standby SPI sequence captured and verified on physical PS Vita 1000 hardware.
- **Phase 2 (SGX543 Power & Bus Bring-Up in Linux 6.12):** ~85% complete:
  - **Verified on hardware in Linux 6.12:**
    - External GPU rail voltage (`Syscon 0x088E`, `0x3004` = 111 MHz) returns `res = 0x00`.
    - On-die SGX543 power domain (`TrustZone SMC 0x110(0, 1)`) returns `0x00000000`, atomically clears isolation clamp `MISC + 0x34` (`1 -> 0`), enables 4-core clock gate `GATE + 0x10` (`0x000F000F`), deasserts `RESET + 0x10` (`1 -> 0`), and powers `GPU_REGBUS` (`0xED800000` reads `0x01006437` cleanly in Linux!).
    - Clean power-down (`SMC 0x110(0, 0)`) verified on `rmmod`.
  - **Built & ready for first test next session (`tools/vita-sgx-probe/vita_sgx_wake.ko` v3):**
    - Adds the missing post-`SOFT_RESET` `ScePower` calls (`SMC 0x114(2, 2)` GPU XBAR clock + `SMC 0x11B(0xF00, 0 -> 1)` `SceGrab` AXI/XBAR bridge unpause) and `SceGpuEs4` Hydra Master soft-reset / `EUR_CR_CLKGATECTL` init (`+0x4080`, `+0x0000`, `+0x0004`).
    - Uses an interactive `/proc/vita_sgx_wake` command interface (`status`, `read_master`, `read_id`, `init_sgx`, `grab_reopen`, `dom1_on`, `dom1_off`) so `module_init` never touches `0xE8400000` and the module can never wedge in `MODULE_STATE_COMING`.

### Exact Commands to Resume Next Session
1. Boot PS Vita cleanly into Linux and copy the pre-built v3 module from PC:
   - `scp /home/zachary/Code/vita-linux/tools/vita-sgx-probe/vita_sgx_wake.ko root@192.168.0.123:/tmp/`
2. Load module and inspect XBAR/Grab/`GPU_REGBUS` state on Vita SSH:
   - `insmod /tmp/vita_sgx_wake.ko`
   - `dmesg | grep vita_sgx_wake | tail -n 15`
3. Probe SGX543 registers in subshells via `/proc/vita_sgx_wake`:
   - `(echo read_master > /proc/vita_sgx_wake) && dmesg | grep vita_sgx_wake | tail -n 5`
   - `(echo read_id > /proc/vita_sgx_wake) && dmesg | grep vita_sgx_wake | tail -n 6`
   - If needed: `(echo init_sgx > /proc/vita_sgx_wake) && dmesg | grep vita_sgx_wake | tail -n 6`
   - Clean unload: `rmmod vita_sgx_wake`
4. Once `EUR_CR_CORE_ID` (`0x01174403`) & `EUR_CR_CORE_REVISION` (`0x00020106`) read cleanly in Linux `dmesg`, move straight to **Phase 3: Standalone DRM/KMS Display Driver (`vita-drm`)** (`IFTU` `0xE5020000`/`0xE5022000`, CDRAM `0x20000000`, VBlank IRQ `GIC_SPI 181` / `178`).

---

## 8. Post-GPU Follow-Up Items
- **SD2Vita (`ux0`) Dynamic MMC Device Discovery (`S05vita` & `vita-storage`):** Depending on MMC host probe ordering at boot, internal eMMC can enumerate as `/dev/mmcblk1` (`p1..p12`, `boot0/1`, `rpmb`) instead of `/dev/mmcblk0`, pushing SD2Vita to `/dev/mmcblk2`. Update `S05vita` and `vita-storage` to identify and skip the `[SCE]` eMMC device (`[ -b "${dev}p12" ]`) when scanning for `ux0` so `/dev/mmcblk2` (and `/dev/mmcblk2p1`) mounts automatically at `/mnt/ux0` on every boot.
- **`vita-reboot` / `vita-reboot-gui` in CLI:** User reported `vita-reboot-gui: failed to open X11 window` when running `reboot` in Linux CLI (likely `$DISPLAY` set without an active X11 server, or `vita-reboot` needs to verify X11 socket `/tmp/.X11-unix/X0` / `x11_open` fallback before aborting). Fix after current GPU debugging session.

