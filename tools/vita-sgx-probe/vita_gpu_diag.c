// SPDX-License-Identifier: GPL-2.0
/*
 * vita_gpu_diag.c - Staged, Verified PowerVR SGX543MP4+ Power & Core ID Bringup
 *
 * All values verified directly from live VitaOS traces on this PS Vita 1000:
 *   1. Elmo PMIC status: Syscon cmd 0x0883 (8 bytes: Elmo 0xA0..0xA3)
 *   2. DD4 / VDDG GPU core rail: sceSysconCtrlVoltageForDriver(type=4, vid=0x5C)
 *      -> Syscon cmd 0x088E, data = 0x04 | (0x5C << 8) = 0x5C04, cmd_len = 3
 *   3. TrustZone SMC 0x114: bus_lvl=6 (222 MHz), gpu_xbar_lvl=3 (111 MHz)
 *   4. Pervasive GPU (+0x10): BASECLK=0x00030003, GATE=0x000F000F, RESET=0x00000000
 *   5. GPU_REGBUS (0xED800000): expected +0x00 = 0x01006437
 *   6. SceSGX543Reg (0xE8400000): expected +0x20 = 0x01174403, +0x24 = 0x00020106
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/spi/spi.h>
#include <linux/mfd/vita-syscon.h>

#define SYSCON_CMD_GET_POWER_STATUS	0x0883
#define SYSCON_CMD_CTRL_VOLTAGE		0x088E
#define SCE_SYSCON_VOLTAGE_DD4		0x04U
#define STOCK_VITA1000_VDDG_111MHZ_VID	0x5CU

#define PERVASIVE_RESET_PA		0xE3101000UL
#define PERVASIVE_GATE_PA		0xE3102000UL
#define PERVASIVE_BASECLK_PA		0xE3103000UL
#define PERVASIVE_GPU_OFF		0x0010UL

#define STOCK_GPU_BASECLK		0x00030003U
#define STOCK_GPU_GATE			0x000F000FU
#define STOCK_GPU_RESET_MASK		0x00000001U

#define GPU_REGBUS_PA			0xED800000UL
#define SGX543_REGS_PA			0xE8400000UL

static bool enable_vddg = false;
module_param(enable_vddg, bool, 0444);
MODULE_PARM_DESC(enable_vddg, "Apply stock VitaOS DD4/VDDG vid=0x5C via Syscon 0x88E (default: false)");

static bool wake_pervasive = false;
module_param(wake_pervasive, bool, 0444);
MODULE_PARM_DESC(wake_pervasive, "Enable Pervasive GPU clock gate and deassert reset (default: false)");

static bool read_regbus = false;
module_param(read_regbus, bool, 0444);
MODULE_PARM_DESC(read_regbus, "Read GPU_REGBUS ID at 0xED800000 (default: false)");

static bool read_sgx = false;
module_param(read_sgx, bool, 0444);
MODULE_PARM_DESC(read_sgx, "Read SGX543 EUR_CR_CORE_ID at 0xE8400020 (default: false)");

static bool gate_off_on_exit = true;
module_param(gate_off_on_exit, bool, 0444);
MODULE_PARM_DESC(gate_off_on_exit, "Restore Pervasive GPU gate/reset on rmmod (default: true)");

static u32 saved_gate;
static u32 saved_reset;
static bool pervasive_touched;

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
	pr_info("vita_gpu_diag: [%s] Elmo 0x883 (ret=%d res=0x%02x): %02x %02x %02x %02x %02x %02x %02x %02x\n",
		tag, ret, rx[SYSCON_RX_RESULT],
		rx[4], rx[5], rx[6], rx[7], rx[8], rx[9], rx[10], rx[11]);
}

static int __init vita_gpu_diag_init(void)
{
	struct vita_syscon *syscon = get_vita_syscon();

	if (!syscon || !syscon->command_read || !syscon->short_command_write) {
		pr_err("vita_gpu_diag: vita-syscon not available\n");
		return -ENODEV;
	}

	log_elmo_status(syscon, "before");

	if (enable_vddg) {
		u32 data = SCE_SYSCON_VOLTAGE_DD4 | (STOCK_VITA1000_VDDG_111MHZ_VID << 8);
		int ret;

		pr_info("vita_gpu_diag: sending stock DD4/VDDG Syscon 0x88E (type=4, vid=0x5c, data=0x%04x)...\n",
			data);
		ret = syscon->short_command_write(syscon, SYSCON_CMD_CTRL_VOLTAGE, data, 3);
		pr_info("vita_gpu_diag: Syscon 0x88E ret=%d\n", ret);
		msleep(30);
		log_elmo_status(syscon, "after_0x88e");
	}

	if (wake_pervasive) {
		void __iomem *rst_base  = ioremap(PERVASIVE_RESET_PA, 0x1000);
		void __iomem *gate_base = ioremap(PERVASIVE_GATE_PA, 0x1000);
		void __iomem *clk_base  = ioremap(PERVASIVE_BASECLK_PA, 0x1000);

		if (!rst_base || !gate_base || !clk_base) {
			if (clk_base)
				iounmap(clk_base);
			if (gate_base)
				iounmap(gate_base);
			if (rst_base)
				iounmap(rst_base);
			return -ENOMEM;
		}

		saved_gate  = readl(gate_base + PERVASIVE_GPU_OFF);
		saved_reset = readl(rst_base + PERVASIVE_GPU_OFF);

		if (readl(clk_base + PERVASIVE_GPU_OFF) != STOCK_GPU_BASECLK) {
			writel(STOCK_GPU_BASECLK, clk_base + PERVASIVE_GPU_OFF);
			(void)readl(clk_base + PERVASIVE_GPU_OFF);
			udelay(50);
		}

		writel(saved_gate | STOCK_GPU_GATE, gate_base + PERVASIVE_GPU_OFF);
		(void)readl(gate_base + PERVASIVE_GPU_OFF);
		udelay(100);

		writel(saved_reset & ~STOCK_GPU_RESET_MASK, rst_base + PERVASIVE_GPU_OFF);
		(void)readl(rst_base + PERVASIVE_GPU_OFF);
		udelay(200);

		pervasive_touched = true;

		pr_info("vita_gpu_diag: Pervasive GPU state: BASECLK=0x%08x GATE=0x%08x RESET=0x%08x\n",
			readl(clk_base + PERVASIVE_GPU_OFF),
			readl(gate_base + PERVASIVE_GPU_OFF),
			readl(rst_base + PERVASIVE_GPU_OFF));

		iounmap(clk_base);
		iounmap(gate_base);
		iounmap(rst_base);
	}

	if (read_regbus) {
		void __iomem *rb;

		pr_info("vita_gpu_diag: reading GPU_REGBUS at 0x%08lx...\n", GPU_REGBUS_PA);
		msleep(30);
		rb = ioremap(GPU_REGBUS_PA, 0x1000);
		if (rb) {
			u32 id  = readl(rb + 0x0000);
			u32 r18 = readl(rb + 0x0018);
			u32 r1c = readl(rb + 0x001C);

			pr_info("vita_gpu_diag: GPU_REGBUS +0x00=0x%08x (expected 0x01006437) +0x18=0x%08x +0x1C=0x%08x\n",
				id, r18, r1c);
			iounmap(rb);
		}
	}

	if (read_sgx) {
		void __iomem *sgx;

		pr_info("vita_gpu_diag: reading SceSGX543Reg at 0x%08lx...\n", SGX543_REGS_PA);
		msleep(30);
		sgx = ioremap(SGX543_REGS_PA, 0x2000);
		if (sgx) {
			u32 core_id  = readl(sgx + 0x0020);
			u32 core_rev = readl(sgx + 0x0024);
			u32 clkgate1 = readl(sgx + 0x0000);
			u32 clkgate2 = readl(sgx + 0x0004);
			u32 mp_cores = readl(sgx + 0x0100);

			pr_info("vita_gpu_diag: EUR_CR_CORE_ID       (+0x0020) = 0x%08x (expected 0x01174403)\n",
				core_id);
			pr_info("vita_gpu_diag: EUR_CR_CORE_REVISION (+0x0024) = 0x%08x (expected 0x00020106)\n",
				core_rev);
			pr_info("vita_gpu_diag: EUR_CR_CLKGATECTL    (+0x0000) = 0x%08x / 0x%08x, MP=0x%08x\n",
				clkgate1, clkgate2, mp_cores);
			iounmap(sgx);
		}
	}

	return 0;
}

static void __exit vita_gpu_diag_exit(void)
{
	if (pervasive_touched && gate_off_on_exit) {
		void __iomem *rst_base  = ioremap(PERVASIVE_RESET_PA, 0x1000);
		void __iomem *gate_base = ioremap(PERVASIVE_GATE_PA, 0x1000);

		if (rst_base && gate_base) {
			writel(saved_reset | STOCK_GPU_RESET_MASK, rst_base + PERVASIVE_GPU_OFF);
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

	pr_info("vita_gpu_diag: unloaded\n");
}

module_init(vita_gpu_diag_init);
module_exit(vita_gpu_diag_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LinuxOnVita");
MODULE_DESCRIPTION("Staged PowerVR SGX543MP4+ DD4/VDDG and Pervasive bringup probe");
