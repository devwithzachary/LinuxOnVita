// SPDX-License-Identifier: GPL-2.0
/*
 * vita_iftu_probe.c - Phase 3 Live Hardware Verification for IFTU & DSI0
 *
 * Safely verifies on a running Linux 6.12 PS Vita without modifying flash:
 *   1. Exact boot state of IFTU Bus 0 Plane A (+0x000..+0x3FF), IFTU CREG0,
 *      and DSI0 (+0x00..+0x54).
 *   2. Hardware 60 Hz VBlank interrupt delivery on GIC_SPI 181 (SceDsi0)
 *      with W1C ack at DSI0 + 0x50 and built-in storm protection.
 *   3. Tear-free VBlank-latched double-buffered CDRAM page-flipping between
 *      0x20000000 (simplefb) and 0x20400000 (secondary CDRAM buffer) via
 *      IFTU shadow config banks (+0x200 / +0x300) and PLANE0_CONFIG_SELECT.
 *
 * Commands via /proc/vita_iftu_probe:
 *   status      - Dump IFTU/DSI0 registers and VBlank counter
 *   vblank      - Enable GIC_SPI 181 VBlank IRQ and measure 1.0s rate (~60 Hz)
 *   vblank_off  - Free GIC_SPI 181 VBlank IRQ
 *   flip        - Flip to 0x20400000 (with R/G/B top test bar) for 2.5s, then restore
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
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/ktime.h>

#define IFTU_P0_PA		0xE5020000UL
#define IFTU_CREG0_PA		0xE5022000UL
#define DSI0_PA			0xE5050000UL

#define FB0_PA			0x20000000UL
#define FB1_PA			0x20400000UL
#define FB_WIDTH		960
#define FB_HEIGHT		544
#define FB_BYTES		(FB_WIDTH * FB_HEIGHT * 4)

#define DSI_REG_SCANLINE	0x4C
#define DSI_REG_IRQ_STATUS	0x50
#define DSI_REG_IRQ_ENABLE	0x54
#define DSI_IRQ_VBLANK_BIT	BIT(1)

#define IFTU_CREG_CTRL		0x00
#define IFTU_CREG_CTRL2		0x04
#define IFTU_CREG_P0_CFG_SEL	0x10
#define IFTU_CREG_P1_CFG_SEL	0x18
#define IFTU_CREG_ALPHA_CTRL	0x20

#define IFTU_CFG_BASE(cfg)	(0x200 + (cfg) * 0x100)
#define IFTU_CFG_FB_PADDR	0x00
#define IFTU_CFG_STRIDE0	0x20
#define IFTU_CFG_SRC_X		0x24
#define IFTU_CFG_SRC_Y		0x28
#define IFTU_CFG_PIXELFMT	0x40
#define IFTU_CFG_WIDTH		0x44
#define IFTU_CFG_HEIGHT		0x48
#define IFTU_CFG_CTRL		0x4C
#define IFTU_CFG_SCALE_W	0x60
#define IFTU_CFG_SCALE_H	0x64
#define IFTU_CFG_DST_FMT	0xA0
#define IFTU_CFG_DST_W		0xA4
#define IFTU_CFG_DST_H		0xA8
#define IFTU_CFG_SRC_W		0xC0
#define IFTU_CFG_SRC_H		0xC4
#define IFTU_CFG_DST_X		0xC8
#define IFTU_CFG_DST_Y		0xCC

#define VITA_DSI0_GIC_SPI	181

static void __iomem *iftu_p0;
static void __iomem *iftu_creg0;
static void __iomem *dsi0;
static struct proc_dir_entry *proc_ent;

static int vblank_virq = -1;
static atomic_t vblank_count = ATOMIC_INIT(0);
static atomic_t total_irq_count = ATOMIC_INIT(0);
static atomic_t last_scanline = ATOMIC_INIT(0);
static atomic_t storm_tripped = ATOMIC_INIT(0);
static ktime_t irq_start_time;

static void dump_cfg_bank(int cfg)
{
	u32 base = IFTU_CFG_BASE(cfg);

	pr_info("vita_iftu_probe: CFG%d (+0x%03x): paddr=0x%08x stride=0x%08x fmt=0x%x %ux%u ctrl=%u dst=%ux%u@(%u,%u) scale=(0x%x,0x%x) src_wh=(0x%x,0x%x)\n",
		cfg, base,
		readl(iftu_p0 + base + IFTU_CFG_FB_PADDR),
		readl(iftu_p0 + base + IFTU_CFG_STRIDE0),
		readl(iftu_p0 + base + IFTU_CFG_PIXELFMT),
		readl(iftu_p0 + base + IFTU_CFG_WIDTH),
		readl(iftu_p0 + base + IFTU_CFG_HEIGHT),
		readl(iftu_p0 + base + IFTU_CFG_CTRL),
		readl(iftu_p0 + base + IFTU_CFG_DST_W),
		readl(iftu_p0 + base + IFTU_CFG_DST_H),
		readl(iftu_p0 + base + IFTU_CFG_DST_X),
		readl(iftu_p0 + base + IFTU_CFG_DST_Y),
		readl(iftu_p0 + base + IFTU_CFG_SCALE_W),
		readl(iftu_p0 + base + IFTU_CFG_SCALE_H),
		readl(iftu_p0 + base + IFTU_CFG_SRC_W),
		readl(iftu_p0 + base + IFTU_CFG_SRC_H));
}

static void dump_status(const char *tag)
{
	u32 p0_04 = readl(iftu_p0 + 0x04);
	u32 dsi_scan = readl(dsi0 + DSI_REG_SCANLINE);

	pr_info("vita_iftu_probe: [%s] P0(+00=0x%08x +04=0x%08x[active=%u,pend=%u] +40=0x%x +50=0x%x +54=0x%x +58=0x%x +8C=0x%x +A0=0x%x +100=0x%x)\n",
		tag,
		readl(iftu_p0 + 0x00), p0_04, (p0_04 >> 1) & 1, p0_04 & 1,
		readl(iftu_p0 + 0x40), readl(iftu_p0 + 0x50),
		readl(iftu_p0 + 0x54), readl(iftu_p0 + 0x58),
		readl(iftu_p0 + 0x8C), readl(iftu_p0 + 0xA0),
		readl(iftu_p0 + 0x100));
	pr_info("vita_iftu_probe: [%s] CREG0(+00=0x%x +04=0x%x P0_SEL=%u P1_SEL=%u ALPHA=0x%x)\n",
		tag,
		readl(iftu_creg0 + IFTU_CREG_CTRL),
		readl(iftu_creg0 + IFTU_CREG_CTRL2),
		readl(iftu_creg0 + IFTU_CREG_P0_CFG_SEL),
		readl(iftu_creg0 + IFTU_CREG_P1_CFG_SEL),
		readl(iftu_creg0 + IFTU_CREG_ALPHA_CTRL));
	dump_cfg_bank(0);
	dump_cfg_bank(1);
	pr_info("vita_iftu_probe: [%s] DSI0(+00=0x%x +04=0x%x +4C=0x%08x[line=%u] IRQ_STAT=0x%08x IRQ_EN=0x%08x) vblanks=%d total_irqs=%d last_isr_line=%d\n",
		tag,
		readl(dsi0 + 0x00), readl(dsi0 + 0x04),
		dsi_scan, (dsi_scan >> 16) & 0x1FFF,
		readl(dsi0 + DSI_REG_IRQ_STATUS),
		readl(dsi0 + DSI_REG_IRQ_ENABLE),
		atomic_read(&vblank_count),
		atomic_read(&total_irq_count),
		atomic_read(&last_scanline));
}

static irqreturn_t vita_dsi0_isr(int irq, void *dev_id)
{
	u32 stat = readl(dsi0 + DSI_REG_IRQ_STATUS);
	int total;

	if (!stat)
		return IRQ_NONE;

	/* Exact SceDsiForDriver W1C ack + posted write flush */
	writel(stat, dsi0 + DSI_REG_IRQ_STATUS);
	(void)readl(dsi0 + DSI_REG_IRQ_STATUS);

	total = atomic_inc_return(&total_irq_count);

	/* Safety guard: mask DSI IRQ if an unacked storm occurs */
	if (unlikely(total == 500)) {
		s64 elapsed_ms = ktime_ms_delta(ktime_get(), irq_start_time);

		if (elapsed_ms < 100) {
			writel(0, dsi0 + DSI_REG_IRQ_ENABLE);
			(void)readl(dsi0 + DSI_REG_IRQ_ENABLE);
			atomic_set(&storm_tripped, 1);
			return IRQ_HANDLED;
		}
	}

	if (stat & DSI_IRQ_VBLANK_BIT) {
		u32 scan = (readl(dsi0 + DSI_REG_SCANLINE) >> 16) & 0x1FFF;

		atomic_set(&last_scanline, (int)scan);
		atomic_inc(&vblank_count);
	}

	return IRQ_HANDLED;
}

