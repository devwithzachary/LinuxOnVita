/*
 * LinuxOnVita - PlayStation Vita Linux Bootstrapper & Installer
 *
 * Original kplugin loader by xerpi (2016)
 * Linux theming and file checks by CreepNT (2018)
 * Updates by DvaMishkiLapa (2020)
 * Storage mount selection and on-device installer by DevWithZachary (2026)
 *
 * Licensed under the GNU General Public License v3.0 (GPL-3.0)
 */

#include "debugScreen.h"
#include <psp2/ctrl.h>
#include <psp2/io/devctl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/vshbridge.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <taihen.h>

#define printf(...) psvDebugScreenPrintf(__VA_ARGS__)

#define CONFIG_DIR_UR0 "ur0:data/LinuxOnVita"
#define CONFIG_PATH_UR0 "ur0:data/LinuxOnVita/mount.cfg"
#define CONFIG_DIR_UX0 "ux0:data/LinuxOnVita"
#define CONFIG_PATH_UX0 "ux0:data/LinuxOnVita/mount.cfg"

typedef struct {
  const char *name;
  int is_mounted;
  uint64_t total_bytes;
  uint64_t free_bytes;
} MountEntry;

static MountEntry g_mounts[] = {
    {"xmc0:", 0, 0, 0},
    {"ux0:", 0, 0, 0},
    {"uma0:", 0, 0, 0},
    {"imc0:", 0, 0, 0},
};
#define NUM_MOUNTS (sizeof(g_mounts) / sizeof(g_mounts[0]))
static int g_selected_mount_idx = 0;

typedef struct {
  const char *src_rel;
  const char *dst_rel;
  int is_mandatory;
  int overwrite_if_exists;
} BundleFile;

static const BundleFile g_bundle_files[] = {
    {"baremetal-loader.skprx", "baremetal-loader.skprx", 1, 1},
    {"baremetal-loader_360.skprx", "baremetal-loader_360.skprx", 0, 1},
    {"payload.bin", "payload.bin", 1, 1},
    {"zImage", "zImage", 1, 1},
    {"vita.dtb", "vita.dtb", 1, 1},
    {"vita1000.dtb", "vita1000.dtb", 0, 1},
    {"vita2000.dtb", "vita2000.dtb", 0, 1},
    {"pstv.dtb", "pstv.dtb", 0, 1},
    {"wpa_supplicant.conf", "wpa_supplicant.conf", 0, 0},
};

typedef struct {
  int kernel_present;
  int dtb_present;
  int payload_present;
  int loader_present;
  int wifi_present;
} MountFileStatus;

static int file_exists(const char *path) {
  SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
  if (fd >= 0) {
    sceIoClose(fd);
    return 1;
  }
  return 0;
}

static int dir_exists(const char *path) {
  SceUID dfd = sceIoDopen(path);
  if (dfd >= 0) {
    sceIoDclose(dfd);
    return 1;
  }
  return 0;
}

static int mount_partition(const char *mount_name) {
  SceVshMountId id;
  if (strcmp(mount_name, "xmc0:") == 0)
    id = SCE_VSH_MOUNT_XMC0;
  else if (strcmp(mount_name, "uma0:") == 0)
    id = SCE_VSH_MOUNT_UMA0;
  else if (strcmp(mount_name, "imc0:") == 0)
    id = SCE_VSH_MOUNT_IMC0;
  else if (strcmp(mount_name, "ux0:") == 0)
    id = SCE_VSH_MOUNT_UX0;
  else
    return -1;

  char buf[0x100];
  memset(buf, 0, sizeof(buf));
  return _vshIoMount(id, NULL, 2, buf);
}

static void refresh_mounts(void) {
  for (size_t i = 0; i < NUM_MOUNTS; i++) {
    g_mounts[i].is_mounted = 0;
    g_mounts[i].total_bytes = 0;
    g_mounts[i].free_bytes = 0;

    SceIoDevInfo info;
    memset(&info, 0, sizeof(info));
    int res =
        sceIoDevctl(g_mounts[i].name, 0x3001, NULL, 0, &info, sizeof(info));
    if (res >= 0 && info.max_size > 0) {
      g_mounts[i].is_mounted = 1;
      g_mounts[i].total_bytes = (uint64_t)info.max_size;
      g_mounts[i].free_bytes = (uint64_t)info.free_size;
    } else {
      char path[32];
      snprintf(path, sizeof(path), "%s/", g_mounts[i].name);
      if (dir_exists(path) || dir_exists(g_mounts[i].name)) {
        g_mounts[i].is_mounted = 1;
      }
    }
  }
}

