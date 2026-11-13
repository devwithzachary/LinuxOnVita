#include "x11_mini.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define WIN_W 600
#define WIN_H 400
#define MAX_NETS 32
#define VISIBLE_NETS 6

typedef struct {
    char ssid[64];
    char bssid[24];
    char security[16];
    int rssi;
    int pct;
    int active;
} WifiNet;

typedef struct {
    int running;
    int needs_redraw;

    char iface[32];
    char wpa_sock[64];
    char cur_ssid[64];
    char cur_ip[32];
    int cur_rssi;
    int cur_pct;
    int radio_off;
    char msg[96];

    WifiNet nets[MAX_NETS];
    int net_count;
    int selected_net;
    int scroll_offset;

    int pass_modal;
    char target_ssid[64];
    char pass_buf[64];
    int pass_len;
    int pass_show;
} WifiState;

static void detect_iface_and_sock(WifiState *st) {
    struct stat sb;
    if (stat("/sys/class/net/mlan0", &sb) == 0) snprintf(st->iface, sizeof(st->iface), "mlan0");
    else if (stat("/sys/class/net/wlan0", &sb) == 0) snprintf(st->iface, sizeof(st->iface), "wlan0");
    else st->iface[0] = '\0';

    if (stat("/var/run/wpa_supplicant", &sb) == 0) snprintf(st->wpa_sock, sizeof(st->wpa_sock), "/var/run/wpa_supplicant");
    else if (stat("/run/wpa_supplicant", &sb) == 0) snprintf(st->wpa_sock, sizeof(st->wpa_sock), "/run/wpa_supplicant");
    else st->wpa_sock[0] = '\0';
}

static FILE *run_wpa(WifiState *st, const char *args) {
    if (!st->iface[0]) return NULL;
    char cmd[256];
    if (st->wpa_sock[0]) {
        snprintf(cmd, sizeof(cmd), "wpa_cli -p %s -i %s %s 2>/dev/null", st->wpa_sock, st->iface, args);
    } else {
        snprintf(cmd, sizeof(cmd), "wpa_cli -i %s %s 2>/dev/null", st->iface, args);
    }
    return popen(cmd, "r");
}

static void refresh_status(WifiState *st) {
    detect_iface_and_sock(st);
    st->cur_ssid[0] = '\0';
    st->cur_ip[0] = '\0';
    st->cur_rssi = 0;
    st->cur_pct = 0;

    struct stat sb;
    st->radio_off = (stat("/tmp/wifi_disabled", &sb) == 0) ? 1 : 0;
    if (!st->iface[0]) return;

    char ip_cmd[128];
    snprintf(ip_cmd, sizeof(ip_cmd), "ip -4 addr show %s 2>/dev/null | grep -o 'inet [0-9.]*' | cut -d' ' -f2", st->iface);
    FILE *fp = popen(ip_cmd, "r");
    if (fp) {
        if (fgets(st->cur_ip, sizeof(st->cur_ip), fp)) {
            size_t l = strlen(st->cur_ip);
            while (l > 0 && (st->cur_ip[l - 1] == '\n' || st->cur_ip[l - 1] == '\r')) st->cur_ip[--l] = '\0';
        }
        pclose(fp);
    }

    fp = run_wpa(st, "status");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "ssid=", 5) == 0) {
                snprintf(st->cur_ssid, sizeof(st->cur_ssid), "%.63s", line + 5);
                size_t l = strlen(st->cur_ssid);
                while (l > 0 && (st->cur_ssid[l - 1] == '\n' || st->cur_ssid[l - 1] == '\r')) st->cur_ssid[--l] = '\0';
            }
        }
        pclose(fp);
    }

    fp = run_wpa(st, "signal_poll");
    if (fp) {
        char line[128];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "RSSI=", 5) == 0) {
                st->cur_rssi = atoi(line + 5);
                st->cur_pct = (st->cur_rssi + 100) * 2;
                if (st->cur_pct > 100) st->cur_pct = 100;
                if (st->cur_pct < 0) st->cur_pct = 0;
            }
        }
        pclose(fp);
    }
}

