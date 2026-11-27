// SPDX-License-Identifier: GPL-2.0
/*
 * vita_sgx_wake.c - SGX543MP4+ Power-Domain, XBAR & Grab Bring-Up Probe
 *
 * Verified from stock VitaOS SceGpuEs4, ScePower, SceLowio, and SceSyscon:
 *   1. ScePower resume (0x018ABBC2) sets external GPU VDDG voltage via
 *      sceSysconCtrlVoltageForDriver(type=4, vid=0x5C -> 111 MHz):
 *        Syscon cmd 0x088E, len=3, payload [0x04, 0x30] (0x8C - 0x5C = 0x30).
 *   2. ScePower boot (0x018ABB0A) & resume (0x018ABCE2) set the GPU XBAR
 *      clock via ScePervasiveForDriver::0x7D02215D (SceLowio 0x00C09290):
 *        TrustZone SMC 0x114 (r0 = 2, r1 = 2) -> 111 MHz GPU XBAR clock
 *      And ScePervasiveForDriver::0xECA675E0 (SceLowio 0x00C0929C) reads it:
 *        TrustZone SMC 0x114 (r0 = 3, r1 = 0) -> current GPU XBAR clock idx
 *   3. SceGpuEs4 SysDevicePostPowerState (0x0207D4A8, D3 -> D0) enables the
 *      internal SoC SGX543 power domain via ScePervasiveForDriver::0x84EAB5BC
 *      (SceLowio 0x00C083F4):
 *        TrustZone SMC 0x110 (r0 = 0, r1 = 1) -> power ON domain 0 (SGX543)
 *      And SysDevicePrePowerState (0x0207D488, D0 -> D3) disables it via
 *      ScePervasiveForDriver::0xE8656ADF (SceLowio 0x00C08400):
 *        TrustZone SMC 0x110 (r0 = 0, r1 = 0) -> power OFF domain 0 (SGX543)
 *   4. ScePower boot (0x018ABB12) & resume (0x018ABCEA) configure the GPU
 *      core/MP clock and unpause the 0xF00 GPU AXI/XBAR Grab bridge
 *      (0x018AB844..0x018AB878):
 *        TrustZone SMC 0x11B (r0 = 0xF00, r1 = 0) -> pause 0xF00 bridge
 *        PERVASIVE_BASECLK + 0x10 (0xE3103010)    = 0x00030003 (111 MHz)
 *        TrustZone SMC 0x11B (r0 = 0xF00, r1 = 1) -> unpause 0xF00 bridge
 *   5. Interactive /proc/vita_sgx_wake commands allow probing 0xE8400000
 *      after module_init completes, so module state is always LIVE and
 *      rmmod always works even if a bus access faults.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/spi/spi.h>
#include <linux/mfd/vita-syscon.h>

#define PROC_NAME			"vita_sgx_wake"

#define SYSCON_CMD_GET_POWER_STATUS	0x0883
#define SYSCON_CMD_CTRL_VOLTAGE		0x088E
/* Exact wire bytes for sceSysconCtrlVoltageForDriver(4, 0x5C): [0x04, 0x30] */
#define STOCK_088E_111MHZ_DATA		0x3004U

#define VITA_SMC_POWER_DOMAIN		0x110U
#define VITA_PD_SGX543			0U
#define VITA_PD_VENEZIA			1U

#define VITA_SMC_CLOCK_CTRL		0x114U
#define VITA_CLK_GET_BUS		1U
#define VITA_CLK_SET_GPU_XBAR		2U
#define VITA_CLK_GET_GPU_XBAR		3U
#define STOCK_GPU_XBAR_111MHZ		2U

#define VITA_SMC_GRAB_BRIDGE		0x11BU
#define VITA_GRAB_GPU_MASK		0x0F00U

