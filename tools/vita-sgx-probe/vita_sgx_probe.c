// SPDX-License-Identifier: GPL-2.0
/*
 * vita_sgx_probe.c - Phase 2B PowerVR SGX543MP4+ Pervasive Wake & Core ID Probe
 *
 * Verified diff between VitaOS (gpu_trace_after.txt) and Linux boot state:
 *   - PERVASIVE_BASECLK + 0x10 (0xE3103010): 0x00030003 (already set in Linux)
 *   - PERVASIVE_GATE    + 0x10 (0xE3102010): 0x00000000 in Linux vs 0x000F000F in VitaOS
 *   - PERVASIVE_RESET   + 0x10 (0xE3101010): 0x00000001 in Linux vs 0x00000000 in VitaOS
 *
 * Applies the stock VitaOS Pervasive clock-gate and reset-deassert sequence,
 * reads read-only SGX543 identification registers at 0xE8400000, and gates the
 * GPU cleanly back off on rmmod.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>

#define PERVASIVE_RESET_PA	0xE3101000UL
#define PERVASIVE_GATE_PA	0xE3102000UL
#define PERVASIVE_BASECLK_PA	0xE3103000UL
#define PERVASIVE_GPU_OFF	0x0010UL

#define SGX543_REGS_PA		0xE8400000UL
#define SGX543_REGS_SIZE	0x2000UL

#define EUR_CR_CLKGATECTL	0x0000UL
#define EUR_CR_CLKGATECTL2	0x0004UL
#define EUR_CR_CORE_ID		0x0020UL
#define EUR_CR_CORE_REVISION	0x0024UL
#define EUR_CR_MULTI_CORE	0x0100UL

#define STOCK_GPU_BASECLK	0x00030003U
#define STOCK_GPU_GATE		0x000F000FU
#define STOCK_GPU_RESET_MASK	0x00000001U

static bool wake_gpu = true;
module_param(wake_gpu, bool, 0444);
MODULE_PARM_DESC(wake_gpu, "Enable Pervasive GPU clock gate and deassert reset (default: true)");

static bool read_sgx = true;
module_param(read_sgx, bool, 0444);
MODULE_PARM_DESC(read_sgx, "Read SGX543 identification registers at 0xE8400000 (default: true)");

static bool gate_off_on_exit = true;
module_param(gate_off_on_exit, bool, 0444);
MODULE_PARM_DESC(gate_off_on_exit, "Restore GPU reset and clock gate on rmmod (default: true)");

static u32 saved_baseclk;
static u32 saved_gate;
static u32 saved_reset;
static bool gpu_woken;

static int __init vita_sgx_probe_init(void)
{
	void __iomem *reset_base = NULL;
	void __iomem *gate_base = NULL;
	void __iomem *clk_base = NULL;
	void __iomem *sgx_base = NULL;
	u32 cur_clk, cur_gate, cur_rst;

	reset_base = ioremap(PERVASIVE_RESET_PA, 0x1000);
	gate_base  = ioremap(PERVASIVE_GATE_PA, 0x1000);
	clk_base   = ioremap(PERVASIVE_BASECLK_PA, 0x1000);

	if (!reset_base || !gate_base || !clk_base) {
		pr_err("vita_sgx_probe: failed to ioremap Pervasive blocks\n");
		if (clk_base)
			iounmap(clk_base);
		if (gate_base)
			iounmap(gate_base);
		if (reset_base)
			iounmap(reset_base);
		return -ENOMEM;
	}

	saved_baseclk = readl(clk_base + PERVASIVE_GPU_OFF);
	saved_gate    = readl(gate_base + PERVASIVE_GPU_OFF);
	saved_reset   = readl(reset_base + PERVASIVE_GPU_OFF);

	pr_info("vita_sgx_probe: initial Pervasive GPU state: BASECLK=0x%08x GATE=0x%08x RESET=0x%08x\n",
		saved_baseclk, saved_gate, saved_reset);

	if (wake_gpu) {
		/* 1. Ensure stock baseclk divider (0x00030003) */
		if (saved_baseclk != STOCK_GPU_BASECLK) {
			writel(STOCK_GPU_BASECLK, clk_base + PERVASIVE_GPU_OFF);
			(void)readl(clk_base + PERVASIVE_GPU_OFF);
			udelay(50);
		}

		/* 2. Enable 4-core GPU clock gate (0x000F000F) */
		writel(saved_gate | STOCK_GPU_GATE, gate_base + PERVASIVE_GPU_OFF);
		(void)readl(gate_base + PERVASIVE_GPU_OFF);
		udelay(100);

		/* 3. Deassert GPU reset (scePervasiveGpuResetDisableForDriver clears bit 0) */
		writel(saved_reset & ~STOCK_GPU_RESET_MASK, reset_base + PERVASIVE_GPU_OFF);
		(void)readl(reset_base + PERVASIVE_GPU_OFF);
		udelay(200);

		gpu_woken = true;

		cur_clk  = readl(clk_base + PERVASIVE_GPU_OFF);
		cur_gate = readl(gate_base + PERVASIVE_GPU_OFF);
		cur_rst  = readl(reset_base + PERVASIVE_GPU_OFF);

		pr_info("vita_sgx_probe: post-wake Pervasive GPU state: BASECLK=0x%08x GATE=0x%08x RESET=0x%08x\n",
			cur_clk, cur_gate, cur_rst);
	}

	iounmap(clk_base);
	iounmap(gate_base);
	iounmap(reset_base);

	if (read_sgx) {
		u32 clkgate1, clkgate2, core_id, core_rev, mp_cores;

		pr_info("vita_sgx_probe: mapping SceSGX543Reg at PA 0x%08lx...\n",
			SGX543_REGS_PA);
		/* Allow log line to flush over SSH before MMIO read */
		msleep(50);

		sgx_base = ioremap(SGX543_REGS_PA, SGX543_REGS_SIZE);
		if (!sgx_base) {
			pr_err("vita_sgx_probe: failed to ioremap SceSGX543Reg\n");
			return -ENOMEM;
		}

		core_id  = readl(sgx_base + EUR_CR_CORE_ID);
		core_rev = readl(sgx_base + EUR_CR_CORE_REVISION);
		clkgate1 = readl(sgx_base + EUR_CR_CLKGATECTL);
		clkgate2 = readl(sgx_base + EUR_CR_CLKGATECTL2);
		mp_cores = readl(sgx_base + EUR_CR_MULTI_CORE);

		pr_info("vita_sgx_probe: EUR_CR_CORE_ID       (+0x0020) = 0x%08x (expected 0x01174403)\n",
			core_id);
		pr_info("vita_sgx_probe: EUR_CR_CORE_REVISION (+0x0024) = 0x%08x (expected 0x00020106)\n",
			core_rev);
		pr_info("vita_sgx_probe: EUR_CR_CLKGATECTL    (+0x0000) = 0x%08x\n",
			clkgate1);
		pr_info("vita_sgx_probe: EUR_CR_CLKGATECTL2   (+0x0004) = 0x%08x\n",
			clkgate2);
		pr_info("vita_sgx_probe: EUR_CR_MULTI_CORE    (+0x0100) = 0x%08x\n",
			mp_cores);

		iounmap(sgx_base);
	}

	return 0;
}