static void refresh_scan(WifiState *st, int trigger_scan) {
    int attempts = 1;
    refresh_status(st);
    if (trigger_scan && st->iface[0]) {
        FILE *sfp = run_wpa(st, "scan");
        if (sfp) pclose(sfp);
        attempts = 3;
    }

    for (int try_no = 0; try_no < attempts; try_no++) {
        if (trigger_scan) usleep(1000000);
        st->net_count = 0;
        FILE *fp = run_wpa(st, "scan_results");
        if (!fp) return;

        char line[512];
        int row_no = 0;
        while (fgets(line, sizeof(line), fp) && st->net_count < MAX_NETS) {
            row_no++;
            if (row_no == 1) continue;
            size_t l = strlen(line);
            while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = '\0';

            char *bssid = strtok(line, "\t");
            char *freq  = strtok(NULL, "\t");
            char *sig   = strtok(NULL, "\t");
            char *flags = strtok(NULL, "\t");
            char *ssid  = strtok(NULL, "\t");
            (void)freq;
            if (!bssid || !sig || !flags || !ssid || !*ssid) continue;

            int rssi = atoi(sig);
            int dup = -1;
            for (int i = 0; i < st->net_count; i++) {
                if (strcmp(st->nets[i].ssid, ssid) == 0) { dup = i; break; }
            }
            int idx = (dup >= 0) ? dup : st->net_count;
            if (dup >= 0 && rssi <= st->nets[dup].rssi) continue;
            if (dup < 0) st->net_count++;

            WifiNet *n = &st->nets[idx];
            snprintf(n->ssid, sizeof(n->ssid), "%.63s", ssid);
            snprintf(n->bssid, sizeof(n->bssid), "%.23s", bssid);
            n->rssi = rssi;
            n->pct = (rssi + 100) * 2;
            if (n->pct > 100) n->pct = 100;
            if (n->pct < 0) n->pct = 0;
            n->active = (st->cur_ssid[0] && strcmp(st->cur_ssid, ssid) == 0) ? 1 : 0;

            if (strstr(flags, "WPA3")) snprintf(n->security, sizeof(n->security), "WPA3");
            else if (strstr(flags, "WPA2")) snprintf(n->security, sizeof(n->security), "WPA2");
            else if (strstr(flags, "WPA")) snprintf(n->security, sizeof(n->security), "WPA");
            else if (strstr(flags, "WEP")) snprintf(n->security, sizeof(n->security), "WEP");
            else snprintf(n->security, sizeof(n->security), "Open");
        }
        pclose(fp);
        if (st->net_count > 0) break;
    }

    if (st->selected_net >= st->net_count) st->selected_net = (st->net_count > 0) ? 0 : -1;
    if (st->selected_net < 0 && st->net_count > 0) st->selected_net = 0;
}