#define PERVASIVE_MISC_PA		0xE3100000UL
#define PERVASIVE_RESET_PA		0xE3101000UL
#define PERVASIVE_GATE_PA		0xE3102000UL
#define PERVASIVE_BASECLK_PA		0xE3103000UL
#define PERVASIVE_GPU_OFF		0x0010UL

#define STOCK_GPU_BASECLK		0x00030003U
#define STOCK_GPU_GATE			0x000F000FU
#define STOCK_GPU_RESET_MASK		0x00000001U

#define GPU_REGBUS_PA			0xED800000UL
#define SGX543_REGS_PA			0xE8400000UL
#define SGX543_REGS_SIZE		0x00020000UL

#define STOCK_EUR_CR_CLKGATECTL		0x002AAAAAU
#define STOCK_EUR_CR_CLKGATECTL2	0x0A8A8AAAU

static bool enable_vddg = true;
module_param(enable_vddg, bool, 0444);
MODULE_PARM_DESC(enable_vddg, "Set stock 111 MHz GPU voltage via Syscon 0x088E [04 30] (default: true)");

static bool enable_xbar = true;
module_param(enable_xbar, bool, 0444);
MODULE_PARM_DESC(enable_xbar, "Set stock 111 MHz GPU XBAR clock via SMC 0x114(2, 2) (default: true)");

static bool enable_smc_power = true;
module_param(enable_smc_power, bool, 0444);
MODULE_PARM_DESC(enable_smc_power, "Enable SGX543 power domain via SMC 0x110(0, 1) (default: true)");

static bool enable_grab = true;
module_param(enable_grab, bool, 0444);
MODULE_PARM_DESC(enable_grab, "Run stock SceGrab 0xF00 unpause + Pervasive clock/gate sequence (default: true)");

static bool read_regbus = true;
module_param(read_regbus, bool, 0444);
MODULE_PARM_DESC(read_regbus, "Dump SonicsSX GPU_REGBUS registers at 0xED800000 (default: true)");

static bool read_sgx = false;
module_param(read_sgx, bool, 0444);
MODULE_PARM_DESC(read_sgx, "Read SGX543 0xE8400020 during module_init (default: false; use /proc/vita_sgx_wake)");

static bool gate_off_on_exit = true;
module_param(gate_off_on_exit, bool, 0444);
MODULE_PARM_DESC(gate_off_on_exit, "Restore Pervasive GPU gate/reset on rmmod (default: true)");

static bool power_off_on_exit = true;
module_param(power_off_on_exit, bool, 0444);
MODULE_PARM_DESC(power_off_on_exit, "Disable SGX543 power domain via SMC 0x110(0, 0) on rmmod (default: true)");

static u32 saved_gate;
static u32 saved_reset;
static bool pervasive_touched;
static bool smc_power_enabled;
static bool dom1_enabled;
static struct proc_dir_entry *sgx_proc;

static u32 vita_smc(u32 cmd, u32 arg0, u32 arg1, u32 arg2, u32 arg3)
{
	register u32 r0 asm("r0") = arg0;
	register u32 r1 asm("r1") = arg1;
	register u32 r2 asm("r2") = arg2;
	register u32 r3 asm("r3") = arg3;
	register u32 r12 asm("r12") = cmd;

	asm volatile(
		".arch_extension sec\n\t"
		"smc #0\n\t"
		: "+r" (r0), "+r" (r1), "+r" (r2), "+r" (r3), "+r" (r12)
		:
		: "cc", "memory"
	);

	return r0;
}

static struct vita_syscon *get_vita_syscon(void)
{
	struct device_node *np;
	struct device *dev;
	struct vita_syscon *syscon;

	np = of_find_compatible_node(NULL, NULL, "vita,syscon");
	if (!np)
		return NULL;

	dev = bus_find_device_by_of_node(&spi_bus_type, np);
	of_node_put(np);
	if (!dev)
		return NULL;

	syscon = spi_get_drvdata(to_spi_device(dev));
	put_device(dev);
	return syscon;
}

