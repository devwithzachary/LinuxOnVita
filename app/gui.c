#include "gui.h"
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>

extern unsigned char psvDebugScreenFont[];

#define SCREEN_FB_SIZE (2 * 1024 * 1024) /* 2MB per framebuffer (256KB-aligned) */

static SceUID g_displayblock = -1;
static uint32_t *g_fb[2] = {NULL, NULL};
static int g_current_fb_idx = 0; /* Front buffer currently scanned out */
static int g_draw_fb_idx = 1;    /* Backbuffer currently being drawn to */

int gui_init(void) {
    if (g_displayblock >= 0) {
        return 0;
    }

    /* Allocate 4MB in CDRAM for two 256KB-aligned framebuffers */
    g_displayblock = sceKernelAllocMemBlock("gui_display", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, SCREEN_FB_SIZE * 2, NULL);
    if (g_displayblock < 0) {
        return g_displayblock;
    }

    void *base = NULL;
    sceKernelGetMemBlockBase(g_displayblock, &base);
    if (!base) {
        return -1;
    }

    g_fb[0] = (uint32_t *)base;
    g_fb[1] = (uint32_t *)((uint8_t *)base + SCREEN_FB_SIZE);

    /* Initialize both framebuffers with the gradient background */
    for (int b = 0; b < 2; b++) {
        for (int y = 0; y < SCREEN_H; y++) {
            uint32_t r = 15 - (6 * y) / SCREEN_H;
            uint32_t g = 20 - (7 * y) / SCREEN_H;
            uint32_t b_col = 28 - (9 * y) / SCREEN_H;
            uint32_t color = COLOR_RGBA(r, g, b_col, 255);
            uint32_t *row = &g_fb[b][y * SCREEN_W];
            for (int x = 0; x < SCREEN_W; x++) {
                row[x] = color;
            }
        }
    }

    g_current_fb_idx = 0;
    g_draw_fb_idx = 1;

    SceDisplayFrameBuf framebuf = {
        .size = sizeof(framebuf),
        .base = g_fb[g_current_fb_idx],
        .pitch = SCREEN_W,
        .pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8,
        .width = SCREEN_W,
        .height = SCREEN_H,
    };

    sceDisplaySetFrameBuf(&framebuf, SCE_DISPLAY_SETBUF_NEXTFRAME);
    sceDisplayWaitVblankStart();

    return 0;
}

void gui_swap_buffers(void) {
    if (!g_fb[g_draw_fb_idx]) return;

    SceDisplayFrameBuf framebuf = {
        .size = sizeof(framebuf),
        .base = g_fb[g_draw_fb_idx],
        .pitch = SCREEN_W,
        .pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8,
        .width = SCREEN_W,
        .height = SCREEN_H,
    };

    sceDisplaySetFrameBuf(&framebuf, SCE_DISPLAY_SETBUF_NEXTFRAME);
    sceDisplayWaitVblankStart();

    g_current_fb_idx = g_draw_fb_idx;
    g_draw_fb_idx = 1 - g_draw_fb_idx;
}

void gui_wait_vblank(void) {
    sceDisplayWaitVblankStart();
}

void gui_touch_init(void) {
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, 1);
}

void gui_touch_poll(GuiTouchState *ts) {
    SceTouchData touch;
    memset(&touch, 0, sizeof(touch));
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

    int prev_down = ts->is_down;
    if (touch.reportNum > 0) {
        ts->is_down = 1;
        /* Front touch digitizer reports 0..1919 and 0..1087 on Vita */
        ts->x = touch.report[0].x / 2;
        ts->y = touch.report[0].y / 2;
        if (ts->x < 0) ts->x = 0;
        if (ts->x >= SCREEN_W) ts->x = SCREEN_W - 1;
        if (ts->y < 0) ts->y = 0;
        if (ts->y >= SCREEN_H) ts->y = SCREEN_H - 1;
    } else {
        ts->is_down = 0;
    }

    ts->just_pressed = (ts->is_down && !prev_down);
    ts->just_released = (!ts->is_down && prev_down);
    ts->was_down = prev_down;
}

int gui_hit_test(const GuiRect *r, int x, int y) {
    if (!r) return 0;
    return (x >= r->x && x < (r->x + r->w) && y >= r->y && y < (r->y + r->h));
}

void gui_clear_screen(void) {
    uint32_t *target_fb = g_fb[g_draw_fb_idx];
    if (!target_fb) return;

    /* Render a subtle, rich vertical gradient across the backbuffer */
    for (int y = 0; y < SCREEN_H; y++) {
        uint32_t r = 15 - (6 * y) / SCREEN_H;
        uint32_t g = 20 - (7 * y) / SCREEN_H;
        uint32_t b = 28 - (9 * y) / SCREEN_H;
        uint32_t color = COLOR_RGBA(r, g, b, 255);

        uint32_t *row = &target_fb[y * SCREEN_W];
        for (int x = 0; x < SCREEN_W; x++) {
            row[x] = color;
        }
    }
}