static void format_size(uint64_t bytes, char *out, size_t out_len) {
  if (bytes >= (1024ULL * 1024ULL * 1024ULL)) {
    unsigned int gb = (unsigned int)(bytes / (1024ULL * 1024ULL * 1024ULL));
    unsigned int frac = (unsigned int)((bytes % (1024ULL * 1024ULL * 1024ULL)) /
                                       (1024ULL * 1024ULL * 100ULL));
    snprintf(out, out_len, "%u.%u GB", gb, frac);
  } else if (bytes >= (1024ULL * 1024ULL)) {
    unsigned int mb = (unsigned int)(bytes / (1024ULL * 1024ULL));
    snprintf(out, out_len, "%u MB", mb);
  } else if (bytes > 0) {
    unsigned int kb = (unsigned int)(bytes / 1024ULL);
    snprintf(out, out_len, "%u KB", kb);
  } else {
    snprintf(out, out_len, "N/A");
  }
}

static void save_mount_preference(const char *mount_name) {
  sceIoMkdir("ur0:data", 0777);
  sceIoMkdir(CONFIG_DIR_UR0, 0777);
  SceUID fd = sceIoOpen(CONFIG_PATH_UR0,
                        SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (fd >= 0) {
    sceIoWrite(fd, mount_name, strlen(mount_name));
    sceIoClose(fd);
  }

  sceIoMkdir("ux0:data", 0777);
  sceIoMkdir(CONFIG_DIR_UX0, 0777);
  fd = sceIoOpen(CONFIG_PATH_UX0, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
                 0777);
  if (fd >= 0) {
    sceIoWrite(fd, mount_name, strlen(mount_name));
    sceIoClose(fd);
  }
}

static void load_mount_preference(void) {
  const char *paths[] = {CONFIG_PATH_UR0, CONFIG_PATH_UX0};
  char buf[32];

  for (size_t p = 0; p < sizeof(paths) / sizeof(paths[0]); p++) {
    SceUID fd = sceIoOpen(paths[p], SCE_O_RDONLY, 0);
    if (fd >= 0) {
      memset(buf, 0, sizeof(buf));
      int rd = sceIoRead(fd, buf, sizeof(buf) - 1);
      sceIoClose(fd);
      if (rd > 0) {
        while (rd > 0 && (buf[rd - 1] == '\r' || buf[rd - 1] == '\n' ||
                          buf[rd - 1] == ' ')) {
          buf[--rd] = '\0';
        }
        for (size_t i = 0; i < NUM_MOUNTS; i++) {
          if (strcmp(buf, g_mounts[i].name) == 0) {
            g_selected_mount_idx = i;
            return;
          }
        }
      }
    }
  }

  /* Smart initial default */
  for (size_t i = 0; i < NUM_MOUNTS; i++) {
    char test_path[64];
    snprintf(test_path, sizeof(test_path), "%slinux/zImage", g_mounts[i].name);
    if (file_exists(test_path)) {
      g_selected_mount_idx = i;
      return;
    }
  }

  if (g_mounts[0].is_mounted) {
    g_selected_mount_idx = 0;
    return;
  }

  g_selected_mount_idx = 1;
}

static void check_mount_files(const char *mount, MountFileStatus *status) {
  char path[256];

  snprintf(path, sizeof(path), "%slinux/zImage", mount);
  status->kernel_present = file_exists(path);

  snprintf(path, sizeof(path), "%slinux/vita.dtb", mount);
  status->dtb_present = file_exists(path);

  snprintf(path, sizeof(path), "%slinux/payload.bin", mount);
  status->payload_present = file_exists(path);
  if (!status->payload_present && file_exists("ux0:linux/payload.bin")) {
    status->payload_present = 1;
  }

  snprintf(path, sizeof(path), "%slinux/baremetal-loader.skprx", mount);
  status->loader_present = file_exists(path);
  if (!status->loader_present &&
      file_exists("ux0:linux/baremetal-loader.skprx")) {
    status->loader_present = 1;
  }

  snprintf(path, sizeof(path), "%slinux/wpa_supplicant.conf", mount);
  status->wifi_present = file_exists(path);
}

static int are_keyfiles_present_on_mount(const MountFileStatus *status) {
  return status->kernel_present && status->dtb_present &&
         status->payload_present && status->loader_present;
}

static int are_bundled_files_present(void) {
  return file_exists("app0:data/zImage") &&
         file_exists("app0:data/payload.bin");
}

static uint32_t wait_button_press(uint32_t mask) {
  SceCtrlData pad;
  while (1) {
    sceCtrlPeekBufferPositive(0, &pad, 1);
    if (!(pad.buttons & mask))
      break;
    sceKernelDelayThread(50 * 1000);
  }
  while (1) {
    sceCtrlPeekBufferPositive(0, &pad, 1);
    if (pad.buttons & mask) {
      uint32_t pressed = pad.buttons & mask;
      while (1) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        if (!(pad.buttons & pressed))
          break;
        sceKernelDelayThread(50 * 1000);
      }
      return pressed;
    }
    sceKernelDelayThread(50 * 1000);
  }
}

