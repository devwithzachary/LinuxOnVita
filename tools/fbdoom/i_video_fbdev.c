// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// $Log:$
//
// DESCRIPTION:
//	DOOM graphics stuff for X11, UNIX.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: i_x.c,v 1.6 1997/02/03 22:45:10 b1 Exp $";

#include "config.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_event.h"
#include "d_main.h"
#include "i_video.h"
#include "i_system.h"
#include "z_zone.h"

#include "tables.h"
#include "doomkeys.h"

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include <stdarg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <sys/ioctl.h>

typedef enum {
    SCALE_FULLSCREEN, // Stretch to entire framebuffer (960x544 on PS Vita)
    SCALE_ASPECT,     // 4:3 aspect ratio fit (725x544 centered on Vita)
    SCALE_INTEGER,    // Fixed integer scaling (e.g. 640x400 for 2x)
} scale_mode_t;

static scale_mode_t scale_mode = SCALE_FULLSCREEN;

struct fb_var_screeninfo fb = {};
int fb_scaling = 1;
int usemouse = 0;

static uint32_t palette32[256];
static uint16_t palette16[256];

static void *fb_mmap_ptr = MAP_FAILED;
static size_t fb_mmap_size = 0;

static int *y_lut = NULL;
static int *x_lut = NULL;
static int render_w = 0;
static int render_h = 0;
static int offset_x = 0;
static int offset_y = 0;

struct color {
    uint32_t b:8;
    uint32_t g:8;
    uint32_t r:8;
    uint32_t a:8;
};

static struct color colors[256];

// The screen buffer; this is modified to draw things to the screen

byte *I_VideoBuffer = NULL;
byte *I_VideoBuffer_FB = NULL;

/* framebuffer file descriptor */
int fd_fb = -1;

int	X_width;
int X_height;

// If true, game is running as a screensaver

boolean screensaver_mode = false;

// Flag indicating whether the screen is currently visible:
// when the screen isnt visible, don't render the screen

boolean screenvisible;

// Mouse acceleration
//
// This emulates some of the behavior of DOS mouse drivers by increasing
// the speed when the mouse is moved fast.
//
// The mouse input values are input directly to the game, but when
// the values exceed the value of mouse_threshold, they are multiplied
// by mouse_acceleration to increase the speed.

float mouse_acceleration = 2.0;
int mouse_threshold = 10;

// Gamma correction level to use

int usegamma = 0;

typedef struct
{
	byte r;
	byte g;
	byte b;
} col_t;

