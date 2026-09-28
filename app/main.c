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
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/vshbridge.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <taihen.h>

#define printf(...) psvDebugScreenPrintf(__VA_ARGS__)

#define CONFIG_DIR_UR0 "ur0:data/LinuxOnVita"
#define CONFIG_PATH_UR0 "ur0:data/LinuxOnVita/mount.cfg"
#define CONFIG_DIR_UX0 "ux0:data/LinuxOnVita"
#define CONFIG_PATH_UX0 "ux0:data/LinuxOnVita/mount.cfg"

#define LOG_PATH_UX0 "ux0:data/LinuxOnVita/boot_debug.log"
#define LOG_PATH_UR0 "ur0:data/LinuxOnVita/boot_debug.log"

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

/* Persistent Boot Debug Logging */
static SceUID g_debug_log_fd = -1;
static char g_active_log_path[128] = LOG_PATH_UX0;

static void debug_log_init(void) {
  sceIoMkdir("ux0:data", 0777);
  sceIoMkdir("ux0:data/LinuxOnVita", 0777);
  sceIoMkdir("ur0:data", 0777);
  sceIoMkdir("ur0:data/LinuxOnVita", 0777);

  if (g_debug_log_fd >= 0) {
    sceIoClose(g_debug_log_fd);
    g_debug_log_fd = -1;
  }

  g_debug_log_fd = sceIoOpen(LOG_PATH_UX0,
                             SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (g_debug_log_fd >= 0) {
    snprintf(g_active_log_path, sizeof(g_active_log_path), "%s", LOG_PATH_UX0);
  } else {
    g_debug_log_fd = sceIoOpen(LOG_PATH_UR0,
                               SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (g_debug_log_fd >= 0) {
      snprintf(g_active_log_path, sizeof(g_active_log_path), "%s",
               LOG_PATH_UR0);
    }
  }
}

static void debug_log_printf(uint32_t color, const char *fmt, ...) {
  char buf[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (color != 0) {
    psvDebugScreenSetFgColor(color);
  }
  printf("%s", buf);

  if (g_debug_log_fd >= 0) {
    sceIoWrite(g_debug_log_fd, buf, strlen(buf));
    sceIoSyncByFd(g_debug_log_fd, 0);
  } else {
    SceUID fd = sceIoOpen(g_active_log_path,
                          SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) {
      sceIoWrite(fd, buf, strlen(buf));
      sceIoSyncByFd(fd, 0);
      sceIoClose(fd);
    }
  }
}

static void debug_log_close(void) {
  if (g_debug_log_fd >= 0) {
    sceIoSyncByFd(g_debug_log_fd, 0);
    sceIoClose(g_debug_log_fd);
    g_debug_log_fd = -1;
  }
}

static int file_exists(const char *path) {
  SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
  if (fd >= 0) {
    sceIoClose(fd);
    return 1;
  }
  return 0;
}

static int inspect_file(const char *path, uint64_t *out_size,
                        int *out_readable) {
  if (out_size)
    *out_size = 0;
  if (out_readable)
    *out_readable = 0;

  SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
  if (fd < 0) {
    return fd;
  }
  if (out_readable)
    *out_readable = 1;

  SceOff sz = sceIoLseek(fd, 0, SCE_SEEK_END);
  if (sz >= 0 && out_size) {
    *out_size = (uint64_t)sz;
  }
  sceIoClose(fd);
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

static void view_boot_log(void) {
  psvDebugScreenClear(COLOR_BLACK);
  psvDebugScreenSetFgColor(COLOR_CYAN);
  printf("========================================================\n");
  printf(" LinuxOnVita - Boot Debug Log Viewer\n");
  printf("========================================================\n\n");
  psvDebugScreenSetFgColor(COLOR_WHITE);

  const char *log_paths[] = {
      LOG_PATH_UX0,
      LOG_PATH_UR0,
  };

  SceUID fd = -1;
  const char *found_path = NULL;
  for (size_t i = 0; i < sizeof(log_paths) / sizeof(log_paths[0]); i++) {
    fd = sceIoOpen(log_paths[i], SCE_O_RDONLY, 0);
    if (fd >= 0) {
      found_path = log_paths[i];
      break;
    }
  }

  if (fd < 0) {
    psvDebugScreenSetFgColor(COLOR_YELLOW);
    printf(" No boot debug log file found.\n\n");
    psvDebugScreenSetFgColor(COLOR_WHITE);
    printf(" Checked paths:\n");
    printf("   - %s\n", LOG_PATH_UX0);
    printf("   - %s\n\n", LOG_PATH_UR0);
    printf(" To generate a log, launch Linux using [SQUARE] (Debug Mode).\n\n");
    printf(" Press CROSS or START to return to menu...\n");
    wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_START | SCE_CTRL_CIRCLE);
    return;
  }

  static char log_buffer[32768];
  memset(log_buffer, 0, sizeof(log_buffer));
  int rd = sceIoRead(fd, log_buffer, sizeof(log_buffer) - 1);
  sceIoClose(fd);

  if (rd <= 0) {
    psvDebugScreenSetFgColor(COLOR_YELLOW);
    printf(" Log file %s is empty (0 bytes).\n\n", found_path);
    psvDebugScreenSetFgColor(COLOR_WHITE);
    printf(" Press CROSS or START to return to menu...\n");
    wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_START | SCE_CTRL_CIRCLE);
    return;
  }

  #define MAX_LOG_LINES 512
  static char *lines[MAX_LOG_LINES];
  int line_count = 0;

  lines[line_count++] = log_buffer;
  for (int i = 0; i < rd && line_count < MAX_LOG_LINES; i++) {
    if (log_buffer[i] == '\r') {
      log_buffer[i] = '\0';
    } else if (log_buffer[i] == '\n') {
      log_buffer[i] = '\0';
      if (i + 1 < rd) {
        lines[line_count++] = &log_buffer[i + 1];
      }
    }
  }

  int scroll_offset = 0;
  const int page_size = 45;

  while (1) {
    psvDebugScreenClear(COLOR_BLACK);
    psvDebugScreenSetFgColor(COLOR_CYAN);
    printf("=== Log: %s (%d lines) ===\n", found_path, line_count);
    psvDebugScreenSetFgColor(COLOR_GREY);
    printf(" [UP/DOWN] Scroll | [L/R] Page | [CIRCLE/START] Back to menu\n");
    printf("--------------------------------------------------------\n");
    psvDebugScreenSetFgColor(COLOR_WHITE);

    int end_line = scroll_offset + page_size;
    if (end_line > line_count) {
      end_line = line_count;
    }

    for (int l = scroll_offset; l < end_line; l++) {
      if (lines[l]) {
        if (strstr(lines[l], "[FAIL]") || strstr(lines[l], "Error") ||
            strstr(lines[l], "error")) {
          psvDebugScreenSetFgColor(COLOR_RED);
        } else if (strstr(lines[l], "[OK]") || strstr(lines[l], "Success")) {
          psvDebugScreenSetFgColor(COLOR_GREEN);
        } else if (strstr(lines[l], "[WARN]") || strstr(lines[l], "Warning")) {
          psvDebugScreenSetFgColor(COLOR_YELLOW);
        } else if (strstr(lines[l], "Baremetal loader by xerpi") ||
                   strstr(lines[l], "Resetting the device")) {
          psvDebugScreenSetFgColor(COLOR_CYAN);
        } else {
          psvDebugScreenSetFgColor(COLOR_WHITE);
        }
        printf("%s\n", lines[l]);
      }
    }

    uint32_t btn = wait_button_press(SCE_CTRL_UP | SCE_CTRL_DOWN |
                                     SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER |
                                     SCE_CTRL_START | SCE_CTRL_CIRCLE |
                                     SCE_CTRL_CROSS);

    if (btn & (SCE_CTRL_START | SCE_CTRL_CIRCLE | SCE_CTRL_CROSS)) {
      break;
    } else if (btn & SCE_CTRL_UP) {
      if (scroll_offset > 0)
        scroll_offset -= 5;
      if (scroll_offset < 0)
        scroll_offset = 0;
    } else if (btn & SCE_CTRL_DOWN) {
      if (scroll_offset + page_size < line_count)
        scroll_offset += 5;
    } else if (btn & SCE_CTRL_LTRIGGER) {
      scroll_offset -= page_size;
      if (scroll_offset < 0)
        scroll_offset = 0;
    } else if (btn & SCE_CTRL_RTRIGGER) {
      if (scroll_offset + page_size < line_count)
        scroll_offset += page_size;
    }
  }
}

static void boot_linux(const char *mount, int debug_mode) {
  debug_log_init();

  psvDebugScreenClear(COLOR_BLACK);
  psvDebugScreenSetFgColor(COLOR_CYAN);
  debug_log_printf(COLOR_CYAN,
                   "========================================================\n");
  if (debug_mode) {
    debug_log_printf(COLOR_CYAN,
                     " PlayStation Vita Linux 6.12 - Debug Boot Mode\n");
  } else {
    debug_log_printf(COLOR_CYAN,
                     " PlayStation Vita Linux 6.12 Bootstrapper\n");
  }
  debug_log_printf(COLOR_CYAN,
                   "========================================================\n\n");
  debug_log_printf(COLOR_WHITE, " Target Storage Mount: %s\n", mount);
  debug_log_printf(COLOR_GREY, " Log File: %s\n\n", g_active_log_path);

  /* Step 1: Hardware & Firmware Environment Inspection */
  debug_log_printf(COLOR_YELLOW, "[1/5] Hardware & Firmware Environment:\n");

  SceKernelFwInfo fw;
  memset(&fw, 0, sizeof(fw));
  fw.size = sizeof(fw);
  int has_fw = (_vshSblGetSystemSwVersion(&fw) >= 0);
  if (has_fw) {
    debug_log_printf(COLOR_WHITE, "   - Firmware Version:      %s (0x%08X)\n",
                     fw.versionString, fw.version);
  } else {
    debug_log_printf(COLOR_WHITE,
                     "   - Firmware Version:      Unknown (query failed)\n");
  }

  int model_raw = sceKernelGetModel();
  int is_vita = vshSblAimgrIsVITA();
  int is_dolce = vshSblAimgrIsDolce();
  int is_mc_emu = vshSysconIsMCEmuCapable();
  int has_wwan = vshSysconHasWWAN();

  const char *model_str = "Unknown Model";
  if (is_dolce) {
    model_str = "PlayStation TV / Vita TV (Dolce, VTE-1000)";
  } else if (is_mc_emu) {
    model_str = "PS Vita Slim (LCD, PCH-2000 series)";
  } else {
    model_str = has_wwan ? "PS Vita 1000 3G/Wi-Fi (OLED, PCH-1100 series)"
                         : "PS Vita 1000 Wi-Fi (OLED, PCH-1000 series)";
  }

  debug_log_printf(COLOR_WHITE, "   - Detected Hardware:     %s\n", model_str);
  debug_log_printf(
      COLOR_GREY,
      "     (Model ID: 0x%08X | VITA: %d | Dolce: %d | MCEmu: %d)\n",
      model_raw, is_vita, is_dolce, is_mc_emu);

  int mc_state = vshMemoryCardGetCardInsertState();
  int rm_state = vshRemovableMemoryGetCardInsertState();

  if (mc_state) {
    debug_log_printf(COLOR_GREEN,
                     "   - Sony Memory Card:      INSERTED (MSIF interface active)\n");
  } else {
    debug_log_printf(COLOR_RED,
                     "   - Sony Memory Card:      NOT DETECTED in MSIF slot!\n");
    debug_log_printf(
        COLOR_YELLOW,
        "     [!] Warning: Baremetal loader requires an authentic Sony Memory Card.\n");
    debug_log_printf(
        COLOR_YELLOW,
        "     [!] Linux cannot boot from SD2Vita without Sony MSIF storage.\n");
  }

  debug_log_printf(COLOR_WHITE, "   - GameCard Slot:         %s\n\n",
                   rm_state ? "Inserted (SD2Vita / GameCard)" : "Empty");

  /* Step 2: Storage Partitions State */
  debug_log_printf(COLOR_YELLOW, "[2/5] Storage Partitions Status:\n");
  for (size_t i = 0; i < NUM_MOUNTS; i++) {
    char total_str[16];
    char free_str[16];
    format_size(g_mounts[i].total_bytes, total_str, sizeof(total_str));
    format_size(g_mounts[i].free_bytes, free_str, sizeof(free_str));
    int is_target = (strcmp(g_mounts[i].name, mount) == 0);
    debug_log_printf(COLOR_WHITE, "   - %-5s: %s", g_mounts[i].name,
                     g_mounts[i].is_mounted ? "[MOUNTED]" : "[NOT MOUNTED]");
    if (g_mounts[i].is_mounted && g_mounts[i].total_bytes > 0) {
      debug_log_printf(COLOR_WHITE, " (%s free of %s)", free_str, total_str);
    }
    if (is_target) {
      debug_log_printf(COLOR_CYAN, " *TARGET*");
    }
    debug_log_printf(COLOR_WHITE, "\n");
  }
  debug_log_printf(COLOR_WHITE, "\n");

  /* Step 3: Kernel and Device Tree Blobs Verification */
  debug_log_printf(COLOR_YELLOW, "[3/5] Kernel & DTB Files (%slinux/):\n",
                   mount);

  int critical_errors = 0;
  char path[256];
  uint64_t file_sz = 0;
  int readable = 0;

  /* Check zImage */
  snprintf(path, sizeof(path), "%slinux/zImage", mount);
  if (inspect_file(path, &file_sz, &readable) == 0 && file_sz > 0) {
    char sz_str[16];
    format_size(file_sz, sz_str, sizeof(sz_str));
    debug_log_printf(COLOR_GREEN,
                     "   - zImage:                PRESENT (%s / %u bytes) [OK]\n",
                     sz_str, (unsigned int)file_sz);
  } else {
    debug_log_printf(COLOR_RED,
                     "   - zImage:                MISSING or unreadable! [FAIL]\n");
    critical_errors++;
  }

  /* Check Model-Specific DTB */
  const char *expected_dtb_rel = "vita.dtb";
  if (is_dolce) {
    expected_dtb_rel = "pstv.dtb";
  } else if (is_mc_emu) {
    expected_dtb_rel = "vita2000.dtb";
  } else {
    expected_dtb_rel = "vita1000.dtb";
  }

  snprintf(path, sizeof(path), "%slinux/%s", mount, expected_dtb_rel);
  if (inspect_file(path, &file_sz, &readable) == 0 && file_sz > 0) {
    debug_log_printf(COLOR_GREEN,
                     "   - %-22s PRESENT (%u bytes) [MATCHES HARDWARE]\n",
                     expected_dtb_rel, (unsigned int)file_sz);
  } else {
    debug_log_printf(COLOR_YELLOW,
                     "   - %-22s NOT FOUND (testing fallback)\n",
                     expected_dtb_rel);
    snprintf(path, sizeof(path), "%slinux/vita.dtb", mount);
    if (inspect_file(path, &file_sz, &readable) == 0 && file_sz > 0) {
      debug_log_printf(COLOR_GREEN,
                       "   - vita.dtb (fallback):   PRESENT (%u bytes) [OK]\n",
                       (unsigned int)file_sz);
    } else {
      debug_log_printf(
          COLOR_RED,
          "   - vita.dtb (fallback):   MISSING! No valid DTB found! [FAIL]\n");
      critical_errors++;
    }
  }

  /* Step 4: Baremetal Loader Files & Multi-Path Fallbacks */
  debug_log_printf(COLOR_YELLOW, "\n[4/5] Baremetal Loader Pre-Flight:\n");

  /* payload.bin checks */
  int payload_found = 0;
  snprintf(path, sizeof(path), "%slinux/payload.bin", mount);
  if (inspect_file(path, &file_sz, &readable) == 0 && file_sz > 0) {
    debug_log_printf(
        COLOR_GREEN,
        "   - Target payload.bin:    PRESENT (%slinux/payload.bin, %u bytes) [OK]\n",
        mount, (unsigned int)file_sz);
    payload_found = 1;
  } else {
    debug_log_printf(COLOR_YELLOW,
                     "   - Target payload.bin:    Not on target %slinux/\n",
                     mount);
  }

  if (inspect_file("ux0:linux/payload.bin", &file_sz, &readable) == 0 &&
      file_sz > 0) {
    debug_log_printf(
        COLOR_GREEN,
        "   - Mirror payload.bin:    PRESENT (ux0:linux/payload.bin, %u bytes) [OK]\n",
        (unsigned int)file_sz);
    payload_found = 1;
  } else if (strcmp(mount, "ux0:") != 0 && payload_found && dir_exists("ux0:")) {
    debug_log_printf(COLOR_WHITE, "   - Syncing payload.bin to ux0:linux/... ");
    sceIoMkdir("ux0:linux", 0777);
    snprintf(path, sizeof(path), "%slinux/payload.bin", mount);
    if (copy_file(path, "ux0:linux/payload.bin", 1) == 0) {
      debug_log_printf(COLOR_GREEN, "[SYNCED]\n");
    } else {
      debug_log_printf(COLOR_YELLOW, "[SKIPPED]\n");
    }
  }

  if (!payload_found) {
    debug_log_printf(
        COLOR_RED,
        "   - payload.bin:           CRITICAL MISSING from all paths! [FAIL]\n");
    critical_errors++;
  }

  /* baremetal-loader.skprx checks */
  char mod_path[128];
  snprintf(mod_path, sizeof(mod_path), "%slinux/baremetal-loader.skprx", mount);
  int loader_found = 0;
  if (inspect_file(mod_path, &file_sz, &readable) == 0 && file_sz > 0) {
    debug_log_printf(
        COLOR_GREEN,
        "   - Target loader.skprx:   PRESENT (%s, %u bytes) [OK]\n",
        mod_path, (unsigned int)file_sz);
    loader_found = 1;
  } else {
    debug_log_printf(COLOR_YELLOW,
                     "   - Target loader.skprx:   Not found on target %s\n",
                     mod_path);
  }

  if (inspect_file("ux0:linux/baremetal-loader.skprx", &file_sz, &readable) ==
          0 &&
      file_sz > 0) {
    debug_log_printf(
        COLOR_GREEN,
        "   - Mirror loader.skprx:   PRESENT (ux0:linux/baremetal-loader.skprx) [OK]\n");
    loader_found = 1;
  } else if (strcmp(mount, "ux0:") != 0 && loader_found && dir_exists("ux0:")) {
    debug_log_printf(COLOR_WHITE, "   - Syncing loader.skprx to ux0:linux/... ");
    sceIoMkdir("ux0:linux", 0777);
    if (copy_file(mod_path, "ux0:linux/baremetal-loader.skprx", 1) == 0) {
      debug_log_printf(COLOR_GREEN, "[SYNCED]\n");
    } else {
      debug_log_printf(COLOR_YELLOW, "[SKIPPED]\n");
    }
  }

  if (!loader_found) {
    debug_log_printf(
        COLOR_RED,
        "   - loader.skprx:          CRITICAL MISSING from all paths! [FAIL]\n");
    critical_errors++;
  }

  /* Step 5: Pre-Flight Assessment */
  debug_log_printf(COLOR_YELLOW, "\n[5/5] Pre-Flight Assessment & Handover:\n");

  if (critical_errors > 0) {
    debug_log_printf(
        COLOR_RED,
        "   [FAIL] Pre-flight halted with %d critical error(s)!\n",
        critical_errors);
    debug_log_printf(
        COLOR_WHITE,
        "   Boot cancelled to prevent console freeze or blackscreen.\n");
    debug_log_printf(COLOR_CYAN, "   Log file written to: %s\n\n",
                     g_active_log_path);
    debug_log_printf(COLOR_WHITE,
                     " Press CROSS or START to return to menu...\n");
    debug_log_close();
    wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_START | SCE_CTRL_CIRCLE);
    return;
  }

  debug_log_printf(
      COLOR_GREEN,
      "   [OK] All required boot files and pre-flight checks verified!\n");
  debug_log_printf(COLOR_CYAN, "   Debug log synced to: %s\n\n",
                   g_active_log_path);

  if (debug_mode) {
    debug_log_printf(COLOR_YELLOW, " Controls:\n");
    debug_log_printf(COLOR_WHITE,
                     "   [X]      Proceed to Launch (Trigger Standby Handover)\n");
    debug_log_printf(COLOR_WHITE,
                     "   [CIRCLE] Cancel & Return to Menu\n\n");

    uint32_t choice =
        wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START);
    if (choice & (SCE_CTRL_CIRCLE | SCE_CTRL_START)) {
      debug_log_printf(
          COLOR_YELLOW,
          " Boot cancelled by user request. Returning to menu...\n");
      debug_log_close();
      sceKernelDelayThread(400 * 1000);
      return;
    }
  }

  /* Initiate loader module launch */
  debug_log_printf(COLOR_CYAN,
                   " Handover: Loading baremetal kernel module...\n");

  tai_module_args_t argg;
  argg.size = sizeof(argg);
  argg.pid = KERNEL_PID;
  argg.args = 0;
  argg.argp = NULL;
  argg.flags = 0;

  SceUID mod_id = -1;
  if (file_exists(mod_path)) {
    debug_log_printf(COLOR_WHITE,
                     "   - Calling taiLoadStartKernelModuleForUser(%s)...\n",
                     mod_path);
    mod_id = taiLoadStartKernelModuleForUser(mod_path, &argg);
  }
  if (mod_id < 0 && file_exists("ux0:linux/baremetal-loader.skprx")) {
    debug_log_printf(
        COLOR_WHITE,
        "   - Calling taiLoadStartKernelModuleForUser(ux0:linux/baremetal-loader.skprx)...\n");
    mod_id = taiLoadStartKernelModuleForUser(
        "ux0:linux/baremetal-loader.skprx", &argg);
  }
  if (mod_id < 0) {
    char mod_360[128];
    snprintf(mod_360, sizeof(mod_360), "%slinux/baremetal-loader_360.skprx",
             mount);
    if (file_exists(mod_360)) {
      debug_log_printf(COLOR_WHITE,
                       "   - Calling taiLoadStartKernelModuleForUser(%s)...\n",
                       mod_360);
      mod_id = taiLoadStartKernelModuleForUser(mod_360, &argg);
    } else if (file_exists("ux0:linux/baremetal-loader_360.skprx")) {
      debug_log_printf(
          COLOR_WHITE,
          "   - Calling taiLoadStartKernelModuleForUser(ux0:linux/baremetal-loader_360.skprx)...\n");
      mod_id = taiLoadStartKernelModuleForUser(
          "ux0:linux/baremetal-loader_360.skprx", &argg);
    }
  }

  if (mod_id < 0) {
    debug_log_printf(
        COLOR_RED,
        "\n   [FAIL] Kernel module start failed with error: 0x%08X\n\n",
        mod_id);
    debug_log_printf(COLOR_CYAN, "   Check %s for diagnostic details.\n\n",
                     g_active_log_path);
    debug_log_printf(COLOR_WHITE, " Press START to return to menu...\n");
    debug_log_close();
    wait_button_press(SCE_CTRL_START | SCE_CTRL_CROSS | SCE_CTRL_CIRCLE);
    return;
  }

  debug_log_printf(COLOR_GREEN,
                   "   [OK] Baremetal loader module running (ID: 0x%08X)\n",
                   mod_id);
  debug_log_printf(
      COLOR_YELLOW,
      "   Syscon hooks active. Requesting VitaOS standby handover...\n");
  debug_log_printf(
      COLOR_WHITE,
      "   Screen will go black as soft-reset transitions into Linux.\n\n");
  debug_log_printf(
      COLOR_GREY,
      "   [VPK logs complete. Subsequent logs appended by kernel module.]\n");

  /* Ensure all log buffers are flushed to persistent storage before standby */
  debug_log_close();
  sceKernelDelayThread(400 * 1000);
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
    printf(" Project LinuxOnVita (v1.3.0)\n");
    printf("========================================================\n");
    psvDebugScreenSetFgColor(COLOR_WHITE);
    printf(" Port by xerpi, CreepNT, DvaMishkiLapa, DevWithZachary\n\n");

    /* Hardware requirement notice */
    psvDebugScreenSetFgColor(COLOR_YELLOW);
    printf(" [!] Sony Memory Card Required (MSIF) - cannot boot from SD2Vita\n");
    psvDebugScreenSetFgColor(COLOR_WHITE);
    printf("     Target must be Sony card (ux0: on standard, xmc0:/uma0: on SD2Vita)\n\n");

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
        printf("   [X]        Boot Linux (Normal)\n");
        printf("   [SQUARE]   Boot Linux (Debug Mode)\n");
        if (has_bundled) {
          printf("   [TRIANGLE] Reinstall / update %slinux/\n", target_mount);
        }
      } else {
        if (has_bundled) {
          printf("   [X]        Install Linux to %slinux/\n", target_mount);
        }
        printf("   [SQUARE]   Boot Linux (Debug Mode - Diagnostics)\n");
        printf("   [TRIANGLE] Copy boot files (zImage/DTB) to %s\n",
               target_mount);
      }
    } else {
      printf("   [X]        Mount %s partition\n", target_mount);
      printf("   [SQUARE]   Debug Diagnostics\n");
    }
    printf("   [SELECT]   View Boot Debug Log\n");
    printf("   [UP/DOWN]  Change target mount (L/R to cycle)\n");
    printf("   [START]    Exit to LiveArea\n\n");

    uint32_t mask = SCE_CTRL_START | SCE_CTRL_UP | SCE_CTRL_DOWN |
                    SCE_CTRL_LEFT | SCE_CTRL_RIGHT | SCE_CTRL_LTRIGGER |
                    SCE_CTRL_RTRIGGER | SCE_CTRL_SELECT;

    if (is_target_mounted) {
      mask |= SCE_CTRL_CROSS;
      mask |= SCE_CTRL_SQUARE;
      mask |= SCE_CTRL_TRIANGLE;
    } else {
      mask |= SCE_CTRL_CROSS;
      mask |= SCE_CTRL_SQUARE;
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
    } else if (btn & SCE_CTRL_SELECT) {
      view_boot_log();
    } else if (btn & SCE_CTRL_CROSS) {
      if (!is_target_mounted) {
        mount_partition_interactive(target_mount);
      } else if (installed) {
        boot_linux(target_mount, 0);
      } else if (has_bundled) {
        install_linux_files_to_mount(target_mount);
        printf("\nPress CROSS or START to continue...\n");
        wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_START);
      }
    } else if (btn & SCE_CTRL_SQUARE) {
      boot_linux(target_mount, 1);
    } else if (btn & SCE_CTRL_TRIANGLE) {
      if (installed && has_bundled) {
        install_linux_files_to_mount(target_mount);
        printf("\nPress CROSS or START to continue...\n");
        wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_START);
      } else {
        copy_boot_files_to_mount(target_mount);
        printf("\nPress CROSS or START to continue...\n");
        wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_START);
      }
    }
  }

  return 0;
}
