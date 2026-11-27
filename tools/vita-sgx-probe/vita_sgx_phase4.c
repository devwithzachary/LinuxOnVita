// SPDX-License-Identifier: GPL-2.0
/*
 * vita_sgx_phase4.c - Phase 4 SGX543MP4+ IRQ, BIF 2-Level MMU & PTLA 2D Probe
 *
 * Verifies all three Phase 4 hardware milestones on live Linux 6.12:
 *   1. SGX543 hardware interrupt delivery on GIC_SPI 20 via EUR_CR_EVENT_TIMER
 *      and EUR_CR_MASTER_EVENT_STATUS TWOD_COMPLETE, with storm protection.
 *   2. SGX543 2-level BIF MMU (4 KB Page Directory + 4 KB Page Tables) mapping
 *      coherent system DRAM and physical CDRAM (0x22000000) into SGX virtual
 *      address space (0x50000000 and 0x50400000) with zero BIF faults.
 *   3. End-to-end GPU hardware 2D colour fill and strided surface blit via the
 *      Hydra Master PTLA (Present and Texture Load Accelerator) slave port
 *      through the BIF 2-level MMU, synchronized by TWOD_COMPLETE IRQ, plus an
 *      optional live OLED scanout render test (oled_test).
 *
 * Interactive commands via /proc/vita_sgx_phase4:
 *   status     - Dump SGX event, BIF MMU, PTLA, timer, and IRQ counters
 *   irq_test   - Verify EUR_CR_TIMER ticks and trigger GIC_SPI 20 timer IRQ
 *   mmu_test   - Program 2-level BIF PD/PT mappings for DRAM and CDRAM
 *   blit_test  - Run PTLA 2D quadrant fills (DRAM) + blit (DRAM -> CDRAM) + verify
 *   oled_test  - Render a 128x128 PTLA 2D RGBW badge onto live OLED scanout
 *   all        - Run irq_test + mmu_test + blit_test sequentially
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/ktime.h>
#include <linux/spi/spi.h>
#include <linux/mfd/vita-syscon.h>

#define PROC_NAME			"vita_sgx_phase4"

/* Syscon & TrustZone SMC power/clock bring-up constants (verified Phase 1/2) */
#define SYSCON_CMD_CTRL_VOLTAGE		0x088E
#define STOCK_088E_111MHZ_DATA		0x3004U

#define VITA_SMC_POWER_DOMAIN		0x110U
#define VITA_PD_SGX543			0U
#define VITA_SMC_CLOCK_CTRL		0x114U
#define VITA_CLK_SET_GPU_XBAR		2U
#define STOCK_GPU_XBAR_111MHZ		2U
#define VITA_SMC_GRAB_BRIDGE		0x11BU
#define VITA_GRAB_GPU_MASK		0x0F00U

#define PERVASIVE_RESET_PA		0xE3101000UL
#define PERVASIVE_GATE_PA		0xE3102000UL
#define PERVASIVE_BASECLK_PA		0xE3103000UL
#define PERVASIVE_GPU_OFF		0x0010UL

#define STOCK_GPU_BASECLK		0x00030003U
#define STOCK_GPU_GATE			0x000F000FU
#define STOCK_GPU_RESET_MASK		0x00000001U

/* Physical memory & MMIO regions */
#define SGX543_REGS_PA			0xE8400000UL
#define SGX543_REGS_SIZE		0x00020000UL
#define SGX543_GIC_SPI			20U

#define CDRAM_TEST_PA			0x22000000UL
#define CDRAM_TEST_SIZE			0x00040000UL /* 256 KB (64 x 4 KB pages) */

#define IFTU_P0_PA			0xE5020000UL
#define IFTU_CREG0_PA			0xE5022000UL
#define OLED_FB_MAP_SIZE		0x00200000UL /* 2 MB covers 960x544x4 */

/* SGX543 Core & Broadcast registers (+0x0000, Core i at +0x8000 + i*0x4000) */
#define EUR_CR_CLKGATECTL		0x0000U
#define EUR_CR_CLKGATECTL2		0x0004U
#define EUR_CR_POWER			0x001CU
#define EUR_CR_CORE_ID			0x0020U
#define EUR_CR_CORE_REVISION		0x0024U
#define EUR_CR_SOFT_RESET		0x0080U
#define EUR_CR_EVENT_HOST_ENABLE2	0x0110U
#define EUR_CR_EVENT_HOST_CLEAR2	0x0114U
#define EUR_CR_EVENT_STATUS2		0x0118U
#define EUR_CR_EVENT_STATUS		0x012CU
#define EUR_CR_EVENT_HOST_ENABLE	0x0130U
#define EUR_CR_EVENT_HOST_CLEAR		0x0134U
#define EUR_CR_TIMER			0x0144U
#define EUR_CR_EVENT_TIMER		0x0ACCU

#define EUR_CR_BIF_CTRL			0x0C00U
#define EUR_CR_BIF_INT_STAT		0x0C04U
#define EUR_CR_BIF_FAULT		0x0C08U
#define EUR_CR_BIF_CTRL_INVAL		0x0C34U
#define EUR_CR_BIF_DIR_LIST_BASE1	0x0C38U
#define EUR_CR_BIF_DIR_LIST_BASE7	0x0C50U
#define EUR_CR_BIF_BANK_SET		0x0C74U
#define EUR_CR_BIF_BANK0		0x0C78U
#define EUR_CR_BIF_DIR_LIST_BASE0	0x0C84U

#define SGX_CORE_BASE(c)		(0x8000U + (c) * 0x4000U)

#define STOCK_EUR_CR_CLKGATECTL		0x002AAAAAU
#define STOCK_EUR_CR_CLKGATECTL2	0x0A8A8AAAU

/* SGX543 Hydra Master registers (+0x4000) */
#define EUR_CR_MASTER_CORE		0x4000U
#define EUR_CR_MASTER_CLKGATECTL	0x4004U
#define EUR_CR_MASTER_CLKGATECTL2	0x4020U
#define EUR_CR_MASTER_SOFT_RESET	0x4080U
#define EUR_CR_MASTER_EVENT_STATUS	0x412CU
#define EUR_CR_MASTER_EVENT_HOST_ENABLE	0x4130U
#define EUR_CR_MASTER_EVENT_HOST_CLEAR	0x4134U

#define EUR_CR_MASTER_PTLA_STATUS	0x4178U
#define EUR_CR_MASTER_PTLA_SLAVE_CMD_0	0x4180U
#define PTLA_STATUS_BUSY_MASK		0x00000001U
#define PTLA_STATUS_FIFO_SHIFT		1U
#define PTLA_STATUS_FIFO_MASK		0x0000007EU

#define EUR_CR_MASTER_BIF_CTRL		0x4C00U
#define EUR_CR_MASTER_BIF_INT_STAT	0x4C04U
#define EUR_CR_MASTER_BIF_FAULT		0x4C08U
#define EUR_CR_MASTER_BIF_CTRL_INVAL	0x4C34U
#define EUR_CR_MASTER_BIF_DIR_LIST_BASE1 0x4C38U
#define EUR_CR_MASTER_BIF_BANK_SET	0x4C74U
#define EUR_CR_MASTER_BIF_BANK0		0x4C78U
#define EUR_CR_MASTER_BIF_DIR_LIST_BASE0 0x4C84U
#define EUR_CR_MASTER_BIF_MEM_REQ_STAT	0x4CA8U

