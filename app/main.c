/*
 * LinuxOnVita - PlayStation Vita Linux Bootstrapper & Installer
 *
 * Original kplugin loader by xerpi (2016)
 * Linux theming and file checks by CreepNT (2018)
 * Updates by DvaMishkiLapa (2020)
 * Storage mount selection and on-device installer by DevWithZachary (2026)
 * Touchscreen Graphical UI Overhaul by DevWithZachary (2026)
 *
 * Licensed under the GNU General Public License v3.0 (GPL-3.0)
 */

#include "gui.h"

#include <psp2/ctrl.h>
#include <psp2/touch.h>
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

#define COLOR_BLACK   0xFF000000
#define COLOR_RED     0xFF0000FF
#define COLOR_GREEN   0xFFFF0000
#define COLOR_YELLOW  0xFF00FFFF
#define COLOR_BLUE    0xFF00FF00
#define COLOR_CYAN    0xFFFFFF00
#define COLOR_WHITE   0xFFFFFFFF
#define COLOR_GREY    0xFF808080

#define CONFIG_DIR_UR0 "ur0:data/LinuxOnVita"
#define CONFIG_PATH_UR0 "ur0:data/LinuxOnVita/mount.cfg"
#define CONFIG_DIR_UX0 "ux0:data/LinuxOnVita"
#define CONFIG_PATH_UX0 "ux0:data/LinuxOnVita/mount.cfg"

#define LOG_PATH_UX0 "ux0:data/LinuxOnVita/boot_debug.log"
#define LOG_PATH_UR0 "ur0:data/LinuxOnVita/boot_debug.log"

typedef struct {
  const char *name;
  const char *label;
  int is_mounted;
  uint64_t total_bytes;
  uint64_t free_bytes;
} MountEntry;

static MountEntry g_mounts[] = {
    {"ux0:", "SD2Vita / Main Storage", 0, 0, 0},
    {"ur0:", "Internal eMMC Storage", 0, 0, 0},
    {"uma0:", "USB / Secondary Storage", 0, 0, 0},
    {"imc0:", "Internal System Memory", 0, 0, 0},
    {"xmc0:", "Sony Memory Card", 0, 0, 0},
};
#define NUM_MOUNTS (sizeof(g_mounts) / sizeof(g_mounts[0]))
static int g_selected_mount_idx = 0;
static int g_show_all_drives = 0; /* 0 = show mounted only by default, 1 = show all */

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
  (void)color;
  char buf[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

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
  SceIoStat stat;
  if (sceIoGetstat(path, &stat) >= 0) {
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
  if (strcmp(mount_name, "ur0:") == 0)
    return 0;
  else if (strcmp(mount_name, "ux0:") == 0)
    id = SCE_VSH_MOUNT_UX0;
  else if (strcmp(mount_name, "xmc0:") == 0)
    id = SCE_VSH_MOUNT_XMC0;
  else if (strcmp(mount_name, "uma0:") == 0)
    id = SCE_VSH_MOUNT_UMA0;
  else if (strcmp(mount_name, "imc0:") == 0)
    id = SCE_VSH_MOUNT_IMC0;
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

  /* Default preference: ux0: if present, else ur0: */
  for (size_t i = 0; i < NUM_MOUNTS; i++) {
    char test_path[32];
    snprintf(test_path, sizeof(test_path), "%slinux/zImage", g_mounts[i].name);
    if (file_exists(test_path)) {
      g_selected_mount_idx = i;
      return;
    }
  }

  g_selected_mount_idx = 0;
}

static void check_mount_files(const char *mount, MountFileStatus *status) {
  char path[128];

  snprintf(path, sizeof(path), "%slinux/zImage", mount);
  status->kernel_present = file_exists(path);

  snprintf(path, sizeof(path), "%slinux/vita.dtb", mount);
  status->dtb_present = file_exists(path);
  if (!status->dtb_present) {
    snprintf(path, sizeof(path), "%slinux/vita1000.dtb", mount);
    status->dtb_present = file_exists(path);
  }
  if (!status->dtb_present) {
    snprintf(path, sizeof(path), "%slinux/vita2000.dtb", mount);
    status->dtb_present = file_exists(path);
  }
  if (!status->dtb_present) {
    snprintf(path, sizeof(path), "%slinux/pstv.dtb", mount);
    status->dtb_present = file_exists(path);
  }

  snprintf(path, sizeof(path), "%slinux/payload.bin", mount);
  status->payload_present = file_exists(path);

  snprintf(path, sizeof(path), "%slinux/baremetal-loader.skprx", mount);
  status->loader_present = file_exists(path);

  snprintf(path, sizeof(path), "%slinux/wpa_supplicant.conf", mount);
  status->wifi_present = file_exists(path);
}

static int are_keyfiles_present_on_mount(const MountFileStatus *status) {
  return (status->kernel_present && status->dtb_present &&
          status->payload_present && status->loader_present);
}

static int are_bundled_files_present(void) {
  return file_exists("app0:data/zImage") &&
         file_exists("app0:data/baremetal-loader.skprx") &&
         file_exists("app0:data/payload.bin");
}

static inline uint32_t wait_button_press(uint32_t mask) {
  return gui_wait_button_press(mask);
}

static inline void draw_progress_modal(const char *title, const char *cur_file, float progress, const char *status_str, const char *badge_prompt) {
  gui_draw_progress_modal(title, cur_file, progress, status_str, badge_prompt);
}

static void mount_partition_interactive(const char *mount) {
  draw_progress_modal("MOUNTING STORAGE PARTITION", mount, 0.5f, "Requesting VitaOS to mount storage...", NULL);
  sceKernelDelayThread(200 * 1000);

  int res = mount_partition(mount);
  if (res >= 0) {
    draw_progress_modal("PARTITION MOUNTED", mount, 1.0f, "[OK] Storage partition successfully mounted!", NULL);
    sceKernelDelayThread(600 * 1000);
    refresh_mounts();
  } else {
    draw_progress_modal("MOUNT FAILED", mount, 0.0f, "Could not mount partition. Ensure storage is inserted.", "TAP SCREEN OR PRESS CROSS TO RETURN");
    sceKernelDelayThread(400 * 1000);
    wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_START | SCE_CTRL_CIRCLE);
  }
}