static void mount_partition_interactive(const char *mount) {
  psvDebugScreenClear(COLOR_BLACK);
  psvDebugScreenSetFgColor(COLOR_CYAN);
  printf("========================================================\n");
  printf(" Mounting Partition: %s\n", mount);
  printf("========================================================\n\n");
  psvDebugScreenSetFgColor(COLOR_WHITE);

  printf(" Attempting to mount %s via VitaOS...\n\n", mount);

  int res = mount_partition(mount);
  if (res >= 0) {
    psvDebugScreenSetFgColor(COLOR_GREEN);
    printf(" [OK] Successfully mounted %s!\n\n", mount);
    psvDebugScreenSetFgColor(COLOR_WHITE);
    sceKernelDelayThread(100 * 1000);
    refresh_mounts();
    sceKernelDelayThread(800 * 1000);
  } else {
    psvDebugScreenSetFgColor(COLOR_RED);
    printf(" [FAIL] Could not mount %s (0x%08X)\n\n", mount, res);
    psvDebugScreenSetFgColor(COLOR_WHITE);
    printf(" Please ensure the storage medium is inserted.\n\n");
    printf(" Press CROSS or START to return to menu...\n");
    wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_START);
  }
}

static int copy_file(const char *src_path, const char *dst_path,
                     int overwrite) {
  if (!overwrite && file_exists(dst_path)) {
    printf("  [SKIP] %s (exists)\n", dst_path);
    return 0;
  }

  SceUID src_fd = sceIoOpen(src_path, SCE_O_RDONLY, 0);
  if (src_fd < 0) {
    return src_fd;
  }

  SceIoStat stat;
  int has_stat = (sceIoGetstat(src_path, &stat) >= 0);
  SceOff total_bytes = has_stat ? stat.st_size : 0;

  SceUID dst_fd =
      sceIoOpen(dst_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (dst_fd < 0) {
    sceIoClose(src_fd);
    return dst_fd;
  }

  static char buffer[128 * 1024];
  SceOff copied = 0;
  int read_bytes;

  while ((read_bytes = sceIoRead(src_fd, buffer, sizeof(buffer))) > 0) {
    int written = sceIoWrite(dst_fd, buffer, read_bytes);
    if (written != read_bytes) {
      sceIoClose(src_fd);
      sceIoClose(dst_fd);
      return -1;
    }
    copied += written;
    if (total_bytes > 0 && total_bytes > (1024 * 1024)) {
      printf("\r  Copying %s: %u / %u MB (%d%%)", dst_path,
             (unsigned int)(copied / (1024 * 1024)),
             (unsigned int)(total_bytes / (1024 * 1024)),
             (int)((copied * 100) / total_bytes));
    }
  }

  sceIoClose(src_fd);
  sceIoClose(dst_fd);

  if (read_bytes < 0) {
    printf("  [FAIL] %s (read error 0x%08X)\n", src_path, read_bytes);
    return read_bytes;
  }

  if (total_bytes > (1024 * 1024)) {
    printf("\n");
  } else {
    printf("  [OK]   %s\n", dst_path);
  }

  return 0;
}

static int install_linux_files_to_mount(const char *mount) {
  psvDebugScreenClear(COLOR_BLACK);
  psvDebugScreenSetFgColor(COLOR_CYAN);
  printf("========================================================\n");
  printf(" Installing Linux to %slinux/\n", mount);
  printf("========================================================\n\n");
  psvDebugScreenSetFgColor(COLOR_WHITE);

  char linux_dir[64];
  snprintf(linux_dir, sizeof(linux_dir), "%slinux", mount);
  sceIoMkdir(linux_dir, 0777);

  int num_files = sizeof(g_bundle_files) / sizeof(g_bundle_files[0]);
  int success_count = 0;
  int error_count = 0;

  for (int i = 0; i < num_files; i++) {
    char src[256];
    char dst[256];
    snprintf(src, sizeof(src), "app0:data/%s", g_bundle_files[i].src_rel);
    snprintf(dst, sizeof(dst), "%slinux/%s", mount, g_bundle_files[i].dst_rel);

    int res = copy_file(src, dst, g_bundle_files[i].overwrite_if_exists);
    if (res == 0) {
      success_count++;
    } else {
      if (g_bundle_files[i].is_mandatory) {
        printf("  [FAIL] Failed: %s (0x%08X)\n", src, res);
        error_count++;
      } else {
        printf("  [INFO] Optional missing: %s\n", g_bundle_files[i].src_rel);
      }
    }
  }

  if (strcmp(mount, "ux0:") != 0 && dir_exists("ux0:")) {
    sceIoMkdir("ux0:linux", 0777);
    copy_file("app0:data/baremetal-loader.skprx",
              "ux0:linux/baremetal-loader.skprx", 1);
    copy_file("app0:data/baremetal-loader_360.skprx",
              "ux0:linux/baremetal-loader_360.skprx", 1);
    copy_file("app0:data/payload.bin", "ux0:linux/payload.bin", 1);
  }

  if (error_count > 0) {
    psvDebugScreenSetFgColor(COLOR_RED);
    printf("\nInstallation finished with %d critical error(s).\n", error_count);
    return -1;
  }

  psvDebugScreenSetFgColor(COLOR_GREEN);
  printf("\nInstallation completed successfully! (%d files)\n", success_count);
  return 0;
}

static int copy_boot_files_to_mount(const char *mount) {
  psvDebugScreenClear(COLOR_BLACK);
  psvDebugScreenSetFgColor(COLOR_CYAN);
  printf("========================================================\n");
  printf(" Copying Boot Files to %slinux/\n", mount);
  printf("========================================================\n\n");
  psvDebugScreenSetFgColor(COLOR_WHITE);

  char linux_dir[64];
  snprintf(linux_dir, sizeof(linux_dir), "%slinux", mount);
  sceIoMkdir(linux_dir, 0777);

  static const char *const boot_files[] = {
      "zImage",
      "vita.dtb",
      "vita1000.dtb",
      "vita2000.dtb",
      "pstv.dtb",
      "payload.bin",
      "baremetal-loader.skprx",
      "baremetal-loader_360.skprx",
  };

  int count = sizeof(boot_files) / sizeof(boot_files[0]);
  int copied_count = 0;

  for (int i = 0; i < count; i++) {
    char src[256];
    char dst[256];

    snprintf(src, sizeof(src), "app0:data/%s", boot_files[i]);
    if (!file_exists(src)) {
      snprintf(src, sizeof(src), "ux0:linux/%s", boot_files[i]);
    }
    if (!file_exists(src)) {
      continue;
    }

    snprintf(dst, sizeof(dst), "%slinux/%s", mount, boot_files[i]);
    if (copy_file(src, dst, 1) == 0) {
      copied_count++;
    }
  }

  psvDebugScreenSetFgColor(COLOR_GREEN);
  printf("\nCopied %d boot file(s) to %s/!\n", copied_count, linux_dir);
  return 0;
}

static void boot_linux(const char *mount) {
  psvDebugScreenClear(COLOR_BLACK);
  psvDebugScreenSetFgColor(COLOR_CYAN);
  printf("========================================================\n");
  printf(" PlayStation Vita Linux 6.12\n");
  printf(" Launching Baremetal Loader...\n");
  printf("========================================================\n\n");
  psvDebugScreenSetFgColor(COLOR_WHITE);

  printf(" Target Memory Card: %s\n\n", mount);

  tai_module_args_t argg;
  argg.size = sizeof(argg);
  argg.pid = KERNEL_PID;
  argg.args = 0;
  argg.argp = NULL;
  argg.flags = 0;

  char mod_path[128];
  snprintf(mod_path, sizeof(mod_path), "%slinux/baremetal-loader.skprx", mount);

  SceUID mod_id = -1;
  if (file_exists(mod_path)) {
    printf(" Loading %s...\n", mod_path);
    mod_id = taiLoadStartKernelModuleForUser(mod_path, &argg);
  }
  if (mod_id < 0 && file_exists("ux0:linux/baremetal-loader.skprx")) {
    printf(" Trying ux0:linux/baremetal-loader.skprx...\n");
    mod_id = taiLoadStartKernelModuleForUser("ux0:linux/baremetal-loader.skprx",
                                             &argg);
  }
  if (mod_id < 0) {
    char mod_360[128];
    snprintf(mod_360, sizeof(mod_360), "%slinux/baremetal-loader_360.skprx",
             mount);
    if (file_exists(mod_360)) {
      printf(" Trying 3.60 loader from %s...\n", mod_360);
      mod_id = taiLoadStartKernelModuleForUser(mod_360, &argg);
    } else if (file_exists("ux0:linux/baremetal-loader_360.skprx")) {
      printf(" Trying 3.60 loader from ux0:linux/...\n");
      mod_id = taiLoadStartKernelModuleForUser(
          "ux0:linux/baremetal-loader_360.skprx", &argg);
    }
  }

  if (mod_id < 0) {
    psvDebugScreenSetFgColor(COLOR_RED);
    printf("\n [FAIL] Error loading baremetal loader: 0x%08X\n\n", mod_id);
    psvDebugScreenSetFgColor(COLOR_WHITE);
    printf(" Press START to return to menu.\n");
    wait_button_press(SCE_CTRL_START);
  } else {
    psvDebugScreenSetFgColor(COLOR_GREEN);
    printf(" [OK] Kernel module started (ID: 0x%08X)\n\n", mod_id);
    psvDebugScreenSetFgColor(COLOR_YELLOW);
    printf(" Entering standby to launch Linux 6.12...\n");
    printf(" Screen will go black before Linux boots.\n\n");
    psvDebugScreenSetFgColor(COLOR_WHITE);
    printf(" Press START to proceed into Linux...\n");
    wait_button_press(SCE_CTRL_START);
    taiStopUnloadKernelModuleForUser(mod_id, &argg, NULL, NULL);
  }
}

int main(int argc, char *argv[]) {
  psvDebugScreenInit();

  /* Auto-mount Sony memory card (xmc0:) if unmounted */
  mount_partition("xmc0:");

  refresh_mounts();
  load_mount_preference();

  /* If preferred target is unmounted, attempt auto-mounting it */
  if (!g_mounts[g_selected_mount_idx].is_mounted) {
    mount_partition(g_mounts[g_selected_mount_idx].name);
    refresh_mounts();
  }

  while (1) {
    refresh_mounts();
    const char *target_mount = g_mounts[g_selected_mount_idx].name;
    int is_target_mounted = g_mounts[g_selected_mount_idx].is_mounted;

    MountFileStatus status;
    memset(&status, 0, sizeof(status));
    if (is_target_mounted) {
      check_mount_files(target_mount, &status);
    }
    int installed = are_keyfiles_present_on_mount(&status);
    int has_bundled = are_bundled_files_present();

    psvDebugScreenClear(COLOR_BLACK);
    psvDebugScreenSetFgColor(COLOR_CYAN);
    printf("========================================================\n");
    printf(" PlayStation Vita Linux Bootstrapper & Installer\n");
    printf(" Project LinuxOnVita (v1.2.0)\n");
    printf("========================================================\n");
    psvDebugScreenSetFgColor(COLOR_WHITE);
    printf(" Port by xerpi, CreepNT, DvaMishkiLapa, DevWithZachary\n\n");

    /* Critical hardware requirement notice (<= 56 chars/line) */
    psvDebugScreenSetFgColor(COLOR_YELLOW);
    printf(" [!] SONY MEMORY CARD REQUIRED (MSIF HARDWARE)\n");
    psvDebugScreenSetFgColor(COLOR_WHITE);
    printf(" Linux CANNOT boot from SD2Vita or internal storage.\n");
    printf(" Boot files MUST be installed on your Sony memory card!\n");
    printf(" - SD2Vita as ux0: Sony card is usually xmc0: or uma0:\n");
    printf(" - Standard Vita:  Sony card is ux0:\n\n");

    /* Storage mounts display (<= 54 chars/line) */
    printf(" Storage Partitions (Target: %s ", target_mount);
    if (is_target_mounted) {
      psvDebugScreenSetFgColor(COLOR_GREEN);
      printf("[READY]");
    } else {
      psvDebugScreenSetFgColor(COLOR_RED);
      printf("[UNMOUNTED]");
    }
    psvDebugScreenSetFgColor(COLOR_WHITE);
    printf("):\n");

    for (size_t i = 0; i < NUM_MOUNTS; i++) {
      int is_selected = ((int)i == g_selected_mount_idx);
      if (is_selected) {
        psvDebugScreenSetFgColor(COLOR_CYAN);
        printf(" -> ");
      } else {
        psvDebugScreenSetFgColor(COLOR_WHITE);
        printf("    ");
      }

      printf("%-5s ", g_mounts[i].name);

      if (g_mounts[i].is_mounted) {
        char total_str[16];
        char free_str[16];
        format_size(g_mounts[i].total_bytes, total_str, sizeof(total_str));
        format_size(g_mounts[i].free_bytes, free_str, sizeof(free_str));
        if (g_mounts[i].total_bytes > 0) {
          printf("%s (%s free) ", total_str, free_str);
        }
        psvDebugScreenSetFgColor(COLOR_GREEN);
        printf("[MOUNTED]");
      } else {
        psvDebugScreenSetFgColor(COLOR_GREY);
        printf("(Not mounted)");
      }

      if (is_selected) {
        psvDebugScreenSetFgColor(COLOR_YELLOW);
        printf(" *TARGET*");
      }
      printf("\n");
    }
    psvDebugScreenSetFgColor(COLOR_WHITE);
    printf("\n");

    /* Status display (<= 54 chars/line) */
    if (!is_target_mounted) {
      psvDebugScreenSetFgColor(COLOR_RED);
      printf(" Status: Target %s is not mounted!\n", target_mount);
      psvDebugScreenSetFgColor(COLOR_YELLOW);
      printf(" Press [X] to mount %s now.\n", target_mount);
      psvDebugScreenSetFgColor(COLOR_WHITE);
      printf(" Or use D-PAD to switch to another partition.\n\n");
    } else if (!installed) {
      psvDebugScreenSetFgColor(COLOR_YELLOW);
      printf(" Status: Linux files NOT detected in %slinux/\n", target_mount);
      psvDebugScreenSetFgColor(COLOR_WHITE);
      printf("   - Kernel (zImage):                   %s\n",
             status.kernel_present ? "PRESENT" : "MISSING");
      printf("   - Device Tree (vita.dtb):            %s\n",
             status.dtb_present ? "PRESENT" : "MISSING");
      printf("   - Baremetal Loader (payload.bin):    %s\n",
             status.payload_present ? "PRESENT" : "MISSING");
      printf("   - Kernel Plugin (loader.skprx):      %s\n",
             status.loader_present ? "PRESENT" : "MISSING");
      printf("   - Wi-Fi Config (wpa_supplicant):     %s\n\n",
             status.wifi_present ? "PRESENT" : "NOT CONFIGURED");

      if (!has_bundled) {
        psvDebugScreenSetFgColor(COLOR_RED);
        printf(" Notice: Bundled data files missing from app0:data/\n");
        printf(" Please reinstall LinuxOnVita.vpk.\n");
        psvDebugScreenSetFgColor(COLOR_WHITE);
      }
    } else {
      psvDebugScreenSetFgColor(COLOR_GREEN);
      printf(" Status: Linux files verified in %slinux/\n", target_mount);
      psvDebugScreenSetFgColor(COLOR_WHITE);
      printf("   - Kernel (zImage):                   PRESENT\n");
      printf("   - Device Tree (vita.dtb):            PRESENT\n");
      printf("   - Baremetal Loader (payload.bin):    PRESENT\n");
      printf("   - Kernel Plugin (loader.skprx):      PRESENT\n");
      printf("   - Wi-Fi Config (wpa_supplicant):     %s\n\n",
             status.wifi_present ? "PRESENT" : "NOT CONFIGURED");
    }

    /* Controls (<= 52 chars/line) */
    printf(" Controls:\n");
    if (is_target_mounted) {
      if (installed) {
        printf("   [X]        Boot Linux (from %slinux/)\n", target_mount);
        if (has_bundled) {
          printf("   [SQUARE]   Reinstall / update %slinux/\n", target_mount);
        }
        printf("   [TRIANGLE] Copy boot files (zImage/DTB) to %s\n",
               target_mount);
      } else {
        if (has_bundled) {
          printf("   [X]        Install Linux to %slinux/\n", target_mount);
        }
        printf("   [TRIANGLE] Copy boot files (zImage/DTB) to %s\n",
               target_mount);
      }
    } else {
      printf("   [X]        Mount %s partition\n", target_mount);
    }
    printf("   [UP/DOWN]  Change target mount (L/R to cycle)\n");
    printf("   [START]    Exit to LiveArea\n\n");

    uint32_t mask = SCE_CTRL_START | SCE_CTRL_UP | SCE_CTRL_DOWN |
                    SCE_CTRL_LEFT | SCE_CTRL_RIGHT | SCE_CTRL_LTRIGGER |
                    SCE_CTRL_RTRIGGER;

    if (is_target_mounted) {
      if (installed) {
        mask |= SCE_CTRL_CROSS;
        if (has_bundled)
          mask |= SCE_CTRL_SQUARE;
        mask |= SCE_CTRL_TRIANGLE;
      } else {
        if (has_bundled)
          mask |= SCE_CTRL_CROSS;
        mask |= SCE_CTRL_TRIANGLE;
      }
    } else {
      mask |= SCE_CTRL_CROSS;
    }

    uint32_t btn = wait_button_press(mask);

    if (btn & (SCE_CTRL_UP | SCE_CTRL_LEFT | SCE_CTRL_LTRIGGER)) {
      g_selected_mount_idx =
          (g_selected_mount_idx + NUM_MOUNTS - 1) % NUM_MOUNTS;
      save_mount_preference(g_mounts[g_selected_mount_idx].name);
      continue;
    } else if (btn & (SCE_CTRL_DOWN | SCE_CTRL_RIGHT | SCE_CTRL_RTRIGGER)) {
      g_selected_mount_idx = (g_selected_mount_idx + 1) % NUM_MOUNTS;
      save_mount_preference(g_mounts[g_selected_mount_idx].name);
      continue;
    } else if (btn & SCE_CTRL_START) {
      break;
    } else if (btn & SCE_CTRL_CROSS) {
      if (!is_target_mounted) {
        mount_partition_interactive(target_mount);
      } else if (installed) {
        boot_linux(target_mount);
      } else if (has_bundled) {
        install_linux_files_to_mount(target_mount);
        printf("\nPress CROSS or START to continue...\n");
        wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_START);
      }
    } else if (btn & SCE_CTRL_SQUARE) {
      if (has_bundled) {
        install_linux_files_to_mount(target_mount);
        printf("\nPress CROSS or START to continue...\n");
        wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_START);
      }
    } else if (btn & SCE_CTRL_TRIANGLE) {
      copy_boot_files_to_mount(target_mount);
      printf("\nPress CROSS or START to continue...\n");
      wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_START);
    }
  }

  return 0;
}
