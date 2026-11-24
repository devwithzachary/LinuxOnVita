/*
 * main.c - LinuxOnVita Phase 1 Read-Only GPU Probe App (GPUPROBE0)
 *
 * Copies bundled gpu_probe.skprx to ux0:data/gpu_probe.skprx temporarily,
 * captures Pervasive + IFTU MMIO before sceGxmInitialize(), then captures
 * Pervasive + IFTU + SceSGX543Reg after sceGxmInitialize(), and cleans up.
 */

#include <psp2/ctrl.h>
#include <psp2/gxm.h>
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

int main(int argc, char *argv[]) {
	(void)argc;
	(void)argv;

	psvDebugScreenInit();
	printf("==================================================\n");
	printf("  LinuxOnVita - Phase 1 Read-Only GPU Probe\n");
	printf("==================================================\n\n");

	int rc = copy_file("app0:gpu_probe.skprx", TMP_SKPRX);
	if (rc < 0) {
		printf("[ERROR] Failed to stage gpu_probe.skprx: 0x%08X\n", rc);
		goto done;
	}

	printf("[1/3] Capturing BEFORE SceGxmInitialize...\n");
	SceUID m1 = run_probe("before");
	printf("      Result: 0x%08X -> ux0:data/gpu_trace_before.txt\n", m1);

	printf("[2/3] Calling sceGxmInitialize()...\n");
	SceGxmInitializeParams gxm_params;
	memset(&gxm_params, 0, sizeof(gxm_params));
	gxm_params.flags                        = 0;
	gxm_params.displayQueueMaxPendingCount  = 2;
	gxm_params.displayQueueCallback         = NULL;
	gxm_params.displayQueueCallbackDataSize = 0;
	gxm_params.parameterBufferSize          = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;

	rc = sceGxmInitialize(&gxm_params);
	printf("      sceGxmInitialize() = 0x%08X\n", rc);

	if (rc >= 0) {
		printf("[3/3] Capturing AFTER SceGxmInitialize (incl. SceSGX543Reg)...\n");
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