static int copy_file(const char *src_path, const char *dst_path, int overwrite) {
  if (!overwrite && file_exists(dst_path)) {
    return 0;
  }

  SceUID src_fd = sceIoOpen(src_path, SCE_O_RDONLY, 0);
  if (src_fd < 0) {
    return src_fd;
  }

  SceUID dst_fd = sceIoOpen(dst_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (dst_fd < 0) {
    sceIoClose(src_fd);
    return dst_fd;
  }

  static char buffer[128 * 1024];
  int read_bytes;

  while ((read_bytes = sceIoRead(src_fd, buffer, sizeof(buffer))) > 0) {
    int written = sceIoWrite(dst_fd, buffer, read_bytes);
    if (written != read_bytes) {
      sceIoClose(src_fd);
      sceIoClose(dst_fd);
      return -1;
    }
  }

  sceIoClose(src_fd);
  sceIoClose(dst_fd);

  if (read_bytes < 0) {
    return read_bytes;
  }

  return 0;
}

static int install_linux_files_to_mount(const char *mount) {
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

    char label_buf[256];
    snprintf(label_buf, sizeof(label_buf), "Writing: %s", g_bundle_files[i].dst_rel);
    float prog = (float)i / (float)num_files;
    draw_progress_modal("INSTALLING LINUX FILES", label_buf, prog, "Copying assets to target partition...", NULL);

    int res = copy_file(src, dst, g_bundle_files[i].overwrite_if_exists);
    if (res == 0) {
      success_count++;
    } else {
      if (g_bundle_files[i].is_mandatory) {
        error_count++;
      }
    }
  }

  /* Sync loaders to ux0: for fallback compatibility */
  if (strcmp(mount, "ux0:") != 0 && dir_exists("ux0:")) {
    sceIoMkdir("ux0:linux", 0777);
    copy_file("app0:data/baremetal-loader.skprx", "ux0:linux/baremetal-loader.skprx", 1);
    copy_file("app0:data/baremetal-loader_360.skprx", "ux0:linux/baremetal-loader_360.skprx", 1);
    copy_file("app0:data/payload.bin", "ux0:linux/payload.bin", 1);
  }

  /* Synchronize storage devices to flush all newly copied files and metadata */
  sceIoSync("ux0:", 0);
  sceIoSync("ur0:", 0);

  if (error_count > 0) {
    draw_progress_modal("INSTALLATION FINISHED WITH ERRORS", "Some required files failed to copy.", 1.0f, "Check SD card free space and try again.", "TAP SCREEN OR PRESS CROSS TO CONTINUE");
  } else {
    char status_str[128];
    snprintf(status_str, sizeof(status_str), "Successfully installed %d assets to %slinux/!", success_count, mount);
    draw_progress_modal("INSTALLATION COMPLETE", status_str, 1.0f, "Ready to boot Linux on PlayStation Vita.", "TAP SCREEN OR PRESS CROSS TO CONTINUE");
  }

  sceKernelDelayThread(400 * 1000);
  wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_START | SCE_CTRL_CIRCLE);
  return (error_count > 0) ? -1 : 0;
}

static int copy_boot_files_to_mount(const char *mount) {
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

    char label_buf[256];
    snprintf(label_buf, sizeof(label_buf), "Copying: %s", boot_files[i]);
    float prog = (float)i / (float)count;
    draw_progress_modal("COPYING BOOT FILES", label_buf, prog, "Transferring bootloader and kernel...", NULL);

    if (copy_file(src, dst, 1) == 0) {
      copied_count++;
    }
  }

  /* Synchronize storage devices to flush all newly copied files and metadata */
  sceIoSync("ux0:", 0);
  sceIoSync("ur0:", 0);

  char status_str[128];
  snprintf(status_str, sizeof(status_str), "Copied %d boot files to %slinux/ successfully!", copied_count, mount);
  draw_progress_modal("COPY COMPLETE", status_str, 1.0f, "Target partition updated.", "TAP SCREEN OR PRESS CROSS TO CONTINUE");
  sceKernelDelayThread(400 * 1000);
  wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_START | SCE_CTRL_CIRCLE);
  return 0;
}

