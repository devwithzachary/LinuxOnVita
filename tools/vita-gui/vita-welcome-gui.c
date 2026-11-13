/*
 * vita-welcome-gui.c - Native C + X11 Desktop Welcome Guide for LinuxOnVita
 *
 * Pure C / zero-dependency static X11 binary. Displays desktop quick-start
 * tips, persists SHOW_WELCOME in /root/.config/linuxonvita/welcome.conf,
 * and can launch sakura terminal directly.
 */

#include "x11_mini.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define WIN_W 560
#define WIN_H 390

#define CONF_DIR  "/root/.config/linuxonvita"
#define CONF_PATH "/root/.config/linuxonvita/welcome.conf"

static int read_show_welcome(void) {
    FILE *f = fopen(CONF_PATH, "r");
    char line[128];
    int val = 1;
    if (!f) return 1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "SHOW_WELCOME=false", 18) == 0) {
            val = 0;
            break;
        }
    }
    fclose(f);
    return val;
}

static void write_show_welcome(int show) {
    FILE *f;
    mkdir("/root/.config", 0755);
    mkdir(CONF_DIR, 0755);
    f = fopen(CONF_PATH, "w");
    if (!f) return;
    fprintf(f, "SHOW_WELCOME=%s\n", show ? "true" : "false");
    fclose(f);
}

static void launch_terminal(void) {
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        execlp("sakura", "sakura", (char *)NULL);
        execlp("xterm", "xterm", (char *)NULL);
        _exit(127);
    }
}

static void render(X11Window *w, int show_welcome) {
    x11_clear(w, COL_BG);

    /* Header bar */
    x11_fill_rect(w, 0, 0, WIN_W, 46, COL_HEADER);
    x11_fill_rect(w, 0, 46, WIN_W, 2, COL_CYAN);
    x11_draw_text(w, 16, 15, "WELCOME TO LINUXONVITA", COL_CYAN, 2);

    /* Intro card */
    x11_fill_round_rect(w, 16, 58, WIN_W - 32, 64, 6, COL_CARD);
    x11_draw_rect(w, 16, 58, WIN_W - 32, 64, COL_BORDER);
    x11_draw_text(w, 28, 68, "Alpine Linux + XFCE4 Desktop", COL_TEXT, 2);
    x11_draw_text(w, 28, 92, "Linux 6.12 LTS  |  PS Vita Hardware", COL_TEXT_DIM, 1);
    x11_draw_text(w, 28, 105, "Native C/X11 system tools ready in panel & CLI", COL_GREEN, 1);

    /* Controls & shortcuts card */
    x11_fill_round_rect(w, 16, 132, WIN_W - 32, 128, 6, COL_CARD);
    x11_draw_rect(w, 16, 132, WIN_W - 32, 128, COL_BORDER);
    x11_draw_text(w, 28, 142, "CONTROLS & DESKTOP SHORTCUTS", COL_CYAN, 1);
    x11_draw_text(w, 28, 160, "Left Stick / Touch : Move mouse cursor", COL_TEXT, 1);
    x11_draw_text(w, 28, 176, "Cross / Tap        : Left click", COL_TEXT, 1);
    x11_draw_text(w, 28, 192, "Circle / 2-Finger  : Right click", COL_TEXT, 1);
    x11_draw_text(w, 28, 208, "D-Pad / R-Stick    : Arrow keys / Scroll", COL_TEXT, 1);
    x11_draw_text(w, 28, 224, "L / R Triggers     : Page Up / Page Down", COL_TEXT, 1);
    x11_draw_text(w, 28, 240, "SELECT / START     : Toggle virtual keyboard / Menu", COL_TEXT, 1);

    /* Useful commands card */
    x11_fill_round_rect(w, 16, 270, WIN_W - 32, 52, 6, COL_CARD);
    x11_draw_rect(w, 16, 270, WIN_W - 32, 52, COL_BORDER);
    x11_draw_text(w, 28, 280, "CLI TOOLS:  vita-wifi  |  vita-cpu  |  vita-help", COL_AMBER, 1);
    x11_draw_text(w, 28, 298, "REBOOT:     Click Reboot icon or run vita-reboot", COL_TEXT_DIM, 1);

    /* Footer: checkbox + buttons */
    x11_fill_rect(w, 0, WIN_H - 52, WIN_W, 52, COL_HEADER);
    x11_fill_rect(w, 0, WIN_H - 52, WIN_W, 1, COL_BORDER);

    /* Checkbox */
    x11_fill_round_rect(w, 16, WIN_H - 35, 18, 18, 3, show_welcome ? COL_BLUE : COL_CARD);
    x11_draw_rect(w, 16, WIN_H - 35, 18, 18, show_welcome ? COL_CYAN : COL_BORDER);
    if (show_welcome) {
        x11_draw_text(w, 21, WIN_H - 30, "X", COL_TEXT, 1);
    }
    x11_draw_text(w, 42, WIN_H - 30, "Show on desktop startup", COL_TEXT, 1);

    /* Buttons */
    x11_draw_button(w, WIN_W - 246, WIN_H - 42, 130, 32, "OPEN TERMINAL", COL_BLUE, COL_CYAN, COL_TEXT, 1);
    x11_draw_button(w, WIN_W - 106, WIN_H - 42, 90, 32, "CLOSE", COL_CARD, COL_BORDER, COL_TEXT, 1);

    x11_present(w);
}

int main(void) {
    X11Window *w;
    X11Event ev;
    int show_welcome;
    int running = 1;

    show_welcome = read_show_welcome();

    w = x11_open(WIN_W, WIN_H, "Welcome to LinuxOnVita");
    if (!w) {
        fprintf(stderr, "vita-welcome-gui: failed to open X11 window\n");
        return 1;
    }

    render(w, show_welcome);

    while (running) {
        int got = x11_poll_event(w, &ev, 250);
        if (got < 0) break;
        if (got == 0) continue;

        if (ev.type == X11_EV_CLOSE) {
            running = 0;
        } else if (ev.type == X11_EV_EXPOSE) {
            render(w, show_welcome);
        } else if (ev.type == X11_EV_KEY) {
            if (ev.keysym == 0xff1b || ev.keysym == 0xff0d) { /* Escape / Enter */
                running = 0;
            }
        } else if (ev.type == X11_EV_MOUSE_DOWN && ev.button == 1) {
            if (x11_pt_in_rect(ev.x, ev.y, 14, WIN_H - 40, 220, 28)) {
                show_welcome = !show_welcome;
                write_show_welcome(show_welcome);
                render(w, show_welcome);
            } else if (x11_pt_in_rect(ev.x, ev.y, WIN_W - 246, WIN_H - 42, 130, 32)) {
                launch_terminal();
            } else if (x11_pt_in_rect(ev.x, ev.y, WIN_W - 106, WIN_H - 42, 90, 32)) {
                running = 0;
            }
        }
    }

    x11_close(w);
    return 0;
}