#define EUR_CR_MASTER_SLC_CTRL		0x4D00U
#define EUR_CR_MASTER_SLC_CTRL_BYPASS	0x4D04U
#define EUR_CR_MASTER_SLC_EVENT_STATUS	0x4D10U
#define EUR_CR_MASTER_SLC_EVENT_CLEAR	0x4D14U
#define EUR_CR_MASTER_SLC_CTRL_FLUSH_INV 0x4D34U

#define SLC_FLUSH_INV_ALL_MASK		0x00000010U
#define SLC_EVENT_FLUSH_INV_MASK	0x00000004U

/* Event status/enable/clear bit masks (shared layout on Core & Master) */
#define SGX_EVT_MASTER_INTERRUPT	0x80000000U
#define SGX_EVT_TIMER			0x20000000U
#define SGX_EVT_TA_DPM_FAULT		0x10000000U
#define SGX_EVT_TWOD_COMPLETE		0x08000000U
#define SGX_EVT_SW_EVENT		0x00004000U

#define SGX_EVENT_TIMER_ENABLE		0x01000000U

/* SGX 32-bit 2-level BIF MMU definitions (4 KB PD, 4 KB PT, 4 KB Data Pages) */
#define SGX_MMU_PAGE_SHIFT		12U
#define SGX_MMU_PAGE_SIZE		4096U
#define SGX_MMU_PD_ENTRIES		1024U
#define SGX_MMU_PT_ENTRIES		1024U
#define SGX_MMU_PDE_VALID		0x00000001U
#define SGX_MMU_PDE_PAGE_SIZE_4K	0x00000000U
#define SGX_MMU_PTE_VALID		0x00000001U
#define SGX_MMU_PTE_CACHECONSISTENT	0x00000008U

#define BIF_CTRL_INVAL_ALL		0x00000008U
#define BIF_INT_STAT_FAULT_REQ_MASK	0x00003FFFU

/* SGX Virtual Addresses used for Phase 4 MMU + PTLA verification */
#define SGX_VA_DRAM_SURF		0x50000000U /* PD[0x140] -> coherent DRAM */
#define SGX_VA_CDRAM_SURF		0x50400000U /* PD[0x141] -> CDRAM 0x22000000 */
#define SGX_VA_OLED_FB			0x50800000U /* PD[0x142] -> active OLED FB */

#define TEST_SURF_W			128U
#define TEST_SURF_H			128U
#define TEST_SURF_STRIDE		(TEST_SURF_W * 4U)
#define TEST_SURF_BYTES			(TEST_SURF_STRIDE * TEST_SURF_H) /* 64 KB */

/* PTLA 2D Hardware Command Stream definitions (sgxptladefs.h) */
#define EURASIA_PTLA_SRCOFF_BH		0x10000000U
#define EURASIA_PTLA_SRC_SURF_BH	0x40000000U
#define EURASIA_PTLA_DST_SURF_BH	0x50000000U
#define EURASIA_PTLA_BLIT_BH		0x60000000U
#define EURASIA_PTLA_FENCE_BH		0x70000000U
#define EURASIA_PTLA_FLUSH_BH		0x80000000U

#define EURASIA_PTLA_FORMAT_8888ARGB	0x06U
#define EURASIA_PTLA_SURF_FORMAT_SHIFT	16U
#define EURASIA_PTLA_FILL_ENABLE	(1U << 4)
#define PTLA_XY(x, y)			((((u32)(x) & 0x1FFFU) << 13) | ((u32)(y) & 0x1FFFU))
#define PTLA_SURF_8888(bh, stride) \
	((bh) | (EURASIA_PTLA_FORMAT_8888ARGB << EURASIA_PTLA_SURF_FORMAT_SHIFT) | \
	 ((u32)(stride) & 0xFFFFU))

/* Module state */
static u32 saved_gate;
static u32 saved_reset;
static bool pervasive_touched;
static bool smc_power_enabled;

static void __iomem *sgx_base;
static void __iomem *cdram_va;
static struct platform_device *dma_pdev;
static struct proc_dir_entry *proc_ent;

static int sgx_virq = -1;
static DECLARE_WAIT_QUEUE_HEAD(sgx_irq_wq);
static atomic_t total_irqs = ATOMIC_INIT(0);
static atomic_t timer_irqs = ATOMIC_INIT(0);
static atomic_t twod_irqs = ATOMIC_INIT(0);
static atomic_t storm_tripped = ATOMIC_INIT(0);
static u32 last_master_stat;
static u32 last_core_stat;

/* BIF 2-level MMU tables & coherent DRAM test surface */
static bool mmu_ready;
static u32 *pd_cpu;
static dma_addr_t pd_dma;
static u32 *pt_dram_cpu;
static dma_addr_t pt_dram_dma;
static u32 *pt_cdram_cpu;
static dma_addr_t pt_cdram_dma;
static u32 *pt_fb_cpu;
static dma_addr_t pt_fb_dma;
static u32 *dram_surf_cpu;
static dma_addr_t dram_surf_dma;

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

static int send_syscon_voltage(struct vita_syscon *syscon, u16 cmd, u32 data, u8 tx_len)
{
	u8 tx[16];
	u8 rx[32];

	memset(tx, 0, sizeof(tx));
	memset(rx, 0, sizeof(rx));

	tx[SYSCON_TX_CMD_LO] = cmd & 0xFF;
	tx[SYSCON_TX_CMD_HI] = (cmd >> 8) & 0xFF;
	tx[SYSCON_TX_LENGTH] = tx_len;
	tx[SYSCON_TX_DATA(0)] = data & 0xFF;
	tx[SYSCON_TX_DATA(1)] = (data >> 8) & 0xFF;

	return syscon->transfer(syscon, tx, rx, sizeof(rx));
}

static void sgx_mask_and_clear_all_irqs(void)
{
	unsigned int c;

	if (!sgx_base)
		return;

	writel(0, sgx_base + EUR_CR_EVENT_TIMER);
	writel(0, sgx_base + EUR_CR_MASTER_EVENT_HOST_ENABLE);
	writel(0, sgx_base + EUR_CR_EVENT_HOST_ENABLE);
	writel(0, sgx_base + EUR_CR_EVENT_HOST_ENABLE2);

	for (c = 0; c < 4; c++) {
		u32 base = SGX_CORE_BASE(c);

		writel(0, sgx_base + base + EUR_CR_EVENT_TIMER);
		writel(0, sgx_base + base + EUR_CR_EVENT_HOST_ENABLE);
		writel(0, sgx_base + base + EUR_CR_EVENT_HOST_ENABLE2);
		writel(0xFFFFFFFFU, sgx_base + base + EUR_CR_EVENT_HOST_CLEAR);
		writel(0xFFFFFFFFU, sgx_base + base + EUR_CR_EVENT_HOST_CLEAR2);
	}

	writel(0xFFFFFFFFU, sgx_base + EUR_CR_MASTER_EVENT_HOST_CLEAR);
	writel(0xFFFFFFFFU, sgx_base + EUR_CR_EVENT_HOST_CLEAR);
	writel(0xFFFFFFFFU, sgx_base + EUR_CR_EVENT_HOST_CLEAR2);
	(void)readl(sgx_base + EUR_CR_MASTER_EVENT_STATUS);
}

