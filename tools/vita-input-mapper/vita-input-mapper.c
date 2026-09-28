/*
 * vita-input-mapper.c
 * Translates PS Vita physical button and analog stick events (vita-buttons
 * evdev) into keyboard keystrokes and mouse movements via Linux /dev/uinput.
 *
 * Operational Modes:
 *   1. Terminal Mode (Default):
 *      - D-Pad Up / Down    : Arrow Up / Arrow Down (shell history)
 *      - D-Pad Left / Right : Arrow Left / Arrow Right (cursor navigation)
 *      - Cross (✕ / BTN_A)  : Enter
 *      - Circle (○ / BTN_B) : Backspace
 *      - Square (□ / BTN_X) : Space
 *      - Triangle (△ / BTN_Y): Tab (auto-complete)
 *      - L Trigger (BTN_TL) : Ctrl+C (Interrupt)
 *      - R Trigger (BTN_TR) : Page Up
 *      - Start (BTN_START)  : Enter
 *
 *   2. Desktop Mode (--desktop / -d):
 *      - Left Analog Stick  : Smooth Mouse Cursor Navigation (REL_X, REL_Y)
 *      - Right Analog Stick : Vertical Scroll Wheel (REL_WHEEL)
 *      - Cross / R Trigger  : Left Mouse Click (BTN_LEFT)
 *      - Circle / L Trigger : Right Mouse Click (BTN_RIGHT)
 *      - Square (□)         : Middle Mouse Click (BTN_MIDDLE)
 *      - Triangle (△)       : Enter (KEY_ENTER)
 *      - Start (BTN_START)  : Super / Windows Key (KEY_LEFTMETA for IceWM Menu)
 *      - Select (BTN_SELECT): Escape (KEY_ESC)
 *      - D-Pad              : Arrow Keys (Up, Down, Left, Right)
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define STICK_CENTER 128
#define STICK_DEADZONE 18
#define SCROLL_DEADZONE 36
#define TICK_MS 16

static volatile sig_atomic_t g_resumed = 0;
static volatile sig_atomic_t g_running = 1;
static volatile sig_atomic_t g_mode_desktop = 0;

static void handle_sigcont(int sig) {
  (void)sig;
  g_resumed = 1;
}

static void handle_sigterm(int sig) {
  (void)sig;
  g_running = 0;
}

static void handle_sigusr1(int sig) {
  (void)sig;
  g_mode_desktop = 1;
}

static void handle_sigusr2(int sig) {
  (void)sig;
  g_mode_desktop = 0;
}

static int find_buttons_device(void) {
  char path[256];
  char name[128];
  for (int i = 0; i < 32; i++) {
    snprintf(path, sizeof(path), "/dev/input/event%d", i);
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
      continue;

    memset(name, 0, sizeof(name));
    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0) {
      if (strstr(name, "PlayStation Vita Buttons") || strstr(name, "Buttons") ||
          strstr(name, "vita-buttons") || strstr(name, "vita_buttons")) {
        printf("[InputMapper] Found buttons device: %s (%s)\n", path, name);
        return fd;
      }
    }
    close(fd);
  }
  return -1;
}

static int setup_uinput_device(int desktop_mode) {
  int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (fd < 0) {
    perror("[InputMapper] Failed to open /dev/uinput");
    return -1;
  }

  ioctl(fd, UI_SET_EVBIT, EV_KEY);
  ioctl(fd, UI_SET_EVBIT, EV_SYN);

  // Keyboard keys common to both modes
  int keys[] = {KEY_UP,       KEY_DOWN,      KEY_LEFT,      KEY_RIGHT,
                KEY_ENTER,    KEY_BACKSPACE, KEY_SPACE,     KEY_TAB,
                KEY_LEFTCTRL, KEY_C,         KEY_PAGEUP,    KEY_PAGEDOWN,
                KEY_ESC,      KEY_LEFTMETA,  KEY_LEFTSHIFT, KEY_LEFTALT};

  for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
    ioctl(fd, UI_SET_KEYBIT, keys[i]);
  }

  // Always register pointer and mouse capabilities so mode switches work
  // seamlessly
  ioctl(fd, UI_SET_EVBIT, EV_REL);
  ioctl(fd, UI_SET_RELBIT, REL_X);
  ioctl(fd, UI_SET_RELBIT, REL_Y);
  ioctl(fd, UI_SET_RELBIT, REL_WHEEL);
  ioctl(fd, UI_SET_RELBIT, REL_HWHEEL);
  ioctl(fd, UI_SET_KEYBIT, BTN_LEFT);
  ioctl(fd, UI_SET_KEYBIT, BTN_RIGHT);
  ioctl(fd, UI_SET_KEYBIT, BTN_MIDDLE);
#ifdef INPUT_PROP_POINTER
  ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_POINTER);
#endif

  struct uinput_setup usetup;
  memset(&usetup, 0, sizeof(usetup));
  usetup.id.bustype = BUS_USB;
  usetup.id.vendor = 0x054c; // Sony Corporation
  usetup.id.product = desktop_mode ? 0x1001 : 0x1000;
  snprintf(usetup.name, sizeof(usetup.name), "%s",
           desktop_mode ? "Vita Desktop Virtual Controller"
                        : "Vita Virtual Keyboard");

  if (ioctl(fd, UI_DEV_SETUP, &usetup) < 0) {
    perror("[InputMapper] UI_DEV_SETUP failed");
    close(fd);
    return -1;
  }

  if (ioctl(fd, UI_DEV_CREATE) < 0) {
    perror("[InputMapper] UI_DEV_CREATE failed");
    close(fd);
    return -1;
  }

  printf("[InputMapper] Virtual device created (%s)\n", usetup.name);
  return fd;
}

static void emit_event(int uinput_fd, int type, int code, int value) {
  struct input_event ev;
  memset(&ev, 0, sizeof(ev));
  ev.type = type;
  ev.code = code;
  ev.value = value;
  if (write(uinput_fd, &ev, sizeof(ev)) < 0) {
    // Ignored
  }
}

static void emit_syn(int uinput_fd) {
  emit_event(uinput_fd, EV_SYN, SYN_REPORT, 0);
}

static void emit_key(int uinput_fd, int keycode, int value) {
  emit_event(uinput_fd, EV_KEY, keycode, value);
  emit_syn(uinput_fd);
}

static void drain_pending_events(int fd) {
  int fl = fcntl(fd, F_GETFL, 0);
  if (fl < 0)
    return;

  fcntl(fd, F_SETFL, fl | O_NONBLOCK);
  struct input_event dummy;
  int count = 0;
  while (read(fd, &dummy, sizeof(dummy)) > 0) {
    count++;
  }
  fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
  if (count > 0) {
    printf("[InputMapper] Drained and discarded %d stale event(s).\n", count);
  }
}

static void show_help(const char *progname) {
  printf("Usage: %s [OPTIONS]\n", progname);
  printf("\n");
  printf("PlayStation Vita Gamepad, Buttons, and Analog Stick Input Mapper.\n");
  printf("\n");
  printf("Options:\n");
  printf("  -d, --desktop, --mouse   Start in Desktop Mouse Emulation mode\n");
  printf("  -t, --terminal           Start in Terminal Keystroke mode "
         "(default)\n");
  printf("  -h, --help               Show this help message\n");
  printf("\n");
  printf("Signals:\n");
  printf("  SIGUSR1                  Switch to Desktop mode\n");
  printf("  SIGUSR2                  Switch to Terminal mode\n");
  printf("  SIGCONT                  Discard stale buffered evdev events\n");
  exit(0);
}

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--desktop") == 0 ||
        strcmp(argv[i], "--mouse") == 0) {
      g_mode_desktop = 1;
    } else if (strcmp(argv[i], "-t") == 0 ||
               strcmp(argv[i], "--terminal") == 0) {
      g_mode_desktop = 0;
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      show_help(argv[0]);
    }
  }

  printf("[InputMapper] Starting Vita Input Mapper (Initial Mode: %s)...\n",
         g_mode_desktop ? "Desktop Mouse + Keystrokes" : "Terminal Keystrokes");

  int buttons_fd = -1;
  while (buttons_fd < 0 && g_running) {
    buttons_fd = find_buttons_device();
    if (buttons_fd < 0) {
      sleep(1);
    }
  }

  int uinput_fd = -1;
  while (uinput_fd < 0 && g_running) {
    uinput_fd = setup_uinput_device(g_mode_desktop);
    if (uinput_fd < 0) {
      fprintf(stderr,
              "[InputMapper] /dev/uinput not ready yet, retrying in 1s...\n");
      sleep(1);
    }
  }

  if (!g_running) {
    if (uinput_fd >= 0)
      close(uinput_fd);
    if (buttons_fd >= 0)
      close(buttons_fd);
    return 0;
  }

  // Set up signal handlers
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = handle_sigcont;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGCONT, &sa, NULL);

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = handle_sigterm;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = handle_sigusr1;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = handle_sigusr2;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR2, &sa, NULL);

  drain_pending_events(buttons_fd);

  // Make buttons_fd non-blocking so we can use poll()
  int flags = fcntl(buttons_fd, F_GETFL, 0);
  fcntl(buttons_fd, F_SETFL, flags | O_NONBLOCK);

  int stick_lx = STICK_CENTER;
  int stick_ly = STICK_CENTER;
  int stick_rx = STICK_CENTER;
  int stick_ry = STICK_CENTER;
  int scroll_accum = 0;
  int hscroll_accum = 0;
  double subpixel_accum_x = 0.0;
  double subpixel_accum_y = 0.0;

  struct pollfd pfd;
  pfd.fd = buttons_fd;
  pfd.events = POLLIN;

  struct input_event ev;

  while (g_running) {
    if (g_resumed) {
      g_resumed = 0;
      drain_pending_events(buttons_fd);
      stick_lx = STICK_CENTER;
      stick_ly = STICK_CENTER;
      stick_rx = STICK_CENTER;
      stick_ry = STICK_CENTER;
      scroll_accum = 0;
      hscroll_accum = 0;
      subpixel_accum_x = 0.0;
      subpixel_accum_y = 0.0;
      continue;
    }

    int poll_ret = poll(&pfd, 1, TICK_MS);
    if (poll_ret < 0) {
      if (errno == EINTR) {
        continue;
      }
      perror("[InputMapper] poll failed");
      break;
    }

    // Process incoming hardware events if available
    if (poll_ret > 0 && (pfd.revents & POLLIN)) {
      while (read(buttons_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_ABS) {
          static int s_abs_count = 0;
          if (s_abs_count < 10) {
            printf("[InputMapper] Hardware analog event: code=%d value=%d\n",
                   ev.code, ev.value);
            s_abs_count++;
          }
          if (ev.code == ABS_X) {
            stick_lx = ev.value;
          } else if (ev.code == ABS_Y) {
            stick_ly = ev.value;
          } else if (ev.code == ABS_RX) {
            stick_rx = ev.value;
          } else if (ev.code == ABS_RY) {
            stick_ry = ev.value;
          }
        } else if (ev.type == EV_KEY) {
          int press = ev.value; // 1 = press, 0 = release, 2 = repeat

          if (g_mode_desktop) {
            // Desktop Mode Button Mapping
            switch (ev.code) {
            case BTN_A:  // Cross (✕) -> Left Click
            case BTN_TR: // R Trigger -> Ergonomic Left Click
              emit_event(uinput_fd, EV_KEY, BTN_LEFT, press);
              emit_syn(uinput_fd);
              break;
            case BTN_B:  // Circle (○) -> Right Click
            case BTN_TL: // L Trigger -> Ergonomic Right Click
              emit_event(uinput_fd, EV_KEY, BTN_RIGHT, press);
              emit_syn(uinput_fd);
              break;
            case BTN_X: // Square (□) -> Middle Click
              emit_event(uinput_fd, EV_KEY, BTN_MIDDLE, press);
              emit_syn(uinput_fd);
              break;
            case BTN_Y: // Triangle (△) -> Enter
              emit_key(uinput_fd, KEY_ENTER, press);
              break;
            case BTN_START: // Start -> Super / Windows Key (IceWM Application
                            // Menu)
              emit_key(uinput_fd, KEY_LEFTMETA, press);
              break;
            case BTN_SELECT: // Select -> Escape
              emit_key(uinput_fd, KEY_ESC, press);
              break;
            case BTN_DPAD_UP:
              emit_key(uinput_fd, KEY_UP, press);
              break;
            case BTN_DPAD_DOWN:
              emit_key(uinput_fd, KEY_DOWN, press);
              break;
            case BTN_DPAD_LEFT:
              emit_key(uinput_fd, KEY_LEFT, press);
              break;
            case BTN_DPAD_RIGHT:
              emit_key(uinput_fd, KEY_RIGHT, press);
              break;
            default:
              break;
            }
          } else {
            // Standard Terminal Keystroke Mapping
            switch (ev.code) {
            case BTN_DPAD_UP:
              emit_key(uinput_fd, KEY_UP, press);
              break;
            case BTN_DPAD_DOWN:
              emit_key(uinput_fd, KEY_DOWN, press);
              break;
            case BTN_DPAD_LEFT:
              emit_key(uinput_fd, KEY_LEFT, press);
              break;
            case BTN_DPAD_RIGHT:
              emit_key(uinput_fd, KEY_RIGHT, press);
              break;
            case BTN_A: // Cross (✕) -> Enter
              emit_key(uinput_fd, KEY_ENTER, press);
              break;
            case BTN_B: // Circle (○) -> Backspace
              emit_key(uinput_fd, KEY_BACKSPACE, press);
              break;
            case BTN_X: // Square (□) -> Space
              emit_key(uinput_fd, KEY_SPACE, press);
              break;
            case BTN_Y: // Triangle (△) -> Tab (auto-complete)
              emit_key(uinput_fd, KEY_TAB, press);
              break;
            case BTN_TL: // L Trigger -> Ctrl+C
              if (press == 1) {
                emit_key(uinput_fd, KEY_LEFTCTRL, 1);
                emit_key(uinput_fd, KEY_C, 1);
                emit_key(uinput_fd, KEY_C, 0);
                emit_key(uinput_fd, KEY_LEFTCTRL, 0);
              }
              break;
            case BTN_TR: // R Trigger -> PageUp
              emit_key(uinput_fd, KEY_PAGEUP, press);
              break;
            case BTN_START:
              emit_key(uinput_fd, KEY_ENTER, press);
              break;
            default:
              break;
            }
          }
        }
      }
    }

    // In Desktop Mode: Apply continuous Analog Stick Mouse & Scroll Movement
    if (g_mode_desktop) {
      int dx = stick_lx - STICK_CENTER;
      int dy = stick_ly - STICK_CENTER;
      int move_x = 0;
      int move_y = 0;

      if (abs(dx) > STICK_DEADZONE) {
        int sign = (dx > 0) ? 1 : -1;
        double eff = (double)(abs(dx) - STICK_DEADZONE);
        double norm = eff / (127.0 - STICK_DEADZONE);
        if (norm > 1.0) {
          norm = 1.0;
        }
        // Continuous dual linear + quadratic curve:
        // Slow fine control at low tilt (0.4-10 px/sec), ramping to ~340 px/sec at full tilt
        double speed_px_sec = 340.0 * (0.12 * norm + 0.88 * norm * norm);
        double delta_px = (speed_px_sec / (1000.0 / TICK_MS)) * sign;
        subpixel_accum_x += delta_px;
      } else {
        subpixel_accum_x = 0.0;
      }

      if (abs(dy) > STICK_DEADZONE) {
        int sign = (dy > 0) ? 1 : -1;
        double eff = (double)(abs(dy) - STICK_DEADZONE);
        double norm = eff / (127.0 - STICK_DEADZONE);
        if (norm > 1.0) {
          norm = 1.0;
        }
        double speed_px_sec = 340.0 * (0.12 * norm + 0.88 * norm * norm);
        double delta_px = (speed_px_sec / (1000.0 / TICK_MS)) * sign;
        subpixel_accum_y += delta_px;
      } else {
        subpixel_accum_y = 0.0;
      }

      if (subpixel_accum_x >= 1.0) {
        move_x = (int)subpixel_accum_x;
        subpixel_accum_x -= move_x;
      } else if (subpixel_accum_x <= -1.0) {
        move_x = (int)subpixel_accum_x;
        subpixel_accum_x -= move_x;
      }

      if (subpixel_accum_y >= 1.0) {
        move_y = (int)subpixel_accum_y;
        subpixel_accum_y -= move_y;
      } else if (subpixel_accum_y <= -1.0) {
        move_y = (int)subpixel_accum_y;
        subpixel_accum_y -= move_y;
      }

      if (move_x != 0 || move_y != 0) {
        static int s_move_count = 0;
        if (s_move_count < 10) {
          printf("[InputMapper] Emitting mouse move: dx=%d dy=%d -> move=(%d, "
                 "%d)\n",
                 dx, dy, move_x, move_y);
          s_move_count++;
        }
        emit_event(uinput_fd, EV_REL, REL_X, move_x);
        emit_event(uinput_fd, EV_REL, REL_Y, move_y);
        emit_syn(uinput_fd);
      }

      // Right stick vertical scrolling with proportional tilt speed
      int dry = stick_ry - STICK_CENTER;
      if (abs(dry) > SCROLL_DEADZONE) {
        int r_mag = abs(dry) - SCROLL_DEADZONE;
        int sign = (dry > 0) ? -1 : 1;
        scroll_accum += sign * r_mag;
        if (abs(scroll_accum) >= 300) {
          emit_event(uinput_fd, EV_REL, REL_WHEEL, (scroll_accum > 0 ? 1 : -1));
          emit_syn(uinput_fd);
          scroll_accum = 0;
        }
      } else {
        scroll_accum = 0;
      }

      // Right stick horizontal scrolling with proportional tilt speed
      int drx = stick_rx - STICK_CENTER;
      if (abs(drx) > SCROLL_DEADZONE) {
        int r_mag = abs(drx) - SCROLL_DEADZONE;
        int sign = (drx > 0) ? 1 : -1;
        hscroll_accum += sign * r_mag;
        if (abs(hscroll_accum) >= 300) {
          emit_event(uinput_fd, EV_REL, REL_HWHEEL,
                     (hscroll_accum > 0 ? 1 : -1));
          emit_syn(uinput_fd);
          hscroll_accum = 0;
        }
      } else {
        hscroll_accum = 0;
      }
    }
  }

  printf("[InputMapper] Shutting down...\n");
  if (uinput_fd >= 0) {
    ioctl(uinput_fd, UI_DEV_DESTROY);
    close(uinput_fd);
  }
  if (buttons_fd >= 0) {
    close(buttons_fd);
  }
  return 0;
}
