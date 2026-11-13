/*
 * vita-cpu-gui.c - Native C + X11 CPU Power Profiles Manager for LinuxOnVita
 *
 * Pure C / zero-dependency static X11 binary. Reads cpufreq sysfs directly
 * and invokes /usr/bin/vita-cpu <mode> when a profile card is clicked.
 */

#include "x11_mini.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WIN_W 460
#define WIN_H 360

typedef struct {
    const char *id;
    const char *title;
    const char *desc;
    const char *tag;
    uint32_t    accent;
} CpuProfile;

static const CpuProfile PROFILES[4] = {
    { "max",       "444 MHz Max",      "Fixed maximum clock (performance)",   "MAX",  COL_RED   },
    { "balanced",  "333 MHz Balanced", "Fixed 333 MHz clock (userspace)",     "BAL",  COL_CYAN  },
    { "ondemand",  "On-Demand",        "Dynamic scaling 111 - 444 MHz",       "AUTO", COL_GREEN },
    { "powersave", "111 MHz Powersave","Minimum clock for battery life",      "LOW",  COL_AMBER }
};

typedef struct {
    int      cur_mhz;
    char     gov[32];
    int      active_idx;
    char     status_msg[80];
    uint32_t status_col;
} CpuState;

static void read_sysfs_line(const char *path, char *buf, size_t len) {
    FILE *f = fopen(path, "r");
    buf[0] = '\0';
    if (!f) return;
    if (fgets(buf, (int)len, f)) {
        size_t n = strlen(buf);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
            buf[--n] = '\0';
        }
    }
    fclose(f);
}

static void refresh_cpu(CpuState *st) {
    char buf[64];
    long khz = 0;

    read_sysfs_line("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", buf, sizeof(buf));
    if (buf[0]) {
        khz = strtol(buf, NULL, 10);
        st->cur_mhz = (int)(khz / 1000L);
    } else {
        st->cur_mhz = 0;
    }

    read_sysfs_line("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", st->gov, sizeof(st->gov));
    if (!st->gov[0]) snprintf(st->gov, sizeof(st->gov), "unknown");

    if (strcmp(st->gov, "performance") == 0 || st->cur_mhz >= 400) {
        st->active_idx = (strcmp(st->gov, "ondemand") == 0) ? 2 : 0;
    } else if (strcmp(st->gov, "ondemand") == 0 || strcmp(st->gov, "schedutil") == 0) {
        st->active_idx = 2;
    } else if (strcmp(st->gov, "powersave") == 0 || st->cur_mhz <= 150) {
        st->active_idx = 3;
    } else {
        st->active_idx = 1;
    }
}

static void apply_profile(CpuState *st, int idx) {
    char cmd[128];
    int rc;
    if (idx < 0 || idx >= 4) return;
    snprintf(cmd, sizeof(cmd), "/usr/bin/vita-cpu %s >/dev/null 2>&1", PROFILES[idx].id);
    rc = system(cmd);
    refresh_cpu(st);
    if (rc == 0) {
        snprintf(st->status_msg, sizeof(st->status_msg), "Applied %s", PROFILES[idx].title);
        st->status_col = COL_GREEN;
    } else {
        snprintf(st->status_msg, sizeof(st->status_msg), "Failed to apply %s", PROFILES[idx].title);
        st->status_col = COL_RED;
    }
}