static void log_elmo_status(struct vita_syscon *syscon, const char *tag)
{
	u8 rx[SYSCON_RX_HEADER_SIZE + 16];
	int ret;

	memset(rx, 0, sizeof(rx));
	ret = syscon->command_read(syscon, SYSCON_CMD_GET_POWER_STATUS,
				   rx, SYSCON_RX_HEADER_SIZE + 8);
	pr_info("vita_sgx_wake: [%s] Elmo 0x0883 (ret=%d res=0x%02x): %02x %02x %02x %02x %02x %02x %02x %02x\n",
		tag, ret, rx[SYSCON_RX_RESULT],
		rx[4], rx[5], rx[6], rx[7], rx[8], rx[9], rx[10], rx[11]);
}

static void log_pervasive_state(const char *tag, void __iomem *misc_base,
				void __iomem *rst_base, void __iomem *gate_base,
				void __iomem *clk_base)
{
	u32 bus_clk = vita_smc(VITA_SMC_CLOCK_CTRL, VITA_CLK_GET_BUS, 0, 0, 0);
	u32 xbar_clk = vita_smc(VITA_SMC_CLOCK_CTRL, VITA_CLK_GET_GPU_XBAR, 0, 0, 0);

	pr_info("vita_sgx_wake: [%s] MISC(+30..+3C)=[%08x %08x %08x %08x] CLK=0x%08x GATE=0x%08x RST=0x%08x BUS_SMC=0x%x XBAR_SMC=0x%x\n",
		tag,
		readl(misc_base + 0x0030),
		readl(misc_base + 0x0034),
		readl(misc_base + 0x0038),
		readl(misc_base + 0x003C),
		readl(clk_base + PERVASIVE_GPU_OFF),
		readl(gate_base + PERVASIVE_GPU_OFF),
		readl(rst_base + PERVASIVE_GPU_OFF),
		bus_clk, xbar_clk);
}

static void log_regbus_state(const char *tag)
{
	void __iomem *regbus;

	if (!read_regbus || !smc_power_enabled)
		return;

	regbus = ioremap(GPU_REGBUS_PA, 0x1000);
	if (!regbus)
		return;

	pr_info("vita_sgx_wake: [%s] GPU_REGBUS +00=%08x +04=%08x +18=%08x +1C=%08x +20=%08x +28=%08x +58=%08x\n",
		tag,
		readl(regbus + 0x0000),
		readl(regbus + 0x0004),
		readl(regbus + 0x0018),
		readl(regbus + 0x001C),
		readl(regbus + 0x0020),
		readl(regbus + 0x0028),
		readl(regbus + 0x0058));
	iounmap(regbus);
}

static int send_syscon_checked(struct vita_syscon *syscon, u16 cmd, u32 data, u8 tx_len)
{
	u8 tx[16];
	u8 rx[32];
	int ret;

	memset(tx, 0, sizeof(tx));
	memset(rx, 0, sizeof(rx));

	tx[SYSCON_TX_CMD_LO] = cmd & 0xFF;
	tx[SYSCON_TX_CMD_HI] = (cmd >> 8) & 0xFF;
	tx[SYSCON_TX_LENGTH] = tx_len;
	tx[SYSCON_TX_DATA(0)] = data & 0xFF;
	tx[SYSCON_TX_DATA(1)] = (data >> 8) & 0xFF;
	tx[SYSCON_TX_DATA(2)] = (data >> 16) & 0xFF;
	tx[SYSCON_TX_DATA(3)] = (data >> 24) & 0xFF;

	ret = syscon->transfer(syscon, tx, rx, sizeof(rx));
	pr_info("vita_sgx_wake: cmd=0x%04x tx=[%02x %02x %02x | %02x %02x %02x] -> ret=%d rx=[%02x %02x len=%02x res=%02x | %02x %02x]\n",
		cmd, tx[0], tx[1], tx[2], tx[3], tx[4], tx[5],
		ret, rx[0], rx[1], rx[2], rx[3], rx[4], rx[5]);
	return ret;
}

