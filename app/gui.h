#ifndef GUI_H
#define GUI_H

#include <stdint.h>
#include <psp2/touch.h>

#define SCREEN_W 960
#define SCREEN_H 544

/* Color Palette (RGBA for Sony Vita A8B8G8R8) */
#define COLOR_RGBA(r, g, b, a) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))

/* Sleek Dark Theme Palette */
#define GUI_COLOR_BG_TOP       COLOR_RGBA(15, 20, 28, 255)
#define GUI_COLOR_BG_BOT       COLOR_RGBA(9, 13, 19, 255)
#define GUI_COLOR_CARD_BG      COLOR_RGBA(22, 31, 46, 255)
#define GUI_COLOR_CARD_BORDER  COLOR_RGBA(38, 53, 74, 255)
#define GUI_COLOR_CARD_SEL_BG  COLOR_RGBA(27, 42, 64, 255)
#define GUI_COLOR_CARD_SEL_BDR COLOR_RGBA(56, 189, 248, 255)

#define GUI_COLOR_TEXT_MAIN    COLOR_RGBA(241, 245, 249, 255)
#define GUI_COLOR_TEXT_MUTED   COLOR_RGBA(148, 163, 184, 255)
#define GUI_COLOR_TEXT_DIM     COLOR_RGBA(100, 116, 139, 255)

#define GUI_COLOR_ACCENT_CYAN  COLOR_RGBA(56, 189, 248, 255)
#define GUI_COLOR_ACCENT_GREEN COLOR_RGBA(16, 185, 129, 255)
#define GUI_COLOR_ACCENT_AMBER COLOR_RGBA(245, 158, 11, 255)
#define GUI_COLOR_ACCENT_RED   COLOR_RGBA(239, 68, 68, 255)
#define GUI_COLOR_ACCENT_BLUE  COLOR_RGBA(59, 130, 246, 255)
#define GUI_COLOR_ACCENT_PURP  COLOR_RGBA(168, 85, 247, 255)

/* Button Style Colors */
#define GUI_BTN_EMERALD        COLOR_RGBA(16, 185, 129, 255)
#define GUI_BTN_SKY            COLOR_RGBA(2, 132, 199, 255)
#define GUI_BTN_SLATE          COLOR_RGBA(30, 41, 59, 255)
#define GUI_BTN_AMBER          COLOR_RGBA(217, 119, 6, 255)

/* Rectangle structure for hit detection and layout */
typedef struct {
    int x, y, w, h;
} GuiRect;

/* Touch input state structure */
typedef struct {
    int is_down;
    int x, y;
    int was_down;
    int just_pressed;
    int just_released;
} GuiTouchState;

/* Subsystem initialization and double-buffered display management */
int  gui_init(void);
void gui_swap_buffers(void);
void gui_wait_vblank(void);
void gui_touch_init(void);
void gui_touch_poll(GuiTouchState *ts);
int  gui_hit_test(const GuiRect *r, int x, int y);

/* Core drawing primitives */
void gui_clear_screen(void);
void gui_draw_rect(int x, int y, int w, int h, uint32_t color);
void gui_draw_rect_outline(int x, int y, int w, int h, int thickness, uint32_t color);
void gui_draw_card(int x, int y, int w, int h, uint32_t bg_color, uint32_t border_color);
void gui_draw_char(int x, int y, char c, uint32_t color, int scale);
void gui_draw_text(int x, int y, const char *str, uint32_t color, int scale);
void gui_draw_text_centered(int cx, int y, const char *str, uint32_t color, int scale);
int  gui_get_text_width(const char *str, int scale);

/* High-level UI components */
void gui_draw_badge(int x, int y, const char *text, uint32_t bg, uint32_t fg);
void gui_draw_action_button(const GuiRect *r, const char *title, const char *subtext, const char *badge, uint32_t bg_color, int pressed);
void gui_draw_progress_bar(int x, int y, int w, int h, float progress, uint32_t bg, uint32_t fill);
void gui_draw_progress_modal(const char *title, const char *cur_file, float progress, const char *status_str, const char *badge_prompt);
uint32_t gui_wait_button_press(uint32_t mask);

#endif /* GUI_H */
