/*
 * gpu_probe.c - Phase 1/2 Read-Only MMIO, Elmo PMIC & Syscon Power Tracer
 *
 * Strictly read-only observation:
 *   - "before" -> snapshots Elmo 0x883, Bus/Xbar clocks, Pervasive, Pervasive2,
 *                 and IFTU to ux0:data/gpu_trace_before.txt
 *   - "after"  -> snapshots all of the above + SceSGX543Reg (0xE8400000) +
 *                 GPU RegBus (0xED800000) to ux0:data/gpu_trace_after.txt
 *   - "hook"   -> stays resident briefly to log Sony's own ScePower -> SceSyscon
 *                 calls (0x88E DD4/VDDG, etc.) into a RAM buffer, then writes
 *                 ux0:data/gpu_trace_power.txt on module_stop.
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

static char g_hook_log[8192];
static uint32_t g_hook_len = 0;

static void hook_append(const char *s) {
	if (!s) return;
	uint32_t n = strlen(s);
	if (g_hook_len + n + 1 < sizeof(g_hook_log)) {
		memcpy(&g_hook_log[g_hook_len], s, n);
		g_hook_len += n;
		g_hook_log[g_hook_len] = '\0';
	}
}

static void log_str(SceUID fd, const char *s) {
	if (fd >= 0 && s) {
		ksceIoWrite(fd, s, strlen(s));
	}
}

static uint32_t query_smc(uint32_t cmd, uint32_t a0) {
	register uint32_t r0 asm("r0") = a0;
	register uint32_t r1 asm("r1") = 0;
	register uint32_t r2 asm("r2") = 0;
	register uint32_t r3 asm("r3") = 0;
	register uint32_t r12 asm("r12") = cmd;

	asm volatile(
		"smc #0\n\t"
		: "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
		: "r"(r12)
		: "cc", "memory"
	);
	return r0;
}

static void dump_power_and_clocks(SceUID fd) {
	char line[192];
	uint8_t pwr[16];
	memset(pwr, 0, sizeof(pwr));

	int rc = ksceSysconReadCommand(0x0883, pwr, 8);
	snprintf(line, sizeof(line),
	         "\n=== ELMO PMIC POWER STATUS (Syscon 0x883, rc=0x%08X) ===\n"
	         "Elmo 0xA0..0xA3: %02X %02X %02X %02X %02X %02X %02X %02X\n",
	         rc, pwr[0], pwr[1], pwr[2], pwr[3], pwr[4], pwr[5], pwr[6], pwr[7]);
	log_str(fd, line);

	int arm_mhz  = kscePowerGetArmClockFrequency();
	int bus_mhz  = kscePowerGetBusClockFrequency();
	int sys_mhz  = kscePowerGetSysClockFrequency();
	int xbar_mhz = kscePowerGetGpuXbarClockFrequency();
	uint32_t smc_bus  = query_smc(0x114, 1);
	uint32_t smc_xbar = query_smc(0x114, 3);

	snprintf(line, sizeof(line),
	         "Clocks (MHz): ARM=%d BUS=%d SYS=%d GPU_XBAR=%d\n"
	         "SMC 0x114: bus_lvl=0x%08lX gpu_xbar_lvl=0x%08lX\n",
	         arm_mhz, bus_mhz, sys_mhz, xbar_mhz,
	         (unsigned long)smc_bus, (unsigned long)smc_xbar);
	log_str(fd, line);
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

/* Read-only taiHEN hooks for observing Sony's ScePower -> SceSyscon calls */
#define SCE_SYSCON_LIB_NID 0x60A35F64

static tai_hook_ref_t g_ref_send_cmd;
static SceUID g_uid_send_cmd = -1;
static tai_hook_ref_t g_ref_ctrl_volt;
static SceUID g_uid_ctrl_volt = -1;
static tai_hook_ref_t g_ref_884, g_ref_885, g_ref_88b, g_ref_88c, g_ref_88d;
static SceUID g_uid_884 = -1, g_uid_885 = -1, g_uid_88b = -1, g_uid_88c = -1, g_uid_88d = -1;