static irqreturn_t vita_sgx_isr(int irq, void *dev_id)
{
	u32 m_stat, m_en, m_pend;
	u32 c_stat, c_en, c_pend;
	u32 c0_stat, c0_en, c0_pend;
	int count;

	if (!sgx_base)
		return IRQ_NONE;

	m_stat  = readl(sgx_base + EUR_CR_MASTER_EVENT_STATUS);
	m_en    = readl(sgx_base + EUR_CR_MASTER_EVENT_HOST_ENABLE);
	c_stat  = readl(sgx_base + EUR_CR_EVENT_STATUS);
	c_en    = readl(sgx_base + EUR_CR_EVENT_HOST_ENABLE);
	c0_stat = readl(sgx_base + SGX_CORE_BASE(0) + EUR_CR_EVENT_STATUS);
	c0_en   = readl(sgx_base + SGX_CORE_BASE(0) + EUR_CR_EVENT_HOST_ENABLE);

	m_pend  = m_stat & m_en;
	c_pend  = c_stat & c_en;
	c0_pend = c0_stat & c0_en;

	last_master_stat = m_stat;
	last_core_stat   = c_stat | c0_stat;
	count = atomic_inc_return(&total_irqs);

	/* Storm protection: disarm immediately if IRQ line stays asserted */
	if (count > 500 && ((m_pend | c_pend | c0_pend) == 0 || count > 2000)) {
		sgx_mask_and_clear_all_irqs();
		atomic_set(&storm_tripped, 1);
		wake_up_all(&sgx_irq_wq);
		return IRQ_HANDLED;
	}

	if ((m_pend | c_pend | c0_pend) & SGX_EVT_TIMER) {
		/* Disarm periodic event timer after first delivery */
		writel(0, sgx_base + EUR_CR_EVENT_TIMER);
		writel(0, sgx_base + SGX_CORE_BASE(0) + EUR_CR_EVENT_TIMER);
		writel(m_en & ~SGX_EVT_TIMER, sgx_base + EUR_CR_MASTER_EVENT_HOST_ENABLE);
		writel(c_en & ~SGX_EVT_TIMER, sgx_base + EUR_CR_EVENT_HOST_ENABLE);
		writel(c0_en & ~SGX_EVT_TIMER,
		       sgx_base + SGX_CORE_BASE(0) + EUR_CR_EVENT_HOST_ENABLE);
		atomic_inc(&timer_irqs);
	}

	if ((m_pend | c_pend | c0_pend) & SGX_EVT_TWOD_COMPLETE)
		atomic_inc(&twod_irqs);

	if (m_stat)
		writel(m_stat | SGX_EVT_MASTER_INTERRUPT,
		       sgx_base + EUR_CR_MASTER_EVENT_HOST_CLEAR);
	if (c_stat)
		writel(c_stat | SGX_EVT_MASTER_INTERRUPT,
		       sgx_base + EUR_CR_EVENT_HOST_CLEAR);
	if (c0_stat)
		writel(c0_stat | SGX_EVT_MASTER_INTERRUPT,
		       sgx_base + SGX_CORE_BASE(0) + EUR_CR_EVENT_HOST_CLEAR);

	(void)readl(sgx_base + EUR_CR_MASTER_EVENT_STATUS);
	wake_up_all(&sgx_irq_wq);
	return IRQ_HANDLED;
}

static void sgx_hw_start_and_reset(void)
{
	u32 reg;

	/* 1. gpuinit_es4 SGXStart: enable 4 Hydra MP cores + Master clock gates */
	writel(0x00000003U, sgx_base + EUR_CR_MASTER_CORE);
	writel(0x000002A9U, sgx_base + EUR_CR_MASTER_CLKGATECTL2);
	writel(0x000000AAU, sgx_base + EUR_CR_MASTER_CLKGATECTL);
	wmb();
	udelay(50);

	/* 2. SceGpuEs4 SGXReset (0x020765B8): pulse Master soft reset */
	writel(0x000007FFU, sgx_base + EUR_CR_MASTER_SOFT_RESET);
	wmb();
	udelay(100);

	writel(0x00000000U, sgx_base + EUR_CR_MASTER_BIF_CTRL);
	writel(0x0004C800U, sgx_base + EUR_CR_MASTER_SLC_CTRL);
	writel(0x04000000U, sgx_base + EUR_CR_MASTER_SLC_CTRL_BYPASS);
	wmb();
	udelay(100);

	writel(0x00000000U, sgx_base + EUR_CR_MASTER_SOFT_RESET);
	wmb();
	udelay(100);

	/* 3. Enable Slave Core clock gates and both USSE pipes per core */
	writel(STOCK_EUR_CR_CLKGATECTL, sgx_base + EUR_CR_CLKGATECTL);
	writel(STOCK_EUR_CR_CLKGATECTL2, sgx_base + EUR_CR_CLKGATECTL2);
	wmb();
	udelay(100);

	writel(0x00000000U, sgx_base + EUR_CR_BIF_CTRL);
	writel(0x00000000U, sgx_base + EUR_CR_BIF_BANK_SET);
	writel(0x00000000U, sgx_base + EUR_CR_BIF_BANK0);
	writel(0x00000000U, sgx_base + EUR_CR_BIF_DIR_LIST_BASE0);
	for (reg = EUR_CR_BIF_DIR_LIST_BASE1; reg <= EUR_CR_BIF_DIR_LIST_BASE7; reg += 4)
		writel(0x00000000U, sgx_base + reg);

	writel(0x00000000U, sgx_base + EUR_CR_MASTER_BIF_BANK_SET);
	writel(0x00000000U, sgx_base + EUR_CR_MASTER_BIF_BANK0);
	writel(0x00000000U, sgx_base + EUR_CR_MASTER_BIF_DIR_LIST_BASE0);
	for (reg = EUR_CR_MASTER_BIF_DIR_LIST_BASE1; reg <= 0x4C50U; reg += 4)
		writel(0x00000000U, sgx_base + reg);

	writel(0x00000000U, sgx_base + EUR_CR_POWER);
	wmb();
	udelay(50);

	sgx_mask_and_clear_all_irqs();
}

static void sgx_slc_flush_inv(void)
{
	int i;

	writel(SLC_EVENT_FLUSH_INV_MASK, sgx_base + EUR_CR_MASTER_SLC_EVENT_CLEAR);
	wmb();
	writel(SLC_FLUSH_INV_ALL_MASK, sgx_base + EUR_CR_MASTER_SLC_CTRL_FLUSH_INV);
	wmb();

	for (i = 0; i < 1000; i++) {
		if (readl(sgx_base + EUR_CR_MASTER_SLC_EVENT_STATUS) & SLC_EVENT_FLUSH_INV_MASK)
			break;
		udelay(5);
	}
	writel(SLC_EVENT_FLUSH_INV_MASK, sgx_base + EUR_CR_MASTER_SLC_EVENT_CLEAR);
	(void)readl(sgx_base + EUR_CR_MASTER_SLC_EVENT_STATUS);
}

