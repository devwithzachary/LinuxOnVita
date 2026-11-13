#include "x11_mini.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

extern unsigned char psvDebugScreenFont[];

static int write_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, p + sent, len - sent);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static int read_all(int fd, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    size_t recvd = 0;
    while (recvd < len) {
        ssize_t n = read(fd, p + recvd, len - recvd);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return -1;
        }
        recvd += (size_t)n;
    }
    return 0;
}

static uint16_t read_u16_be(const uint8_t *b) {
    return (uint16_t)((b[0] << 8) | b[1]);
}

/* Read MIT-MAGIC-COOKIE-1 from XAUTHORITY or ~/.Xauthority if available */
static int load_xauthority(const char *dpy_str, char *auth_name, size_t max_name,
                           uint8_t *auth_data, size_t max_data, size_t *out_data_len) {
    char path[512];
    const char *xauth = getenv("XAUTHORITY");
    if (xauth && *xauth) {
        snprintf(path, sizeof(path), "%s", xauth);
    } else {
        const char *home = getenv("HOME");
        if (!home) home = "/root";
        snprintf(path, sizeof(path), "%s/.Xauthority", home);
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    int found = -1;
    uint8_t hdr[2];
    while (fread(hdr, 1, 2, fp) == 2) {
        uint16_t addr_len = 0, num_len = 0, name_len = 0, data_len = 0;
        uint8_t len_buf[2];
        char addr[256] = {0}, num[64] = {0}, name[128] = {0};
        uint8_t data[256] = {0};

        if (fread(len_buf, 1, 2, fp) != 2) break;
        addr_len = read_u16_be(len_buf);
        if (addr_len >= sizeof(addr)) break;
        if (addr_len > 0 && fread(addr, 1, addr_len, fp) != addr_len) break;

        if (fread(len_buf, 1, 2, fp) != 2) break;
        num_len = read_u16_be(len_buf);
        if (num_len >= sizeof(num)) break;
        if (num_len > 0 && fread(num, 1, num_len, fp) != num_len) break;

        if (fread(len_buf, 1, 2, fp) != 2) break;
        name_len = read_u16_be(len_buf);
        if (name_len >= sizeof(name)) break;
        if (name_len > 0 && fread(name, 1, name_len, fp) != name_len) break;

        if (fread(len_buf, 1, 2, fp) != 2) break;
        data_len = read_u16_be(len_buf);
        if (data_len >= sizeof(data)) break;
        if (data_len > 0 && fread(data, 1, data_len, fp) != data_len) break;

        if (strcmp(num, dpy_str) == 0 && name_len < max_name && data_len <= max_data) {
            memcpy(auth_name, name, name_len);
            auth_name[name_len] = '\0';
            memcpy(auth_data, data, data_len);
            *out_data_len = data_len;
            found = 0;
            break;
        }
    }
    fclose(fp);
    return found;
}

static uint32_t x11_intern_atom(X11Window *w, const char *name) {
    size_t nlen = strlen(name);
    size_t pad_len = (nlen + 3) & ~3u;
    size_t req_len = 8 + pad_len;
    uint8_t req[256];
    if (req_len > sizeof(req)) return 0;
    memset(req, 0, req_len);

    req[0] = 16; /* InternAtom */
    req[1] = 0;  /* only_if_exists = False */
    uint16_t words = (uint16_t)(req_len / 4);
    uint16_t nl = (uint16_t)nlen;
    memcpy(req + 2, &words, 2);
    memcpy(req + 4, &nl, 2);
    memcpy(req + 8, name, nlen);

    if (write_all(w->fd, req, req_len) < 0) return 0;

    uint8_t reply[32];
    while (read_all(w->fd, reply, 32) == 0) {
        if (reply[0] == 1) {
            uint32_t atom = 0;
            memcpy(&atom, reply + 8, 4);
            return atom;
        }
        if (reply[0] == 0) return 0; /* Error */
    }
    return 0;
}

static void x11_change_prop(X11Window *w, uint32_t prop, uint32_t type,
                            uint8_t format, const void *data, uint32_t num_elems) {
    size_t data_bytes = (size_t)num_elems * (format / 8);
    size_t pad_bytes = (data_bytes + 3) & ~3u;
    size_t req_len = 24 + pad_bytes;
    uint8_t *req = calloc(1, req_len);
    if (!req) return;

    req[0] = 18; /* ChangeProperty */
    req[1] = 0;  /* Replace */
    uint16_t words = (uint16_t)(req_len / 4);
    memcpy(req + 2, &words, 2);
    memcpy(req + 4, &w->win, 4);
    memcpy(req + 8, &prop, 4);
    memcpy(req + 12, &type, 4);
    req[16] = format;
    memcpy(req + 20, &num_elems, 4);
    memcpy(req + 24, data, data_bytes);

    write_all(w->fd, req, req_len);
    free(req);
}

static void x11_load_keymap(X11Window *w) {
    if (w->max_keycode < w->min_keycode) return;
    uint8_t count = (uint8_t)(w->max_keycode - w->min_keycode + 1);
    uint8_t req[8] = {0};
    req[0] = 101; /* GetKeyboardMapping */
    uint16_t words = 2;
    memcpy(req + 2, &words, 2);
    req[4] = w->min_keycode;
    req[5] = count;

    if (write_all(w->fd, req, 8) < 0) return;

    uint8_t reply[32];
    while (read_all(w->fd, reply, 32) == 0) {
        if (reply[0] == 1) {
            uint32_t extra_words = 0;
            memcpy(&extra_words, reply + 4, 4);
            w->keysyms_per_kc = reply[1];
            if (extra_words > 0 && extra_words < 65536) {
                w->keymap = malloc((size_t)extra_words * 4);
                if (w->keymap) {
                    if (read_all(w->fd, w->keymap, (size_t)extra_words * 4) < 0) {
                        free(w->keymap);
                        w->keymap = NULL;
                    }
                }
            }
            return;
        }
        if (reply[0] == 0) return;
    }
}

X11Window *x11_open(int width, int height, const char *title) {
    const char *dpy_env = getenv("DISPLAY");
    if (!dpy_env || !*dpy_env) dpy_env = ":0";

    const char *colon = strchr(dpy_env, ':');
    int dpy_num = colon ? atoi(colon + 1) : 0;
    char dpy_str[16];
    snprintf(dpy_str, sizeof(dpy_str), "%d", dpy_num);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return NULL;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "/tmp/.X11-unix/X%d", dpy_num);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return NULL;
    }

    char auth_name[128] = {0};
    uint8_t auth_data[256] = {0};
    size_t auth_data_len = 0;
    if (load_xauthority(dpy_str, auth_name, sizeof(auth_name),
                        auth_data, sizeof(auth_data), &auth_data_len) < 0) {
        auth_name[0] = '\0';
        auth_data_len = 0;
    }

    size_t auth_name_len = strlen(auth_name);
    size_t pad_an = (auth_name_len + 3) & ~3u;
    size_t pad_ad = (auth_data_len + 3) & ~3u;
    size_t setup_len = 12 + pad_an + pad_ad;
    uint8_t *setup_req = calloc(1, setup_len);
    if (!setup_req) {
        close(fd);
        return NULL;
    }

    setup_req[0] = 'l'; /* Little-endian */
    uint16_t maj = 11, min = 0;
    uint16_t anl = (uint16_t)auth_name_len;
    uint16_t adl = (uint16_t)auth_data_len;
    memcpy(setup_req + 2, &maj, 2);
    memcpy(setup_req + 4, &min, 2);
    memcpy(setup_req + 6, &anl, 2);
    memcpy(setup_req + 8, &adl, 2);
    if (auth_name_len > 0) memcpy(setup_req + 12, auth_name, auth_name_len);
    if (auth_data_len > 0) memcpy(setup_req + 12 + pad_an, auth_data, auth_data_len);

    if (write_all(fd, setup_req, setup_len) < 0) {
        free(setup_req);
        close(fd);
        return NULL;
    }
    free(setup_req);

    uint8_t hdr[8];
    if (read_all(fd, hdr, 8) < 0 || hdr[0] != 1) {
        close(fd);
        return NULL;
    }

    uint16_t extra_words = 0;
    memcpy(&extra_words, hdr + 6, 2);
    size_t body_len = (size_t)extra_words * 4;
    uint8_t *body = malloc(body_len);
    if (!body || read_all(fd, body, body_len) < 0) {
        free(body);
        close(fd);
        return NULL;
    }

    X11Window *w = calloc(1, sizeof(X11Window));
    if (!w) {
        free(body);
        close(fd);
        return NULL;
    }
    w->fd = fd;
    w->width = width;
    w->height = height;

    memcpy(&w->rid_base, body + 4, 4);
    uint16_t vendor_len = 0, max_req_words = 0;
    memcpy(&vendor_len, body + 16, 2);
    memcpy(&max_req_words, body + 18, 2);
    w->max_req_bytes = (uint32_t)max_req_words * 4;
    uint8_t num_formats = body[21];
    w->min_keycode = body[26];
    w->max_keycode = body[27];

    size_t pad_vendor = (vendor_len + 3) & ~3u;
    size_t fmt_off = 32 + pad_vendor;
    size_t scr_off = fmt_off + (size_t)num_formats * 8;
    if (scr_off + 40 > body_len) {
        free(body);
        free(w);
        close(fd);
        return NULL;
    }

    const uint8_t *scr = body + scr_off;
    memcpy(&w->root_win, scr + 0, 4);
    memcpy(&w->screen_w, scr + 20, 2);
    memcpy(&w->screen_h, scr + 22, 2);
    memcpy(&w->root_visual, scr + 32, 4);
    w->root_depth = scr[38];

    /* Determine bits_per_pixel for root_depth from FORMAT records */
    w->bpp = 32;
    for (uint8_t i = 0; i < num_formats; i++) {
        const uint8_t *fmt = body + fmt_off + i * 8;
        if (fmt[0] == w->root_depth) {
            w->bpp = fmt[1];
            break;
        }
    }
    free(body);

    w->fb = calloc((size_t)width * height, sizeof(uint32_t));
    w->xmit_buf = malloc((size_t)width * 64 * 4);
    if (!w->fb || !w->xmit_buf) {
        x11_close(w);
        return NULL;
    }

    w->win = w->rid_base | 1u;
    w->gc  = w->rid_base | 2u;

    /* CreateWindow (opcode 1) */
    {
        int16_t wx = (int16_t)((w->screen_w > width) ? (w->screen_w - width) / 2 : 0);
        int16_t wy = (int16_t)((w->screen_h > height) ? (w->screen_h - height) / 2 : 0);
        uint32_t mask = 0x0002u | 0x0800u; /* CWBackPixel | CWEventMask */
        uint32_t bg = COL_BG;
        uint32_t evmask = (1u << 15) | (1u << 0) | (1u << 2) | (1u << 3) | (1u << 6);

        uint8_t req[40] = {0};
        req[0] = 1; /* CreateWindow */
        req[1] = w->root_depth;
        uint16_t words = 10;
        uint16_t ww = (uint16_t)width, wh = (uint16_t)height, cls = 1;
        memcpy(req + 2, &words, 2);
        memcpy(req + 4, &w->win, 4);
        memcpy(req + 8, &w->root_win, 4);
        memcpy(req + 12, &wx, 2);
        memcpy(req + 14, &wy, 2);
        memcpy(req + 16, &ww, 2);
        memcpy(req + 18, &wh, 2);
        memcpy(req + 22, &cls, 2);
        memcpy(req + 24, &w->root_visual, 4);
        memcpy(req + 28, &mask, 4);
        memcpy(req + 32, &bg, 4);
        memcpy(req + 36, &evmask, 4);
        write_all(w->fd, req, 40);
    }

    /* Set WM_NAME (atom 39, type STRING = 31) */
    if (title) {
        x11_change_prop(w, 39, 31, 8, title, (uint32_t)strlen(title));
    }

    /* Register WM_DELETE_WINDOW */
    w->atom_wm_protocols = x11_intern_atom(w, "WM_PROTOCOLS");
    w->atom_wm_delete = x11_intern_atom(w, "WM_DELETE_WINDOW");
    if (w->atom_wm_protocols && w->atom_wm_delete) {
        x11_change_prop(w, w->atom_wm_protocols, 4 /* ATOM */, 32, &w->atom_wm_delete, 1);
    }

    /* Load keyboard mapping for text input */
    x11_load_keymap(w);

    /* CreateGC (opcode 55) */
    {
        uint8_t req[16] = {0};
        req[0] = 55;
        uint16_t words = 4;
        memcpy(req + 2, &words, 2);
        memcpy(req + 4, &w->gc, 4);
        memcpy(req + 8, &w->win, 4);
        write_all(w->fd, req, 16);
    }

    /* MapWindow (opcode 8) */
    {
        uint8_t req[8] = {0};
        req[0] = 8;
        uint16_t words = 2;
        memcpy(req + 2, &words, 2);
        memcpy(req + 4, &w->win, 4);
        write_all(w->fd, req, 8);
    }

    return w;
}