static int hook_ksceSysconSendCommand(int cmd, const void *buffer, unsigned int size) {
	char msg[160];
	const uint8_t *b = (const uint8_t *)buffer;
	uint32_t b0 = (b && size > 0) ? b[0] : 0;
	uint32_t b1 = (b && size > 1) ? b[1] : 0;
	uint32_t b2 = (b && size > 2) ? b[2] : 0;
	uint32_t b3 = (b && size > 3) ? b[3] : 0;
	snprintf(msg, sizeof(msg),
	         "ksceSysconSendCommand(cmd=0x%04X, size=%u, data=%02lX %02lX %02lX %02lX)\n",
	         cmd, size,
	         (unsigned long)b0, (unsigned long)b1, (unsigned long)b2, (unsigned long)b3);
	hook_append(msg);
	return TAI_CONTINUE(int, g_ref_send_cmd, cmd, buffer, size);
}

static int hook_sceSysconCtrlVoltage(uint32_t type, uint32_t vid) {
	char msg[96];
	snprintf(msg, sizeof(msg), "sceSysconCtrlVoltageForDriver(type=%lu, vid=0x%08lX)\n",
	         (unsigned long)type, (unsigned long)vid);
	hook_append(msg);
	return TAI_CONTINUE(int, g_ref_ctrl_volt, type, vid);
}

#define DEF_SHORT_HOOK(name, ref_var, cmd_hex) \
static int name(uint32_t a0, uint32_t a1) { \
	char msg[96]; \
	snprintf(msg, sizeof(msg), "Syscon_" cmd_hex "(0x%08lX, 0x%08lX)\n", \
	         (unsigned long)a0, (unsigned long)a1); \
	hook_append(msg); \
	return TAI_CONTINUE(int, ref_var, a0, a1); \
}

DEF_SHORT_HOOK(hook_884, g_ref_884, "0x884")
DEF_SHORT_HOOK(hook_885, g_ref_885, "0x885")
DEF_SHORT_HOOK(hook_88b, g_ref_88b, "0x88B")
DEF_SHORT_HOOK(hook_88c, g_ref_88c, "0x88C")
DEF_SHORT_HOOK(hook_88d, g_ref_88d, "0x88D")

static int g_is_hook_mode = 0;

void _start() __attribute__((weak, alias("module_start")));