static void sgx_bif_invalidate_tlb(void)
{
	int i;

	wmb();
	writel(BIF_CTRL_INVAL_ALL, sgx_base + EUR_CR_MASTER_BIF_CTRL_INVAL);
	writel(BIF_CTRL_INVAL_ALL, sgx_base + EUR_CR_BIF_CTRL_INVAL);
	wmb();
	udelay(50);

	for (i = 0; i < 200; i++) {
		if ((readl(sgx_base + EUR_CR_MASTER_BIF_MEM_REQ_STAT) & 0xFFU) == 0)
			break;
		udelay(5);
	}
}

/* Step 4A: Verify EUR_CR_TIMER ticks & GIC_SPI 20 hardware interrupt delivery */
static int run_irq_test(void)
{
	u32 t0, t1, c0_t0, c0_t1;
	int before_timer, before_total;
	ktime_t start_kt;
	long waited;
	s64 elapsed_us;

	t0    = readl(sgx_base + EUR_CR_TIMER);
	c0_t0 = readl(sgx_base + SGX_CORE_BASE(0) + EUR_CR_TIMER);
	udelay(1000);
	t1    = readl(sgx_base + EUR_CR_TIMER);
	c0_t1 = readl(sgx_base + SGX_CORE_BASE(0) + EUR_CR_TIMER);

	pr_info("vita_sgx_phase4: [irq_test] EUR_CR_TIMER 1ms delta: bcast=0x%08x->0x%08x (+%u) core0=0x%08x->0x%08x (+%u)\n",
		t0, t1, t1 - t0, c0_t0, c0_t1, c0_t1 - c0_t0);

	sgx_mask_and_clear_all_irqs();
	atomic_set(&storm_tripped, 0);
	before_timer = atomic_read(&timer_irqs);
	before_total = atomic_read(&total_irqs);

	/* Enable TIMER + MASTER_INTERRUPT on Master and Core 0, then arm EVENT_TIMER */
	writel(SGX_EVT_MASTER_INTERRUPT | SGX_EVT_TIMER,
	       sgx_base + EUR_CR_MASTER_EVENT_HOST_ENABLE);
	writel(SGX_EVT_MASTER_INTERRUPT | SGX_EVT_TIMER,
	       sgx_base + EUR_CR_EVENT_HOST_ENABLE);
	writel(SGX_EVT_MASTER_INTERRUPT | SGX_EVT_TIMER,
	       sgx_base + SGX_CORE_BASE(0) + EUR_CR_EVENT_HOST_ENABLE);
	wmb();

	start_kt = ktime_get();
	writel(SGX_EVENT_TIMER_ENABLE | 0x1000U, sgx_base + EUR_CR_EVENT_TIMER);
	writel(SGX_EVENT_TIMER_ENABLE | 0x1000U,
	       sgx_base + SGX_CORE_BASE(0) + EUR_CR_EVENT_TIMER);
	wmb();

	waited = wait_event_timeout(sgx_irq_wq,
				    atomic_read(&timer_irqs) > before_timer ||
				    atomic_read(&total_irqs) > before_total,
				    msecs_to_jiffies(200));
	elapsed_us = ktime_us_delta(ktime_get(), start_kt);

	sgx_mask_and_clear_all_irqs();

	if (waited == 0 && atomic_read(&total_irqs) == before_total) {
		pr_err("vita_sgx_phase4: [irq_test] TIMEOUT waiting for GIC_SPI 20 (virq=%d), m_stat=0x%08x c_stat=0x%08x c0_stat=0x%08x\n",
		       sgx_virq,
		       readl(sgx_base + EUR_CR_MASTER_EVENT_STATUS),
		       readl(sgx_base + EUR_CR_EVENT_STATUS),
		       readl(sgx_base + SGX_CORE_BASE(0) + EUR_CR_EVENT_STATUS));
		return -ETIMEDOUT;
	}

	pr_info("vita_sgx_phase4: [irq_test] PASS: GIC_SPI 20 (virq=%d) fired in %lld us! timer_irqs=%d total_irqs=%d last_m_stat=0x%08x last_c_stat=0x%08x storm=%d\n",
		sgx_virq, elapsed_us,
		atomic_read(&timer_irqs), atomic_read(&total_irqs),
		last_master_stat, last_core_stat, atomic_read(&storm_tripped));
	return 0;
}

