/*
 * gpu_probe.c - Phase 2 Read-Only Kernel Module Segment & Export Dumper (v1.03)
 *
 * Strictly read-only: dumps the in-RAM .text/.rodata segments (including export
 * and import tables) of SceSyscon, ScePower, SceLowio, SceDisplay, and SceGpuEs4
 * to ux0:data/kdump_*.bin + ux0:data/kdump_info.txt so we have 100% ground-truth
 * disassembly of:
 *   1. How VitaOS switches Elmo DD4/VDDG ON (byte[5] 0x40 -> 0xC0)
 *   2. Exact SceLowio / ScePervasive / SMC GPU bus initialization
 *   3. SceDisplay IFTU VBlank & plane registers (for Phase 3 vita-drm)
 *   4. SceGpuEs4 SGX543MP4+ SysInitialise, IRQ 52, and BIF MMU setup (Phase 4)
 */

#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/syscon.h>
#include <psp2kern/power.h>
#include <psp2kern/io/fcntl.h>
#include <psp2kern/kernel/sysclib.h>
#include <taihen.h>
#include <stdint.h>
#include <string.h>

static void log_str(SceUID fd, const char *s) {
	if (fd >= 0 && s) {
		ksceIoWrite(fd, s, strlen(s));
	}
}

static void dump_kernel_module_text(SceUID info_fd, const char *modname) {
	char line[256];
	char out_path[128];
	tai_module_info_t tinfo;
	memset(&tinfo, 0, sizeof(tinfo));
	tinfo.size = sizeof(tinfo);

	int rc = taiGetModuleInfoForKernel(KERNEL_PID, modname, &tinfo);
	if (rc < 0) {
		snprintf(line, sizeof(line), "[%s] taiGetModuleInfoForKernel failed: 0x%08X\n",
		         modname, rc);
		log_str(info_fd, line);
		return;
	}

	SceUID mb = ksceKernelFindMemBlockByAddr((const void *)tinfo.exports_start, 0);
	if (mb < 0) {
		snprintf(line, sizeof(line), "[%s] FindMemBlock(0x%08lX) failed: 0x%08X\n",
		         modname, (unsigned long)tinfo.exports_start, mb);
		log_str(info_fd, line);
		return;
	}

	void *base = NULL;
	SceSize map_size = 0;
	int b_rc = ksceKernelGetMemBlockBase(mb, &base);
	int s_rc = ksceKernelGetMemBlockAllocMapSize(mb, &map_size);
	if (b_rc < 0 || s_rc < 0 || !base || map_size == 0) {
		snprintf(line, sizeof(line),
		         "[%s] GetBase/Size failed: b=0x%08X s=0x%08X base=%p sz=0x%X\n",
		         modname, b_rc, s_rc, base, map_size);
		log_str(info_fd, line);
		return;
	}

	snprintf(out_path, sizeof(out_path), "ux0:data/kdump_%s.bin", modname);
	SceUID bfd = ksceIoOpen(out_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (bfd < 0) {
		snprintf(line, sizeof(line), "[%s] open %s failed: 0x%08X\n",
		         modname, out_path, bfd);
		log_str(info_fd, line);
		return;
	}

	/* Write in 4 KB chunks via stack buffer to avoid any kernel pointer restrictions in IoWrite */
	uint8_t chunk[1024];
	uint32_t written = 0;
	while (written < map_size) {
		uint32_t n = map_size - written;
		if (n > sizeof(chunk))
			n = sizeof(chunk);
		memcpy(chunk, (const uint8_t *)base + written, n);
		int w = ksceIoWrite(bfd, chunk, n);
		if (w <= 0)
			break;
		written += (uint32_t)w;
	}
	ksceIoClose(bfd);

	snprintf(line, sizeof(line),
	         "MODULE %s: modid=0x%08X nid=0x%08lX text_base=0x%08lX text_size=0x%08lX (wrote=0x%08lX) "
	         "exports=[0x%08lX..0x%08lX] imports=[0x%08lX..0x%08lX]\n",
	         modname, tinfo.modid, (unsigned long)tinfo.module_nid,
	         (unsigned long)(uintptr_t)base, (unsigned long)map_size, (unsigned long)written,
	         (unsigned long)tinfo.exports_start, (unsigned long)tinfo.exports_end,
	         (unsigned long)tinfo.imports_start, (unsigned long)tinfo.imports_end);
	log_str(info_fd, line);
}

void _start() __attribute__((weak, alias("module_start")));

int module_start(SceSize argc, const void *args) {
	(void)argc;
	(void)args;

	SceUID fd = ksceIoOpen("ux0:data/kdump_info.txt",
	                       SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd < 0)
		return SCE_KERNEL_START_NO_RESIDENT;

	log_str(fd, "LinuxOnVita v1.03 Kernel Module Segment & Export Dump\n\n");

	static const char * const mods[] = {
		"SceSyscon",
		"ScePower",
		"SceLowio",
		"SceDisplay",
		"SceGpuEs4",
		"SceExcpmgr",
	};

	for (unsigned int i = 0; i < sizeof(mods) / sizeof(mods[0]); i++) {
		dump_kernel_module_text(fd, mods[i]);
	}

	ksceIoClose(fd);
	return SCE_KERNEL_START_NO_RESIDENT;
}

int module_stop(SceSize argc, const void *args) {
	(void)argc;
	(void)args;
	return SCE_KERNEL_STOP_SUCCESS;
}