static void stop_vblank_irq(void)
{
	if (vblank_virq >= 0) {
		free_irq(vblank_virq, &vblank_virq);
		irq_dispose_mapping(vblank_virq);
		vblank_virq = -1;
		pr_info("vita_iftu_probe: freed VBlank IRQ\n");
	}
}

static int start_and_measure_vblank(void)
{
	struct device_node *gic_np;
	struct irq_fwspec fwspec;
	u32 stale, en;
	int ret, c0, c1;

	if (vblank_virq < 0) {
		gic_np = of_find_compatible_node(NULL, NULL, "arm,cortex-a9-gic");
		if (!gic_np) {
			pr_err("vita_iftu_probe: GIC node not found\n");
			return -ENODEV;
		}

		memset(&fwspec, 0, sizeof(fwspec));
		fwspec.fwnode = of_node_to_fwnode(gic_np);
		fwspec.param_count = 3;
		fwspec.param[0] = 0; /* GIC_SPI */
		fwspec.param[1] = VITA_DSI0_GIC_SPI;
		fwspec.param[2] = IRQ_TYPE_LEVEL_HIGH;

		vblank_virq = irq_create_fwspec_mapping(&fwspec);
		of_node_put(gic_np);
		if (vblank_virq <= 0) {
			pr_err("vita_iftu_probe: irq_create_fwspec_mapping(SPI %d) failed: %d\n",
			       VITA_DSI0_GIC_SPI, vblank_virq);
			vblank_virq = -1;
			return -EINVAL;
		}

		atomic_set(&vblank_count, 0);
		atomic_set(&total_irq_count, 0);
		atomic_set(&storm_tripped, 0);
		irq_start_time = ktime_get();

		/* Clear any latched status and ensure bit 1 (VBlank) is enabled */
		stale = readl(dsi0 + DSI_REG_IRQ_STATUS);
		writel(stale, dsi0 + DSI_REG_IRQ_STATUS);
		(void)readl(dsi0 + DSI_REG_IRQ_STATUS);

		en = readl(dsi0 + DSI_REG_IRQ_ENABLE);
		writel(en | DSI_IRQ_VBLANK_BIT, dsi0 + DSI_REG_IRQ_ENABLE);
		(void)readl(dsi0 + DSI_REG_IRQ_ENABLE);

		ret = request_irq(vblank_virq, vita_dsi0_isr, 0,
				  "vita_dsi0_vblank", &vblank_virq);
		if (ret) {
			pr_err("vita_iftu_probe: request_irq(%d) failed: %d\n",
			       vblank_virq, ret);
			irq_dispose_mapping(vblank_virq);
			vblank_virq = -1;
			return ret;
		}
		pr_info("vita_iftu_probe: registered GIC_SPI %d -> virq %d (stale_stat=0x%x, irq_en=0x%x)\n",
			VITA_DSI0_GIC_SPI, vblank_virq, stale,
			readl(dsi0 + DSI_REG_IRQ_ENABLE));
	}

	c0 = atomic_read(&vblank_count);
	msleep(1000);
	c1 = atomic_read(&vblank_count);

	pr_info("vita_iftu_probe: 1000 ms VBlank measurement: +%d VBlanks (total=%d, storm=%d, last_isr_line=%d)\n",
		c1 - c0, c1, atomic_read(&storm_tripped),
		atomic_read(&last_scanline));
	return 0;
}