static void render(X11Window *w, WifiState *st) {
    x11_clear(w, COL_BG);

    /* Header */
    x11_fill_rect(w, 0, 0, WIN_W, 42, COL_HEADER);
    x11_fill_rect(w, 0, 41, WIN_W, 1, COL_BORDER);
    x11_draw_text(w, 16, 13, "VITA WI-FI MANAGER", COL_CYAN, 2);
    x11_draw_button(w, WIN_W - 84, 6, 70, 30, "CLOSE", COL_CARD, COL_RED, COL_TEXT, 1);

    /* Status Card */
    x11_fill_round_rect(w, 14, 52, WIN_W - 28, 48, 4, COL_CARD);
    x11_draw_rect(w, 14, 52, WIN_W - 28, 48, COL_BORDER);

    char l1[128], l2[128];
    if (!st->iface[0]) {
        x11_draw_text(w, 26, 60, "NO WIRELESS INTERFACE FOUND", COL_RED, 1);
        x11_draw_text(w, 26, 78, "Marvell 88W8787 (mlan0) not detected", COL_TEXT_DIM, 1);
    } else if (st->radio_off) {
        x11_draw_text(w, 26, 60, "STATUS: WI-FI RADIO OFF", COL_RED, 1);
        x11_draw_text(w, 26, 78, "Click 'RADIO ON/OFF' below to enable wireless", COL_TEXT_DIM, 1);
    } else if (st->cur_ssid[0] && st->cur_ip[0]) {
        snprintf(l1, sizeof(l1), "CONNECTED: %s", st->cur_ssid);
        snprintf(l2, sizeof(l2), "IP: %s  |  Signal: %d%% (%d dBm)", st->cur_ip, st->cur_pct, st->cur_rssi);
        x11_draw_text(w, 26, 60, l1, COL_GREEN, 1);
        x11_draw_text(w, 26, 78, l2, COL_TEXT, 1);
    } else if (st->cur_ip[0]) {
        snprintf(l1, sizeof(l1), "CONNECTED (ONLINE)  |  IP: %s", st->cur_ip);
        x11_draw_text(w, 26, 68, l1, COL_GREEN, 1);
    } else if (st->cur_ssid[0]) {
        snprintf(l1, sizeof(l1), "ASSOCIATED: %s (Waiting for IP...)", st->cur_ssid);
        x11_draw_text(w, 26, 68, l1, COL_AMBER, 1);
    } else {
        x11_draw_text(w, 26, 68, "DISCONNECTED / IDLE (Select a network below)", COL_TEXT_DIM, 1);
    }

    /* Network List */
    int list_y = 110, row_h = 36;
    x11_fill_rect(w, 14, list_y, WIN_W - 28, VISIBLE_NETS * row_h + 4, COL_CARD);
    x11_draw_rect(w, 14, list_y, WIN_W - 28, VISIBLE_NETS * row_h + 4, COL_BORDER);

    if (st->net_count == 0) {
        x11_draw_text(w, 26, list_y + 18, st->msg[0] ? st->msg : "No networks found. Click RESCAN.", COL_TEXT_DIM, 1);
    } else {
        for (int v = 0; v < VISIBLE_NETS; v++) {
            int idx = st->scroll_offset + v;
            if (idx >= st->net_count) break;
            WifiNet *n = &st->nets[idx];
            int ry = list_y + 2 + v * row_h;
            int sel = (idx == st->selected_net);
            x11_fill_rect(w, 16, ry, WIN_W - 32, row_h - 2, sel ? COL_CARD_SEL : (v % 2 ? COL_CARD : 0x131925));
            if (sel) x11_draw_rect(w, 16, ry, WIN_W - 32, row_h - 2, COL_CYAN);

            char sd[36], sc[16], sg[28];
            snprintf(sd, sizeof(sd), "%s%.31s", n->active ? "* " : "", n->ssid);
            snprintf(sc, sizeof(sc), "[%s]", n->security);
            snprintf(sg, sizeof(sg), "%3d%% (%ddBm)", n->pct, n->rssi);
            uint32_t scol = (n->pct >= 60) ? COL_GREEN : ((n->pct >= 30) ? COL_AMBER : COL_RED);

            x11_draw_text(w, 26, ry + 13, sd, n->active ? COL_GREEN : COL_TEXT, 1);
            x11_draw_text(w, 340, ry + 13, sc, COL_TEXT_DIM, 1);
            x11_draw_text(w, 430, ry + 13, sg, scol, 1);
        }
    }

    if (st->msg[0]) x11_draw_text(w, 16, 338, st->msg, COL_CYAN, 1);

    /* Action Buttons */
    int by = 356;
    x11_draw_button(w, 14,  by, 134, 34, "CONNECT",      COL_BLUE, COL_CYAN,   COL_TEXT, 1);
    x11_draw_button(w, 158, by, 134, 34, "RESCAN",       COL_CARD, COL_BORDER, COL_TEXT, 1);
    x11_draw_button(w, 302, by, 134, 34, "DISCONNECT",   COL_CARD, COL_BORDER, COL_TEXT, 1);
    x11_draw_button(w, 446, by, 140, 34, "RADIO ON/OFF", COL_CARD, COL_BORDER, COL_TEXT, 1);

    /* Password Modal */
    if (st->pass_modal) {
        int mx = 70, my = 96, mw = 460, mh = 195;
        x11_fill_round_rect(w, mx, my, mw, mh, 6, COL_HEADER);
        x11_draw_rect(w, mx, my, mw, mh, COL_CYAN);

        char ttl[96];
        snprintf(ttl, sizeof(ttl), "PASSWORD: %.40s", st->target_ssid);
        x11_draw_text(w, mx + 18, my + 18, ttl, COL_CYAN, 1);
        x11_draw_text(w, mx + 18, my + 38, "Type passphrase (or use on-screen keyboard):", COL_TEXT_DIM, 1);

        x11_fill_rect(w, mx + 18, my + 60, mw - 36, 36, COL_BG);
        x11_draw_rect(w, mx + 18, my + 60, mw - 36, 36, COL_BLUE);

        char disp[68];
        if (st->pass_show) snprintf(disp, sizeof(disp), "%s_", st->pass_buf);
        else {
            int i;
            for (i = 0; i < st->pass_len && i < 50; i++) disp[i] = '*';
            disp[i++] = '_';
            disp[i] = '\0';
        }
        x11_draw_text(w, mx + 28, my + 74, disp, COL_TEXT, 1);

        x11_draw_button(w, mx + 18,  my + 112, 130, 30, st->pass_show ? "HIDE TEXT" : "SHOW TEXT", COL_CARD, COL_BORDER, COL_TEXT_DIM, 1);
        x11_draw_button(w, mx + 160, my + 112, 130, 30, "BACKSPACE", COL_CARD, COL_BORDER, COL_TEXT_DIM, 1);
        x11_draw_button(w, mx + 18,  my + 150, 200, 34, "CONNECT", COL_BLUE, COL_CYAN, COL_TEXT, 1);
        x11_draw_button(w, mx + 242, my + 150, 200, 34, "CANCEL",  COL_CARD, COL_RED,  COL_TEXT, 1);
    }

    x11_present(w);
}