static void render(X11Window *w, const CpuState *st) {
    char line[96];
    int i;

    x11_clear(w, COL_BG);

    /* Header bar */
    x11_fill_rect(w, 0, 0, WIN_W, 44, COL_HEADER);
    x11_fill_rect(w, 0, 44, WIN_W, 2, COL_CYAN);
    x11_draw_text(w, 16, 14, "CPU POWER PROFILES", COL_CYAN, 2);

    /* Current clock + governor card */
    x11_fill_round_rect(w, 16, 56, WIN_W - 32, 42, 6, COL_CARD);
    x11_draw_rect(w, 16, 56, WIN_W - 32, 42, COL_BORDER);
    if (st->cur_mhz > 0) {
        snprintf(line, sizeof(line), "Current: %d MHz (%s)", st->cur_mhz, st->gov);
    } else {
        snprintf(line, sizeof(line), "Current: N/A (%s)", st->gov);
    }
    x11_draw_text(w, 28, 73, line, COL_TEXT, 1);

    /* 4 profile cards */
    for (i = 0; i < 4; i++) {
        int y = 110 + i * 50;
        int is_active = (st->active_idx == i);
        uint32_t bg = is_active ? COL_CARD_SEL : COL_CARD;
        uint32_t bd = is_active ? PROFILES[i].accent : COL_BORDER;

        x11_fill_round_rect(w, 16, y, WIN_W - 32, 42, 6, bg);
        x11_draw_rect(w, 16, y, WIN_W - 32, 42, bd);

        /* Left accent stripe */
        x11_fill_rect(w, 16, y + 4, 4, 34, PROFILES[i].accent);

        x11_draw_text(w, 30, y + 8, PROFILES[i].title, COL_TEXT, 2);
        x11_draw_text(w, 30, y + 27, PROFILES[i].desc, COL_TEXT_DIM, 1);

        /* Active badge on the right */
        if (is_active) {
            x11_fill_round_rect(w, WIN_W - 96, y + 10, 68, 22, 4, PROFILES[i].accent);
            x11_draw_text(w, WIN_W - 86, y + 17, "ACTIVE", COL_BG, 1);
        } else {
            x11_draw_text(w, WIN_W - 64, y + 17, PROFILES[i].tag, COL_TEXT_DIM, 1);
        }
    }

    /* Footer: status message + Close button */
    x11_fill_rect(w, 0, WIN_H - 46, WIN_W, 46, COL_HEADER);
    x11_fill_rect(w, 0, WIN_H - 46, WIN_W, 1, COL_BORDER);
    x11_draw_text(w, 16, WIN_H - 27, st->status_msg, st->status_col, 1);
    x11_draw_button(w, WIN_W - 106, WIN_H - 38, 90, 30, "CLOSE", COL_CARD, COL_BORDER, COL_TEXT, 1);

    x11_present(w);
}

int main(void) {
    X11Window *w;
    CpuState st;
    X11Event ev;
    int running = 1;

    memset(&st, 0, sizeof(st));
    snprintf(st.gov, sizeof(st.gov), "...");
    snprintf(st.status_msg, sizeof(st.status_msg), "Click a profile to apply (keys 1-4)");
    st.status_col = COL_TEXT_DIM;
    st.active_idx = -1;

    w = x11_open(WIN_W, WIN_H, "CPU Power Profiles");
    if (!w) {
        fprintf(stderr, "vita-cpu-gui: failed to open X11 window\n");
        return 1;
    }

    refresh_cpu(&st);
    render(w, &st);

    while (running) {
        int got = x11_poll_event(w, &ev, 500);
        if (got < 0) break;
        if (got == 0) {
            int prev_mhz = st.cur_mhz;
            refresh_cpu(&st);
            if (st.cur_mhz != prev_mhz) render(w, &st);
            continue;
        }

        if (ev.type == X11_EV_CLOSE) {
            running = 0;
        } else if (ev.type == X11_EV_EXPOSE) {
            render(w, &st);
        } else if (ev.type == X11_EV_KEY) {
            if (ev.keysym == 0xff1b) { /* Escape */
                running = 0;
            } else if (ev.ch >= '1' && ev.ch <= '4') {
                apply_profile(&st, ev.ch - '1');
                render(w, &st);
            }
        } else if (ev.type == X11_EV_MOUSE_DOWN && ev.button == 1) {
            int i;
            if (x11_pt_in_rect(ev.x, ev.y, WIN_W - 106, WIN_H - 38, 90, 30)) {
                running = 0;
            } else {
                for (i = 0; i < 4; i++) {
                    int y = 110 + i * 50;
                    if (x11_pt_in_rect(ev.x, ev.y, 16, y, WIN_W - 32, 42)) {
                        apply_profile(&st, i);
                        render(w, &st);
                        break;
                    }
                }
            }
        }
    }

    x11_close(w);
    return 0;
}