static void view_boot_log(void) {
  const char *log_paths[] = {LOG_PATH_UX0, LOG_PATH_UR0};
  const char *found_path = NULL;
  SceUID fd = -1;

  for (size_t i = 0; i < sizeof(log_paths) / sizeof(log_paths[0]); i++) {
    fd = sceIoOpen(log_paths[i], SCE_O_RDONLY, 0);
    if (fd >= 0) {
      found_path = log_paths[i];
      break;
    }
  }

  if (fd < 0) {
    draw_progress_modal("BOOT LOG NOT FOUND", "No boot_debug.log found on ux0: or ur0:.", 0.0f, "Launch Linux via Debug Boot to generate a log.", "TAP SCREEN OR PRESS CROSS TO RETURN");
    sceKernelDelayThread(400 * 1000);
    wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_START | SCE_CTRL_CIRCLE);
    return;
  }

  static char log_buffer[32768];
  memset(log_buffer, 0, sizeof(log_buffer));
  int rd = sceIoRead(fd, log_buffer, sizeof(log_buffer) - 1);
  sceIoClose(fd);

  if (rd <= 0) {
    draw_progress_modal("BOOT LOG EMPTY", "Log file is 0 bytes.", 0.0f, "Launch Linux to generate diagnostic entries.", "TAP SCREEN OR PRESS CROSS TO RETURN");
    sceKernelDelayThread(400 * 1000);
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
  const int page_size = 28;

  GuiRect r_back = {780, 10, 160, 36};
  GuiRect r_up = {895, 68, 45, 220};
  GuiRect r_dn = {895, 300, 45, 220};

  int log_needs_redraw = 1;

  while (1) {
    if (log_needs_redraw) {
      gui_clear_screen();

      /* Header Bar */
      gui_draw_text(24, 16, "BOOT DEBUG LOG", GUI_COLOR_ACCENT_CYAN, 2);
      char path_buf[128];
      snprintf(path_buf, sizeof(path_buf), "File: %s (%d lines)", found_path, line_count);
      gui_draw_text(24, 38, path_buf, GUI_COLOR_TEXT_MUTED, 1);

      gui_draw_action_button(&r_back, "BACK", NULL, "CIRCLE", GUI_COLOR_CARD_BG, 0);
      gui_draw_rect(0, 58, SCREEN_W, 1, GUI_COLOR_CARD_BORDER);

      /* Log card container */
      gui_draw_card(20, 68, 860, 456, GUI_COLOR_CARD_BG, GUI_COLOR_CARD_BORDER);

      /* Render text lines */
      int end_line = scroll_offset + page_size;
      if (end_line > line_count) end_line = line_count;

      for (int l = scroll_offset; l < end_line; l++) {
        if (lines[l]) {
          uint32_t text_col = GUI_COLOR_TEXT_MAIN;
          if (strstr(lines[l], "[FAIL]") || strstr(lines[l], "Error") || strstr(lines[l], "error")) {
            text_col = GUI_COLOR_ACCENT_RED;
          } else if (strstr(lines[l], "[OK]") || strstr(lines[l], "Success")) {
            text_col = GUI_COLOR_ACCENT_GREEN;
          } else if (strstr(lines[l], "[WARN]") || strstr(lines[l], "Warning")) {
            text_col = GUI_COLOR_ACCENT_AMBER;
          } else if (strstr(lines[l], "===") || strstr(lines[l], "PlayStation")) {
            text_col = GUI_COLOR_ACCENT_CYAN;
          }

          gui_draw_text(32, 78 + (l - scroll_offset) * 15, lines[l], text_col, 1);
        }
      }

      /* Scroll touch controls on right */
      gui_draw_action_button(&r_up, "UP", NULL, "^", GUI_COLOR_CARD_BG, 0);
      gui_draw_action_button(&r_dn, "DN", NULL, "v", GUI_COLOR_CARD_BG, 0);

      gui_swap_buffers();
      log_needs_redraw = 0;
    } else {
      gui_wait_vblank();
    }

    /* Poll touch and gamepad */
    GuiTouchState ts;
    gui_touch_poll(&ts);

    if (ts.just_pressed) {
      if (gui_hit_test(&r_back, ts.x, ts.y)) break;
      if (gui_hit_test(&r_up, ts.x, ts.y)) {
        scroll_offset -= 12;
        if (scroll_offset < 0) scroll_offset = 0;
        log_needs_redraw = 1;
      }
      if (gui_hit_test(&r_dn, ts.x, ts.y)) {
        scroll_offset += 12;
        if (scroll_offset > line_count - page_size) scroll_offset = line_count - page_size;
        if (scroll_offset < 0) scroll_offset = 0;
        log_needs_redraw = 1;
      }
    }

    SceCtrlData pad;
    sceCtrlPeekBufferPositive(0, &pad, 1);
    if (pad.buttons & (SCE_CTRL_CIRCLE | SCE_CTRL_START | SCE_CTRL_CROSS)) {
      while (1) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        if (!(pad.buttons & (SCE_CTRL_CIRCLE | SCE_CTRL_START | SCE_CTRL_CROSS))) break;
        gui_wait_vblank();
      }
      break;
    }
    if (pad.buttons & (SCE_CTRL_UP | SCE_CTRL_LTRIGGER)) {
      scroll_offset -= (pad.buttons & SCE_CTRL_LTRIGGER) ? 14 : 2;
      if (scroll_offset < 0) scroll_offset = 0;
      log_needs_redraw = 1;
      sceKernelDelayThread(80 * 1000);
    }
    if (pad.buttons & (SCE_CTRL_DOWN | SCE_CTRL_RTRIGGER)) {
      scroll_offset += (pad.buttons & SCE_CTRL_RTRIGGER) ? 14 : 2;
      if (scroll_offset > line_count - page_size) scroll_offset = line_count - page_size;
      if (scroll_offset < 0) scroll_offset = 0;
      log_needs_redraw = 1;
      sceKernelDelayThread(80 * 1000);
    }
  }
}

