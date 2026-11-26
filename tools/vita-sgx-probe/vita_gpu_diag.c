// SPDX-License-Identifier: GPL-2.0
/*
 * vita_gpu_diag.c - Standby CDRAM Spy Reader (PA 0x27F00000 / 0x40F00000)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

#define SPY_MAGIC	0x53505931U /* "SPY1" */
#define SPY_MAX_PKTS	120

struct spy_pkt_entry {
	u32 seq;
	u8  tx[12];
	u8  rx[12];
};

static bool dump_spy = true;
module_param(dump_spy, bool, 0444);
MODULE_PARM_DESC(dump_spy, "Dump standby Syscon SPI packets from PA 0x27F00000/0x40F00000");

static void dump_standby_spy_buffers(void)
{
	static const phys_addr_t pas[2] = { 0x27F00000UL, 0x40F00000UL };
	int i;

	for (i = 0; i < 2; i++) {
		void __iomem *base = ioremap(pas[i], 0x2000);
		u32 magic, count, paddr, n, j;

		if (!base)
			continue;

		magic = readl(base + 0);
		paddr = readl(base + 4);
		count = readl(base + 8);

		pr_info("vita_gpu_diag: spy check PA 0x%08lx -> magic=0x%08x paddr=0x%08x count=%u\n",
			(unsigned long)pas[i], magic, paddr, count);

		if (magic == SPY_MAGIC && count > 0) {
			n = count < SPY_MAX_PKTS ? count : SPY_MAX_PKTS;
			for (j = 0; j < n; j++) {
				struct spy_pkt_entry e;

				memcpy_fromio(&e, base + 16 + j * sizeof(e), sizeof(e));
				pr_info("vita_gpu_diag: SPY[slot=%03u seq=%03u] cmd=0x%04x tx=[%02x %02x len=%02x | %02x %02x %02x %02x %02x %02x] rx=[%02x %02x len=%02x res=%02x | %02x %02x %02x %02x %02x %02x %02x %02x]\n",
					j, e.seq,
					(u16)e.tx[0] | ((u16)e.tx[1] << 8),
					e.tx[0], e.tx[1], e.tx[2],
					e.tx[3], e.tx[4], e.tx[5], e.tx[6], e.tx[7], e.tx[8],
					e.rx[0], e.rx[1], e.rx[2], e.rx[3],
					e.rx[4], e.rx[5], e.rx[6], e.rx[7], e.rx[8], e.rx[9], e.rx[10], e.rx[11]);
			}
		}

		iounmap(base);
	}
}

static int __init vita_gpu_diag_init(void)
{
	if (dump_spy)
		dump_standby_spy_buffers();
	return 0;
}

static void __exit vita_gpu_diag_exit(void)
{
	pr_info("vita_gpu_diag: unloaded\n");
}

module_init(vita_gpu_diag_init);
module_exit(vita_gpu_diag_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LinuxOnVita");
MODULE_DESCRIPTION("Standby CDRAM spy reader");
