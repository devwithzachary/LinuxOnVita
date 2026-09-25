/*
 * vita-input-mapper.c
 * Translates PS Vita physical button events (vita-buttons evdev)
 * into keyboard keystrokes via Linux /dev/uinput.
 *
 * Button Mappings:
 *   - D-Pad Up / Down   : Arrow Up / Arrow Down (history)
 *   - D-Pad Left / Right : Arrow Left / Arrow Right (cursor)
 *   - Cross (✕ / BTN_A) : Enter
 *   - Circle (○ / BTN_B): Backspace
 *   - Square (□ / BTN_X): Space
 *   - Triangle (△ / BTN_Y): Tab (auto-complete)
 *   - L Trigger (BTN_TL): Ctrl+C (Interrupt)
 *   - R Trigger (BTN_TR): Page Up
 *   - Start (BTN_START) : Enter
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <signal.h>
#include <linux/input.h>
#include <linux/uinput.h>

#define VITA_BUTTONS_DEVNAME "vita-buttons"

static int find_buttons_device(void) {
    char path[256];
    char name[128];
    for (int i = 0; i < 32; i++) {
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        memset(name, 0, sizeof(name));
        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0) {
            if (strstr(name, "PlayStation Vita Buttons") ||
                strstr(name, "Buttons") ||
                strstr(name, "vita-buttons") ||
                strstr(name, "vita_buttons")) {
                printf("[InputMapper] Found buttons device: %s (%s)\n", path, name);
                return fd;
            }
        }
        close(fd);
    }
    return -1;
}

static int setup_uinput_keyboard(void) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("[InputMapper] Failed to open /dev/uinput");
        return -1;
    }

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);

    // Keyboard keys we will synthesize
    int keys[] = {
        KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
        KEY_ENTER, KEY_BACKSPACE, KEY_SPACE, KEY_TAB,
        KEY_LEFTCTRL, KEY_C, KEY_PAGEUP, KEY_PAGEDOWN,
        KEY_ESC
    };

    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
        ioctl(fd, UI_SET_KEYBIT, keys[i]);
    }

    struct uinput_setup usetup;
    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x054c;  // Sony
    usetup.id.product = 0x1000;
    strcpy(usetup.name, "Vita Virtual Keyboard");

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

    printf("[InputMapper] Virtual keyboard created via /dev/uinput\n");
    return fd;
}

static void emit_key(int uinput_fd, int keycode, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_KEY;
    ev.code = keycode;
    ev.value = value;
    if (write(uinput_fd, &ev, sizeof(ev)) < 0) {
        // Ignored
    }

    memset(&ev, 0, sizeof(ev));
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    if (write(uinput_fd, &ev, sizeof(ev)) < 0) {
        // Ignored
    }
}

static volatile sig_atomic_t g_resumed = 0;

static void handle_sigcont(int sig) {
    (void)sig;
    g_resumed = 1;
}

static void drain_pending_events(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return;

    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    struct input_event dummy;
    int count = 0;
    while (read(fd, &dummy, sizeof(dummy)) > 0) {
        count++;
    }
    fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
    if (count > 0) {
        printf("[InputMapper] Resumed: drained and discarded %d stale event(s).\n", count);
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    printf("[InputMapper] Starting Vita Gamepad-to-Keystroke Mapper...\n");

    int buttons_fd = -1;
    while (buttons_fd < 0) {
        buttons_fd = find_buttons_device();
        if (buttons_fd < 0) {
            sleep(1);
        }
    }

    int uinput_fd = -1;
    while (uinput_fd < 0) {
        uinput_fd = setup_uinput_keyboard();
        if (uinput_fd < 0) {
            fprintf(stderr, "[InputMapper] /dev/uinput not ready yet, retrying in 1s...\n");
            sleep(1);
        }
    }

    // Register SIGCONT handler to discard stale events queued while suspended
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigcont;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // Do not restart syscalls so blocking read unblocks with EINTR
    sigaction(SIGCONT, &sa, NULL);

    // Initial drain of any button events queued before mapper started
    drain_pending_events(buttons_fd);

    // Set non-blocking to blocking for event reading
    int flags = fcntl(buttons_fd, F_GETFL, 0);
    fcntl(buttons_fd, F_SETFL, flags & ~O_NONBLOCK);

    struct input_event ev;
    while (1) {
        if (g_resumed) {
            g_resumed = 0;
            drain_pending_events(buttons_fd);
            continue;
        }

        ssize_t n = read(buttons_fd, &ev, sizeof(ev));
        if (n < 0) {
            if (errno == EINTR) {
                if (g_resumed) {
                    g_resumed = 0;
                    drain_pending_events(buttons_fd);
                }
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            perror("[InputMapper] read failed");
            break;
        }

        if (g_resumed) {
            g_resumed = 0;
            drain_pending_events(buttons_fd);
            continue;
        }

        if (n < (ssize_t)sizeof(ev)) {
            continue;
        }

        if (ev.type != EV_KEY) continue;

        int press = ev.value; // 1 = pressed, 0 = released, 2 = repeat
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
            case BTN_A: // Cross (✕)
                emit_key(uinput_fd, KEY_ENTER, press);
                break;
            case BTN_B: // Circle (○)
                emit_key(uinput_fd, KEY_BACKSPACE, press);
                break;
            case BTN_X: // Square (□)
                emit_key(uinput_fd, KEY_SPACE, press);
                break;
            case BTN_Y: // Triangle (△)
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

    close(uinput_fd);
    close(buttons_fd);
    return 0;
}
