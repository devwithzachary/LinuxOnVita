/*
 * fbkeyboard.c
 * PlayStation Vita Compact On-Screen Framebuffer Touch Keyboard
 * Dual-screen layout: ABC (Letters) & ?123 (Numbers / Symbols)
 * Renders directly to /dev/fb0 and injects keys via /dev/uinput (with /dev/tty1 VT isolation)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <linux/vt.h>
#include <termios.h>
#include <stdint.h>
#include <poll.h>
#include <time.h>
#include <signal.h>

#define FB_DEV "/dev/fb0"
#define KB_HEIGHT 150
#define NUM_ROWS 4

// Colors in 32-bit ARGB / RGBA
#define COLOR_BG        0xFF1E1E2E  // Modern dark purple/grey
#define COLOR_KEY_BG    0xFF313244  // Key surface
#define COLOR_KEY_PRESS 0xFF585B70  // Key pressed
#define COLOR_KEY_BORDER 0xFF45475A // Border
#define COLOR_TEXT      0xFFCDD6F4  // Soft white text
#define COLOR_SPECIAL   0xFF89B4FA  // Blue accent for Enter/Shift/123

typedef struct {
    const char *label;
    const char *shift_label;
    int keycode;
    int need_shift;
    int x, y, w, h;
    int is_special;
} Key;

static Key keys[64];
static int num_keys = 0;

static int shift_active = 0;
static int keyboard_visible = 1;
static int keyboard_page = 0; // 0 = ABC, 1 = ?123 Symbols

static int fb_fd = -1;
static int tty_fd = -1;
static uint32_t *fb_mem = NULL;
static long fb_size = 0;
static int screen_w = 960;
static int screen_h = 544;

// 8x16 Basic Console Font Bitmaps for ASCII 32 to 126
static void draw_char(int x, int y, char c, uint32_t color) {
    if (c < 32 || c > 126) return;
    static const unsigned char font[95][8] = {
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
        {0x18,0x3c,0x3c,0x18,0x18,0x00,0x18,0x00}, // !
        {0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00}, // "
        {0x6c,0x6c,0xfe,0x6c,0xfe,0x6c,0x6c,0x00}, // #
        {0x18,0x3e,0x60,0x3c,0x06,0x7c,0x18,0x00}, // $
        {0x00,0x63,0x66,0x0c,0x18,0x33,0x63,0x00}, // %
        {0x38,0x6c,0x38,0x76,0xdc,0xcc,0x76,0x00}, // &
        {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00}, // '
        {0x0c,0x18,0x30,0x30,0x30,0x18,0x0c,0x00}, // (
        {0x30,0x18,0x0c,0x0c,0x0c,0x18,0x30,0x00}, // )
        {0x00,0x66,0x3c,0xff,0x3c,0x66,0x00,0x00}, // *
        {0x00,0x18,0x18,0x7e,0x18,0x18,0x00,0x00}, // +
        {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, // ,
        {0x00,0x00,0x00,0x7e,0x00,0x00,0x00,0x00}, // -
        {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // .
        {0x06,0x0c,0x18,0x30,0x60,0xc0,0x80,0x00}, // /
        {0x3c,0x66,0xc3,0xc3,0xc3,0x66,0x3c,0x00}, // 0
        {0x18,0x38,0x18,0x18,0x18,0x18,0x7e,0x00}, // 1
        {0x3c,0x66,0x06,0x1c,0x30,0x66,0x7e,0x00}, // 2
        {0x3c,0x66,0x06,0x1c,0x06,0x66,0x3c,0x00}, // 3
        {0x0e,0x1e,0x36,0x66,0x7f,0x06,0x0f,0x00}, // 4
        {0x7e,0x60,0x7c,0x06,0x06,0x66,0x3c,0x00}, // 5
        {0x1c,0x30,0x60,0x7c,0x66,0x66,0x3c,0x00}, // 6
        {0x7e,0x66,0x0c,0x18,0x18,0x18,0x18,0x00}, // 7
        {0x3c,0x66,0x66,0x3c,0x66,0x66,0x3c,0x00}, // 8
        {0x3c,0x66,0x66,0x3e,0x06,0x0c,0x38,0x00}, // 9
        {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00}, // :
        {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30}, // ;
        {0x0c,0x18,0x30,0x60,0x30,0x18,0x0c,0x00}, // <
        {0x00,0x00,0x7e,0x00,0x7e,0x00,0x00,0x00}, // =
        {0x30,0x18,0x0c,0x06,0x0c,0x18,0x30,0x00}, // >
        {0x3c,0x66,0x06,0x0c,0x18,0x00,0x18,0x00}, // ?
        {0x3c,0x66,0x6e,0x6e,0x60,0x62,0x3c,0x00}, // @
        {0x18,0x3c,0x66,0x66,0x7e,0x66,0x66,0x00}, // A
        {0x7c,0x66,0x66,0x7c,0x66,0x66,0x7c,0x00}, // B
        {0x3c,0x66,0x60,0x60,0x60,0x66,0x3c,0x00}, // C
        {0x78,0x6c,0x66,0x66,0x66,0x6c,0x78,0x00}, // D
        {0x7e,0x60,0x60,0x7c,0x60,0x60,0x7e,0x00}, // E
        {0x7e,0x60,0x60,0x7c,0x60,0x60,0x60,0x00}, // F
        {0x3c,0x66,0x60,0x6e,0x66,0x66,0x3a,0x00}, // G
        {0x66,0x66,0x66,0x7e,0x66,0x66,0x66,0x00}, // H
        {0x3c,0x18,0x18,0x18,0x18,0x18,0x3c,0x00}, // I
        {0x1e,0x0c,0x0c,0x0c,0x0c,0x6c,0x38,0x00}, // J
        {0x66,0x6c,0x78,0x70,0x78,0x6c,0x66,0x00}, // K
        {0x60,0x60,0x60,0x60,0x60,0x60,0x7e,0x00}, // L
        {0x63,0x77,0x7f,0x6b,0x63,0x63,0x63,0x00}, // M
        {0x66,0x76,0x7e,0x7e,0x6e,0x66,0x66,0x00}, // N
        {0x3c,0x66,0x66,0x66,0x66,0x66,0x3c,0x00}, // O
        {0x7c,0x66,0x66,0x7c,0x60,0x60,0x60,0x00}, // P
        {0x3c,0x66,0x66,0x66,0x6a,0x6c,0x36,0x00}, // Q
        {0x7c,0x66,0x66,0x7c,0x6c,0x66,0x66,0x00}, // R
        {0x3c,0x66,0x60,0x3c,0x06,0x66,0x3c,0x00}, // S
        {0x7e,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, // T
        {0x66,0x66,0x66,0x66,0x66,0x66,0x3c,0x00}, // U
        {0x66,0x66,0x66,0x66,0x66,0x3c,0x18,0x00}, // V
        {0x63,0x63,0x63,0x6b,0x7f,0x77,0x63,0x00}, // W
        {0x66,0x66,0x3c,0x18,0x3c,0x66,0x66,0x00}, // X
        {0x66,0x66,0x66,0x3c,0x18,0x18,0x18,0x00}, // Y
        {0x7e,0x06,0x0c,0x18,0x30,0x60,0x7e,0x00}, // Z
        {0x3c,0x30,0x30,0x30,0x30,0x30,0x3c,0x00}, // [
        {0x60,0x30,0x18,0x0c,0x06,0x03,0x01,0x00}, // "\"
        {0x3c,0x0c,0x0c,0x0c,0x0c,0x0c,0x3c,0x00}, // ]
        {0x08,0x1c,0x36,0x63,0x00,0x00,0x00,0x00}, // ^
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff}, // _
        {0x18,0x18,0x0c,0x00,0x00,0x00,0x00,0x00}, // `
        {0x00,0x00,0x3c,0x06,0x3e,0x66,0x3b,0x00}, // a
        {0x60,0x60,0x7c,0x66,0x66,0x66,0x7c,0x00}, // b
        {0x00,0x00,0x3c,0x66,0x60,0x66,0x3c,0x00}, // c
        {0x06,0x06,0x3e,0x66,0x66,0x66,0x3e,0x00}, // d
        {0x00,0x00,0x3c,0x66,0x7e,0x60,0x3c,0x00}, // e
        {0x0e,0x18,0x7c,0x18,0x18,0x18,0x18,0x00}, // f
        {0x00,0x00,0x3b,0x66,0x66,0x3e,0x06,0x7c}, // g
        {0x60,0x60,0x7c,0x66,0x66,0x66,0x66,0x00}, // h
        {0x18,0x00,0x38,0x18,0x18,0x18,0x3c,0x00}, // i
        {0x06,0x00,0x0e,0x06,0x06,0x06,0x66,0x3c}, // j
        {0x60,0x60,0x66,0x6c,0x78,0x6c,0x66,0x00}, // k
        {0x38,0x18,0x18,0x18,0x18,0x18,0x3c,0x00}, // l
        {0x00,0x00,0x66,0x7f,0x7f,0x6b,0x63,0x00}, // m
        {0x00,0x00,0x7c,0x66,0x66,0x66,0x66,0x00}, // n
        {0x00,0x00,0x3c,0x66,0x66,0x66,0x3c,0x00}, // o
        {0x00,0x00,0x7c,0x66,0x66,0x7c,0x60,0x60}, // p
        {0x00,0x00,0x3e,0x66,0x66,0x3e,0x06,0x07}, // q
        {0x00,0x00,0x6c,0x76,0x60,0x60,0x60,0x00}, // r
        {0x00,0x00,0x3e,0x60,0x3c,0x06,0x7c,0x00}, // s
        {0x18,0x18,0x7e,0x18,0x18,0x18,0x0e,0x00}, // t
        {0x00,0x00,0x66,0x66,0x66,0x66,0x3b,0x00}, // u
        {0x00,0x00,0x66,0x66,0x66,0x3c,0x18,0x00}, // v
        {0x00,0x00,0x63,0x6b,0x7f,0x3e,0x36,0x00}, // w
        {0x00,0x00,0x66,0x3c,0x18,0x3c,0x66,0x00}, // x
        {0x00,0x00,0x66,0x66,0x66,0x3e,0x06,0x7c}, // y
        {0x00,0x00,0x7e,0x0c,0x18,0x30,0x7e,0x00}, // z
        {0x0e,0x18,0x18,0x70,0x18,0x18,0x0e,0x00}, // {
        {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // |
        {0x70,0x18,0x18,0x0e,0x18,0x18,0x70,0x00}, // }
        {0x00,0x32,0x4c,0x00,0x00,0x00,0x00,0x00}  // ~
    };

    const unsigned char *glyph = font[c - 32];
    for (int row = 0; row < 8; row++) {
        unsigned char bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if ((bits >> (7 - col)) & 1) {
                int px = x + col;
                int py1 = y + row * 2;
                int py2 = py1 + 1;
                if (px >= 0 && px < screen_w) {
                    if (py1 >= 0 && py1 < screen_h) fb_mem[py1 * screen_w + px] = color;
                    if (py2 >= 0 && py2 < screen_h) fb_mem[py2 * screen_w + px] = color;
                }
            }
        }
    }
}

static void draw_string(int x, int y, const char *s, uint32_t color) {
    while (*s) {
        draw_char(x, y, *s, color);
        x += 8;
        s++;
    }
}

static void draw_rect(int x, int y, int w, int h, uint32_t color) {
    for (int j = y; j < y + h; j++) {
        if (j < 0 || j >= screen_h) continue;
        for (int i = x; i < x + w; i++) {
            if (i >= 0 && i < screen_w) {
                fb_mem[j * screen_w + i] = color;
            }
        }
    }
}

static void draw_line_h(int x, int y, int w, uint32_t color) {
    if (y < 0 || y >= screen_h) return;
    for (int i = x; i < x + w; i++) {
        if (i >= 0 && i < screen_w) fb_mem[y * screen_w + i] = color;
    }
}

static void draw_key(const Key *k, int pressed) {
    uint32_t bg = pressed ? COLOR_KEY_PRESS : (k->is_special ? COLOR_SPECIAL : COLOR_KEY_BG);
    uint32_t text_color = (k->is_special && !pressed) ? 0xFF11111B : COLOR_TEXT;

    // Key body
    draw_rect(k->x + 1, k->y + 1, k->w - 2, k->h - 2, bg);

    // Border
    draw_line_h(k->x, k->y, k->w, COLOR_KEY_BORDER);
    draw_line_h(k->x, k->y + k->h - 1, k->w, COLOR_KEY_BORDER);
    for (int j = k->y; j < k->y + k->h; j++) {
        if (j >= 0 && j < screen_h) {
            if (k->x >= 0 && k->x < screen_w) fb_mem[j * screen_w + k->x] = COLOR_KEY_BORDER;
            if (k->x + k->w - 1 >= 0 && k->x + k->w - 1 < screen_w)
                fb_mem[j * screen_w + (k->x + k->w - 1)] = COLOR_KEY_BORDER;
        }
    }

    const char *label = (shift_active && k->shift_label) ? k->shift_label : k->label;
    int text_len = strlen(label);
    int tx = k->x + (k->w - text_len * 8) / 2;
    int ty = k->y + (k->h - 16) / 2;
    draw_string(tx, ty, label, text_color);
}

static void init_key_layout(void) {
    num_keys = 0;
    int kb_y = screen_h - KB_HEIGHT;
    int row_h = KB_HEIGHT / NUM_ROWS;

    if (keyboard_page == 0) {
        // ================= PAGE 0: ABC LETTERS =================
        // Row 0: QWERTY + Bksp (11 keys)
        struct { const char *l; const char *s; int k; int w_rel; } r0[] = {
            {"q", "Q", KEY_Q, 10}, {"w", "W", KEY_W, 10}, {"e", "E", KEY_E, 10},
            {"r", "R", KEY_R, 10}, {"t", "T", KEY_T, 10}, {"y", "Y", KEY_Y, 10},
            {"u", "U", KEY_U, 10}, {"i", "I", KEY_I, 10}, {"o", "O", KEY_O, 10},
            {"p", "P", KEY_P, 10}, {"Bksp", "Bksp", KEY_BACKSPACE, 16}
        };
        int x = 4;
        for (size_t i = 0; i < sizeof(r0)/sizeof(r0[0]); i++) {
            int kw = (r0[i].w_rel * (screen_w - 8)) / 116;
            keys[num_keys++] = (Key){r0[i].l, r0[i].s, r0[i].k, 0, x, kb_y, kw, row_h, (r0[i].k == KEY_BACKSPACE)};
            x += kw;
        }

        // Row 1: ASDF + Tab + Enter (11 keys)
        kb_y += row_h;
        struct { const char *l; const char *s; int k; int w_rel; } r1[] = {
            {"Tab", "Tab", KEY_TAB, 12}, {"a", "A", KEY_A, 10}, {"s", "S", KEY_S, 10},
            {"d", "D", KEY_D, 10}, {"f", "F", KEY_F, 10}, {"g", "G", KEY_G, 10},
            {"h", "H", KEY_H, 10}, {"j", "J", KEY_J, 10}, {"k", "K", KEY_K, 10},
            {"l", "L", KEY_L, 10}, {"Enter", "Enter", KEY_ENTER, 14}
        };
        x = 4;
        for (size_t i = 0; i < sizeof(r1)/sizeof(r1[0]); i++) {
            int kw = (r1[i].w_rel * (screen_w - 8)) / 116;
            keys[num_keys++] = (Key){r1[i].l, r1[i].s, r1[i].k, 0, x, kb_y, kw, row_h, (r1[i].k == KEY_TAB || r1[i].k == KEY_ENTER)};
            x += kw;
        }

        // Row 2: ZXCV + Shift + Punctuation (11 keys)
        kb_y += row_h;
        struct { const char *l; const char *s; int k; int w_rel; } r2[] = {
            {"Shift", "Shift", KEY_LEFTSHIFT, 16}, {"z", "Z", KEY_Z, 10}, {"x", "X", KEY_X, 10},
            {"c", "C", KEY_C, 10}, {"v", "V", KEY_V, 10}, {"b", "B", KEY_B, 10},
            {"n", "N", KEY_N, 10}, {"m", "M", KEY_M, 10}, {",", "<", KEY_COMMA, 10},
            {".", ">", KEY_DOT, 10}, {"/", "?", KEY_SLASH, 10}
        };
        x = 4;
        for (size_t i = 0; i < sizeof(r2)/sizeof(r2[0]); i++) {
            int kw = (r2[i].w_rel * (screen_w - 8)) / 116;
            keys[num_keys++] = (Key){r2[i].l, r2[i].s, r2[i].k, 0, x, kb_y, kw, row_h, (r2[i].k == KEY_LEFTSHIFT)};
            x += kw;
        }

        // Row 3: ?123, Modifiers, Space, Arrows, Hide (10 keys)
        kb_y += row_h;
        int r3_h = screen_h - kb_y; // fill to screen bottom
        struct { const char *l; const char *s; int k; int w_rel; } r3[] = {
            {"?123", "?123", -100, 14}, {"Ctrl", "Ctrl", KEY_LEFTCTRL, 10},
            {"Alt", "Alt", KEY_LEFTALT, 10}, {"Space", "Space", KEY_SPACE, 34},
            {"Esc", "Esc", KEY_ESC, 10}, {"Left", "Left", KEY_LEFT, 9},
            {"Down", "Down", KEY_DOWN, 9}, {"Up", "Up", KEY_UP, 9},
            {"Right", "Right", KEY_RIGHT, 9}, {"Hide", "Hide", -99, 12}
        };
        x = 4;
        for (size_t i = 0; i < sizeof(r3)/sizeof(r3[0]); i++) {
            int kw = (r3[i].w_rel * (screen_w - 8)) / 126;
            keys[num_keys++] = (Key){r3[i].l, r3[i].s, r3[i].k, 0, x, kb_y, kw, r3_h, (r3[i].k == -100 || r3[i].k == -99)};
            x += kw;
        }
    } else {
        // ================= PAGE 1: NUMBERS & SYMBOLS =================
        // Row 0: Numbers 1-0 + Bksp (11 keys)
        struct { const char *l; const char *s; int k; int w_rel; } r0[] = {
            {"1", "1", KEY_1, 10}, {"2", "2", KEY_2, 10}, {"3", "3", KEY_3, 10},
            {"4", "4", KEY_4, 10}, {"5", "5", KEY_5, 10}, {"6", "6", KEY_6, 10},
            {"7", "7", KEY_7, 10}, {"8", "8", KEY_8, 10}, {"9", "9", KEY_9, 10},
            {"0", "0", KEY_0, 10}, {"Bksp", "Bksp", KEY_BACKSPACE, 16}
        };
        int x = 4;
        for (size_t i = 0; i < sizeof(r0)/sizeof(r0[0]); i++) {
            int kw = (r0[i].w_rel * (screen_w - 8)) / 116;
            keys[num_keys++] = (Key){r0[i].l, r0[i].s, r0[i].k, 0, x, kb_y, kw, row_h, (r0[i].k == KEY_BACKSPACE)};
            x += kw;
        }

        // Row 1: Shell & Math symbols + Tab + Enter (11 keys)
        kb_y += row_h;
        struct { const char *l; const char *s; int k; int shift; int w_rel; } r1[] = {
            {"Tab", "Tab", KEY_TAB, 0, 12}, {"!", "!", KEY_1, 1, 10}, {"@", "@", KEY_2, 1, 10},
            {"#", "#", KEY_3, 1, 10}, {"$", "$", KEY_4, 1, 10}, {"%", "%", KEY_5, 1, 10},
            {"^", "^", KEY_6, 1, 10}, {"&", "&", KEY_7, 1, 10}, {"*", "*", KEY_8, 1, 10},
            {"(", "(", KEY_9, 1, 10}, {")", ")", KEY_0, 1, 14}
        };
        x = 4;
        for (size_t i = 0; i < sizeof(r1)/sizeof(r1[0]); i++) {
            int kw = (r1[i].w_rel * (screen_w - 8)) / 116;
            keys[num_keys++] = (Key){r1[i].l, r1[i].s, r1[i].k, r1[i].shift, x, kb_y, kw, row_h, (r1[i].k == KEY_TAB)};
            x += kw;
        }

        // Row 2: Braces, dashes, pipe, tilde (11 keys)
        kb_y += row_h;
        struct { const char *l; const char *s; int k; int shift; int w_rel; } r2[] = {
            {"-", "-", KEY_MINUS, 0, 10}, {"_", "_", KEY_MINUS, 1, 10},
            {"+", "+", KEY_EQUAL, 1, 10}, {"=", "=", KEY_EQUAL, 0, 10},
            {"[", "[", KEY_LEFTBRACE, 0, 10}, {"]", "]", KEY_RIGHTBRACE, 0, 10},
            {"{", "{", KEY_LEFTBRACE, 1, 10}, {"}", "}", KEY_RIGHTBRACE, 1, 10},
            {"\\", "\\", KEY_BACKSLASH, 0, 10}, {"|", "|", KEY_BACKSLASH, 1, 10},
            {"~", "~", KEY_GRAVE, 1, 16}
        };
        x = 4;
        for (size_t i = 0; i < sizeof(r2)/sizeof(r2[0]); i++) {
            int kw = (r2[i].w_rel * (screen_w - 8)) / 116;
            keys[num_keys++] = (Key){r2[i].l, r2[i].s, r2[i].k, r2[i].shift, x, kb_y, kw, row_h, 0};
            x += kw;
        }

        // Row 3: ABC switch, Quotes, Punctuation, Ctrl+C, Hide (10 keys)
        kb_y += row_h;
        int r3_h = screen_h - kb_y;
        struct { const char *l; const char *s; int k; int shift; int w_rel; } r3[] = {
            {"ABC", "ABC", -100, 0, 14}, {"Ctrl+C", "Ctrl+C", -98, 0, 14},
            {":", ":", KEY_SEMICOLON, 1, 10}, {";", ";", KEY_SEMICOLON, 0, 10},
            {"'", "'", KEY_APOSTROPHE, 0, 10}, {"\"", "\"", KEY_APOSTROPHE, 1, 10},
            {"<", "<", KEY_COMMA, 1, 9}, {">", ">", KEY_DOT, 1, 9},
            {"Space", "Space", KEY_SPACE, 0, 26}, {"Hide", "Hide", -99, 0, 14}
        };
        x = 4;
        for (size_t i = 0; i < sizeof(r3)/sizeof(r3[0]); i++) {
            int kw = (r3[i].w_rel * (screen_w - 8)) / 126;
            keys[num_keys++] = (Key){r3[i].l, r3[i].s, r3[i].k, r3[i].shift, x, kb_y, kw, r3_h, (r3[i].k == -100 || r3[i].k == -99 || r3[i].k == -98)};
            x += kw;
        }
    }
}

static void draw_keyboard(void) {
    draw_rect(0, screen_h - KB_HEIGHT, screen_w, KB_HEIGHT, COLOR_BG);
    draw_line_h(0, screen_h - KB_HEIGHT, screen_w, COLOR_KEY_BORDER);
    for (int i = 0; i < num_keys; i++) {
        draw_key(&keys[i], 0);
    }
}

static void apply_console_geometry(int active) {
    if (tty_fd < 0) return;

    // When keyboard is active: reserve bottom 150px (rows 0-23 for terminal text)
    // When hidden: restore full 34 rows
    unsigned short rows = active ? 24 : 34;

    struct vt_sizes vts;
    vts.v_rows = rows;
    vts.v_cols = 120;
    vts.v_scrollsize = 0;
    ioctl(tty_fd, VT_RESIZE, &vts);

    struct winsize ws;
    ws.ws_row = rows;
    ws.ws_col = 120;
    ws.ws_xpixel = screen_w;
    ws.ws_ypixel = active ? (screen_h - KB_HEIGHT) : screen_h;
    ioctl(tty_fd, TIOCSWINSZ, &ws);
}

static int find_touch_device(void) {
    char path[256];
    char name[128];
    for (int i = 0; i < 32; i++) {
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        memset(name, 0, sizeof(name));
        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0) {
            if (strstr(name, "Touchscreen") || strstr(name, "touch")) {
                printf("[fbkeyboard] Found Touchscreen device: %s (%s)\n", path, name);
                return fd;
            }
        }
        close(fd);
    }
    return -1;
}

static int setup_uinput(void) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("[fbkeyboard] Cannot open /dev/uinput");
        return -1;
    }

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);

    for (int k = 1; k < 255; k++) {
        ioctl(fd, UI_SET_KEYBIT, k);
    }

    struct uinput_setup usetup;
    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_VIRTUAL;
    strcpy(usetup.name, "Vita Framebuffer Touch Keyboard");

    ioctl(fd, UI_DEV_SETUP, &usetup);
    ioctl(fd, UI_DEV_CREATE);
    return fd;
}

static void emit_key(int uinput_fd, int keycode, int press) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_KEY;
    ev.code = keycode;
    ev.value = press;
    if (write(uinput_fd, &ev, sizeof(ev)) < 0) {
        // Ignored
    }

    memset(&ev, 0, sizeof(ev));
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    if (write(uinput_fd, &ev, sizeof(ev)) < 0) {
        // Ignored
    }
}

static void emit_key_or_tty(int uinput_fd, int keycode, int need_shift, const char *label, int press) {
    if (uinput_fd >= 0) {
        if (need_shift && press == 1) emit_key(uinput_fd, KEY_LEFTSHIFT, 1);
        emit_key(uinput_fd, keycode, press);
        if (need_shift && press == 0) emit_key(uinput_fd, KEY_LEFTSHIFT, 0);
    } else if (tty_fd >= 0 && press == 0) {
        // Fallback to TIOCSTI when uinput is unavailable
        if (keycode == KEY_ENTER) {
            char c = '\n';
            ioctl(tty_fd, TIOCSTI, &c);
        } else if (keycode == KEY_BACKSPACE) {
            char c = 0x7f;
            ioctl(tty_fd, TIOCSTI, &c);
        } else if (keycode == KEY_TAB) {
            char c = '\t';
            ioctl(tty_fd, TIOCSTI, &c);
        } else if (keycode == KEY_SPACE) {
            char c = ' ';
            ioctl(tty_fd, TIOCSTI, &c);
        } else if (label && strlen(label) == 1) {
            char c = label[0];
            ioctl(tty_fd, TIOCSTI, &c);
        }
    }
}

static volatile sig_atomic_t g_resumed = 0;

static void handle_sigcont(int sig) {
    (void)sig;
    g_resumed = 1;
}

static void drain_pending_touch(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return;
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    struct input_event dummy;
    while (read(fd, &dummy, sizeof(dummy)) > 0) {}
    fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    printf("[fbkeyboard] Starting PlayStation Vita Compact Touch Keyboard...\n");

    fb_fd = open(FB_DEV, O_RDWR);
    if (fb_fd < 0) {
        perror("[fbkeyboard] Failed to open /dev/fb0");
        return 1;
    }

    struct fb_var_screeninfo vinfo;
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) == 0) {
        screen_w = vinfo.xres;
        screen_h = vinfo.yres;
    }
    printf("[fbkeyboard] Framebuffer resolution: %dx%d (%d bpp)\n", screen_w, screen_h, vinfo.bits_per_pixel);

    fb_size = screen_w * screen_h * 4;
    fb_mem = (uint32_t *)mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_mem == MAP_FAILED) {
        perror("[fbkeyboard] mmap failed");
        close(fb_fd);
        return 1;
    }

    tty_fd = open("/dev/tty1", O_RDWR);
    apply_console_geometry(1);

    init_key_layout();
    draw_keyboard();

    int uinput_fd = -1;
    for (int retry = 0; retry < 5 && uinput_fd < 0; retry++) {
        uinput_fd = setup_uinput();
        if (uinput_fd < 0) {
            printf("[fbkeyboard] Waiting for /dev/uinput (try %d/5)...\n", retry + 1);
            sleep(1);
        }
    }
    if (uinput_fd < 0 && tty_fd < 0) {
        fprintf(stderr, "[fbkeyboard] Warning: neither /dev/uinput nor /dev/tty1 accessible. Continuing in visual mode.\n");
    }

    int touch_fd = -1;
    while (touch_fd < 0) {
        touch_fd = find_touch_device();
        if (touch_fd < 0) sleep(1);
    }

    // Register SIGCONT handler to discard touch events queued while suspended
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigcont;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGCONT, &sa, NULL);

    drain_pending_touch(touch_fd);

    int flags = fcntl(touch_fd, F_GETFL, 0);
    fcntl(touch_fd, F_SETFL, flags & ~O_NONBLOCK);

    struct input_event ev;
    int cur_touch_x = -1, cur_touch_y = -1;
    int is_touching = 0;
    int was_touching = 0;
    Key *active_key = NULL;

    struct pollfd pfd;
    pfd.fd = touch_fd;
    pfd.events = POLLIN;

    while (1) {
        if (g_resumed) {
            g_resumed = 0;
            drain_pending_touch(touch_fd);
            is_touching = 0;
            was_touching = 0;
            active_key = NULL;
            if (keyboard_visible) {
                draw_keyboard();
            }
            continue;
        }

        int poll_res = poll(&pfd, 1, 1000);

        if (poll_res < 0) {
            if (errno == EINTR) {
                if (g_resumed) {
                    g_resumed = 0;
                    drain_pending_touch(touch_fd);
                    is_touching = 0;
                    was_touching = 0;
                    active_key = NULL;
                    if (keyboard_visible) {
                        draw_keyboard();
                    }
                }
                continue;
            }
            break;
        }

        if (poll_res == 0) {
            continue;
        }

        if (g_resumed) {
            g_resumed = 0;
            drain_pending_touch(touch_fd);
            is_touching = 0;
            was_touching = 0;
            active_key = NULL;
            if (keyboard_visible) {
                draw_keyboard();
            }
            continue;
        }

        if (read(touch_fd, &ev, sizeof(ev)) <= 0) {
            break;
        }

        if (ev.type == EV_ABS) {
            if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X) {
                cur_touch_x = (ev.value * screen_w) / 1920;
            } else if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y) {
                cur_touch_y = (ev.value * screen_h) / 1080;
            } else if (ev.code == ABS_MT_TRACKING_ID) {
                is_touching = (ev.value >= 0);
            }
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            is_touching = ev.value;
        } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            // Frame completed: evaluate touch state transitions
            if (is_touching && !was_touching) {
                was_touching = 1;

                if (!keyboard_visible) {
                    // Tap on show tab at bottom right unhides keyboard
                    if (cur_touch_y > screen_h - 30 && cur_touch_x > screen_w - 120) {
                        keyboard_visible = 1;
                        apply_console_geometry(1);
                        init_key_layout();
                        draw_keyboard();
                    }
                    continue;
                }

                // Check key hit
                active_key = NULL;
                for (int i = 0; i < num_keys; i++) {
                    if (cur_touch_x >= keys[i].x && cur_touch_x < keys[i].x + keys[i].w &&
                        cur_touch_y >= keys[i].y && cur_touch_y < keys[i].y + keys[i].h) {
                        active_key = &keys[i];
                        draw_key(active_key, 1);
                        break;
                    }
                }
            } else if (!is_touching && was_touching) {
                was_touching = 0;
                if (active_key) {
                    draw_key(active_key, 0);

                    if (active_key->keycode == -100) {
                        // Toggle between ABC and ?123 Symbols
                        keyboard_page = !keyboard_page;
                        init_key_layout();
                        draw_keyboard();
                    } else if (active_key->keycode == -99) {
                        // Hide key
                        keyboard_visible = 0;
                        draw_rect(0, screen_h - KB_HEIGHT, screen_w, KB_HEIGHT, 0x00000000);
                        // Subtle Show tab
                        draw_rect(screen_w - 90, screen_h - 22, 90, 22, COLOR_SPECIAL);
                        draw_string(screen_w - 75, screen_h - 18, "[KB]", 0xFF11111B);
                        apply_console_geometry(0);
                    } else if (active_key->keycode == -98) {
                        // Ctrl+C
                        if (uinput_fd >= 0) {
                            emit_key(uinput_fd, KEY_LEFTCTRL, 1);
                            emit_key(uinput_fd, KEY_C, 1);
                            emit_key(uinput_fd, KEY_C, 0);
                            emit_key(uinput_fd, KEY_LEFTCTRL, 0);
                        } else if (tty_fd >= 0) {
                            char c = 0x03;
                            ioctl(tty_fd, TIOCSTI, &c);
                        }
                    } else if (active_key->keycode == KEY_LEFTSHIFT) {
                        shift_active = !shift_active;
                        draw_keyboard();
                    } else {
                        const char *lbl = shift_active ? active_key->shift_label : active_key->label;
                        if (shift_active && uinput_fd >= 0) emit_key(uinput_fd, KEY_LEFTSHIFT, 1);
                        emit_key_or_tty(uinput_fd, active_key->keycode, active_key->need_shift, lbl, 1);
                        emit_key_or_tty(uinput_fd, active_key->keycode, active_key->need_shift, lbl, 0);
                        if (shift_active) {
                            if (uinput_fd >= 0) emit_key(uinput_fd, KEY_LEFTSHIFT, 0);
                            shift_active = 0;
                            draw_keyboard();
                        }
                    }
                    active_key = NULL;
                }
            }
        }
    }

    munmap(fb_mem, fb_size);
    close(fb_fd);
    if (uinput_fd >= 0) close(uinput_fd);
    if (tty_fd >= 0) close(tty_fd);
    close(touch_fd);
    return 0;
}