static void do_connect(X11Window *w, WifiState *st, const char *ssid, const char *pass) {
    snprintf(st->msg, sizeof(st->msg), "Connecting to %s...", ssid);
    render(w, st);
    char cmd[512];
    if (pass && *pass) snprintf(cmd, sizeof(cmd), "/usr/bin/vita-wifi connect \"%s\" \"%s\" >/dev/null 2>&1", ssid, pass);
    else snprintf(cmd, sizeof(cmd), "/usr/bin/vita-wifi connect \"%s\" >/dev/null 2>&1", ssid);
    int rc = system(cmd);
    (void)rc;
    refresh_scan(st, 0);
    if (st->cur_ip[0]) snprintf(st->msg, sizeof(st->msg), "Connected! IP: %s", st->cur_ip);
    else snprintf(st->msg, sizeof(st->msg), "Connection finished. Check password if offline.");
}

int main(void) {
    WifiState st;
    memset(&st, 0, sizeof(st));
    st.running = 1;
    st.selected_net = -1;
    snprintf(st.msg, sizeof(st.msg), "Loading Wi-Fi status...");

    X11Window *w = x11_open(WIN_W, WIN_H, "PlayStation Vita Wi-Fi Manager");
    if (!w) return 1;

    /* Render immediately so window appears instantaneously */
    render(w, &st);
    refresh_scan(&st, 0);
    if (st.net_count == 0 && !st.radio_off && st.iface[0]) {
        snprintf(st.msg, sizeof(st.msg), "Scanning 2.4 GHz networks...");
        render(w, &st);
        refresh_scan(&st, 1);
    }
    snprintf(st.msg, sizeof(st.msg), "Found %d network(s).", st.net_count);
    render(w, &st);

    X11Event ev;
    while (st.running) {
        if (x11_poll_event(w, &ev, 100)) {
            if (ev.type == X11_EV_CLOSE) st.running = 0;
            else if (ev.type == X11_EV_EXPOSE) st.needs_redraw = 1;
            else if (ev.type == X11_EV_MOUSE_DOWN) {
                if (ev.button == 4 && st.scroll_offset > 0) { st.scroll_offset--; st.needs_redraw = 1; }
                else if (ev.button == 5 && st.scroll_offset + VISIBLE_NETS < st.net_count) { st.scroll_offset++; st.needs_redraw = 1; }
                else if (ev.button == 1) {
                    int x = ev.x, y = ev.y;
                    if (st.pass_modal) {
                        int mx = 70, my = 96;
                        if (x11_pt_in_rect(x, y, mx + 18, my + 112, 130, 30)) st.pass_show = !st.pass_show;
                        else if (x11_pt_in_rect(x, y, mx + 160, my + 112, 130, 30)) {
                            if (st.pass_len > 0) st.pass_buf[--st.pass_len] = '\0';
                        } else if (x11_pt_in_rect(x, y, mx + 18, my + 150, 200, 34)) {
                            st.pass_modal = 0;
                            do_connect(w, &st, st.target_ssid, st.pass_buf);
                        } else if (x11_pt_in_rect(x, y, mx + 242, my + 150, 200, 34)) {
                            st.pass_modal = 0;
                        }
                        st.needs_redraw = 1;
                    } else {
                        if (x11_pt_in_rect(x, y, WIN_W - 84, 6, 70, 30)) st.running = 0;
                        else if (x11_pt_in_rect(x, y, 16, 112, WIN_W - 32, VISIBLE_NETS * 36)) {
                            int idx = st.scroll_offset + (y - 112) / 36;
                            if (idx >= 0 && idx < st.net_count) { st.selected_net = idx; st.needs_redraw = 1; }
                        } else if (x11_pt_in_rect(x, y, 14, 356, 134, 34)) {
                            if (st.selected_net >= 0 && st.selected_net < st.net_count) {
                                WifiNet *n = &st.nets[st.selected_net];
                                if (strcmp(n->security, "Open") == 0) do_connect(w, &st, n->ssid, "");
                                else {
                                    snprintf(st.target_ssid, sizeof(st.target_ssid), "%s", n->ssid);
                                    st.pass_buf[0] = '\0';
                                    st.pass_len = 0;
                                    st.pass_modal = 1;
                                }
                                st.needs_redraw = 1;
                            }
                        } else if (x11_pt_in_rect(x, y, 158, 356, 134, 34)) {
                            snprintf(st.msg, sizeof(st.msg), "Scanning 2.4 GHz networks...");
                            render(w, &st);
                            refresh_scan(&st, 1);
                            snprintf(st.msg, sizeof(st.msg), "Found %d network(s).", st.net_count);
                            st.needs_redraw = 1;
                        } else if (x11_pt_in_rect(x, y, 302, 356, 134, 34)) {
                            int rc = system("/usr/bin/vita-wifi disconnect >/dev/null 2>&1");
                            (void)rc;
                            refresh_scan(&st, 0);
                            snprintf(st.msg, sizeof(st.msg), "Disconnected from Wi-Fi.");
                            st.needs_redraw = 1;
                        } else if (x11_pt_in_rect(x, y, 446, 356, 140, 34)) {
                            int rc = system("/usr/bin/vita-wifi toggle >/dev/null 2>&1");
                            (void)rc;
                            refresh_scan(&st, 0);
                            snprintf(st.msg, sizeof(st.msg), st.radio_off ? "Wi-Fi Radio OFF." : "Wi-Fi Radio ON.");
                            st.needs_redraw = 1;
                        }
                    }
                }
            } else if (ev.type == X11_EV_KEY) {
                if (st.pass_modal) {
                    if (ev.keysym == 0xff1b) st.pass_modal = 0;
                    else if (ev.keysym == 0xff08) { if (st.pass_len > 0) st.pass_buf[--st.pass_len] = '\0'; }
                    else if (ev.keysym == 0xff0d) { st.pass_modal = 0; do_connect(w, &st, st.target_ssid, st.pass_buf); }
                    else if (ev.ch && st.pass_len < (int)sizeof(st.pass_buf) - 1) {
                        st.pass_buf[st.pass_len++] = ev.ch;
                        st.pass_buf[st.pass_len] = '\0';
                    }
                    st.needs_redraw = 1;
                } else if (ev.keysym == 0xff1b) {
                    st.running = 0;
                }
            }
        }
        if (st.needs_redraw && st.running) {
            st.needs_redraw = 0;
            render(w, &st);
        }
    }

    x11_close(w);
    return 0;
}