static void iftu_flip_to_paddr(u32 paddr)
{
	u32 stat = readl(iftu_p0 + 0x04);
	u32 active_cfg = (stat >> 1) & 1;
	u32 next_cfg = active_cfg ^ 1;
	u32 base = IFTU_CFG_BASE(next_cfg);

	writel(paddr, iftu_p0 + base + IFTU_CFG_FB_PADDR);
	wmb();
	writel(next_cfg, iftu_creg0 + IFTU_CREG_P0_CFG_SEL);
	(void)readl(iftu_creg0 + IFTU_CREG_P0_CFG_SEL);
}

static int run_flip_test(void)
{
	u32 __iomem *fb0_va;
	u32 __iomem *fb1_va;
	u32 before_04, after_04, restore_04;
	int x, y, i;

	fb0_va = ioremap_wc(FB0_PA, FB_BYTES);
	fb1_va = ioremap_wc(FB1_PA, FB_BYTES);
	if (!fb0_va || !fb1_va) {
		if (fb0_va)
			iounmap(fb0_va);
		if (fb1_va)
			iounmap(fb1_va);
		pr_err("vita_iftu_probe: failed to ioremap_wc CDRAM buffers\n");
		return -ENOMEM;
	}

	/* Copy active console framebuffer so text stays intact */
	for (i = 0; i < (FB_WIDTH * FB_HEIGHT); i++)
		writel_relaxed(readl_relaxed(fb0_va + i), fb1_va + i);

	/* Paint a 32px top banner: Left=Red, Middle=Green, Right=Blue (A8B8G8R8) */
	for (y = 0; y < 32; y++) {
		for (x = 0; x < FB_WIDTH; x++) {
			u32 pixel;

			if (x < (FB_WIDTH / 3))
				pixel = 0xFF0000FFU; /* Red in A8B8G8R8 */
			else if (x < (2 * FB_WIDTH / 3))
				pixel = 0xFF00FF00U; /* Green in A8B8G8R8 */
			else
				pixel = 0xFFFF0000U; /* Blue in A8B8G8R8 */
			writel_relaxed(pixel, fb1_va + y * FB_WIDTH + x);
		}
	}
	wmb();

	before_04 = readl(iftu_p0 + 0x04);
	iftu_flip_to_paddr(FB1_PA);
	msleep(40);
	after_04 = readl(iftu_p0 + 0x04);

	pr_info("vita_iftu_probe: flipped to FB1 (0x%08lx): P0+04 0x%08x(active=%u) -> 0x%08x(active=%u)\n",
		FB1_PA, before_04, (before_04 >> 1) & 1,
		after_04, (after_04 >> 1) & 1);

	/* Hold for 2.5 seconds so the R/G/B banner is clearly visible on OLED */
	msleep(2500);

	iftu_flip_to_paddr(FB0_PA);
	msleep(40);
	restore_04 = readl(iftu_p0 + 0x04);

	pr_info("vita_iftu_probe: restored FB0 (0x%08lx): P0+04 -> 0x%08x(active=%u)\n",
		FB0_PA, restore_04, (restore_04 >> 1) & 1);

	iounmap(fb1_va);
	iounmap(fb0_va);
	return 0;
}

