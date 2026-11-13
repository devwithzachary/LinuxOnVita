/*
 * vita-reboot-gui.c - Native C + X11 Reboot Confirmation Dialog for LinuxOnVita
 *
 * Returns exit code 0 when REBOOT NOW is confirmed, 1 when cancelled or closed.
 */

#include "x11_mini.h"

#include <stdio.h>

#define WIN_W 440
#define WIN_H 200

static void render(X11Window *w) {
    x11_clear(w, COL_BG);

    /* Header bar */
    x11_fill_rect(w, 0, 0, WIN_W, 44, COL_HEADER);
    x11_fill_rect(w, 0, 44, WIN_W, 2, COL_RED);
    x11_draw_text(w, 16, 14, "REBOOT TO VITAOS", COL_RED, 2);

    /* Message box */
    x11_fill_round_rect(w, 16, 58, WIN_W - 32, 74, 6, COL_CARD);
    x11_draw_rect(w, 16, 58, WIN_W - 32, 74, COL_BORDER);
    x11_draw_text(w, 28, 72, "Reboot to VitaOS?", COL_TEXT, 2);
    x11_draw_text(w, 28, 102, "Return to PlayStation Vita LiveArea.", COL_CYAN, 1);

    /* Action buttons */
    x11_draw_button(w, 16, WIN_H - 50, 140, 34, "CANCEL (ESC)", COL_CARD, COL_BORDER, COL_TEXT, 1);
    x11_draw_button(w, WIN_W - 176, WIN_H - 50, 160, 34, "REBOOT NOW", COL_RED, COL_RED, COL_TEXT, 1);

    x11_present(w);
}

int main(void) {
    X11Window *w;
    X11Event ev;
    int exit_code = 1;
    int running = 1;

    w = x11_open(WIN_W, WIN_H, "Reboot to VitaOS");
    if (!w) {
        fprintf(stderr, "vita-reboot-gui: failed to open X11 window\n");
        return 1;
    }

    render(w);

    while (running) {
        int got = x11_poll_event(w, &ev, 250);
        if (got < 0) break;
        if (got == 0) continue;

        if (ev.type == X11_EV_CLOSE) {
            exit_code = 1;
            running = 0;
        } else if (ev.type == X11_EV_EXPOSE) {
            render(w);
        } else if (ev.type == X11_EV_KEY) {
            if (ev.keysym == 0xff1b) { /* Escape */
                exit_code = 1;
                running = 0;
            } else if (ev.keysym == 0xff0d) { /* Enter */
                exit_code = 0;
                running = 0;
            }
        } else if (ev.type == X11_EV_MOUSE_DOWN && ev.button == 1) {
            if (x11_pt_in_rect(ev.x, ev.y, 16, WIN_H - 50, 140, 34)) {
                exit_code = 1;
                running = 0;
            } else if (x11_pt_in_rect(ev.x, ev.y, WIN_W - 176, WIN_H - 50, 160, 34)) {
                exit_code = 0;
                running = 0;
            }
        }
    }

    x11_close(w);
    return exit_code;
}