void I_InitGraphics (void)
{
    int i;

    /* Open fbdev file descriptor */
    fd_fb = open("/dev/fb0", O_RDWR);
    if (fd_fb < 0)
    {
        printf("Could not open /dev/fb0\n");
        exit(-1);
    }

    /* fetch framebuffer info */
    ioctl(fd_fb, FBIOGET_VSCREENINFO, &fb);
    printf("I_InitGraphics: framebuffer: x_res: %d, y_res: %d, bpp: %d\n",
            fb.xres, fb.yres, fb.bits_per_pixel);
    printf("I_InitGraphics: DOOM internal canvas: %d x %d\n", SCREENWIDTH, SCREENHEIGHT);

    // Scaling mode selection: Fullscreen (960x544) is default on PS Vita
    scale_mode = SCALE_FULLSCREEN;

    if (M_CheckParm("-aspect") || M_CheckParm("-fit") || M_CheckParm("-4:3")) {
        scale_mode = SCALE_ASPECT;
    } else if (M_CheckParm("-integer") || M_CheckParm("-scale2") || M_CheckParm("-1:1")) {
        scale_mode = SCALE_INTEGER;
        fb_scaling = 2;
    } else if ((i = M_CheckParmWithArgs("-scaling", 1)) > 0) {
        fb_scaling = atoi(myargv[i + 1]);
        if (fb_scaling <= 0) {
            scale_mode = SCALE_FULLSCREEN;
        } else {
            scale_mode = SCALE_INTEGER;
        }
    } else if (M_CheckParm("-fullscreen") || M_CheckParm("-stretch")) {
        scale_mode = SCALE_FULLSCREEN;
    }

    if (scale_mode == SCALE_FULLSCREEN) {
        render_w = fb.xres;
        render_h = fb.yres;
        offset_x = 0;
        offset_y = 0;
        printf("I_InitGraphics: Scaling mode: FULLSCREEN (%dx%d edge-to-edge)\n", render_w, render_h);
    } else if (scale_mode == SCALE_ASPECT) {
        // Fit vertically, 4:3 aspect ratio -> width = (height * 4) / 3
        render_h = fb.yres;
        render_w = (fb.yres * 4) / 3;
        if (render_w > (int)fb.xres) render_w = fb.xres;
        offset_x = (fb.xres - render_w) / 2;
        offset_y = 0;
        printf("I_InitGraphics: Scaling mode: ASPECT 4:3 (%dx%d pillarbox, x_offset=%d)\n",
               render_w, render_h, offset_x);
    } else { // SCALE_INTEGER
        if (fb_scaling < 1) fb_scaling = 1;
        render_w = SCREENWIDTH * fb_scaling;
        render_h = SCREENHEIGHT * fb_scaling;
        if (render_w > (int)fb.xres) render_w = fb.xres;
        if (render_h > (int)fb.yres) render_h = fb.yres;
        offset_x = (fb.xres - render_w) / 2;
        offset_y = (fb.yres - render_h) / 2;
        printf("I_InitGraphics: Scaling mode: INTEGER %dx (%dx%d centered, x_offset=%d, y_offset=%d)\n",
               fb_scaling, render_w, render_h, offset_x, offset_y);
    }

    y_lut = (int *)malloc(render_h * sizeof(int));
    for (int y = 0; y < render_h; y++) {
        y_lut[y] = (y * SCREENHEIGHT) / render_h;
    }

    x_lut = (int *)malloc(render_w * sizeof(int));
    for (int x = 0; x < render_w; x++) {
        x_lut[x] = (x * SCREENWIDTH) / render_w;
    }

    /* Allocate screen buffer for DOOM to draw on */
    I_VideoBuffer = (byte*)Z_Malloc (SCREENWIDTH * SCREENHEIGHT, PU_STATIC, NULL);

    /* Map framebuffer directly via mmap for zero-copy high performance rendering */
    fb_mmap_size = fb.xres * fb.yres * (fb.bits_per_pixel / 8);
    fb_mmap_ptr = mmap(NULL, fb_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_fb, 0);
    if (fb_mmap_ptr != MAP_FAILED) {
        memset(fb_mmap_ptr, 0, fb_mmap_size); // Clear screen to black
        printf("I_InitGraphics: Framebuffer mapped via mmap (%zu bytes)\n", fb_mmap_size);
    } else {
        printf("I_InitGraphics: mmap failed, falling back to write buffer\n");
        I_VideoBuffer_FB = (byte*)malloc(fb_mmap_size);
        if (I_VideoBuffer_FB) memset(I_VideoBuffer_FB, 0, fb_mmap_size);
    }

    screenvisible = true;

    I_AtExit(I_ShutdownGraphics, true);

    extern int I_InitInput(void);
    I_InitInput();
}

void I_ShutdownGraphics (void)
{
    if (y_lut) {
        free(y_lut);
        y_lut = NULL;
    }
    if (x_lut) {
        free(x_lut);
        x_lut = NULL;
    }
    if (fb_mmap_ptr != MAP_FAILED) {
        memset(fb_mmap_ptr, 0, fb_mmap_size);
        munmap(fb_mmap_ptr, fb_mmap_size);
        fb_mmap_ptr = MAP_FAILED;
    }
    if (I_VideoBuffer) {
        Z_Free(I_VideoBuffer);
        I_VideoBuffer = NULL;
    }
    if (I_VideoBuffer_FB) {
        free(I_VideoBuffer_FB);
        I_VideoBuffer_FB = NULL;
    }
    if (fd_fb >= 0) {
        close(fd_fb);
        fd_fb = -1;
    }
}

void I_StartFrame (void)
{

}

