/*
 * main.c - LinuxOnVita Read-Only GPU & Power Rail Probe App (GPUPROBE0 v1.01)
 *
 * Captures:
 *   1. ux0:data/gpu_trace_before.txt (Elmo 0x883, clocks, SMC 0x114, Pervasive, P2)
 *   2. ux0:data/gpu_trace_power.txt  (Sony's ScePower -> SceSyscon calls during
 *      sceGxmInitialize + standard stock GPU/Xbar clock transitions)
 *   3. ux0:data/gpu_trace_after.txt  (Elmo 0x883, GPU RegBus 0xED800000, SGX543)
 */

#include <psp2/ctrl.h>
#include <psp2/gxm.h>
#include <psp2/power.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <taihen.h>

#include <stdio.h>
#include <string.h>

#include "../../../app/debugScreen.h"

#define printf psvDebugScreenPrintf

#define TMP_SKPRX "ux0:data/gpu_probe.skprx"

static int copy_file(const char *src, const char *dst) {
	SceUID in = sceIoOpen(src, SCE_O_RDONLY, 0);
	if (in < 0) return in;

	sceIoMkdir("ux0:data", 0777);
	SceUID out = sceIoOpen(dst, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (out < 0) {
		sceIoClose(in);
		return out;
	}

	char buf[4096];
	int n;
	while ((n = sceIoRead(in, buf, sizeof(buf))) > 0) {
		sceIoWrite(out, buf, n);
	}

	sceIoClose(out);
	sceIoClose(in);
	return 0;
}

static SceUID run_probe(const char *mode) {
	tai_module_args_t argg;
	memset(&argg, 0, sizeof(argg));
	argg.size  = sizeof(argg);
	argg.pid   = KERNEL_PID;
	argg.args  = (SceSize)(strlen(mode) + 1);
	argg.argp  = (void *)mode;
	argg.flags = 0;

	return taiLoadStartKernelModuleForUser(TMP_SKPRX, &argg);
}

static int stop_probe(SceUID mod_id) {
	tai_module_args_t argg;
	memset(&argg, 0, sizeof(argg));
	argg.size = sizeof(argg);
	argg.pid  = KERNEL_PID;
	return taiStopUnloadKernelModuleForUser(mod_id, &argg, NULL, NULL);
}

int main(int argc, char *argv[]) {
	(void)argc;
	(void)argv;

	psvDebugScreenInit();
	printf("==================================================\n");
	printf("  LinuxOnVita - GPU & Power Rail Probe v1.01\n");
	printf("==================================================\n\n");

	int rc = copy_file("app0:gpu_probe.skprx", TMP_SKPRX);
	if (rc < 0) {
		printf("[ERROR] Failed to stage gpu_probe.skprx: 0x%08X\n", rc);
		goto done;
	}

	printf("[1/4] Capturing BEFORE SceGxmInitialize...\n");
	SceUID m1 = run_probe("before");
	printf("      Result: 0x%08X -> ux0:data/gpu_trace_before.txt\n", m1);

	printf("[2/4] Starting Syscon hook & calling sceGxmInitialize()...\n");
	SceUID m_hook = run_probe("hook");
	printf("      Hook module UID: 0x%08X\n", m_hook);

	SceGxmInitializeParams gxm_params;
	memset(&gxm_params, 0, sizeof(gxm_params));
	gxm_params.flags                        = 0;
	gxm_params.displayQueueMaxPendingCount  = 2;
	gxm_params.displayQueueCallback         = NULL;
	gxm_params.displayQueueCallbackDataSize = 0;
	gxm_params.parameterBufferSize          = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;

	rc = sceGxmInitialize(&gxm_params);
	printf("      sceGxmInitialize() = 0x%08X\n", rc);

	/* Exercise official stock VitaOS ScePower APIs so hooks observe DD4/VDDG */
	scePowerSetGpuClockFrequency(222);
	scePowerSetGpuXbarClockFrequency(166);
	sceKernelDelayThread(20 * 1000);
	scePowerSetGpuClockFrequency(111);
	scePowerSetGpuXbarClockFrequency(111);
	sceKernelDelayThread(20 * 1000);

	if (m_hook >= 0) {
		int s_rc = stop_probe(m_hook);
		printf("[3/4] Stopped hook (0x%08X) -> ux0:data/gpu_trace_power.txt\n", s_rc);
	}

	if (rc >= 0) {
		printf("[4/4] Capturing AFTER SceGxmInitialize...\n");
		SceUID m2 = run_probe("after");
		printf("      Result: 0x%08X -> ux0:data/gpu_trace_after.txt\n", m2);
		sceGxmTerminate();
	} else {
		printf("[WARN] Skipping AFTER trace because sceGxmInitialize failed.\n");
	}

	sceIoRemove(TMP_SKPRX);
	sceIoSync("ux0:", 0);

	printf("\nDone! Files saved on ux0:data/:\n");
	printf("  - ux0:data/gpu_trace_before.txt\n");
	printf("  - ux0:data/gpu_trace_power.txt\n");
	printf("  - ux0:data/gpu_trace_after.txt\n");

done:
	printf("\nPress CROSS (X) or START to exit.\n");
	SceCtrlData pad;
	while (1) {
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if (pad.buttons & (SCE_CTRL_CROSS | SCE_CTRL_START)) {
			break;
		}
		sceKernelDelayThread(16 * 1000);
	}

	sceKernelExitProcess(0);
	return 0;
}