/* Step 4B: Allocate & program 2-level SGX543 BIF MMU (PD + PTs) */
static int run_mmu_setup(void)
{
	struct device *dev = &dma_pdev->dev;
	unsigned int i, pd_idx_dram, pd_idx_cdram;
	u32 m_stat, m_fault, c_stat, c_fault;

	if (!pd_cpu) {
		pd_cpu = dma_alloc_coherent(dev, SGX_MMU_PAGE_SIZE, &pd_dma, GFP_KERNEL);
		pt_dram_cpu = dma_alloc_coherent(dev, SGX_MMU_PAGE_SIZE, &pt_dram_dma, GFP_KERNEL);
		pt_cdram_cpu = dma_alloc_coherent(dev, SGX_MMU_PAGE_SIZE, &pt_cdram_dma, GFP_KERNEL);
		pt_fb_cpu = dma_alloc_coherent(dev, SGX_MMU_PAGE_SIZE, &pt_fb_dma, GFP_KERNEL);
		dram_surf_cpu = dma_alloc_coherent(dev, TEST_SURF_BYTES, &dram_surf_dma, GFP_KERNEL);

		if (!pd_cpu || !pt_dram_cpu || !pt_cdram_cpu || !pt_fb_cpu || !dram_surf_cpu) {
			pr_err("vita_sgx_phase4: [mmu_test] coherent DMA allocation failed\n");
			return -ENOMEM;
		}
	}

	memset(pd_cpu, 0, SGX_MMU_PAGE_SIZE);
	memset(pt_dram_cpu, 0, SGX_MMU_PAGE_SIZE);
	memset(pt_cdram_cpu, 0, SGX_MMU_PAGE_SIZE);

	/* Map SGX VA 0x50000000 (PD[0x140]) -> 64 KB coherent system DRAM (16 x 4 KB) */
	pd_idx_dram = SGX_VA_DRAM_SURF >> 22;
	for (i = 0; i < (TEST_SURF_BYTES / SGX_MMU_PAGE_SIZE); i++) {
		u32 pa = (u32)(dram_surf_dma + i * SGX_MMU_PAGE_SIZE);

		pt_dram_cpu[i] = (pa & 0xFFFFF000U) |
				 SGX_MMU_PTE_CACHECONSISTENT |
				 SGX_MMU_PTE_VALID;
	}
	pd_cpu[pd_idx_dram] = ((u32)pt_dram_dma & 0xFFFFF000U) |
			      SGX_MMU_PDE_PAGE_SIZE_4K |
			      SGX_MMU_PDE_VALID;

	/* Map SGX VA 0x50400000 (PD[0x141]) -> 256 KB physical CDRAM 0x22000000 (64 x 4 KB) */
	pd_idx_cdram = SGX_VA_CDRAM_SURF >> 22;
	for (i = 0; i < (CDRAM_TEST_SIZE / SGX_MMU_PAGE_SIZE); i++) {
		u32 pa = (u32)(CDRAM_TEST_PA + i * SGX_MMU_PAGE_SIZE);

		pt_cdram_cpu[i] = (pa & 0xFFFFF000U) |
				  SGX_MMU_PTE_CACHECONSISTENT |
				  SGX_MMU_PTE_VALID;
	}
	pd_cpu[pd_idx_cdram] = ((u32)pt_cdram_dma & 0xFFFFF000U) |
			       SGX_MMU_PDE_PAGE_SIZE_4K |
			       SGX_MMU_PDE_VALID;

	wmb();

	/* Bind Context 0 (DIR_LIST_BASE0) for PTLA, TA, 3D, and EDM on Master & Core BIF */
	writel(0x00000000U, sgx_base + EUR_CR_MASTER_BIF_CTRL);
	writel(0x00000000U, sgx_base + EUR_CR_MASTER_BIF_BANK_SET);
	writel(0x00000000U, sgx_base + EUR_CR_MASTER_BIF_BANK0);
	writel((u32)pd_dma & 0xFFFFF000U, sgx_base + EUR_CR_MASTER_BIF_DIR_LIST_BASE0);

	writel(0x00000000U, sgx_base + EUR_CR_BIF_CTRL);
	writel(0x00000000U, sgx_base + EUR_CR_BIF_BANK_SET);
	writel(0x00000000U, sgx_base + EUR_CR_BIF_BANK0);
	writel((u32)pd_dma & 0xFFFFF000U, sgx_base + EUR_CR_BIF_DIR_LIST_BASE0);
	writel((u32)pd_dma & 0xFFFFF000U, sgx_base + EUR_CR_BIF_DIR_LIST_BASE7);

	sgx_bif_invalidate_tlb();

	m_stat  = readl(sgx_base + EUR_CR_MASTER_BIF_INT_STAT);
	m_fault = readl(sgx_base + EUR_CR_MASTER_BIF_FAULT);
	c_stat  = readl(sgx_base + EUR_CR_BIF_INT_STAT);
	c_fault = readl(sgx_base + EUR_CR_BIF_FAULT);

	mmu_ready = true;
	pr_info("vita_sgx_phase4: [mmu_test] PASS: PD_PA=0x%08x (DIR_LIST_BASE0=0x%08x) VA 0x%08x->DRAM 0x%08x, VA 0x%08x->CDRAM 0x%08lx | MASTER_BIF_STAT=0x%08x(fault=0x%08x) CORE_BIF_STAT=0x%08x(fault=0x%08x)\n",
		(u32)pd_dma,
		readl(sgx_base + EUR_CR_MASTER_BIF_DIR_LIST_BASE0),
		SGX_VA_DRAM_SURF, (u32)dram_surf_dma,
		SGX_VA_CDRAM_SURF, CDRAM_TEST_PA,
		m_stat, m_fault, c_stat, c_fault);

	return (m_stat & BIF_INT_STAT_FAULT_REQ_MASK) ? -EFAULT : 0;
}

static int ptla_submit_and_wait(const u32 *cmds, unsigned int num_words, const char *tag)
{
	int before_twod = atomic_read(&twod_irqs);
	unsigned int i;
	long waited;
	ktime_t start_kt;
	s64 elapsed_us;
	u32 ptla_st, m_bif_st, m_bif_flt;

	/* Enable TWOD_COMPLETE + MASTER_INTERRUPT on Hydra Master & Core */
	writel(SGX_EVT_TWOD_COMPLETE | SGX_EVT_MASTER_INTERRUPT,
	       sgx_base + EUR_CR_MASTER_EVENT_HOST_CLEAR);
	writel(SGX_EVT_TWOD_COMPLETE | SGX_EVT_MASTER_INTERRUPT,
	       sgx_base + EUR_CR_EVENT_HOST_CLEAR);
	writel(SGX_EVT_MASTER_INTERRUPT | SGX_EVT_TWOD_COMPLETE,
	       sgx_base + EUR_CR_MASTER_EVENT_HOST_ENABLE);
	writel(SGX_EVT_MASTER_INTERRUPT | SGX_EVT_TWOD_COMPLETE,
	       sgx_base + EUR_CR_EVENT_HOST_ENABLE);
	wmb();

	start_kt = ktime_get();

	for (i = 0; i < num_words; i++) {
		int spin;

		for (spin = 0; spin < 1000; spin++) {
			u32 st = readl(sgx_base + EUR_CR_MASTER_PTLA_STATUS);
			u32 fifo = (st & PTLA_STATUS_FIFO_MASK) >> PTLA_STATUS_FIFO_SHIFT;

			if (fifo < 28)
				break;
			udelay(2);
		}
		writel(cmds[i], sgx_base + EUR_CR_MASTER_PTLA_SLAVE_CMD_0 + ((i & 31U) * 4U));
	}
	wmb();

	waited = wait_event_timeout(sgx_irq_wq,
				    atomic_read(&twod_irqs) > before_twod,
				    msecs_to_jiffies(250));

	for (i = 0; i < 2000; i++) {
		ptla_st = readl(sgx_base + EUR_CR_MASTER_PTLA_STATUS);
		if (!(ptla_st & PTLA_STATUS_BUSY_MASK))
			break;
		udelay(5);
	}
	elapsed_us = ktime_us_delta(ktime_get(), start_kt);

	sgx_slc_flush_inv();

	ptla_st   = readl(sgx_base + EUR_CR_MASTER_PTLA_STATUS);
	m_bif_st  = readl(sgx_base + EUR_CR_MASTER_BIF_INT_STAT);
	m_bif_flt = readl(sgx_base + EUR_CR_MASTER_BIF_FAULT);

	pr_info("vita_sgx_phase4: [%s] words=%u elapsed=%lld us irq_wait=%ld twod_irqs=%d PTLA_STATUS=0x%08x MASTER_BIF_STAT=0x%08x FAULT=0x%08x\n",
		tag, num_words, elapsed_us, waited,
		atomic_read(&twod_irqs), ptla_st, m_bif_st, m_bif_flt);

	if (m_bif_st & BIF_INT_STAT_FAULT_REQ_MASK)
		return -EFAULT;
	if (ptla_st & PTLA_STATUS_BUSY_MASK)
		return -EBUSY;
	return 0;
}

static unsigned int emit_ptla_fill(u32 *out, u32 dst_va, u32 stride,
				   u32 x, u32 y, u32 w, u32 h, u32 argb)
{
	unsigned int n = 0;

	out[n++] = PTLA_SURF_8888(EURASIA_PTLA_DST_SURF_BH, stride);
	out[n++] = dst_va;
	/* Hardware requirement from sgxtransfer_2d.c: dummy strided SRC_SURF */
	out[n++] = PTLA_SURF_8888(EURASIA_PTLA_SRC_SURF_BH, stride);
	out[n++] = dst_va;
	out[n++] = EURASIA_PTLA_BLIT_BH | EURASIA_PTLA_FILL_ENABLE;
	out[n++] = argb;
	out[n++] = PTLA_XY(x, y);
	out[n++] = PTLA_XY(w, h);
	return n;
}