void x11_close(X11Window *w) {
    if (!w) return;
    if (w->fd >= 0) close(w->fd);
    free(w->fb);
    free(w->xmit_buf);
    free(w->keymap);
    free(w);
}

void x11_present(X11Window *w) {
    if (!w || !w->fb) return;

    uint32_t bytes_per_px = (w->bpp == 16) ? 2u : 4u;
    uint32_t row_bytes = (uint32_t)w->width * bytes_per_px;
    uint32_t max_payload = (w->max_req_bytes > 256) ? (w->max_req_bytes - 64u) : 65000u;
    int strip_max = (int)(max_payload / row_bytes);
    if (strip_max > 64) strip_max = 64;
    if (strip_max < 1) strip_max = 1;

    for (int y = 0; y < w->height; y += strip_max) {
        int sh = (y + strip_max <= w->height) ? strip_max : (w->height - y);
        size_t data_bytes = (size_t)w->width * sh * bytes_per_px;
        size_t pad_bytes = (data_bytes + 3) & ~3u;
        size_t req_len = 24 + pad_bytes;

        if (w->bpp == 16) {
            uint16_t *dst = (uint16_t *)w->xmit_buf;
            const uint32_t *src = w->fb + y * w->width;
            size_t total = (size_t)w->width * sh;
            for (size_t i = 0; i < total; i++) {
                uint32_t c = src[i];
                uint16_t r = (uint16_t)((c >> 19) & 0x1F);
                uint16_t g = (uint16_t)((c >> 10) & 0x3F);
                uint16_t b = (uint16_t)((c >> 3) & 0x1F);
                dst[i] = (uint16_t)((r << 11) | (g << 5) | b);
            }
        } else {
            memcpy(w->xmit_buf, w->fb + y * w->width, data_bytes);
        }

        uint8_t hdr[24] = {0};
        hdr[0] = 72; /* PutImage */
        hdr[1] = 2;  /* ZPixmap */
        uint16_t words = (uint16_t)(req_len / 4);
        uint16_t ww = (uint16_t)w->width;
        uint16_t hh = (uint16_t)sh;
        int16_t dx = 0, dy = (int16_t)y;
        memcpy(hdr + 2, &words, 2);
        memcpy(hdr + 4, &w->win, 4);
        memcpy(hdr + 8, &w->gc, 4);
        memcpy(hdr + 12, &ww, 2);
        memcpy(hdr + 14, &hh, 2);
        memcpy(hdr + 16, &dx, 2);
        memcpy(hdr + 18, &dy, 2);
        hdr[20] = 0;
        hdr[21] = w->root_depth;

        write_all(w->fd, hdr, 24);
        write_all(w->fd, w->xmit_buf, pad_bytes);
    }
}