static void probe_sgx_id(void)
{
	void __iomem *sgx;

	pr_info("vita_sgx_wake: Probing SGX543 EUR_CR_CORE_ID at 0x%08lx+0x0020...\n",
		SGX543_REGS_PA);
	sgx = ioremap(SGX543_REGS_PA, SGX543_REGS_SIZE);
	if (!sgx) {
		pr_err("vita_sgx_wake: ioremap(0x%08lx) failed\n", SGX543_REGS_PA);
		return;
	}

	pr_info("vita_sgx_wake: EUR_CR_CORE_ID       (+0x0020) = 0x%08x (expected 0x01174403)\n",
		readl(sgx + 0x0020));
	pr_info("vita_sgx_wake: EUR_CR_CORE_REVISION (+0x0024) = 0x%08x (expected 0x00020106)\n",
		readl(sgx + 0x0024));
	pr_info("vita_sgx_wake: EUR_CR_CLKGATECTL    (+0x0000) = 0x%08x / 0x%08x, MP(+0x0100)=0x%08x\n",
		readl(sgx + 0x0000), readl(sgx + 0x0004),
		readl(sgx + 0x0100));
	iounmap(sgx);
}

static void probe_sgx_master(void)
{
	void __iomem *sgx;

	pr_info("vita_sgx_wake: Probing SGX543 Hydra Master bank at 0x%08lx+0x4000...\n",
		SGX543_REGS_PA);
	sgx = ioremap(SGX543_REGS_PA, SGX543_REGS_SIZE);
	if (!sgx)
		return;

	pr_info("vita_sgx_wake: MASTER +0x4000 = 0x%08x, +0x4080 = 0x%08x\n",
		readl(sgx + 0x4000), readl(sgx + 0x4080));
	iounmap(sgx);
}

static void probe_sgx_init_and_id(void)
{
	void __iomem *sgx;

	pr_info("vita_sgx_wake: Running stock gpuinit_es4 SGXStart + SceGpuEs4 SGXReset at 0x%08lx...\n",
		SGX543_REGS_PA);
	sgx = ioremap(SGX543_REGS_PA, SGX543_REGS_SIZE);
	if (!sgx)
		return;

	/*
	 * 1. gpuinit_es4 SGXStart: enable 4 Hydra MP cores and Master clock
	 *    gating BEFORE any SGX register read (reading 0xE8400020 while
	 *    Master clock gates are off aborts/hangs the bus).
	 */
	writel(0x00000003U, sgx + 0x4000); /* EUR_CR_MASTER_CORE = 3 (4 cores) */
	writel(0x000002A9U, sgx + 0x4020); /* EUR_CR_MASTER_CLKGATECTL2 */
	writel(0x000000AAU, sgx + 0x4004); /* EUR_CR_MASTER_CLKGATECTL */

	/*
	 * 2. SceGpuEs4 0x020765B8 / 0x0207659A SGXReset: pulse Master soft
	 *    reset, configure BIF & SLC while in reset, then enable core
	 *    clock gating.
	 */
	writel(0x000007FFU, sgx + 0x4080); /* EUR_CR_MASTER_SOFT_RESET */
	wmb();
	udelay(100);
	writel(0x00000000U, sgx + 0x4C00); /* EUR_CR_MASTER_BIF_CTRL */
	writel(0x0004C800U, sgx + 0x4D00); /* EUR_CR_MASTER_SLC_CTRL */
	writel(0x04000000U, sgx + 0x4D04); /* EUR_CR_MASTER_SLC_CTRL_BYPASS */
	wmb();
	udelay(100);
	writel(0x00000000U, sgx + 0x4080); /* Release EUR_CR_MASTER_SOFT_RESET */
	wmb();
	udelay(100);
	writel(STOCK_EUR_CR_CLKGATECTL, sgx + 0x0000);
	writel(STOCK_EUR_CR_CLKGATECTL2, sgx + 0x0004);
	wmb();
	udelay(100);

	pr_info("vita_sgx_wake: EUR_CR_CORE_ID       (+0x0020) = 0x%08x (expected 0x01174403)\n",
		readl(sgx + 0x0020));
	pr_info("vita_sgx_wake: EUR_CR_CORE_REVISION (+0x0024) = 0x%08x (expected 0x00020106)\n",
		readl(sgx + 0x0024));
	pr_info("vita_sgx_wake: EUR_CR_CLKGATECTL    (+0x0000) = 0x%08x / 0x%08x, MP(+0x0100)=0x%08x, MASTER(+0x4000)=0x%08x\n",
		readl(sgx + 0x0000), readl(sgx + 0x0004),
		readl(sgx + 0x0100), readl(sgx + 0x4000));
	iounmap(sgx);
}