__attribute__ ((weak)) void I_GetEvent (void)
{
//	event_t event;
//	bool button_state;
//
//	button_state = button_read ();
//
//	if (last_button_state != button_state)
//	{
//		last_button_state = button_state;
//
//		event.type = last_button_state ? ev_keydown : ev_keyup;
//		event.data1 = KEY_FIRE;
//		event.data2 = -1;
//		event.data3 = -1;
//
//		D_PostEvent (&event);
//	}
//
//	touch_main ();
//
//	if ((touch_state.x != last_touch_state.x) || (touch_state.y != last_touch_state.y) || (touch_state.status != last_touch_state.status))
//	{
//		last_touch_state = touch_state;
//
//		event.type = (touch_state.status == TOUCH_PRESSED) ? ev_keydown : ev_keyup;
//		event.data1 = -1;
//		event.data2 = -1;
//		event.data3 = -1;
//
//		if ((touch_state.x > 49)
//		 && (touch_state.x < 72)
//		 && (touch_state.y > 104)
//		 && (touch_state.y < 143))
//		{
//			// select weapon
//			if (touch_state.x < 60)
//			{
//				// lower row (5-7)
//				if (touch_state.y < 119)
//				{
//					event.data1 = '5';
//				}
//				else if (touch_state.y < 131)
//				{
//					event.data1 = '6';
//				}
//				else
//				{
//					event.data1 = '1';
//				}
//			}
//			else
//			{
//				// upper row (2-4)
//				if (touch_state.y < 119)
//				{
//					event.data1 = '2';
//				}
//				else if (touch_state.y < 131)
//				{
//					event.data1 = '3';
//				}
//				else
//				{
//					event.data1 = '4';
//				}
//			}
//		}
//		else if (touch_state.x < 40)
//		{
//			// button bar at bottom screen
//			if (touch_state.y < 40)
//			{
//				// enter
//				event.data1 = KEY_ENTER;
//			}
//			else if (touch_state.y < 80)
//			{
//				// escape
//				event.data1 = KEY_ESCAPE;
//			}
//			else if (touch_state.y < 120)
//			{
//				// use
//				event.data1 = KEY_USE;
//			}
//			else if (touch_state.y < 160)
//			{
//				// map
//				event.data1 = KEY_TAB;
//			}
//			else if (touch_state.y < 200)
//			{
//				// pause
//				event.data1 = KEY_PAUSE;
//			}
//			else if (touch_state.y < 240)
//			{
//				// toggle run
//				if (touch_state.status == TOUCH_PRESSED)
//				{
//					run = !run;
//
//					event.data1 = KEY_RSHIFT;
//
//					if (run)
//					{
//						event.type = ev_keydown;
//					}
//					else
//					{
//						event.type = ev_keyup;
//					}
//				}
//				else
//				{
//					return;
//				}
//			}
//			else if (touch_state.y < 280)
//			{
//				// save
//				event.data1 = KEY_F2;
//			}
//			else if (touch_state.y < 320)
//			{
//				// load
//				event.data1 = KEY_F3;
//			}
//		}
//		else
//		{
//			// movement/menu navigation
//			if (touch_state.x < 100)
//			{
//				if (touch_state.y < 100)
//				{
//					event.data1 = KEY_STRAFE_L;
//				}
//				else if (touch_state.y < 220)
//				{
//					event.data1 = KEY_DOWNARROW;
//				}
//				else
//				{
//					event.data1 = KEY_STRAFE_R;
//				}
//			}
//			else if (touch_state.x < 180)
//			{
//				if (touch_state.y < 160)
//				{
//					event.data1 = KEY_LEFTARROW;
//				}
//				else
//				{
//					event.data1 = KEY_RIGHTARROW;
//				}
//			}
//			else
//			{
//				event.data1 = KEY_UPARROW;
//			}
//		}
//
//		D_PostEvent (&event);
//	}
}

__attribute__ ((weak)) void I_StartTic (void)
{
	I_GetEvent();
}

void I_UpdateNoBlit (void)
{
}

//
// I_FinishUpdate
//