int x11_poll_event(X11Window *w, X11Event *ev, int timeout_ms) {
    memset(ev, 0, sizeof(*ev));
    struct pollfd pfd = { .fd = w->fd, .events = POLLIN, .revents = 0 };
    int pr = poll(&pfd, 1, timeout_ms);
    if (pr <= 0) return 0;

    uint8_t pkt[32];
    if (read_all(w->fd, pkt, 32) < 0) {
        ev->type = X11_EV_CLOSE;
        return 1;
    }

    uint8_t type = pkt[0] & 0x7Fu;
    if (type == 12) { /* Expose */
        uint16_t count = 0;
        memcpy(&count, pkt + 16, 2);
        if (count == 0) {
            ev->type = X11_EV_EXPOSE;
            return 1;
        }
    } else if (type == 4 || type == 5) { /* ButtonPress / ButtonRelease */
        int16_t ex = 0, ey = 0;
        memcpy(&ex, pkt + 24, 2);
        memcpy(&ey, pkt + 26, 2);
        ev->type = (type == 4) ? X11_EV_MOUSE_DOWN : X11_EV_MOUSE_UP;
        ev->button = pkt[1];
        ev->x = ex;
        ev->y = ey;
        return 1;
    } else if (type == 6) { /* MotionNotify */
        int16_t ex = 0, ey = 0;
        memcpy(&ex, pkt + 24, 2);
        memcpy(&ey, pkt + 26, 2);
        ev->type = X11_EV_MOUSE_MOVE;
        ev->x = ex;
        ev->y = ey;
        return 1;
    } else if (type == 2) { /* KeyPress */
        ev->type = X11_EV_KEY;
        uint8_t kc = pkt[1];
        uint16_t state = 0;
        memcpy(&state, pkt + 28, 2);
        int shift = (state & 1u) ? 1 : 0;
        if (w->keymap && kc >= w->min_keycode && kc <= w->max_keycode) {
            int idx = (kc - w->min_keycode) * w->keysyms_per_kc;
            uint32_t ks = w->keymap[idx + (shift && w->keysyms_per_kc > 1 ? 1 : 0)];
            if (ks == 0 && shift) ks = w->keymap[idx];
            ev->keysym = ks;
            if (ks >= 0x20 && ks <= 0x7e) {
                ev->ch = (char)ks;
            }
        }
        return 1;
    } else if (type == 33) { /* ClientMessage */
        uint32_t atom = 0;
        memcpy(&atom, pkt + 12, 4);
        if (atom == w->atom_wm_delete) {
            ev->type = X11_EV_CLOSE;
            return 1;
        }
    }
    return 1;
}

