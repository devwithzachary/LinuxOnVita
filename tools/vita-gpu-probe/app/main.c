/*
 * main.c - LinuxOnVita Kernel Driver Segment Dumper (GPUPROBE0 v1.03)
 */

#include <psp2/ctrl.h>
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

int main(int argc, char *argv[]) {
	(void)argc;
	(void)argv;

	psvDebugScreenInit();
	printf("==================================================\n");
	printf("  LinuxOnVita - Kernel Driver Dumper v1.03\n");
	printf("==================================================\n\n");

	int rc = copy_file("app0:gpu_probe.skprx", TMP_SKPRX);
	if (rc < 0) {
		printf("[ERROR] Failed to stage gpu_probe.skprx: 0x%08X\n", rc);
		goto done;
	}

	printf("Dumping SceSyscon, ScePower, SceLowio, SceDisplay, SceGpuEs4...\n");
	tai_module_args_t argg;
	memset(&argg, 0, sizeof(argg));
	argg.size = sizeof(argg);
	argg.pid  = KERNEL_PID;
	SceUID mod = taiLoadStartKernelModuleForUser(TMP_SKPRX, &argg);
	printf("Result: 0x%08X\n\n", mod);

	sceIoRemove(TMP_SKPRX);
	sceIoSync("ux0:", 0);

	printf("Saved to ux0:data/:\n");
	printf("  - kdump_info.txt\n");
	printf("  - kdump_SceSyscon.bin\n");
	printf("  - kdump_ScePower.bin\n");
	printf("  - kdump_SceLowio.bin\n");
	printf("  - kdump_SceDisplay.bin\n");
	printf("  - kdump_SceGpuEs4.bin\n");
	printf("  - kdump_SceExcpmgr.bin\n");

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