static unsigned int emit_ptla_blit(u32 *out, u32 src_va, u32 src_stride,
				   u32 sx, u32 sy, u32 dst_va, u32 dst_stride,
				   u32 dx, u32 dy, u32 w, u32 h)
{
	unsigned int n = 0;

	out[n++] = PTLA_SURF_8888(EURASIA_PTLA_SRC_SURF_BH, src_stride);
	out[n++] = src_va;
	out[n++] = EURASIA_PTLA_SRCOFF_BH | PTLA_XY(sx, sy);
	out[n++] = PTLA_XY(w, h);
	out[n++] = PTLA_SURF_8888(EURASIA_PTLA_DST_SURF_BH, dst_stride);
	out[n++] = dst_va;
	out[n++] = EURASIA_PTLA_BLIT_BH;
	out[n++] = PTLA_XY(dx, dy);
	out[n++] = PTLA_XY(w, h);
	return n;
}

static u32 expected_quadrant_pixel(unsigned int x, unsigned int y)
{
	if (y < 64)
		return (x < 64) ? 0xFFFF0000U : 0xFF00FF00U;
	return (x < 64) ? 0xFF0000FFU : 0xFFFFFFFFU;
}

/* Step 4C: Hardware PTLA 2D Fill (DRAM) + Blit (DRAM -> CDRAM) through BIF MMU */
static int run_blit_test(void)
{
	u32 cmds[40];
	unsigned int n, x, y;
	unsigned int dram_ok = 0, cdram_ok = 0;
	int ret;

	if (!mmu_ready) {
		ret = run_mmu_setup();
		if (ret)
			return ret;
	}

	/* Poison both DRAM and CDRAM surfaces with distinct patterns before GPU kick */
	for (n = 0; n < (TEST_SURF_BYTES / 4U); n++) {
		dram_surf_cpu[n] = 0x11223344U;
		writel(0x55667788U, cdram_va + n * 4U);
	}
	wmb();

	/* 1. PTLA 2D Hardware Fill: 4 solid 64x64 ARGB8888 quadrants into SGX VA 0x50000000 */
	n = 0;
	n += emit_ptla_fill(&cmds[n], SGX_VA_DRAM_SURF, TEST_SURF_STRIDE,
			    0, 0, 64, 64, 0xFFFF0000U);
	cmds[n++] = EURASIA_PTLA_FENCE_BH;
	n += emit_ptla_fill(&cmds[n], SGX_VA_DRAM_SURF, TEST_SURF_STRIDE,
			    64, 0, 64, 64, 0xFF00FF00U);
	cmds[n++] = EURASIA_PTLA_FENCE_BH;
	n += emit_ptla_fill(&cmds[n], SGX_VA_DRAM_SURF, TEST_SURF_STRIDE,
			    0, 64, 64, 64, 0xFF0000FFU);
	cmds[n++] = EURASIA_PTLA_FENCE_BH;
	n += emit_ptla_fill(&cmds[n], SGX_VA_DRAM_SURF, TEST_SURF_STRIDE,
			    64, 64, 64, 64, 0xFFFFFFFFU);
	cmds[n++] = EURASIA_PTLA_FLUSH_BH;

	ret = ptla_submit_and_wait(cmds, n, "ptla_fill_dram");
	if (ret)
		return ret;

	/* 2. PTLA 2D Hardware Blit: copy 128x128 from SGX VA 0x50000000 -> 0x50400000 (CDRAM) */
	n = 0;
	n += emit_ptla_blit(&cmds[n],
			    SGX_VA_DRAM_SURF, TEST_SURF_STRIDE, 0, 0,
			    SGX_VA_CDRAM_SURF, TEST_SURF_STRIDE, 0, 0,
			    TEST_SURF_W, TEST_SURF_H);
	cmds[n++] = EURASIA_PTLA_FLUSH_BH;

	ret = ptla_submit_and_wait(cmds, n, "ptla_blit_cdram");
	if (ret)
		return ret;

	rmb();
	for (y = 0; y < TEST_SURF_H; y++) {
		for (x = 0; x < TEST_SURF_W; x++) {
			unsigned int idx = y * TEST_SURF_W + x;
			u32 exp = expected_quadrant_pixel(x, y);

			if (dram_surf_cpu[idx] == exp)
				dram_ok++;
			if (readl(cdram_va + idx * 4U) == exp)
				cdram_ok++;
		}
	}

	pr_info("vita_sgx_phase4: [blit_test] DRAM samples: TL=0x%08x TR=0x%08x BL=0x%08x BR=0x%08x (%u/16384 exact)\n",
		dram_surf_cpu[0],
		dram_surf_cpu[64],
		dram_surf_cpu[64 * TEST_SURF_W],
		dram_surf_cpu[64 * TEST_SURF_W + 64],
		dram_ok);
	pr_info("vita_sgx_phase4: [blit_test] CDRAM(0x22000000) samples: TL=0x%08x TR=0x%08x BL=0x%08x BR=0x%08x (%u/16384 exact)\n",
		readl(cdram_va + 0),
		readl(cdram_va + 64 * 4U),
		readl(cdram_va + (64 * TEST_SURF_W) * 4U),
		readl(cdram_va + (64 * TEST_SURF_W + 64) * 4U),
		cdram_ok);

	if (dram_ok != 16384U || cdram_ok != 16384U) {
		pr_err("vita_sgx_phase4: [blit_test] MISMATCH (dram_ok=%u cdram_ok=%u)\n",
		       dram_ok, cdram_ok);
		return -EIO;
	}

	pr_info("vita_sgx_phase4: [blit_test] PASS: 100%% GPU hardware 2D fill + blit verified across DRAM & CDRAM via 2-level BIF MMU!\n");
	return 0;
}