void I_FinishUpdate (void)
{
    if (!screenvisible) return;

    uint8_t *fb_dest = (uint8_t *)(fb_mmap_ptr != MAP_FAILED ? fb_mmap_ptr : I_VideoBuffer_FB);
    if (!fb_dest) return;

    int bpp = fb.bits_per_pixel;
    int bytes_per_pixel = bpp / 8;

    // Fast-path: 32-bit PS Vita native fullscreen (960x544, exact 3x horizontal scale)
    if (bpp == 32 && render_w == 960 && render_h == 544 && offset_x == 0 && offset_y == 0) {
        uint32_t *d32 = (uint32_t *)fb_dest;
        int prev_src_y = -1;
        uint32_t *prev_row = NULL;

        for (int y = 0; y < 544; y++) {
            int src_y = y_lut[y];
            uint32_t *row = d32 + y * 960;

            if (src_y == prev_src_y && prev_row != NULL) {
                memcpy(row, prev_row, 960 * sizeof(uint32_t));
            } else {
                const byte *src = I_VideoBuffer + src_y * SCREENWIDTH;
                for (int x = 0; x < SCREENWIDTH; x++) {
                    uint32_t pix = palette32[src[x]];
                    row[x * 3]     = pix;
                    row[x * 3 + 1] = pix;
                    row[x * 3 + 2] = pix;
                }
                prev_src_y = src_y;
                prev_row = row;
            }
        }
    } else if (bpp == 32) {
        // General 32-bit blitter (aspect ratio, integer, or custom resolution)
        uint32_t *d32 = (uint32_t *)fb_dest;
        int prev_src_y = -1;
        uint32_t *prev_row = NULL;

        for (int y = 0; y < render_h; y++) {
            int src_y = y_lut[y];
            uint32_t *row = d32 + (y + offset_y) * fb.xres + offset_x;

            if (src_y == prev_src_y && prev_row != NULL) {
                memcpy(row, prev_row, render_w * sizeof(uint32_t));
            } else {
                const byte *src = I_VideoBuffer + src_y * SCREENWIDTH;
                for (int x = 0; x < render_w; x++) {
                    row[x] = palette32[src[x_lut[x]]];
                }
                prev_src_y = src_y;
                prev_row = row;
            }
        }
    } else if (bpp == 16) {
        // 16-bit RGB565 blitter
        uint16_t *d16 = (uint16_t *)fb_dest;
        int prev_src_y = -1;
        uint16_t *prev_row = NULL;

        for (int y = 0; y < render_h; y++) {
            int src_y = y_lut[y];
            uint16_t *row = d16 + (y + offset_y) * fb.xres + offset_x;

            if (src_y == prev_src_y && prev_row != NULL) {
                memcpy(row, prev_row, render_w * sizeof(uint16_t));
            } else {
                const byte *src = I_VideoBuffer + src_y * SCREENWIDTH;
                for (int x = 0; x < render_w; x++) {
                    row[x] = palette16[src[x_lut[x]]];
                }
                prev_src_y = src_y;
                prev_row = row;
            }
        }
    }

    // If mmap was not available, fall back to write()
    if (fb_mmap_ptr == MAP_FAILED && fd_fb >= 0) {
        lseek(fd_fb, 0, SEEK_SET);
        if (write(fd_fb, I_VideoBuffer_FB, fb.xres * fb.yres * bytes_per_pixel) < 0) {
            // Ignored
        }
    }
}

//
// I_ReadScreen
//
void I_ReadScreen (byte* scr)
{
    memcpy (scr, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
}

//
// I_SetPalette
//
#define GFX_RGB565(r, g, b)			((((r & 0xF8) >> 3) << 11) | (((g & 0xFC) >> 2) << 5) | ((b & 0xF8) >> 3))
#define GFX_RGB565_R(color)			((0xF800 & color) >> 11)
#define GFX_RGB565_G(color)			((0x07E0 & color) >> 5)
#define GFX_RGB565_B(color)			(0x001F & color)

void I_SetPalette (byte* palette)
{
    int i;
    for (i = 0; i < 256; ++i) {
        uint8_t r = gammatable[usegamma][*palette++];
        uint8_t g = gammatable[usegamma][*palette++];
        uint8_t b = gammatable[usegamma][*palette++];

        colors[i].a = 0xFF;
        colors[i].r = r;
        colors[i].g = g;
        colors[i].b = b;

        uint32_t r_val = (uint32_t)(r >> (8 - fb.red.length)) << fb.red.offset;
        uint32_t g_val = (uint32_t)(g >> (8 - fb.green.length)) << fb.green.offset;
        uint32_t b_val = (uint32_t)(b >> (8 - fb.blue.length)) << fb.blue.offset;
        uint32_t a_val = 0;
        if (fb.transp.length > 0) {
            a_val = (uint32_t)(0xFF >> (8 - fb.transp.length)) << fb.transp.offset;
        }
        palette32[i] = r_val | g_val | b_val | a_val;
        palette16[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
}

// Given an RGB value, find the closest matching palette index.

int I_GetPaletteIndex (int r, int g, int b)
{
    int best, best_diff, diff;
    int i;
    col_t color;

    printf("I_GetPaletteIndex\n");

    best = 0;
    best_diff = INT_MAX;

    for (i = 0; i < 256; ++i)
    {
        color.r = GFX_RGB565_R(palette16[i]);
        color.g = GFX_RGB565_G(palette16[i]);
        color.b = GFX_RGB565_B(palette16[i]);

        diff = (r - color.r) * (r - color.r)
             + (g - color.g) * (g - color.g)
             + (b - color.b) * (b - color.b);

        if (diff < best_diff)
        {
            best = i;
            best_diff = diff;
        }

        if (diff == 0)
        {
            break;
        }
    }

    return best;
}

void I_BeginRead (void)
{
}

void I_EndRead (void)
{
}

void I_SetWindowTitle (char *title)
{
}

void I_GraphicsCheckCommandLine (void)
{
}

void I_SetGrabMouseCallback (grabmouse_callback_t func)
{
}

void I_EnableLoadingDisk(void)
{
}

void I_BindVideoVariables (void)
{
}

void I_DisplayFPSDots (boolean dots_on)
{
}

void I_CheckIsScreensaver (void)
{
}