static ssize_t probe_proc_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	char cmd[32];
	size_t len = min(count, sizeof(cmd) - 1);

	if (copy_from_user(cmd, ubuf, len))
		return -EFAULT;
	cmd[len] = '\0';
	strim(cmd);

	if (strcmp(cmd, "status") == 0) {
		dump_status("manual");
	} else if (strcmp(cmd, "vblank") == 0) {
		start_and_measure_vblank();
	} else if (strcmp(cmd, "vblank_off") == 0) {
		stop_vblank_irq();
	} else if (strcmp(cmd, "flip") == 0) {
		run_flip_test();
	} else {
		pr_warn("vita_iftu_probe: unknown cmd '%s' (use: status, vblank, vblank_off, flip)\n",
			cmd);
	}

	return count;
}

static const struct proc_ops probe_proc_ops = {
	.proc_write = probe_proc_write,
};

static int __init vita_iftu_probe_init(void)
{
	iftu_p0 = ioremap(IFTU_P0_PA, 0x1000);
	iftu_creg0 = ioremap(IFTU_CREG0_PA, 0x1000);
	dsi0 = ioremap(DSI0_PA, 0x1000);
	if (!iftu_p0 || !iftu_creg0 || !dsi0) {
		if (iftu_p0)
			iounmap(iftu_p0);
		if (iftu_creg0)
			iounmap(iftu_creg0);
		if (dsi0)
			iounmap(dsi0);
		return -ENOMEM;
	}

	dump_status("boot");

	proc_ent = proc_create("vita_iftu_probe", 0200, NULL, &probe_proc_ops);
	if (!proc_ent) {
		iounmap(dsi0);
		iounmap(iftu_creg0);
		iounmap(iftu_p0);
		return -ENOMEM;
	}

	pr_info("vita_iftu_probe: ready (/proc/vita_iftu_probe: status, vblank, vblank_off, flip)\n");
	return 0;
}

static void __exit vita_iftu_probe_exit(void)
{
	proc_remove(proc_ent);
	stop_vblank_irq();

	/* Ensure both shadow config banks point back to FB0_PA (0x20000000) */
	writel(FB0_PA, iftu_p0 + IFTU_CFG_BASE(0) + IFTU_CFG_FB_PADDR);
	writel(FB0_PA, iftu_p0 + IFTU_CFG_BASE(1) + IFTU_CFG_FB_PADDR);
	wmb();
	writel(0, iftu_creg0 + IFTU_CREG_P0_CFG_SEL);
	(void)readl(iftu_creg0 + IFTU_CREG_P0_CFG_SEL);

	iounmap(dsi0);
	iounmap(iftu_creg0);
	iounmap(iftu_p0);
	pr_info("vita_iftu_probe: unloaded cleanly\n");
}

module_init(vita_iftu_probe_init);
module_exit(vita_iftu_probe_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LinuxOnVita");
MODULE_DESCRIPTION("Phase 3 PS Vita IFTU/DSI0 VBlank and Page-Flip Probe");