/* Optional visual test: PTLA blits a 128x128 RGBW GPU badge onto live OLED scanout */
static int run_oled_test(void)
{
	void __iomem *iftu_p0, *iftu_creg0;
	u32 cfg, fb_pa, fb_stride;
	unsigned int i, pd_idx_fb, n;
	u32 cmds[24];
	int ret;

	ret = run_blit_test();
	if (ret)
		return ret;

	iftu_p0    = ioremap(IFTU_P0_PA, 0x1000);
	iftu_creg0 = ioremap(IFTU_CREG0_PA, 0x1000);
	if (!iftu_p0 || !iftu_creg0) {
		if (iftu_creg0)
			iounmap(iftu_creg0);
		if (iftu_p0)
			iounmap(iftu_p0);
		return -ENOMEM;
	}

	cfg       = readl(iftu_creg0 + 0x10) & 1U;
	fb_pa     = readl(iftu_p0 + 0x200 + cfg * 0x100 + 0x00);
	fb_stride = readl(iftu_p0 + 0x200 + cfg * 0x100 + 0x20);
	if (fb_stride == 0)
		fb_stride = 960U * 4U;

	iounmap(iftu_creg0);
	iounmap(iftu_p0);

	if (fb_pa < 0x20000000U || fb_pa >= 0x28000000U || (fb_pa & 0xFFFU)) {
		pr_err("vita_sgx_phase4: [oled_test] unexpected IFTU scanout PA 0x%08x\n", fb_pa);
		return -EINVAL;
	}

	/* Map 2 MB active OLED CDRAM framebuffer at SGX VA 0x50800000 (PD[0x142]) */
	memset(pt_fb_cpu, 0, SGX_MMU_PAGE_SIZE);
	pd_idx_fb = SGX_VA_OLED_FB >> 22;
	for (i = 0; i < (OLED_FB_MAP_SIZE / SGX_MMU_PAGE_SIZE); i++) {
		u32 pa = fb_pa + i * SGX_MMU_PAGE_SIZE;

		pt_fb_cpu[i] = (pa & 0xFFFFF000U) |
			       SGX_MMU_PTE_CACHECONSISTENT |
			       SGX_MMU_PTE_VALID;
	}
	pd_cpu[pd_idx_fb] = ((u32)pt_fb_dma & 0xFFFFF000U) |
			    SGX_MMU_PDE_PAGE_SIZE_4K |
			    SGX_MMU_PDE_VALID;
	sgx_bif_invalidate_tlb();

	/* Draw dark border (136x136) + blit the 128x128 RGBW CDRAM texture at (796, 28) */
	n = 0;
	n += emit_ptla_fill(&cmds[n], SGX_VA_OLED_FB, fb_stride,
			    792, 24, 136, 136, 0xFF181824U);
	cmds[n++] = EURASIA_PTLA_FENCE_BH;
	n += emit_ptla_blit(&cmds[n],
			    SGX_VA_CDRAM_SURF, TEST_SURF_STRIDE, 0, 0,
			    SGX_VA_OLED_FB, fb_stride, 796, 28,
			    TEST_SURF_W, TEST_SURF_H);
	cmds[n++] = EURASIA_PTLA_FLUSH_BH;

	ret = ptla_submit_and_wait(cmds, n, "ptla_oled_badge");
	if (ret == 0)
		pr_info("vita_sgx_phase4: [oled_test] PASS: SGX543 PTLA rendered 128x128 RGBW badge onto live OLED FB 0x%08x (stride=%u) at (796,28)\n",
			fb_pa, fb_stride);
	return ret;
}

static int phase4_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "CORE_ID=0x%08x REV=0x%08x virq=%d mmu_ready=%d\n",
		   readl(sgx_base + EUR_CR_CORE_ID),
		   readl(sgx_base + EUR_CR_CORE_REVISION),
		   sgx_virq, mmu_ready);
	seq_printf(m, "IRQs: total=%d timer=%d twod=%d storm=%d last_m_stat=0x%08x last_c_stat=0x%08x\n",
		   atomic_read(&total_irqs), atomic_read(&timer_irqs),
		   atomic_read(&twod_irqs), atomic_read(&storm_tripped),
		   last_master_stat, last_core_stat);
	seq_printf(m, "MASTER: EVT_STAT=0x%08x EVT_EN=0x%08x PTLA_STATUS=0x%08x BIF_STAT=0x%08x BIF_FAULT=0x%08x DIR0=0x%08x\n",
		   readl(sgx_base + EUR_CR_MASTER_EVENT_STATUS),
		   readl(sgx_base + EUR_CR_MASTER_EVENT_HOST_ENABLE),
		   readl(sgx_base + EUR_CR_MASTER_PTLA_STATUS),
		   readl(sgx_base + EUR_CR_MASTER_BIF_INT_STAT),
		   readl(sgx_base + EUR_CR_MASTER_BIF_FAULT),
		   readl(sgx_base + EUR_CR_MASTER_BIF_DIR_LIST_BASE0));
	seq_printf(m, "CORE:   EVT_STAT=0x%08x TIMER=0x%08x BIF_STAT=0x%08x BIF_FAULT=0x%08x DIR0=0x%08x\n",
		   readl(sgx_base + EUR_CR_EVENT_STATUS),
		   readl(sgx_base + EUR_CR_TIMER),
		   readl(sgx_base + EUR_CR_BIF_INT_STAT),
		   readl(sgx_base + EUR_CR_BIF_FAULT),
		   readl(sgx_base + EUR_CR_BIF_DIR_LIST_BASE0));
	return 0;
}

static int phase4_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, phase4_proc_show, NULL);
}

static ssize_t phase4_proc_write(struct file *file, const char __user *ubuf,
				 size_t count, loff_t *ppos)
{
	char cmd[32];
	size_t len = min(count, sizeof(cmd) - 1);
	int ret = 0;

	if (copy_from_user(cmd, ubuf, len))
		return -EFAULT;
	cmd[len] = '\0';
	strim(cmd);

	if (strcmp(cmd, "status") == 0) {
		pr_info("vita_sgx_phase4: [status] total_irqs=%d timer=%d twod=%d PTLA=0x%08x MASTER_BIF=0x%08x/0x%08x CORE_BIF=0x%08x/0x%08x TIMER=0x%08x\n",
			atomic_read(&total_irqs), atomic_read(&timer_irqs),
			atomic_read(&twod_irqs),
			readl(sgx_base + EUR_CR_MASTER_PTLA_STATUS),
			readl(sgx_base + EUR_CR_MASTER_BIF_INT_STAT),
			readl(sgx_base + EUR_CR_MASTER_BIF_FAULT),
			readl(sgx_base + EUR_CR_BIF_INT_STAT),
			readl(sgx_base + EUR_CR_BIF_FAULT),
			readl(sgx_base + EUR_CR_TIMER));
	} else if (strcmp(cmd, "irq_test") == 0) {
		ret = run_irq_test();
	} else if (strcmp(cmd, "mmu_test") == 0) {
		ret = run_mmu_setup();
	} else if (strcmp(cmd, "blit_test") == 0) {
		ret = run_blit_test();
	} else if (strcmp(cmd, "oled_test") == 0) {
		ret = run_oled_test();
	} else if (strcmp(cmd, "all") == 0) {
		ret = run_irq_test();
		if (!ret)
			ret = run_mmu_setup();
		if (!ret)
			ret = run_blit_test();
		if (!ret)
			pr_info("vita_sgx_phase4: [all] ALL PHASE 4 TESTS PASSED (IRQ + 2-Level BIF MMU + PTLA 2D Fill/Blit)!\n");
	} else {
		pr_warn("vita_sgx_phase4: unknown cmd '%s' (valid: status, irq_test, mmu_test, blit_test, oled_test, all)\n",
			cmd);
		return -EINVAL;
	}

	return ret ? ret : (ssize_t)count;
}

static const struct proc_ops phase4_proc_ops = {
	.proc_open	= phase4_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
	.proc_write	= phase4_proc_write,
};

static int map_sgx_irq(void)
{
	struct device_node *np, *gic_np;
	struct irq_fwspec fwspec;
	int virq = 0;

	np = of_find_node_by_path("/soc/gpu@e8400000");
	if (np) {
		virq = irq_of_parse_and_map(np, 0);
		of_node_put(np);
		if (virq > 0)
			return virq;
	}

	gic_np = of_find_compatible_node(NULL, NULL, "arm,cortex-a9-gic");
	if (!gic_np)
		return -ENODEV;

	memset(&fwspec, 0, sizeof(fwspec));
	fwspec.fwnode = of_node_to_fwnode(gic_np);
	fwspec.param_count = 3;
	fwspec.param[0] = 0; /* GIC_SPI */
	fwspec.param[1] = SGX543_GIC_SPI;
	fwspec.param[2] = IRQ_TYPE_LEVEL_HIGH;

	virq = irq_create_fwspec_mapping(&fwspec);
	of_node_put(gic_np);
	return (virq > 0) ? virq : -EINVAL;
}

