// SPDX-License-Identifier: GPL-2.0
/*
 * vita_sgx_probe.c - Phase 2 Safe Read-Only Pervasive & Display State Dump
 *
 * Strictly read-only: dumps PERVASIVE_MISC, PERVASIVE_RESET, PERVASIVE_GATE,
 * PERVASIVE_BASECLK, and IFTU_BUS0_CREG to dmesg so we can diff Linux's boot
 * state against VitaOS's gpu_trace_after.txt before probing 0xE8400000.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

static void dump_block(const char *label, phys_addr_t pa, size_t bytes)
{
	void __iomem *base = ioremap(pa, 0x1000);
	size_t i;

	if (!base) {
		pr_err("vita_sgx_probe: ioremap(0x%08lx) failed\n", (unsigned long)pa);
		return;
	}

	pr_info("vita_sgx_probe: === %s (PA 0x%08lx) ===\n", label, (unsigned long)pa);
	for (i = 0; i < bytes; i += 16) {
		u32 v0 = readl(base + i + 0);
		u32 v1 = readl(base + i + 4);
		u32 v2 = readl(base + i + 8);
		u32 v3 = readl(base + i + 12);
		if (v0 || v1 || v2 || v3) {
			pr_info("vita_sgx_probe: %s +0x%04zx: %08x %08x %08x %08x\n",
				label, i, v0, v1, v2, v3);
		}
	}

	iounmap(base);
}

static int __init vita_sgx_probe_init(void)
{
	dump_block("MISC",    0xE3100000, 0x200);
	dump_block("RESET",   0xE3101000, 0x200);
	dump_block("GATE",    0xE3102000, 0x200);
	dump_block("BASECLK", 0xE3103000, 0x200);
	return 0;
}

static void __exit vita_sgx_probe_exit(void)
{
	pr_info("vita_sgx_probe: unloaded\n");
}

module_init(vita_sgx_probe_init);
module_exit(vita_sgx_probe_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LinuxOnVita");
MODULE_DESCRIPTION("Phase 2 safe read-only Pervasive state dumper");