void gui_draw_rect(int x, int y, int w, int h, uint32_t color) {
    uint32_t *target_fb = g_fb[g_draw_fb_idx];
    if (!target_fb) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;

    for (int j = y; j < y + h; j++) {
        uint32_t *row = &target_fb[j * SCREEN_W];
        for (int i = x; i < x + w; i++) {
            row[i] = color;
        }
    }
}

void gui_draw_rect_outline(int x, int y, int w, int h, int thickness, uint32_t color) {
    if (thickness <= 0) return;
    /* Top */
    gui_draw_rect(x, y, w, thickness, color);
    /* Bottom */
    gui_draw_rect(x, y + h - thickness, w, thickness, color);
    /* Left */
    gui_draw_rect(x, y, thickness, h, color);
    /* Right */
    gui_draw_rect(x + w - thickness, y, thickness, h, color);
}

void gui_draw_card(int x, int y, int w, int h, uint32_t bg_color, uint32_t border_color) {
    gui_draw_rect(x + 1, y + 1, w - 2, h - 2, bg_color);
    gui_draw_rect_outline(x, y, w, h, 1, border_color);
}

void gui_draw_char(int x, int y, char c, uint32_t color, int scale) {
    uint32_t *target_fb = g_fb[g_draw_fb_idx];
    if (!target_fb || scale <= 0) return;
    if (c < 0) return;

    const unsigned char *glyph = &psvDebugScreenFont[(int)(unsigned char)c * 8];
    for (int row = 0; row < 8; row++) {
        unsigned char bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if ((bits >> (7 - col)) & 1) {
                int px = x + col * scale;
                int py = y + row * scale;
                for (int dy = 0; dy < scale; dy++) {
                    int curr_y = py + dy;
                    if (curr_y < 0 || curr_y >= SCREEN_H) continue;
                    uint32_t *fb_row = &target_fb[curr_y * SCREEN_W];
                    for (int dx = 0; dx < scale; dx++) {
                        int curr_x = px + dx;
                        if (curr_x >= 0 && curr_x < SCREEN_W) {
                            fb_row[curr_x] = color;
                        }
                    }
                }
            }
        }
    }
}

void gui_draw_text(int x, int y, const char *str, uint32_t color, int scale) {
    if (!str) return;
    int cur_x = x;
    while (*str) {
        gui_draw_char(cur_x, y, *str, color, scale);
        cur_x += 8 * scale;
        str++;
    }
}

int gui_get_text_width(const char *str, int scale) {
    if (!str) return 0;
    return strlen(str) * 8 * scale;
}

void gui_draw_text_centered(int cx, int y, const char *str, uint32_t color, int scale) {
    int w = gui_get_text_width(str, scale);
    gui_draw_text(cx - w / 2, y, str, color, scale);
}

void gui_draw_badge(int x, int y, const char *text, uint32_t bg, uint32_t fg) {
    if (!text) return;
    int tw = gui_get_text_width(text, 1);
    int bw = tw + 10;
    int bh = 16;
    gui_draw_rect(x, y, bw, bh, bg);
    gui_draw_rect_outline(x, y, bw, bh, 1, fg);
    gui_draw_text(x + 5, y + 4, text, fg, 1);
}