static void boot_linux(const char *mount, int debug_mode) {
  debug_log_init();

  debug_log_printf(COLOR_CYAN,
                   "========================================================\n");
  debug_log_printf(COLOR_CYAN,
                   " PlayStation Vita Linux 6.12 Bootstrapper\n");
  debug_log_printf(COLOR_CYAN,
                   "========================================================\n\n");
  debug_log_printf(COLOR_WHITE, " Target Storage Mount: %s\n", mount);
  debug_log_printf(COLOR_WHITE, " Log File: %s\n\n", g_active_log_path);

  /* Step 1: Hardware Environment */
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

  const char *model_desc = "Unknown Model";
  if (is_dolce) {
    model_desc = "PlayStation TV / Vita TV (Dolce, VTE-1000)";
  } else if (is_mc_emu) {
    model_desc = "PS Vita Slim (LCD, PCH-2000 series)";
  } else {
    model_desc = has_wwan ? "PS Vita 1000 3G/Wi-Fi (OLED, PCH-1100 series)"
                          : "PS Vita 1000 Wi-Fi (OLED, PCH-1000 series)";
  }
  debug_log_printf(COLOR_WHITE, "   - Detected Hardware:     %s\n", model_desc);
  debug_log_printf(
      COLOR_GREY,
      "     (Model ID: 0x%08X | VITA: %d | Dolce: %d | MCEmu: %d)\n",
      model_raw, is_vita, is_dolce, is_mc_emu);

  int mc_res = mount_partition("xmc0:");
  debug_log_printf(COLOR_WHITE, "   - Sony Memory Card:      %s\n",
                   (mc_res >= 0) ? "INSERTED (MSIF interface active)"
                                 : "NOT INSERTED or absent");

  /* Step 2: Storage Partitions */
  debug_log_printf(COLOR_YELLOW, "\n[2/5] Storage Partitions Status:\n");
  for (size_t i = 0; i < NUM_MOUNTS; i++) {
    char total_str[16];
    char free_str[16];
    format_size(g_mounts[i].total_bytes, total_str, sizeof(total_str));
    format_size(g_mounts[i].free_bytes, free_str, sizeof(free_str));

    int is_target = (strcmp(g_mounts[i].name, mount) == 0);
    if (g_mounts[i].is_mounted) {
      debug_log_printf(
          COLOR_GREEN,
          "   - %-6s : [MOUNTED] (%s free of %s)%s\n",
          g_mounts[i].name, free_str, total_str,
          is_target ? " *TARGET*" : "");
    } else {
      debug_log_printf(COLOR_GREY, "   - %-6s : [NOT MOUNTED]\n",
                       g_mounts[i].name);
    }
  }

  /* Step 3: Kernel & DTB Files */
  debug_log_printf(COLOR_YELLOW, "\n[3/5] Kernel & DTB Files (%slinux/):\n",
                   mount);
  char path[128];
  uint64_t file_sz = 0;
  int readable = 0;
  int critical_errors = 0;

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

  int payload_found = 0;
  snprintf(path, sizeof(path), "%slinux/payload.bin", mount);
  if (inspect_file(path, &file_sz, &readable) == 0 && file_sz > 0) {
    debug_log_printf(
        COLOR_GREEN,
        "   - Target payload.bin:    PRESENT (%slinux/payload.bin, %u bytes) [OK]\n",
        mount, (unsigned int)file_sz);
    payload_found = 1;
  }

  if (inspect_file("ux0:linux/payload.bin", &file_sz, &readable) == 0 && file_sz > 0) {
    payload_found = 1;
  }

  if (!payload_found) {
    debug_log_printf(COLOR_RED, "   - payload.bin:           CRITICAL MISSING! [FAIL]\n");
    critical_errors++;
  }

  char mod_path[128];
  snprintf(mod_path, sizeof(mod_path), "%slinux/baremetal-loader.skprx", mount);
  int loader_found = 0;
  if (inspect_file(mod_path, &file_sz, &readable) == 0 && file_sz > 0) {
    loader_found = 1;
  }
  if (inspect_file("ux0:linux/baremetal-loader.skprx", &file_sz, &readable) == 0 && file_sz > 0) {
    loader_found = 1;
  }

  if (!loader_found) {
    debug_log_printf(COLOR_RED, "   - loader.skprx:          CRITICAL MISSING! [FAIL]\n");
    critical_errors++;
  }

  /* Step 5: Pre-Flight Assessment */
  debug_log_printf(COLOR_YELLOW, "\n[5/5] Pre-Flight Assessment & Handover:\n");

  if (critical_errors > 0) {
    debug_log_printf(COLOR_RED, "   [FAIL] Pre-flight halted with %d critical error(s)!\n", critical_errors);
    draw_progress_modal("PRE-FLIGHT VALIDATION FAILED", "Required kernel or loader files missing.", 0.0f, "Check boot_debug.log or tap Install Linux.", "TAP SCREEN OR PRESS CROSS TO RETURN");
    debug_log_close();
    sceKernelDelayThread(400 * 1000);
    wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_START | SCE_CTRL_CIRCLE);
    return;
  }

  debug_log_printf(COLOR_GREEN, "   [OK] All required boot files and pre-flight checks verified!\n");

  if (debug_mode) {
    debug_log_printf(COLOR_YELLOW, "\nControls: [X] Proceed | [CIRCLE] Cancel\n");
    draw_progress_modal("DEBUG BOOT: PRE-FLIGHT VERIFIED", "Diagnostic log recorded to boot_debug.log.", 1.0f, "Press CROSS to launch Linux, or CIRCLE to cancel.", "CROSS: LAUNCH  |  CIRCLE: CANCEL");
    uint32_t choice = wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START);
    if (choice & (SCE_CTRL_CIRCLE | SCE_CTRL_START)) {
      debug_log_close();
      return;
    }
  }

  /* Modal launch announcement */
  draw_progress_modal("LAUNCHING LINUX 6.12", "Handover in progress: Transitioning via Syscon...", 0.9f, "Screen will turn black as soft-reset transitions into Linux.", NULL);
  sceKernelDelayThread(400 * 1000);

  /* Flush and close debug log before module launch */
  debug_log_close();

  /* Sync all storage devices so SD2Vita / Sony memory cards are fully committed */
  sceIoSync("ux0:", 0);
  sceIoSync("ur0:", 0);
  sceKernelDelayThread(500 * 1000);

  /* Initiate loader module launch */
  tai_module_args_t argg;
  argg.size = sizeof(argg);
  argg.pid = KERNEL_PID;
  argg.args = strlen(mount) + 1;
  argg.argp = (void *)mount;
  argg.flags = 0;

  SceUID mod_id = -1;
  if (file_exists(mod_path)) {
    mod_id = taiLoadStartKernelModuleForUser(mod_path, &argg);
  }
  if (mod_id < 0 && file_exists("ux0:linux/baremetal-loader.skprx")) {
    mod_id = taiLoadStartKernelModuleForUser("ux0:linux/baremetal-loader.skprx", &argg);
  }
  if (mod_id < 0) {
    char mod_360[128];
    snprintf(mod_360, sizeof(mod_360), "%slinux/baremetal-loader_360.skprx", mount);
    if (file_exists(mod_360)) {
      mod_id = taiLoadStartKernelModuleForUser(mod_360, &argg);
    } else if (file_exists("ux0:linux/baremetal-loader_360.skprx")) {
      mod_id = taiLoadStartKernelModuleForUser("ux0:linux/baremetal-loader_360.skprx", &argg);
    }
  }

  if (mod_id < 0) {
    draw_progress_modal("KERNEL MODULE LAUNCH FAILED", "Could not start baremetal-loader.skprx.", 0.0f, "Check boot_debug.log for error code.", "TAP SCREEN OR PRESS CROSS TO RETURN");
    debug_log_close();
    sceKernelDelayThread(400 * 1000);
    wait_button_press(SCE_CTRL_START | SCE_CTRL_CROSS | SCE_CTRL_CIRCLE);
    return;
  }

  debug_log_close();
  sceKernelDelayThread(400 * 1000);
}

