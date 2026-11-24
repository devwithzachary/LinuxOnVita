/*
 * gpu_probe.c - Phase 1 Read-Only MMIO Snapshot Kernel Module
 *
 * Strictly read-only (SCE_KERNEL_MEMBLOCK_TYPE_KERNEL_IO_SO_R).
 * Never writes to any hardware register. Maps Pervasive, IFTU, and
 * optionally SceSGX543Reg (0xE8400000) and logs 32-bit words to a file.
 *
 * Args passed from userland:
 *   "before" -> dumps Pervasive + IFTU only -> ux0:data/gpu_trace_before.txt
 *   "after"  -> dumps Pervasive + IFTU + SceSGX543Reg -> ux0:data/gpu_trace_after.txt
 */

#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/io/fcntl.h>
#include <psp2kern/kernel/sysclib.h>
#include <stdint.h>
#include <string.h>

static void log_str(SceUID fd, const char *s) {
	if (fd >= 0 && s) {
		ksceIoWrite(fd, s, strlen(s));
	}
}

static void dump_mmio_ro(SceUID fd, const char *label, uint32_t paddr,
                         uint32_t map_size, uint32_t dump_offset, uint32_t dump_bytes) {
	char line[128];
	SceKernelAllocMemBlockKernelOpt opt;
	memset(&opt, 0, sizeof(opt));
	opt.size  = sizeof(opt);
	opt.attr  = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_PADDR;
	opt.paddr = paddr;

	SceUID uid = ksceKernelAllocMemBlock("gpu_ro",
	                                     SCE_KERNEL_MEMBLOCK_TYPE_KERNEL_IO_SO_R,
	                                     map_size, &opt);
	if (uid < 0) {
		snprintf(line, sizeof(line), "[%s] map 0x%08lX failed: 0x%08X\n",
		         label, (unsigned long)paddr, uid);
		log_str(fd, line);
		return;
	}

	void *base = NULL;
	int rc = ksceKernelGetMemBlockBase(uid, &base);
	if (rc < 0 || !base) {
		snprintf(line, sizeof(line), "[%s] getbase 0x%08lX failed: 0x%08X\n",
		         label, (unsigned long)paddr, rc);
		log_str(fd, line);
		ksceKernelFreeMemBlock(uid);
		return;
	}

	snprintf(line, sizeof(line), "\n=== %s (PA 0x%08lX + 0x%04lX) ===\n",
	         label, (unsigned long)paddr, (unsigned long)dump_offset);
	log_str(fd, line);

	volatile const uint32_t *regs = (volatile const uint32_t *)((const uint8_t *)base + dump_offset);
	uint32_t words = dump_bytes / 4;
	for (uint32_t i = 0; i < words; i += 4) {
		uint32_t off = dump_offset + i * 4;
		uint32_t v0 = regs[i + 0];
		uint32_t v1 = (i + 1 < words) ? regs[i + 1] : 0;
		uint32_t v2 = (i + 2 < words) ? regs[i + 2] : 0;
		uint32_t v3 = (i + 3 < words) ? regs[i + 3] : 0;
		snprintf(line, sizeof(line), "+0x%04lX: %08lX %08lX %08lX %08lX\n",
		         (unsigned long)off,
		         (unsigned long)v0, (unsigned long)v1,
		         (unsigned long)v2, (unsigned long)v3);
		log_str(fd, line);
	}

	ksceKernelFreeMemBlock(uid);
}

void _start() __attribute__((weak, alias("module_start")));

int module_start(SceSize argc, const void *args) {
	int is_after = 0;
	if (argc > 0 && args != NULL) {
		const char *mode = (const char *)args;
		if (strncmp(mode, "after", 5) == 0) {
			is_after = 1;
		}
	}

	const char *out_path = is_after ? "ux0:data/gpu_trace_after.txt"
	                                : "ux0:data/gpu_trace_before.txt";

	SceUID fd = ksceIoOpen(out_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd < 0) {
		return SCE_KERNEL_START_NO_RESIDENT;
	}

	log_str(fd, is_after ? "LinuxOnVita Phase 1 Read-Only GPU Trace (AFTER SceGxmInitialize)\n"
	                     : "LinuxOnVita Phase 1 Read-Only GPU Trace (BEFORE SceGxmInitialize)\n");

	/* Pervasive blocks (always clocked) */
	dump_mmio_ro(fd, "PERVASIVE_MISC",    0xE3100000, 0x1000, 0x0000, 0x200);
	dump_mmio_ro(fd, "PERVASIVE_RESET",   0xE3101000, 0x1000, 0x0000, 0x200);
	dump_mmio_ro(fd, "PERVASIVE_GATE",    0xE3102000, 0x1000, 0x0000, 0x200);
	dump_mmio_ro(fd, "PERVASIVE_BASECLK", 0xE3103000, 0x1000, 0x0000, 0x200);

	/* Display Engine (IFTU Bus 0 + Control) */
	dump_mmio_ro(fd, "IFTU_BUS0_P0",      0xE5020000, 0x4000, 0x0000, 0x100);
	dump_mmio_ro(fd, "IFTU_BUS0_CREG",    0xE5020000, 0x4000, 0x2000, 0x080);

	/* Only probe SceSGX543Reg when SceGxmInitialize() has enabled GPU clocks/reset */
	if (is_after) {
		dump_mmio_ro(fd, "SceSGX543Reg_0000", 0xE8400000, 0x2000, 0x0000, 0x200);
		dump_mmio_ro(fd, "SceSGX543Reg_0C00", 0xE8400000, 0x2000, 0x0C00, 0x100);
	}

	ksceIoClose(fd);
	return SCE_KERNEL_START_NO_RESIDENT;
}

int module_stop(SceSize argc, const void *args) {
	(void)argc;
	(void)args;
	return SCE_KERNEL_STOP_SUCCESS;
}