void x11_clear(X11Window *w, uint32_t color) {
    size_t total = (size_t)w->width * w->height;
    for (size_t i = 0; i < total; i++) {
        w->fb[i] = color;
    }
}

void x11_fill_rect(X11Window *w, int x, int y, int rw, int rh, uint32_t color) {
    int x0 = (x < 0) ? 0 : x;
    int y0 = (y < 0) ? 0 : y;
    int x1 = (x + rw > w->width) ? w->width : x + rw;
    int y1 = (y + rh > w->height) ? w->height : y + rh;
    for (int py = y0; py < y1; py++) {
        uint32_t *row = w->fb + py * w->width;
        for (int px = x0; px < x1; px++) {
            row[px] = color;
        }
    }
}

void x11_draw_rect(X11Window *w, int x, int y, int rw, int rh, uint32_t color) {
    x11_fill_rect(w, x, y, rw, 1, color);
    x11_fill_rect(w, x, y + rh - 1, rw, 1, color);
    x11_fill_rect(w, x, y, 1, rh, color);
    x11_fill_rect(w, x + rw - 1, y, 1, rh, color);
}

void x11_fill_round_rect(X11Window *w, int x, int y, int rw, int rh, int r, uint32_t color) {
    if (r <= 1 || rw < 4 || rh < 4) {
        x11_fill_rect(w, x, y, rw, rh, color);
        return;
    }
    x11_fill_rect(w, x + r, y, rw - 2 * r, rh, color);
    x11_fill_rect(w, x, y + r, r, rh - 2 * r, color);
    x11_fill_rect(w, x + rw - r, y + r, r, rh - 2 * r, color);
    for (int dy = 0; dy < r; dy++) {
        for (int dx = 0; dx < r; dx++) {
            int ox = r - 1 - dx;
            int oy = r - 1 - dy;
            if (ox * ox + oy * oy <= r * r) {
                x11_fill_rect(w, x + dx, y + dy, 1, 1, color);
                x11_fill_rect(w, x + rw - 1 - dx, y + dy, 1, 1, color);
                x11_fill_rect(w, x + dx, y + rh - 1 - dy, 1, 1, color);
                x11_fill_rect(w, x + rw - 1 - dx, y + rh - 1 - dy, 1, 1, color);
            }
        }
    }
}