static void render_main_ui(const MountFileStatus *status, int installed, int has_bundled, int pressed_btn,
                           const GuiRect *r_exit, const GuiRect *r_btn1, const GuiRect *r_btn2,
                           const GuiRect *r_btn3, const GuiRect *r_btn4) {
  gui_clear_screen();

  /* Header Bar */
  gui_draw_text(24, 16, "LinuxOnVita", GUI_COLOR_ACCENT_CYAN, 2);
  gui_draw_badge(210, 18, "v1.5.0", GUI_COLOR_CARD_BG, GUI_COLOR_ACCENT_CYAN);
  gui_draw_text(24, 38, "PlayStation Vita Linux Bootstrapper & Installer", GUI_COLOR_TEXT_MUTED, 1);
  gui_draw_action_button(r_exit, "EXIT", NULL, "START", GUI_COLOR_CARD_BG, pressed_btn == 5);
  gui_draw_rect(0, 58, SCREEN_W, 1, GUI_COLOR_CARD_BORDER);

  /* Left Column: Storage Partitions (Mounted only by default) */
  gui_draw_text(24, 72, g_show_all_drives ? "STORAGE PARTITIONS (ALL DRIVES)" : "STORAGE PARTITIONS (MOUNTED DRIVES)", GUI_COLOR_TEXT_DIM, 1);

  int visible_indices[NUM_MOUNTS];
  int visible_count = 0;
  for (size_t i = 0; i < NUM_MOUNTS; i++) {
    if (g_show_all_drives || g_mounts[i].is_mounted) {
      visible_indices[visible_count++] = (int)i;
    }
  }
  if (visible_count == 0) {
    for (size_t i = 0; i < NUM_MOUNTS; i++) {
      visible_indices[visible_count++] = (int)i;
    }
  }

  int card_h = g_show_all_drives ? 56 : 76;
  int card_gap = g_show_all_drives ? 6 : 10;

  for (int v = 0; v < visible_count; v++) {
    int i = visible_indices[v];
    int is_sel = (i == g_selected_mount_idx);
    GuiRect r_card = {20, 92 + v * (card_h + card_gap), 430, card_h};

    uint32_t bg = is_sel ? GUI_COLOR_CARD_SEL_BG : GUI_COLOR_CARD_BG;
    uint32_t bdr = is_sel ? GUI_COLOR_CARD_SEL_BDR : GUI_COLOR_CARD_BORDER;

    gui_draw_card(r_card.x, r_card.y, r_card.w, r_card.h, bg, bdr);
    if (is_sel) {
      gui_draw_rect_outline(r_card.x, r_card.y, r_card.w, r_card.h, 2, GUI_COLOR_ACCENT_CYAN);
    }

    /* Partition Title */
    char title_str[64];
    snprintf(title_str, sizeof(title_str), "%s (%s)", g_mounts[i].name, g_mounts[i].label);
    gui_draw_text(r_card.x + 12, r_card.y + 10, title_str, is_sel ? GUI_COLOR_ACCENT_CYAN : GUI_COLOR_TEXT_MAIN, 1);

    if (card_h >= 70) {
      /* Spacious layout (mounted drives default) */
      /* Row 2: Recommendation Badge on left, Mount status on right */
      if (strcmp(g_mounts[i].name, "ux0:") == 0) {
        gui_draw_badge(r_card.x + 12, r_card.y + 28, "RECOMMENDED (SD2VITA)", GUI_COLOR_ACCENT_GREEN, COLOR_RGBA(15, 23, 42, 255));
      } else if (strcmp(g_mounts[i].name, "ur0:") == 0) {
        gui_draw_badge(r_card.x + 12, r_card.y + 28, "RECOMMENDED (INTERNAL)", GUI_COLOR_ACCENT_CYAN, COLOR_RGBA(15, 23, 42, 255));
      } else {
        gui_draw_badge(r_card.x + 12, r_card.y + 28, "OPTIONAL STORAGE", GUI_COLOR_CARD_BG, GUI_COLOR_TEXT_DIM);
      }

      if (g_mounts[i].is_mounted) {
        gui_draw_badge(r_card.x + r_card.w - 85, r_card.y + 28, "MOUNTED", GUI_COLOR_CARD_BG, GUI_COLOR_ACCENT_GREEN);
        char total_str[16];
        char free_str[16];
        format_size(g_mounts[i].total_bytes, total_str, sizeof(total_str));
        format_size(g_mounts[i].free_bytes, free_str, sizeof(free_str));
        char sub_str[64];
        snprintf(sub_str, sizeof(sub_str), "Free: %s  /  Total: %s", free_str, total_str);
        gui_draw_text(r_card.x + 12, r_card.y + 54, sub_str, GUI_COLOR_TEXT_MUTED, 1);
      } else {
        gui_draw_badge(r_card.x + r_card.w - 95, r_card.y + 28, "UNMOUNTED", GUI_COLOR_CARD_BG, GUI_COLOR_TEXT_DIM);
        gui_draw_text(r_card.x + 12, r_card.y + 54, "Status: Not Mounted / Not Inserted", GUI_COLOR_TEXT_DIM, 1);
      }
    } else {
      /* Compact layout when all drives are displayed */
      if (strcmp(g_mounts[i].name, "ux0:") == 0) {
        gui_draw_badge(r_card.x + 12, r_card.y + 30, "REC (SD2VITA)", GUI_COLOR_ACCENT_GREEN, COLOR_RGBA(15, 23, 42, 255));
      } else if (strcmp(g_mounts[i].name, "ur0:") == 0) {
        gui_draw_badge(r_card.x + 12, r_card.y + 30, "REC (INTERNAL)", GUI_COLOR_ACCENT_CYAN, COLOR_RGBA(15, 23, 42, 255));
      }

      if (g_mounts[i].is_mounted) {
        char total_str[16];
        char free_str[16];
        format_size(g_mounts[i].total_bytes, total_str, sizeof(total_str));
        format_size(g_mounts[i].free_bytes, free_str, sizeof(free_str));
        char sub_str[64];
        snprintf(sub_str, sizeof(sub_str), "Free: %s / %s", free_str, total_str);
        gui_draw_text(r_card.x + 135, r_card.y + 34, sub_str, GUI_COLOR_TEXT_MUTED, 1);
        gui_draw_badge(r_card.x + r_card.w - 85, r_card.y + 30, "MOUNTED", GUI_COLOR_CARD_BG, GUI_COLOR_ACCENT_GREEN);
      } else {
        gui_draw_badge(r_card.x + r_card.w - 95, r_card.y + 30, "UNMOUNTED", GUI_COLOR_CARD_BG, GUI_COLOR_TEXT_DIM);
      }
    }

    if (is_sel) {
      gui_draw_badge(r_card.x + r_card.w - 85, r_card.y + 8, "TARGET", GUI_COLOR_ACCENT_CYAN, COLOR_RGBA(15, 23, 42, 255));
    }
  }

  /* Toggle button below partition list */
  int y_toggle = 92 + visible_count * (card_h + card_gap);
  int toggle_h = g_show_all_drives ? 30 : 34;
  GuiRect r_toggle = {20, y_toggle, 430, toggle_h};
  gui_draw_action_button(&r_toggle, g_show_all_drives ? "[-] HIDE UNMOUNTED DRIVES" : "[+] SHOW ALL STORAGE DRIVES", NULL, NULL, GUI_COLOR_CARD_BG, 0);

  /* Right Column: Boot Validation Card */
  gui_draw_text(474, 72, "TARGET BOOT VALIDATION", GUI_COLOR_TEXT_DIM, 1);
  gui_draw_card(470, 92, 470, 335, GUI_COLOR_CARD_BG, GUI_COLOR_CARD_BORDER);

  const char *target_mount = g_mounts[g_selected_mount_idx].name;
  int is_target_mounted = g_mounts[g_selected_mount_idx].is_mounted;

  /* Readiness Banner */
  if (!is_target_mounted) {
    gui_draw_card(482, 104, 446, 54, COLOR_RGBA(127, 29, 29, 255), GUI_COLOR_ACCENT_RED);
    gui_draw_text(498, 114, "[!] TARGET UNMOUNTED", GUI_COLOR_TEXT_MAIN, 2);
    gui_draw_text(498, 136, "Tap Mount Partition below or press CROSS", GUI_COLOR_ACCENT_AMBER, 1);
  } else if (installed) {
    gui_draw_card(482, 104, 446, 54, COLOR_RGBA(6, 78, 59, 255), GUI_COLOR_ACCENT_GREEN);
    gui_draw_text(498, 114, "[OK] READY TO BOOT LINUX", GUI_COLOR_TEXT_MAIN, 2);
    gui_draw_text(498, 136, "All required kernel and loader assets verified", GUI_COLOR_TEXT_MAIN, 1);
  } else {
    gui_draw_card(482, 104, 446, 54, COLOR_RGBA(120, 53, 15, 255), GUI_COLOR_ACCENT_AMBER);
    gui_draw_text(498, 114, "[^] SETUP REQUIRED", GUI_COLOR_TEXT_MAIN, 2);
    gui_draw_text(498, 136, "Tap Install Linux below to copy bundled files", GUI_COLOR_TEXT_MAIN, 1);
  }

  gui_draw_rect(482, 170, 446, 1, GUI_COLOR_CARD_BORDER);

  /* Asset rows */
  gui_draw_text(494, 182, "Kernel Image (zImage)", GUI_COLOR_TEXT_MAIN, 1);
  gui_draw_badge(790, 178, status->kernel_present ? "PRESENT" : "MISSING", GUI_COLOR_CARD_BG, status->kernel_present ? GUI_COLOR_ACCENT_GREEN : GUI_COLOR_ACCENT_RED);

  gui_draw_text(494, 210, "Device Tree (vita.dtb)", GUI_COLOR_TEXT_MAIN, 1);
  gui_draw_badge(790, 206, status->dtb_present ? "PRESENT" : "MISSING", GUI_COLOR_CARD_BG, status->dtb_present ? GUI_COLOR_ACCENT_GREEN : GUI_COLOR_ACCENT_RED);

  gui_draw_text(494, 238, "Baremetal Loader (payload.bin)", GUI_COLOR_TEXT_MAIN, 1);
  gui_draw_badge(790, 234, status->payload_present ? "PRESENT" : "MISSING", GUI_COLOR_CARD_BG, status->payload_present ? GUI_COLOR_ACCENT_GREEN : GUI_COLOR_ACCENT_RED);

  gui_draw_text(494, 266, "Kernel Plugin (loader.skprx)", GUI_COLOR_TEXT_MAIN, 1);
  gui_draw_badge(790, 262, status->loader_present ? "PRESENT" : "MISSING", GUI_COLOR_CARD_BG, status->loader_present ? GUI_COLOR_ACCENT_GREEN : GUI_COLOR_ACCENT_RED);

  gui_draw_text(494, 294, "Wi-Fi Config (wpa_supplicant)", GUI_COLOR_TEXT_MAIN, 1);
  gui_draw_badge(790, 290, status->wifi_present ? "CONFIGURED" : "TEMPLATE", GUI_COLOR_CARD_BG, status->wifi_present ? GUI_COLOR_ACCENT_GREEN : GUI_COLOR_ACCENT_AMBER);

  gui_draw_rect(482, 320, 446, 1, GUI_COLOR_CARD_BORDER);

  /* Target summary */
  char dst_info[64];
  snprintf(dst_info, sizeof(dst_info), "%slinux/", target_mount);
  gui_draw_text(494, 330, "Destination:", GUI_COLOR_TEXT_DIM, 1);
  gui_draw_text(600, 330, dst_info, GUI_COLOR_ACCENT_CYAN, 1);

  gui_draw_text(494, 348, "Pre-Staging:", GUI_COLOR_TEXT_DIM, 1);
  gui_draw_text(600, 348, "DRAM In-Memory (Active)", GUI_COLOR_ACCENT_GREEN, 1);

  char free_str[16];
  format_size(g_mounts[g_selected_mount_idx].free_bytes, free_str, sizeof(free_str));
  gui_draw_text(494, 366, "Free Space:", GUI_COLOR_TEXT_DIM, 1);
  gui_draw_text(600, 366, free_str, GUI_COLOR_TEXT_MAIN, 1);

  gui_draw_text(494, 384, "Storage:", GUI_COLOR_TEXT_DIM, 1);
  if (strcmp(target_mount, "ux0:") == 0) {
    gui_draw_text(600, 384, "SD2Vita (Recommended #1)", GUI_COLOR_ACCENT_GREEN, 1);
  } else if (strcmp(target_mount, "ur0:") == 0) {
    gui_draw_text(600, 384, "Internal Storage (Recommended #2)", GUI_COLOR_ACCENT_CYAN, 1);
  } else {
    gui_draw_text(600, 384, "Secondary / Custom Partition", GUI_COLOR_TEXT_MUTED, 1);
  }

  /* Bottom Action Buttons */
  if (!is_target_mounted) {
    gui_draw_action_button(r_btn1, "MOUNT", "Mount storage partition", "CROSS", GUI_BTN_AMBER, pressed_btn == 1);
  } else if (installed) {
    gui_draw_action_button(r_btn1, "BOOT LINUX", "Launch Linux 6.12", "CROSS", GUI_BTN_EMERALD, pressed_btn == 1);
  } else {
    gui_draw_action_button(r_btn1, "INSTALL", "Copy files to target", "CROSS", GUI_BTN_SKY, pressed_btn == 1);
  }

  gui_draw_action_button(r_btn2, "DEBUG BOOT", "Verbose diagnostics", "SQUARE", GUI_BTN_SLATE, pressed_btn == 2);
  gui_draw_action_button(r_btn3, installed ? "REINSTALL" : "COPY BOOT", "Sync bundled files", "TRIANGLE", GUI_BTN_SLATE, pressed_btn == 3);
  gui_draw_action_button(r_btn4, "BOOT LOG", "Inspect diagnostics", "SELECT", GUI_BTN_SLATE, pressed_btn == 4);

  gui_swap_buffers();
}