static void __exit vita_sgx_probe_exit(void)
{
	if (gpu_woken && gate_off_on_exit) {
		void __iomem *reset_base = ioremap(PERVASIVE_RESET_PA, 0x1000);
		void __iomem *gate_base  = ioremap(PERVASIVE_GATE_PA, 0x1000);

		if (reset_base && gate_base) {
			/* Put GPU back into reset before gating clocks off */
			writel(saved_reset | STOCK_GPU_RESET_MASK,
			       reset_base + PERVASIVE_GPU_OFF);
			(void)readl(reset_base + PERVASIVE_GPU_OFF);
			udelay(50);

			writel(saved_gate, gate_base + PERVASIVE_GPU_OFF);
			(void)readl(gate_base + PERVASIVE_GPU_OFF);

			pr_info("vita_sgx_probe: restored GPU reset (0x%08x) and gate (0x%08x)\n",
				saved_reset | STOCK_GPU_RESET_MASK, saved_gate);
		}
		if (gate_base)
			iounmap(gate_base);
		if (reset_base)
			iounmap(reset_base);
	}

	pr_info("vita_sgx_probe: unloaded\n");
}

module_init(vita_sgx_probe_init);
module_exit(vita_sgx_probe_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LinuxOnVita");
MODULE_DESCRIPTION("Phase 2B PowerVR SGX543MP4+ Pervasive wake and Core ID probe");