static ssize_t sgx_proc_write(struct file *file, const char __user *ubuf,
			      size_t count, loff_t *ppos)
{
	char cmd[32];
	size_t len = min(count, sizeof(cmd) - 1);

	if (copy_from_user(cmd, ubuf, len))
		return -EFAULT;
	cmd[len] = '\0';
	strim(cmd);

	if (strcmp(cmd, "status") == 0) {
		void __iomem *misc = ioremap(PERVASIVE_MISC_PA, 0x1000);
		void __iomem *rst  = ioremap(PERVASIVE_RESET_PA, 0x1000);
		void __iomem *gate = ioremap(PERVASIVE_GATE_PA, 0x1000);
		void __iomem *clk  = ioremap(PERVASIVE_BASECLK_PA, 0x1000);

		if (misc && rst && gate && clk)
			log_pervasive_state("status", misc, rst, gate, clk);
		if (clk)
			iounmap(clk);
		if (gate)
			iounmap(gate);
		if (rst)
			iounmap(rst);
		if (misc)
			iounmap(misc);
		log_regbus_state("status");
	} else if (strcmp(cmd, "read_id") == 0) {
		probe_sgx_id();
	} else if (strcmp(cmd, "read_master") == 0) {
		probe_sgx_master();
	} else if (strcmp(cmd, "init_sgx") == 0) {
		probe_sgx_init_and_id();
	} else if (strcmp(cmd, "grab_reopen") == 0) {
		u32 r0 = vita_smc(VITA_SMC_GRAB_BRIDGE, VITA_GRAB_GPU_MASK, 0, 0, 0);
		u32 r1;

		udelay(50);
		r1 = vita_smc(VITA_SMC_GRAB_BRIDGE, VITA_GRAB_GPU_MASK, 1, 0, 0);
		pr_info("vita_sgx_wake: SMC 0x11B(0xF00, 0->1) ret=[0x%08x, 0x%08x]\n",
			r0, r1);
		log_regbus_state("after_grab_reopen");
	} else if (strcmp(cmd, "dom1_on") == 0) {
		u32 ret = vita_smc(VITA_SMC_POWER_DOMAIN, VITA_PD_VENEZIA, 1, 0, 0);

		msleep(10);
		pr_info("vita_sgx_wake: SMC 0x110(domain=1, state=1) -> ret=0x%08x\n", ret);
		if ((s32)ret >= 0)
			dom1_enabled = true;
	} else if (strcmp(cmd, "dom1_off") == 0) {
		u32 ret = vita_smc(VITA_SMC_POWER_DOMAIN, VITA_PD_VENEZIA, 0, 0, 0);

		pr_info("vita_sgx_wake: SMC 0x110(domain=1, state=0) -> ret=0x%08x\n", ret);
		dom1_enabled = false;
	} else {
		pr_warn("vita_sgx_wake: unknown cmd '%s' (valid: status, read_id, read_master, init_sgx, grab_reopen, dom1_on, dom1_off)\n",
			cmd);
		return -EINVAL;
	}

	return count;
}

static const struct proc_ops sgx_proc_ops = {
	.proc_write = sgx_proc_write,
};

