#ifndef X11_MINI_H
#define X11_MINI_H

#include <stdint.h>
#include <stddef.h>

/* PlayStation Slate Color Palette (0x00RRGGBB) */
#define COL_BG          0x0b0e14
#define COL_HEADER      0x111722
#define COL_CARD        0x161d2b
#define COL_CARD_HOVER  0x1d2638
#define COL_CARD_SEL    0x182b44
#define COL_BORDER      0x273347
#define COL_CYAN        0x00d2ff
#define COL_BLUE        0x0070d1
#define COL_BLUE_HOVER  0x0a84ef
#define COL_GREEN       0x00c853
#define COL_AMBER       0xffab00
#define COL_RED         0xe53935
#define COL_TEXT        0xf0f4f8
#define COL_TEXT_DIM    0x8b9bb4
#define COL_TEXT_DARK   0x0b0e14

typedef enum {
    X11_EV_NONE = 0,
    X11_EV_EXPOSE,
    X11_EV_MOUSE_MOVE,
    X11_EV_MOUSE_DOWN,
    X11_EV_MOUSE_UP,
    X11_EV_KEY,
    X11_EV_CLOSE
} X11EventType;

typedef struct {
    X11EventType type;
    int x;
    int y;
    int button;     /* 1 = Left, 3 = Right, 4 = WheelUp, 5 = WheelDown */
    char ch;        /* Printable ASCII if applicable, else 0 */
    uint32_t keysym;
} X11Event;

typedef struct {
    int fd;
    uint32_t rid_base;
    uint32_t root_win;
    uint32_t root_visual;
    uint8_t root_depth;
    uint8_t bpp;        /* 32 or 16 */
    uint16_t screen_w;
    uint16_t screen_h;
    uint32_t max_req_bytes;

    uint32_t win;
    uint32_t gc;
    uint32_t atom_wm_protocols;
    uint32_t atom_wm_delete;

    int width;
    int height;
    uint32_t *fb;       /* 0x00RRGGBB backbuffer */
    uint8_t *xmit_buf;  /* Converted pixel strip buffer */

    uint8_t min_keycode;
    uint8_t max_keycode;
    uint8_t keysyms_per_kc;
    uint32_t *keymap;
} X11Window;

/* Window lifecycle */
X11Window *x11_open(int width, int height, const char *title);
void x11_close(X11Window *w);
void x11_present(X11Window *w);

/* Event polling: timeout_ms = -1 blocks, 0 returns immediately */
int x11_poll_event(X11Window *w, X11Event *ev, int timeout_ms);

/* 2D Drawing Primitives */
void x11_clear(X11Window *w, uint32_t color);
void x11_fill_rect(X11Window *w, int x, int y, int rw, int rh, uint32_t color);
void x11_draw_rect(X11Window *w, int x, int y, int rw, int rh, uint32_t color);
void x11_fill_round_rect(X11Window *w, int x, int y, int rw, int rh, int r, uint32_t color);
void x11_draw_round_rect(X11Window *w, int x, int y, int rw, int rh, int r, uint32_t color);

/* Text rendering (built-in 8x16 crisp font, scale >= 1) */
void x11_draw_text(X11Window *w, int x, int y, const char *str, uint32_t color, int scale);
int x11_text_width(const char *str, int scale);

/* Helper UI components */
int x11_pt_in_rect(int px, int py, int rx, int ry, int rw, int rh);
void x11_draw_button(X11Window *w, int x, int y, int bw, int bh, const char *label,
                     uint32_t bg, uint32_t border, uint32_t fg, int scale);

#endif /* X11_MINI_H */