int module_start(SceSize argc, const void *args) {
	const char *mode = (argc > 0 && args != NULL) ? (const char *)args : "before";

	if (strncmp(mode, "hook", 4) == 0) {
		g_is_hook_mode = 1;
		g_hook_len = 0;
		hook_append("LinuxOnVita Read-Only ScePower -> SceSyscon Trace\n");

		g_uid_send_cmd = taiHookFunctionExportForKernel(KERNEL_PID, &g_ref_send_cmd,
			"SceSyscon", SCE_SYSCON_LIB_NID, 0xE26488B9, hook_ksceSysconSendCommand);
		g_uid_ctrl_volt = taiHookFunctionExportForKernel(KERNEL_PID, &g_ref_ctrl_volt,
			"SceSyscon", SCE_SYSCON_LIB_NID, 0x7F198FA2, hook_sceSysconCtrlVoltage);
		g_uid_884 = taiHookFunctionExportForKernel(KERNEL_PID, &g_ref_884,
			"SceSyscon", SCE_SYSCON_LIB_NID, 0x3FDD29D6, hook_884);
		g_uid_885 = taiHookFunctionExportForKernel(KERNEL_PID, &g_ref_885,
			"SceSyscon", SCE_SYSCON_LIB_NID, 0x79E6DD8B, hook_885);
		g_uid_88b = taiHookFunctionExportForKernel(KERNEL_PID, &g_ref_88b,
			"SceSyscon", SCE_SYSCON_LIB_NID, 0x5A614349, hook_88b);
		g_uid_88c = taiHookFunctionExportForKernel(KERNEL_PID, &g_ref_88c,
			"SceSyscon", SCE_SYSCON_LIB_NID, 0xB872E904, hook_88c);
		g_uid_88d = taiHookFunctionExportForKernel(KERNEL_PID, &g_ref_88d,
			"SceSyscon", SCE_SYSCON_LIB_NID, 0xDD16ABD9, hook_88d);

		char hdr[192];
		snprintf(hdr, sizeof(hdr),
		         "Hook UIDs: send=0x%X volt=0x%X 884=0x%X 885=0x%X 88b=0x%X 88c=0x%X 88d=0x%X\n",
		         g_uid_send_cmd, g_uid_ctrl_volt, g_uid_884, g_uid_885,
		         g_uid_88b, g_uid_88c, g_uid_88d);
		hook_append(hdr);
		return SCE_KERNEL_START_SUCCESS;
	}

	int is_after = (strncmp(mode, "after", 5) == 0);
	const char *out_path = is_after ? "ux0:data/gpu_trace_after.txt"
	                                : "ux0:data/gpu_trace_before.txt";

	SceUID fd = ksceIoOpen(out_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd < 0) {
		return SCE_KERNEL_START_NO_RESIDENT;
	}

	log_str(fd, is_after ? "LinuxOnVita Phase 2 Read-Only GPU Trace (AFTER SceGxmInitialize)\n"
	                     : "LinuxOnVita Phase 2 Read-Only GPU Trace (BEFORE SceGxmInitialize)\n");

	dump_power_and_clocks(fd);

	/* Pervasive blocks */
	dump_mmio_ro(fd, "PERVASIVE_MISC",    0xE3100000, 0x1000, 0x0000, 0x200);
	dump_mmio_ro(fd, "PERVASIVE_RESET",   0xE3101000, 0x1000, 0x0000, 0x200);
	dump_mmio_ro(fd, "PERVASIVE_GATE",    0xE3102000, 0x1000, 0x0000, 0x200);
	dump_mmio_ro(fd, "PERVASIVE_BASECLK", 0xE3103000, 0x1000, 0x0000, 0x200);
	dump_mmio_ro(fd, "PERVASIVE2",        0xE3110000, 0x1000, 0x0000, 0x100);

	/* Display Engine (IFTU Bus 0 + Control) */
	dump_mmio_ro(fd, "IFTU_BUS0_P0",      0xE5020000, 0x4000, 0x0000, 0x100);
	dump_mmio_ro(fd, "IFTU_BUS0_CREG",    0xE5020000, 0x4000, 0x2000, 0x080);

	if (is_after) {
		dump_mmio_ro(fd, "GPU_REGBUS",        0xED800000, 0x1000, 0x0000, 0x100);
		dump_mmio_ro(fd, "SceSGX543Reg_0000", 0xE8400000, 0x2000, 0x0000, 0x200);
		dump_mmio_ro(fd, "SceSGX543Reg_0C00", 0xE8400000, 0x2000, 0x0C00, 0x100);
	}

	ksceIoClose(fd);
	return SCE_KERNEL_START_NO_RESIDENT;
}

int module_stop(SceSize argc, const void *args) {
	(void)argc;
	(void)args;

	if (g_is_hook_mode) {
		if (g_uid_88d >= 0) taiHookReleaseForKernel(g_uid_88d, g_ref_88d);
		if (g_uid_88c >= 0) taiHookReleaseForKernel(g_uid_88c, g_ref_88c);
		if (g_uid_88b >= 0) taiHookReleaseForKernel(g_uid_88b, g_ref_88b);
		if (g_uid_885 >= 0) taiHookReleaseForKernel(g_uid_885, g_ref_885);
		if (g_uid_884 >= 0) taiHookReleaseForKernel(g_uid_884, g_ref_884);
		if (g_uid_ctrl_volt >= 0) taiHookReleaseForKernel(g_uid_ctrl_volt, g_ref_ctrl_volt);
		if (g_uid_send_cmd >= 0) taiHookReleaseForKernel(g_uid_send_cmd, g_ref_send_cmd);

		SceUID fd = ksceIoOpen("ux0:data/gpu_trace_power.txt",
		                       SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
		if (fd >= 0) {
			uint8_t pwr[16];
			memset(pwr, 0, sizeof(pwr));
			ksceSysconReadCommand(0x0883, pwr, 8);
			char tail[128];
			snprintf(tail, sizeof(tail),
			         "\nPost-test Elmo 0x883: %02X %02X %02X %02X %02X %02X %02X %02X\n",
			         pwr[0], pwr[1], pwr[2], pwr[3], pwr[4], pwr[5], pwr[6], pwr[7]);
			hook_append(tail);

			ksceIoWrite(fd, g_hook_log, g_hook_len);
			ksceIoClose(fd);
		}
		g_is_hook_mode = 0;
	}

	return SCE_KERNEL_STOP_SUCCESS;
}