int main(int argc, char *argv[]) {
  if (gui_init() < 0) {
    return -1;
  }
  gui_touch_init();

  /* Auto-mount Sony memory card (xmc0:) if unmounted */
  mount_partition("xmc0:");

  refresh_mounts();
  load_mount_preference();

  if (!g_mounts[g_selected_mount_idx].is_mounted) {
    mount_partition(g_mounts[g_selected_mount_idx].name);
    refresh_mounts();
  }

  /* If preferred drive is still not mounted, fallback to first mounted drive */
  if (!g_mounts[g_selected_mount_idx].is_mounted) {
    for (size_t i = 0; i < NUM_MOUNTS; i++) {
      if (g_mounts[i].is_mounted) {
        g_selected_mount_idx = i;
        break;
      }
    }
  }

  GuiRect r_exit = {790, 12, 150, 36};
  GuiRect r_btn1 = {20, 448, 235, 76};
  GuiRect r_btn2 = {265, 448, 215, 76};
  GuiRect r_btn3 = {490, 448, 215, 76};
  GuiRect r_btn4 = {715, 448, 225, 76};

  int pressed_btn = 0;
  int needs_redraw = 1;
  int frame_counter = 0;

  MountFileStatus status;
  memset(&status, 0, sizeof(status));

  int is_target_mounted = g_mounts[g_selected_mount_idx].is_mounted;
  if (is_target_mounted) {
    check_mount_files(g_mounts[g_selected_mount_idx].name, &status);
  }
  int installed = are_keyfiles_present_on_mount(&status);
  int has_bundled = are_bundled_files_present();

  while (1) {
    /* Periodic storage poll (every 120 frames / ~2 seconds) to avoid I/O stutters */
    frame_counter++;
    if (frame_counter >= 120) {
      frame_counter = 0;
      refresh_mounts();
      is_target_mounted = g_mounts[g_selected_mount_idx].is_mounted;
      memset(&status, 0, sizeof(status));
      if (is_target_mounted) {
        check_mount_files(g_mounts[g_selected_mount_idx].name, &status);
      }
      installed = are_keyfiles_present_on_mount(&status);
      has_bundled = are_bundled_files_present();
      needs_redraw = 1;
    }

    /* Redraw only when state or input changes */
    if (needs_redraw) {
      render_main_ui(&status, installed, has_bundled, pressed_btn, &r_exit, &r_btn1, &r_btn2, &r_btn3, &r_btn4);
      needs_redraw = 0;
    } else {
      gui_wait_vblank();
    }

    const char *target_mount = g_mounts[g_selected_mount_idx].name;

    /* Compute visible drive geometry for touch / controller navigation */
    int visible_indices[NUM_MOUNTS];
    int visible_count = 0;
    for (size_t i = 0; i < NUM_MOUNTS; i++) {
      if (g_show_all_drives || g_mounts[i].is_mounted) {
        visible_indices[visible_count++] = (int)i;
      }
    }
    if (visible_count == 0) {
      for (size_t i = 0; i < NUM_MOUNTS; i++) {
        visible_indices[visible_count++] = (int)i;
      }
    }
    int card_h = g_show_all_drives ? 56 : 76;
    int card_gap = g_show_all_drives ? 6 : 10;
    int y_toggle = 92 + visible_count * (card_h + card_gap);
    int toggle_h = g_show_all_drives ? 30 : 34;
    GuiRect r_toggle = {20, y_toggle, 430, toggle_h};

    /* ================= 2. TOUCH & CONTROLLER POLLING ================= */
    GuiTouchState ts;
    gui_touch_poll(&ts);

    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    sceCtrlPeekBufferPositive(0, &pad, 1);

    /* Check touch interaction */
    if (ts.just_pressed) {
      /* Exit Button */
      if (gui_hit_test(&r_exit, ts.x, ts.y)) {
        pressed_btn = 5;
        render_main_ui(&status, installed, has_bundled, pressed_btn, &r_exit, &r_btn1, &r_btn2, &r_btn3, &r_btn4);
        sceKernelDelayThread(80 * 1000);
        break;
      }

      /* Toggle Show All / Mounted Only */
      if (gui_hit_test(&r_toggle, ts.x, ts.y)) {
        g_show_all_drives = !g_show_all_drives;
        needs_redraw = 1;
        sceKernelDelayThread(80 * 1000);
        continue;
      }

      /* Partition selection touch */
      for (int v = 0; v < visible_count; v++) {
        int i = visible_indices[v];
        GuiRect r_card = {20, 92 + v * (card_h + card_gap), 430, card_h};
        if (gui_hit_test(&r_card, ts.x, ts.y)) {
          if (g_selected_mount_idx != i) {
            g_selected_mount_idx = i;
            save_mount_preference(g_mounts[g_selected_mount_idx].name);
            is_target_mounted = g_mounts[g_selected_mount_idx].is_mounted;
            memset(&status, 0, sizeof(status));
            if (is_target_mounted) {
              check_mount_files(g_mounts[g_selected_mount_idx].name, &status);
            }
            installed = are_keyfiles_present_on_mount(&status);
            needs_redraw = 1;
          }
          break;
        }
      }

      /* Primary Action Button */
      if (gui_hit_test(&r_btn1, ts.x, ts.y)) {
        pressed_btn = 1;
        render_main_ui(&status, installed, has_bundled, pressed_btn, &r_exit, &r_btn1, &r_btn2, &r_btn3, &r_btn4);
        sceKernelDelayThread(80 * 1000);
        if (!is_target_mounted) {
          mount_partition_interactive(target_mount);
        } else if (installed) {
          boot_linux(target_mount, 0);
        } else if (has_bundled) {
          install_linux_files_to_mount(target_mount);
        }
        refresh_mounts();
        is_target_mounted = g_mounts[g_selected_mount_idx].is_mounted;
        memset(&status, 0, sizeof(status));
        if (is_target_mounted) {
          check_mount_files(g_mounts[g_selected_mount_idx].name, &status);
        }
        installed = are_keyfiles_present_on_mount(&status);
        has_bundled = are_bundled_files_present();
        pressed_btn = 0;
        needs_redraw = 1;
        continue;
      }

      /* Debug Boot Button */
      if (gui_hit_test(&r_btn2, ts.x, ts.y)) {
        pressed_btn = 2;
        render_main_ui(&status, installed, has_bundled, pressed_btn, &r_exit, &r_btn1, &r_btn2, &r_btn3, &r_btn4);
        sceKernelDelayThread(80 * 1000);
        boot_linux(target_mount, 1);
        refresh_mounts();
        is_target_mounted = g_mounts[g_selected_mount_idx].is_mounted;
        memset(&status, 0, sizeof(status));
        if (is_target_mounted) {
          check_mount_files(g_mounts[g_selected_mount_idx].name, &status);
        }
        installed = are_keyfiles_present_on_mount(&status);
        has_bundled = are_bundled_files_present();
        pressed_btn = 0;
        needs_redraw = 1;
        continue;
      }

      /* Reinstall / Copy Button */
      if (gui_hit_test(&r_btn3, ts.x, ts.y)) {
        pressed_btn = 3;
        render_main_ui(&status, installed, has_bundled, pressed_btn, &r_exit, &r_btn1, &r_btn2, &r_btn3, &r_btn4);
        sceKernelDelayThread(80 * 1000);
        if (installed && has_bundled) {
          install_linux_files_to_mount(target_mount);
        } else {
          copy_boot_files_to_mount(target_mount);
        }
        refresh_mounts();
        is_target_mounted = g_mounts[g_selected_mount_idx].is_mounted;
        memset(&status, 0, sizeof(status));
        if (is_target_mounted) {
          check_mount_files(g_mounts[g_selected_mount_idx].name, &status);
        }
        installed = are_keyfiles_present_on_mount(&status);
        has_bundled = are_bundled_files_present();
        pressed_btn = 0;
        needs_redraw = 1;
        continue;
      }

      /* View Logs Button */
      if (gui_hit_test(&r_btn4, ts.x, ts.y)) {
        pressed_btn = 4;
        render_main_ui(&status, installed, has_bundled, pressed_btn, &r_exit, &r_btn1, &r_btn2, &r_btn3, &r_btn4);
        sceKernelDelayThread(80 * 1000);
        view_boot_log();
        pressed_btn = 0;
        needs_redraw = 1;
        continue;
      }
    }

    /* Check Gamepad button shortcuts */
    if (pad.buttons) {
      if (pad.buttons & (SCE_CTRL_UP | SCE_CTRL_LEFT)) {
        int cur_pos = 0;
        for (int v = 0; v < visible_count; v++) {
          if (visible_indices[v] == g_selected_mount_idx) {
            cur_pos = v;
            break;
          }
        }
        cur_pos = (cur_pos + visible_count - 1) % visible_count;
        g_selected_mount_idx = visible_indices[cur_pos];
        save_mount_preference(g_mounts[g_selected_mount_idx].name);
        is_target_mounted = g_mounts[g_selected_mount_idx].is_mounted;
        memset(&status, 0, sizeof(status));
        if (is_target_mounted) {
          check_mount_files(g_mounts[g_selected_mount_idx].name, &status);
        }
        installed = are_keyfiles_present_on_mount(&status);
        needs_redraw = 1;
        sceKernelDelayThread(150 * 1000);
        continue;
      } else if (pad.buttons & (SCE_CTRL_DOWN | SCE_CTRL_RIGHT | SCE_CTRL_RTRIGGER | SCE_CTRL_R1)) {
        int cur_pos = 0;
        for (int v = 0; v < visible_count; v++) {
          if (visible_indices[v] == g_selected_mount_idx) {
            cur_pos = v;
            break;
          }
        }
        cur_pos = (cur_pos + 1) % visible_count;
        g_selected_mount_idx = visible_indices[cur_pos];
        save_mount_preference(g_mounts[g_selected_mount_idx].name);
        is_target_mounted = g_mounts[g_selected_mount_idx].is_mounted;
        memset(&status, 0, sizeof(status));
        if (is_target_mounted) {
          check_mount_files(g_mounts[g_selected_mount_idx].name, &status);
        }
        installed = are_keyfiles_present_on_mount(&status);
        needs_redraw = 1;
        sceKernelDelayThread(150 * 1000);
        continue;
      } else if (pad.buttons & SCE_CTRL_START) {
        break;
      } else if (pad.buttons & SCE_CTRL_SELECT) {
        pressed_btn = 4;
        render_main_ui(&status, installed, has_bundled, pressed_btn, &r_exit, &r_btn1, &r_btn2, &r_btn3, &r_btn4);
        sceKernelDelayThread(80 * 1000);
        view_boot_log();
        pressed_btn = 0;
        needs_redraw = 1;
        continue;
      } else if (pad.buttons & SCE_CTRL_CROSS) {
        pressed_btn = 1;
        render_main_ui(&status, installed, has_bundled, pressed_btn, &r_exit, &r_btn1, &r_btn2, &r_btn3, &r_btn4);
        sceKernelDelayThread(80 * 1000);
        if (!is_target_mounted) {
          mount_partition_interactive(target_mount);
        } else if (installed) {
          boot_linux(target_mount, 0);
        } else if (has_bundled) {
          install_linux_files_to_mount(target_mount);
        }
        refresh_mounts();
        is_target_mounted = g_mounts[g_selected_mount_idx].is_mounted;
        memset(&status, 0, sizeof(status));
        if (is_target_mounted) {
          check_mount_files(g_mounts[g_selected_mount_idx].name, &status);
        }
        installed = are_keyfiles_present_on_mount(&status);
        has_bundled = are_bundled_files_present();
        pressed_btn = 0;
        needs_redraw = 1;
        continue;
      } else if (pad.buttons & SCE_CTRL_SQUARE) {
        pressed_btn = 2;
        render_main_ui(&status, installed, has_bundled, pressed_btn, &r_exit, &r_btn1, &r_btn2, &r_btn3, &r_btn4);
        sceKernelDelayThread(80 * 1000);
        boot_linux(target_mount, 1);
        refresh_mounts();
        is_target_mounted = g_mounts[g_selected_mount_idx].is_mounted;
        memset(&status, 0, sizeof(status));
        if (is_target_mounted) {
          check_mount_files(g_mounts[g_selected_mount_idx].name, &status);
        }
        installed = are_keyfiles_present_on_mount(&status);
        has_bundled = are_bundled_files_present();
        pressed_btn = 0;
        needs_redraw = 1;
        continue;
      } else if (pad.buttons & SCE_CTRL_TRIANGLE) {
        pressed_btn = 3;
        render_main_ui(&status, installed, has_bundled, pressed_btn, &r_exit, &r_btn1, &r_btn2, &r_btn3, &r_btn4);
        sceKernelDelayThread(80 * 1000);
        if (installed && has_bundled) {
          install_linux_files_to_mount(target_mount);
        } else {
          copy_boot_files_to_mount(target_mount);
        }
        refresh_mounts();
        is_target_mounted = g_mounts[g_selected_mount_idx].is_mounted;
        memset(&status, 0, sizeof(status));
        if (is_target_mounted) {
          check_mount_files(g_mounts[g_selected_mount_idx].name, &status);
        }
        installed = are_keyfiles_present_on_mount(&status);
        has_bundled = are_bundled_files_present();
        pressed_btn = 0;
        needs_redraw = 1;
        continue;
      }
    }
  }

  return 0;
}