static int __init vita_sgx_wake_init(void)
{
	struct vita_syscon *syscon = get_vita_syscon();
	void __iomem *misc_base;
	void __iomem *rst_base;
	void __iomem *gate_base;
	void __iomem *clk_base;
	u32 smc_ret = 0;
	u32 cur_gate, cur_rst;

	if (!syscon || !syscon->command_read || !syscon->transfer) {
		pr_err("vita_sgx_wake: vita-syscon not available\n");
		return -ENODEV;
	}

	misc_base = ioremap(PERVASIVE_MISC_PA, 0x1000);
	rst_base  = ioremap(PERVASIVE_RESET_PA, 0x1000);
	gate_base = ioremap(PERVASIVE_GATE_PA, 0x1000);
	clk_base  = ioremap(PERVASIVE_BASECLK_PA, 0x1000);
	if (!misc_base || !rst_base || !gate_base || !clk_base) {
		if (clk_base)
			iounmap(clk_base);
		if (gate_base)
			iounmap(gate_base);
		if (rst_base)
			iounmap(rst_base);
		if (misc_base)
			iounmap(misc_base);
		return -ENOMEM;
	}

	saved_gate  = readl(gate_base + PERVASIVE_GPU_OFF);
	saved_reset = readl(rst_base + PERVASIVE_GPU_OFF);

	log_elmo_status(syscon, "before");
	log_pervasive_state("before", misc_base, rst_base, gate_base, clk_base);

	/* Step 1: Set stock 111 MHz GPU rail voltage (ScePower 0x018ABBC2) */
	if (enable_vddg) {
		send_syscon_checked(syscon, SYSCON_CMD_CTRL_VOLTAGE,
				    STOCK_088E_111MHZ_DATA, 3);
		msleep(10);
	}

	/* Step 2: Set stock 111 MHz GPU XBAR clock via SMC 0x114(2, 2) */
	if (enable_xbar) {
		u32 xbar_ret = vita_smc(VITA_SMC_CLOCK_CTRL,
					VITA_CLK_SET_GPU_XBAR,
					STOCK_GPU_XBAR_111MHZ, 0, 0);
		u32 xbar_now;

		udelay(200);
		xbar_now = vita_smc(VITA_SMC_CLOCK_CTRL,
				    VITA_CLK_GET_GPU_XBAR, 0, 0, 0);
		pr_info("vita_sgx_wake: SMC 0x114(2, 2) -> ret=0x%08x, XBAR_SMC readback=0x%x\n",
			xbar_ret, xbar_now);
	}

	/* Step 3: Enable SGX543 power domain via TrustZone SMC 0x110(0, 1) */
	if (enable_smc_power) {
		smc_ret = vita_smc(VITA_SMC_POWER_DOMAIN, VITA_PD_SGX543, 1, 0, 0);
		msleep(20);
		pr_info("vita_sgx_wake: SMC 0x110(domain=0, state=1) -> ret=0x%08x\n",
			smc_ret);
		if ((s32)smc_ret >= 0)
			smc_power_enabled = true;

		log_pervasive_state("after_smc110", misc_base, rst_base,
				    gate_base, clk_base);
		log_regbus_state("after_smc110");
	}

	/* Step 4: Unpause SceGrab 0xF00 GPU AXI/XBAR bridge & set 111 MHz clock */
	if (enable_grab) {
		u32 g0, g1;

		g0 = vita_smc(VITA_SMC_GRAB_BRIDGE, VITA_GRAB_GPU_MASK, 0, 0, 0);
		udelay(50);

		writel(STOCK_GPU_BASECLK, clk_base + PERVASIVE_GPU_OFF);
		(void)readl(clk_base + PERVASIVE_GPU_OFF);
		udelay(50);

		g1 = vita_smc(VITA_SMC_GRAB_BRIDGE, VITA_GRAB_GPU_MASK, 1, 0, 0);
		udelay(100);
		pr_info("vita_sgx_wake: SMC 0x11B(0xF00, 0->1) -> ret=[0x%08x, 0x%08x]\n",
			g0, g1);

		cur_gate = readl(gate_base + PERVASIVE_GPU_OFF);
		if ((cur_gate & STOCK_GPU_GATE) != STOCK_GPU_GATE) {
			writel(cur_gate | STOCK_GPU_GATE, gate_base + PERVASIVE_GPU_OFF);
			(void)readl(gate_base + PERVASIVE_GPU_OFF);
			udelay(100);
			pervasive_touched = true;
		}

		cur_rst = readl(rst_base + PERVASIVE_GPU_OFF);
		if (cur_rst & STOCK_GPU_RESET_MASK) {
			writel(cur_rst & ~STOCK_GPU_RESET_MASK, rst_base + PERVASIVE_GPU_OFF);
			(void)readl(rst_base + PERVASIVE_GPU_OFF);
			udelay(200);
			pervasive_touched = true;
		}

		log_pervasive_state("after_grab", misc_base, rst_base,
				    gate_base, clk_base);
		log_regbus_state("after_grab");
	}

	cur_gate = readl(gate_base + PERVASIVE_GPU_OFF);
	cur_rst  = readl(rst_base + PERVASIVE_GPU_OFF);

	iounmap(clk_base);
	iounmap(gate_base);
	iounmap(rst_base);
	iounmap(misc_base);

	sgx_proc = proc_create(PROC_NAME, 0200, NULL, &sgx_proc_ops);
	if (sgx_proc)
		pr_info("vita_sgx_wake: ready (/proc/%s: status, read_id, read_master, init_sgx, grab_reopen, dom1_on, dom1_off)\n",
			PROC_NAME);

	if (read_sgx && smc_power_enabled &&
	    (cur_gate & STOCK_GPU_GATE) == STOCK_GPU_GATE &&
	    (cur_rst & STOCK_GPU_RESET_MASK) == 0) {
		msleep(10);
		probe_sgx_id();
	}

	return 0;
}