void gui_draw_action_button(const GuiRect *r, const char *title, const char *subtext, const char *badge, uint32_t bg_color, int pressed) {
    if (!r) return;
    int bx = r->x;
    int by = r->y;
    int bw = r->w;
    int bh = r->h;

    uint32_t cur_bg = bg_color;
    uint32_t border_color = COLOR_RGBA(255, 255, 255, 60);

    if (pressed) {
        /* Pressed feedback: highlight border and lighten background slightly */
        border_color = COLOR_RGBA(255, 255, 255, 180);
        cur_bg = COLOR_RGBA(
            ((bg_color & 0xFF) + 30 > 255 ? 255 : (bg_color & 0xFF) + 30),
            (((bg_color >> 8) & 0xFF) + 30 > 255 ? 255 : ((bg_color >> 8) & 0xFF) + 30),
            (((bg_color >> 16) & 0xFF) + 30 > 255 ? 255 : ((bg_color >> 16) & 0xFF) + 30),
            255
        );
    }

    gui_draw_rect(bx + 1, by + 1, bw - 2, bh - 2, cur_bg);
    gui_draw_rect_outline(bx, by, bw, bh, pressed ? 2 : 1, border_color);

    if (bw < 60) {
        /* Narrow vertical buttons (e.g. scroll up/down strips) */
        if (badge && strlen(badge) > 0) {
            gui_draw_text_centered(bx + bw / 2, by + bh / 2 - 14, badge, GUI_COLOR_ACCENT_CYAN, 1);
        }
        if (title) {
            gui_draw_text_centered(bx + bw / 2, by + bh / 2 + 4, title, GUI_COLOR_TEXT_MAIN, 1);
        }
    } else if (bh > 50) {
        /* Large Action Card Buttons (e.g. bottom toolbar buttons) */
        /* Row 1: Shortcut badge at top-right */
        if (badge && strlen(badge) > 0) {
            int badgew = gui_get_text_width(badge, 1) + 8;
            int badgex = bx + bw - badgew - 8;
            int badgey = by + 6;
            gui_draw_rect(badgex, badgey, badgew, 14, COLOR_RGBA(0, 0, 0, 100));
            gui_draw_rect_outline(badgex, badgey, badgew, 14, 1, COLOR_RGBA(255, 255, 255, 90));
            gui_draw_text(badgex + 4, badgey + 3, badge, GUI_COLOR_TEXT_MAIN, 1);
        }

        /* Row 2: Action Name / Title (moved down to avoid badge overlap) */
        if (title) {
            gui_draw_text(bx + 12, by + 26, title, GUI_COLOR_TEXT_MAIN, 2);
        }

        /* Row 3: Subtext description */
        if (subtext) {
            gui_draw_text(bx + 12, by + 49, subtext, GUI_COLOR_TEXT_MUTED, 1);
        }
    } else {
        /* Compact Header / Control Buttons (e.g. EXIT, BACK, TOGGLE) */
        int badge_w = 0;
        int badgex = 0;
        if (badge && strlen(badge) > 0) {
            badge_w = gui_get_text_width(badge, 1) + 8;
            badgex = bx + bw - badge_w - 8;
            int badgey = by + (bh - 14) / 2;
            gui_draw_rect(badgex, badgey, badge_w, 14, COLOR_RGBA(0, 0, 0, 100));
            gui_draw_rect_outline(badgex, badgey, badge_w, 14, 1, COLOR_RGBA(255, 255, 255, 90));
            gui_draw_text(badgex + 4, badgey + 3, badge, GUI_COLOR_TEXT_MAIN, 1);
        }

        if (title) {
            int title_w2 = gui_get_text_width(title, 2);
            int max_w = (badge_w > 0) ? (badgex - bx - 20) : (bw - 24);
            if (title_w2 <= max_w && bh >= 32) {
                int title_y = by + (bh - 16) / 2;
                gui_draw_text(bx + 14, title_y, title, GUI_COLOR_TEXT_MAIN, 2);
            } else {
                int title_y = by + (bh - 8) / 2;
                if (!badge) {
                    gui_draw_text_centered(bx + bw / 2, title_y, title, GUI_COLOR_ACCENT_CYAN, 1);
                } else {
                    gui_draw_text(bx + 14, title_y, title, GUI_COLOR_TEXT_MAIN, 1);
                }
            }
        }
    }
}

void gui_draw_progress_bar(int x, int y, int w, int h, float progress, uint32_t bg, uint32_t fill) {
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    gui_draw_rect(x, y, w, h, bg);
    gui_draw_rect_outline(x, y, w, h, 1, COLOR_RGBA(255, 255, 255, 40));

    int fill_w = (int)((w - 2) * progress);
    if (fill_w > 0) {
        gui_draw_rect(x + 1, y + 1, fill_w, h - 2, fill);
    }
}

void gui_draw_progress_modal(const char *title, const char *cur_file, float progress, const char *status_str, const char *badge_prompt) {
    gui_clear_screen();

    int mw = 640;
    int mh = 240;
    int mx = (SCREEN_W - mw) / 2;
    int my = (SCREEN_H - mh) / 2;

    gui_draw_card(mx, my, mw, mh, GUI_COLOR_CARD_BG, GUI_COLOR_ACCENT_CYAN);
    gui_draw_rect_outline(mx, my, mw, mh, 2, GUI_COLOR_ACCENT_CYAN);

    gui_draw_text(mx + 24, my + 24, title, GUI_COLOR_ACCENT_CYAN, 2);
    gui_draw_rect(mx + 24, my + 54, mw - 48, 1, GUI_COLOR_CARD_BORDER);

    if (cur_file) {
        gui_draw_text(mx + 24, my + 74, cur_file, GUI_COLOR_TEXT_MAIN, 1);
    }

    gui_draw_progress_bar(mx + 24, my + 106, mw - 48, 22, progress, GUI_COLOR_BG_BOT, GUI_COLOR_ACCENT_GREEN);

    if (status_str) {
        gui_draw_text(mx + 24, my + 146, status_str, GUI_COLOR_TEXT_MUTED, 1);
    }

    if (badge_prompt) {
        int bw = gui_get_text_width(badge_prompt, 1) + 20;
        gui_draw_badge(mx + (mw - bw) / 2, my + mh - 38, badge_prompt, GUI_COLOR_CARD_SEL_BG, GUI_COLOR_ACCENT_AMBER);
    }

    gui_swap_buffers();
}

uint32_t gui_wait_button_press(uint32_t mask) {
    SceCtrlData pad;
    GuiTouchState ts;

    while (1) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        gui_touch_poll(&ts);
        if (!(pad.buttons & mask) && !ts.is_down)
            break;
        gui_wait_vblank();
    }
    while (1) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        gui_touch_poll(&ts);
        if (ts.just_pressed) {
            return SCE_CTRL_CROSS;
        }
        if (pad.buttons & mask) {
            uint32_t pressed = pad.buttons & mask;
            while (1) {
                sceCtrlPeekBufferPositive(0, &pad, 1);
                if (!(pad.buttons & pressed))
                    break;
                gui_wait_vblank();
            }
            return pressed;
        }
        gui_wait_vblank();
    }
}