void x11_draw_round_rect(X11Window *w, int x, int y, int rw, int rh, int r, uint32_t color) {
    (void)r;
    x11_draw_rect(w, x, y, rw, rh, color);
}

void x11_draw_text(X11Window *w, int x, int y, const char *str, uint32_t color, int scale) {
    if (!str || scale < 1) return;
    int cx = x;
    while (*str) {
        unsigned char c = (unsigned char)*str++;
        if (c == '\n') {
            y += 10 * scale;
            cx = x;
            continue;
        }
        if (c >= 128) c = '?';
        const unsigned char *glyph = &psvDebugScreenFont[c * 8];
        for (int row = 0; row < 8; row++) {
            unsigned char bits = glyph[row];
            for (int col = 0; col < 8; col++) {
                if (bits & (0x80 >> col)) {
                    x11_fill_rect(w, cx + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        cx += 8 * scale;
    }
}

int x11_text_width(const char *str, int scale) {
    if (!str) return 0;
    return (int)strlen(str) * 8 * scale;
}

int x11_pt_in_rect(int px, int py, int rx, int ry, int rw, int rh) {
    return (px >= rx && px < rx + rw && py >= ry && py < ry + rh);
}

void x11_draw_button(X11Window *w, int x, int y, int bw, int bh, const char *label,
                     uint32_t bg, uint32_t border, uint32_t fg, int scale) {
    x11_fill_round_rect(w, x, y, bw, bh, 4, bg);
    x11_draw_rect(w, x, y, bw, bh, border);
    int tw = x11_text_width(label, scale);
    int th = 8 * scale;
    int tx = x + (bw - tw) / 2;
    int ty = y + (bh - th) / 2;
    x11_draw_text(w, tx, ty, label, fg, scale);
}