static void __exit vita_sgx_wake_exit(void)
{
	if (sgx_proc)
		proc_remove(sgx_proc);

	if (dom1_enabled) {
		u32 ret = vita_smc(VITA_SMC_POWER_DOMAIN, VITA_PD_VENEZIA, 0, 0, 0);

		pr_info("vita_sgx_wake: SMC 0x110(domain=1, state=0) -> ret=0x%08x\n", ret);
	}

	if (pervasive_touched && gate_off_on_exit) {
		void __iomem *rst_base  = ioremap(PERVASIVE_RESET_PA, 0x1000);
		void __iomem *gate_base = ioremap(PERVASIVE_GATE_PA, 0x1000);

		if (rst_base && gate_base) {
			writel(saved_reset | STOCK_GPU_RESET_MASK,
			       rst_base + PERVASIVE_GPU_OFF);
			(void)readl(rst_base + PERVASIVE_GPU_OFF);
			udelay(50);
			writel(saved_gate, gate_base + PERVASIVE_GPU_OFF);
			(void)readl(gate_base + PERVASIVE_GPU_OFF);
		}
		if (gate_base)
			iounmap(gate_base);
		if (rst_base)
			iounmap(rst_base);
	}

	if (smc_power_enabled && power_off_on_exit) {
		u32 ret = vita_smc(VITA_SMC_POWER_DOMAIN, VITA_PD_SGX543, 0, 0, 0);

		pr_info("vita_sgx_wake: SMC 0x110(domain=0, state=0) -> ret=0x%08x\n",
			ret);
	}

	pr_info("vita_sgx_wake: unloaded\n");
}

module_init(vita_sgx_wake_init);
module_exit(vita_sgx_wake_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LinuxOnVita");
MODULE_DESCRIPTION("SGX543MP4+ bring-up via TrustZone SMC 0x110/0x114/0x11B and Syscon 0x088E");