static void cleanup_phase4(void)
{
	if (proc_ent) {
		proc_remove(proc_ent);
		proc_ent = NULL;
	}

	if (sgx_base)
		sgx_mask_and_clear_all_irqs();

	if (sgx_virq >= 0) {
		free_irq(sgx_virq, &sgx_virq);
		irq_dispose_mapping(sgx_virq);
		sgx_virq = -1;
	}

	if (dma_pdev) {
		struct device *dev = &dma_pdev->dev;

		if (dram_surf_cpu)
			dma_free_coherent(dev, TEST_SURF_BYTES, dram_surf_cpu, dram_surf_dma);
		if (pt_fb_cpu)
			dma_free_coherent(dev, SGX_MMU_PAGE_SIZE, pt_fb_cpu, pt_fb_dma);
		if (pt_cdram_cpu)
			dma_free_coherent(dev, SGX_MMU_PAGE_SIZE, pt_cdram_cpu, pt_cdram_dma);
		if (pt_dram_cpu)
			dma_free_coherent(dev, SGX_MMU_PAGE_SIZE, pt_dram_cpu, pt_dram_dma);
		if (pd_cpu)
			dma_free_coherent(dev, SGX_MMU_PAGE_SIZE, pd_cpu, pd_dma);

		dram_surf_cpu = NULL;
		pt_fb_cpu = NULL;
		pt_cdram_cpu = NULL;
		pt_dram_cpu = NULL;
		pd_cpu = NULL;
		platform_device_unregister(dma_pdev);
		dma_pdev = NULL;
	}

	if (cdram_va) {
		iounmap(cdram_va);
		cdram_va = NULL;
	}

	if (sgx_base) {
		iounmap(sgx_base);
		sgx_base = NULL;
	}

	if (pervasive_touched) {
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
		pervasive_touched = false;
	}

	if (smc_power_enabled) {
		vita_smc(VITA_SMC_POWER_DOMAIN, VITA_PD_SGX543, 0, 0, 0);
		smc_power_enabled = false;
	}
}

static int __init vita_sgx_phase4_init(void)
{
	struct vita_syscon *syscon = get_vita_syscon();
	void __iomem *rst_base, *gate_base, *clk_base;
	u32 cur_gate, cur_rst, smc_ret;
	int ret;

	if (!syscon || !syscon->transfer) {
		pr_err("vita_sgx_phase4: vita-syscon not available\n");
		return -ENODEV;
	}

	rst_base  = ioremap(PERVASIVE_RESET_PA, 0x1000);
	gate_base = ioremap(PERVASIVE_GATE_PA, 0x1000);
	clk_base  = ioremap(PERVASIVE_BASECLK_PA, 0x1000);
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

	/* 1. VDDG 111 MHz rail + XBAR 111 MHz + SGX543 power domain + Grab 0xF00 */
	send_syscon_voltage(syscon, SYSCON_CMD_CTRL_VOLTAGE, STOCK_088E_111MHZ_DATA, 3);
	msleep(10);

	vita_smc(VITA_SMC_CLOCK_CTRL, VITA_CLK_SET_GPU_XBAR, STOCK_GPU_XBAR_111MHZ, 0, 0);
	udelay(200);

	smc_ret = vita_smc(VITA_SMC_POWER_DOMAIN, VITA_PD_SGX543, 1, 0, 0);
	msleep(20);
	if ((s32)smc_ret >= 0)
		smc_power_enabled = true;

	vita_smc(VITA_SMC_GRAB_BRIDGE, VITA_GRAB_GPU_MASK, 0, 0, 0);
	udelay(50);
	writel(STOCK_GPU_BASECLK, clk_base + PERVASIVE_GPU_OFF);
	(void)readl(clk_base + PERVASIVE_GPU_OFF);
	udelay(50);
	vita_smc(VITA_SMC_GRAB_BRIDGE, VITA_GRAB_GPU_MASK, 1, 0, 0);
	udelay(100);

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

	iounmap(clk_base);
	iounmap(gate_base);
	iounmap(rst_base);

	/* 2. Map SGX543 MMIO & CDRAM test surface, then run SGXStart + SGXReset */
	sgx_base = ioremap(SGX543_REGS_PA, SGX543_REGS_SIZE);
	cdram_va = ioremap_wc(CDRAM_TEST_PA, CDRAM_TEST_SIZE);
	if (!sgx_base || !cdram_va) {
		ret = -ENOMEM;
		goto err_out;
	}

	sgx_hw_start_and_reset();

	/* 3. Register helper platform_device for coherent system DRAM allocations */
	dma_pdev = platform_device_register_simple("vita_sgx_dma", -1, NULL, 0);
	if (IS_ERR(dma_pdev)) {
		ret = PTR_ERR(dma_pdev);
		dma_pdev = NULL;
		goto err_out;
	}
	ret = dma_coerce_mask_and_coherent(&dma_pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		goto err_out;

	/* 4. Map and request GIC_SPI 20 (SGX543 hardware interrupt) */
	ret = map_sgx_irq();
	if (ret < 0) {
		pr_err("vita_sgx_phase4: failed to map GIC_SPI 20: %d\n", ret);
		goto err_out;
	}
	sgx_virq = ret;

	ret = request_irq(sgx_virq, vita_sgx_isr, 0, "vita-sgx543", &sgx_virq);
	if (ret) {
		pr_err("vita_sgx_phase4: request_irq(%d) failed: %d\n", sgx_virq, ret);
		irq_dispose_mapping(sgx_virq);
		sgx_virq = -1;
		goto err_out;
	}

	proc_ent = proc_create(PROC_NAME, 0644, NULL, &phase4_proc_ops);
	if (!proc_ent) {
		ret = -ENOMEM;
		goto err_out;
	}

	pr_info("vita_sgx_phase4: SGX543MP4+ online: CORE_ID=0x%08x REV=0x%08x PTLA_STATUS=0x%08x GIC_SPI_20->virq=%d\n",
		readl(sgx_base + EUR_CR_CORE_ID),
		readl(sgx_base + EUR_CR_CORE_REVISION),
		readl(sgx_base + EUR_CR_MASTER_PTLA_STATUS),
		sgx_virq);
	pr_info("vita_sgx_phase4: ready (/proc/%s: status, irq_test, mmu_test, blit_test, oled_test, all)\n",
		PROC_NAME);
	return 0;

err_out:
	cleanup_phase4();
	return ret;
}

static void __exit vita_sgx_phase4_exit(void)
{
	cleanup_phase4();
	pr_info("vita_sgx_phase4: unloaded cleanly\n");
}

module_init(vita_sgx_phase4_init);
module_exit(vita_sgx_phase4_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LinuxOnVita");
MODULE_DESCRIPTION("Phase 4 SGX543MP4+ IRQ (GIC_SPI 20), 2-level BIF MMU & PTLA 2D hardware probe");
