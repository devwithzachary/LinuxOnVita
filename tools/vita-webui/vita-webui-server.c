/*
 * vita-webui-server: Lightweight Web Management Portal & Wireless File Transfer
 * PlayStation Vita Linux 6.12
 *
 * Provides a self-contained, zero-dependency embedded HTTP server on port 8080:
 *   - Live System Telemetry Dashboard (CPU, RAM, ZRAM, Storage, Wi-Fi, Bluetooth)
 *   - Drag-and-Drop Wireless File Manager (Upload, Download, Delete, Mkdir on ux0)
 *   - Quick Handheld Control Actions (CPU scaling, display sleep, LED triggers, reboot)
 *   - In-Browser Web Terminal / Command Runner
 *
 * Licensed under the GNU General Public License v3.0 (GPL-3.0)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/statvfs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>

#define DEFAULT_PORT 8080
#define DEFAULT_DOCROOT "/mnt/ux0"
#define FALLBACK_DOCROOT "/"
#define BUFFER_SIZE 16384
#define MAX_PATH_LEN 1024
#define SERVER_NAME "vita-webui/1.6.0"

static char g_docroot[MAX_PATH_LEN] = DEFAULT_DOCROOT;
static int g_port = DEFAULT_PORT;

/* -------------------------------------------------------------------------- */
/* Signal & Process Helpers                                                   */
/* -------------------------------------------------------------------------- */

static void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

/* URL-decode string in-place */
static void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a' - 'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a' - 'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16 * a + b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/* Sanitize path against directory traversal */
static void sanitize_path(char *dst, size_t dst_size, const char *base, const char *req_path) {
    char cleaned[MAX_PATH_LEN / 2];
    const char *p = req_path;
    int is_abs = (*p == '/');

    /* Copy while rejecting ../ */
    size_t i = 0;
    while (*p && i < sizeof(cleaned) - 1) {
        if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0')) {
            p += (p[2] == '/') ? 3 : 2;
            continue;
        }
        cleaned[i++] = *p++;
    }
    cleaned[i] = '\0';

    if (cleaned[0] == '\0') {
        snprintf(dst, dst_size, "%.511s", base);
    } else if (is_abs) {
        snprintf(dst, dst_size, "%.1023s", cleaned);
    } else {
        snprintf(dst, dst_size, "%.511s/%.511s", base, cleaned);
    }
}

/* Check if storage is mounted read-only */
static int is_ux0_readonly(void) {
    FILE *fp = fopen("/proc/mounts", "r");
    if (!fp) return 0;
    char line[512];
    int is_ro = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, " /mnt/ux0 ") != NULL) {
            if (strstr(line, " ro,") != NULL || strstr(line, " ro ") != NULL || strstr(line, ",ro,") != NULL || strstr(line, ",ro ") != NULL) {
                is_ro = 1;
            }
            break;
        }
    }
    fclose(fp);
    return is_ro;
}

/* -------------------------------------------------------------------------- */
/* HTTP Response Helpers                                                      */
/* -------------------------------------------------------------------------- */

static void send_response(int fd, int code, const char *status, const char *content_type, const char *body, size_t body_len) {
    char header[1024];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Server: %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n",
        code, status, SERVER_NAME, content_type, body_len);
    write(fd, header, hlen);
    if (body && body_len > 0) {
        write(fd, body, body_len);
    }
}

static void send_json(int fd, int code, const char *json) {
    send_response(fd, code, code == 200 ? "OK" : "Bad Request", "application/json; charset=utf-8", json, strlen(json));
}

static void send_error(int fd, int code, const char *msg) {
    char json[512];
    snprintf(json, sizeof(json), "{\"error\":\"%s\",\"code\":%d}\n", msg, code);
    send_json(fd, code, json);
}

/* -------------------------------------------------------------------------- */
/* Embedded Web UI Frontend (HTML/CSS/JS)                                     */
/* -------------------------------------------------------------------------- */

static const char HTML_INDEX[] =
"<!DOCTYPE html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"<meta charset=\"UTF-8\">\n"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"<title>PlayStation Vita Linux - Web Management Portal</title>\n"
"<style>\n"
":root {\n"
"  --bg: #0b0e14;\n"
"  --card-bg: #141a24;\n"
"  --card-border: #1f2838;\n"
"  --text: #e2e8f0;\n"
"  --text-muted: #8899a6;\n"
"  --cyan: #00d2ff;\n"
"  --blue: #0070d1;\n"
"  --green: #10b981;\n"
"  --yellow: #f59e0b;\n"
"  --red: #ef4444;\n"
"  --hover: #1e2636;\n"
"}\n"
"* { box-sizing: border-box; margin: 0; padding: 0; }\n"
"body {\n"
"  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;\n"
"  background-color: var(--bg);\n"
"  color: var(--text);\n"
"  line-height: 1.5;\n"
"  padding-bottom: 40px;\n"
"}\n"
"header {\n"
"  background: linear-gradient(135deg, #101622 0%, #151d2d 100%);\n"
"  border-bottom: 1px solid var(--card-border);\n"
"  padding: 16px 24px;\n"
"  display: flex;\n"
"  flex-wrap: wrap;\n"
"  justify-content: space-between;\n"
"  align-items: center;\n"
"  gap: 12px;\n"
"}\n"
".brand {\n"
"  display: flex;\n"
"  align-items: center;\n"
"  gap: 12px;\n"
"}\n"
".brand-icon {\n"
"  width: 36px;\n"
"  height: 36px;\n"
"  background: linear-gradient(135deg, var(--blue), var(--cyan));\n"
"  border-radius: 8px;\n"
"  display: flex;\n"
"  align-items: center;\n"
"  justify-content: center;\n"
"  font-weight: bold;\n"
"  font-size: 18px;\n"
"  color: #fff;\n"
"}\n"
".brand-title h1 { font-size: 1.15rem; font-weight: 700; color: #fff; letter-spacing: -0.02em; }\n"
".brand-title p { font-size: 0.8rem; color: var(--text-muted); }\n"
".badges { display: flex; gap: 8px; flex-wrap: wrap; }\n"
".badge {\n"
"  background: var(--card-bg);\n"
"  border: 1px solid var(--card-border);\n"
"  padding: 4px 10px;\n"
"  border-radius: 999px;\n"
"  font-size: 0.75rem;\n"
"  font-weight: 500;\n"
"  display: flex;\n"
"  align-items: center;\n"
"  gap: 6px;\n"
"}\n"
".dot { width: 8px; height: 8px; border-radius: 50%; background: var(--green); }\n"
"nav {\n"
"  display: flex;\n"
"  background: #111622;\n"
"  border-bottom: 1px solid var(--card-border);\n"
"  padding: 0 24px;\n"
"  gap: 4px;\n"
"}\n"
".tab-btn {\n"
"  background: none;\n"
"  border: none;\n"
"  color: var(--text-muted);\n"
"  font-size: 0.9rem;\n"
"  font-weight: 600;\n"
"  padding: 12px 18px;\n"
"  cursor: pointer;\n"
"  border-bottom: 2px solid transparent;\n"
"  transition: all 0.2s;\n"
"}\n"
".tab-btn:hover { color: #fff; background: rgba(255,255,255,0.02); }\n"
".tab-btn.active { color: var(--cyan); border-bottom-color: var(--cyan); }\n"
".container { max-width: 1200px; margin: 24px auto; padding: 0 16px; }\n"
".tab-content { display: none; }\n"
".tab-content.active { display: block; }\n"
".grid {\n"
"  display: grid;\n"
"  grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));\n"
"  gap: 16px;\n"
"  margin-bottom: 24px;\n"
"}\n"
".card {\n"
"  background: var(--card-bg);\n"
"  border: 1px solid var(--card-border);\n"
"  border-radius: 12px;\n"
"  padding: 18px;\n"
"}\n"
".card-title {\n"
"  font-size: 0.8rem;\n"
"  font-weight: 700;\n"
"  text-transform: uppercase;\n"
"  letter-spacing: 0.05em;\n"
"  color: var(--text-muted);\n"
"  margin-bottom: 10px;\n"
"  display: flex;\n"
"  justify-content: space-between;\n"
"  align-items: center;\n"
"}\n"
".card-value {\n"
"  font-size: 1.4rem;\n"
"  font-weight: 700;\n"
"  color: #fff;\n"
"  margin-bottom: 8px;\n"
"}\n"
".progress-bar {\n"
"  width: 100%;\n"
"  height: 6px;\n"
"  background: #232c3d;\n"
"  border-radius: 999px;\n"
"  overflow: hidden;\n"
"  margin: 8px 0;\n"
"}\n"
".progress-fill {\n"
"  height: 100%;\n"
"  background: linear-gradient(90deg, var(--blue), var(--cyan));\n"
"  border-radius: 999px;\n"
"  transition: width 0.3s ease;\n"
"}\n"
".card-sub {\n"
"  font-size: 0.8rem;\n"
"  color: var(--text-muted);\n"
"}\n"
".actions-grid {\n"
"  display: grid;\n"
"  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));\n"
"  gap: 12px;\n"
"}\n"
".btn {\n"
"  background: #1b2434;\n"
"  color: #fff;\n"
"  border: 1px solid var(--card-border);\n"
"  padding: 10px 14px;\n"
"  border-radius: 8px;\n"
"  font-size: 0.85rem;\n"
"  font-weight: 600;\n"
"  cursor: pointer;\n"
"  display: inline-flex;\n"
"  align-items: center;\n"
"  justify-content: center;\n"
"  gap: 8px;\n"
"  transition: all 0.2s;\n"
"}\n"
".btn:hover { background: #26334a; border-color: #3b4d6e; }\n"
".btn-primary { background: var(--blue); border-color: var(--blue); }\n"
".btn-primary:hover { background: #0080f0; border-color: #0080f0; }\n"
".btn-danger { background: rgba(239, 68, 68, 0.15); border-color: rgba(239, 68, 68, 0.4); color: #fca5a5; }\n"
".btn-danger:hover { background: rgba(239, 68, 68, 0.25); }\n"
".btn-sm { padding: 4px 8px; font-size: 0.75rem; border-radius: 6px; }\n"
".dropzone {\n"
"  border: 2px dashed #2d3748;\n"
"  border-radius: 12px;\n"
"  padding: 32px 20px;\n"
"  text-align: center;\n"
"  background: rgba(20, 26, 36, 0.4);\n"
"  cursor: pointer;\n"
"  transition: all 0.2s;\n"
"  margin-bottom: 20px;\n"
"}\n"
".dropzone.dragover {\n"
"  border-color: var(--cyan);\n"
"  background: rgba(0, 210, 255, 0.05);\n"
"}\n"
".dropzone-icon { font-size: 32px; margin-bottom: 8px; color: var(--cyan); }\n"
".file-table {\n"
"  width: 100%;\n"
"  border-collapse: collapse;\n"
"  text-align: left;\n"
"  font-size: 0.85rem;\n"
"}\n"
".file-table th {\n"
"  padding: 10px 12px;\n"
"  background: #111622;\n"
"  color: var(--text-muted);\n"
"  font-weight: 600;\n"
"  border-bottom: 1px solid var(--card-border);\n"
"}\n"
".file-table td {\n"
"  padding: 10px 12px;\n"
"  border-bottom: 1px solid rgba(255,255,255,0.03);\n"
"}\n"
".file-table tr:hover td { background: var(--hover); }\n"
".file-name {\n"
"  display: flex;\n"
"  align-items: center;\n"
"  gap: 8px;\n"
"  color: #fff;\n"
"  text-decoration: none;\n"
"  font-weight: 500;\n"
"  cursor: pointer;\n"
"}\n"
".breadcrumbs {\n"
"  display: flex;\n"
"  align-items: center;\n"
"  gap: 6px;\n"
"  margin-bottom: 14px;\n"
"  font-size: 0.85rem;\n"
"  color: var(--text-muted);\n"
"  flex-wrap: wrap;\n"
"}\n"
".crumb { color: var(--cyan); text-decoration: none; cursor: pointer; }\n"
".terminal-window {\n"
"  background: #06090e;\n"
"  border: 1px solid var(--card-border);\n"
"  border-radius: 12px;\n"
"  overflow: hidden;\n"
"  display: flex;\n"
"  flex-direction: column;\n"
"  height: 520px;\n"
"}\n"
".term-header {\n"
"  background: #0f141d;\n"
"  padding: 8px 14px;\n"
"  display: flex;\n"
"  align-items: center;\n"
"  justify-content: space-between;\n"
"  border-bottom: 1px solid var(--card-border);\n"
"  font-size: 0.8rem;\n"
"  color: var(--text-muted);\n"
"}\n"
".term-output {\n"
"  flex: 1;\n"
"  padding: 14px;\n"
"  overflow-y: auto;\n"
"  font-family: 'SFMono-Regular', Consolas, 'Liberation Mono', Menlo, monospace;\n"
"  font-size: 0.82rem;\n"
"  color: #e2e8f0;\n"
"  white-space: pre-wrap;\n"
"  word-break: break-all;\n"
"}\n"
".term-input-row {\n"
"  display: flex;\n"
"  background: #0f141d;\n"
"  border-top: 1px solid var(--card-border);\n"
"  padding: 8px 12px;\n"
"  align-items: center;\n"
"  gap: 8px;\n"
"}\n"
".term-prompt { font-family: monospace; font-size: 0.85rem; color: var(--cyan); font-weight: bold; }\n"
".term-input {\n"
"  flex: 1;\n"
"  background: none;\n"
"  border: none;\n"
"  color: #fff;\n"
"  font-family: monospace;\n"
"  font-size: 0.85rem;\n"
"  outline: none;\n"
"}\n"
".term-shortcuts {\n"
"  display: flex;\n"
"  gap: 6px;\n"
"  flex-wrap: wrap;\n"
"  margin-top: 12px;\n"
"}\n"
"#toast {\n"
"  position: fixed;\n"
"  bottom: 24px;\n"
"  right: 24px;\n"
"  background: #1b2434;\n"
"  border: 1px solid var(--card-border);\n"
"  padding: 12px 18px;\n"
"  border-radius: 8px;\n"
"  font-size: 0.85rem;\n"
"  color: #fff;\n"
"  display: none;\n"
"  box-shadow: 0 10px 15px -3px rgba(0,0,0,0.5);\n"
"  z-index: 1000;\n"
"}\n"
"</style>\n"
"</head>\n"
"<body>\n"
"<header>\n"
"  <div class=\"brand\">\n"
"    <div class=\"brand-icon\">V</div>\n"
"    <div class=\"brand-title\">\n"
"      <h1>PlayStation Vita Linux</h1>\n"
"      <p id=\"hdr-model\">Connecting...</p>\n"
"    </div>\n"
"  </div>\n"
"  <div class=\"badges\">\n"
"    <div class=\"badge\"><div class=\"dot\"></div><span id=\"hdr-ip\">127.0.0.1</span></div>\n"
"    <div class=\"badge\"><span id=\"hdr-cpu\">333 MHz</span></div>\n"
"    <div class=\"badge\"><span id=\"hdr-storage\">ux0: RW</span></div>\n"
"  </div>\n"
"</header>\n"
"<nav>\n"
"  <button class=\"tab-btn active\" onclick=\"showTab('dashboard')\">Dashboard</button>\n"
"  <button class=\"tab-btn\" onclick=\"showTab('files')\">File Manager</button>\n"
"  <button class=\"tab-btn\" onclick=\"showTab('terminal')\">Web Terminal</button>\n"
"</nav>\n"
"<div class=\"container\">\n"
"  <div id=\"dashboard\" class=\"tab-content active\">\n"
"    <div class=\"grid\">\n"
"      <div class=\"card\">\n"
"        <div class=\"card-title\"><span>CPU Frequency</span><span id=\"cpu-gov\">ondemand</span></div>\n"
"        <div class=\"card-value\" id=\"cpu-freq\">--- MHz</div>\n"
"        <div class=\"progress-bar\"><div class=\"progress-fill\" id=\"cpu-fill\" style=\"width: 75%\"></div></div>\n"
"        <div class=\"card-sub\" id=\"cpu-load\">Load avg: 0.00, 0.00, 0.00</div>\n"
"      </div>\n"
"      <div class=\"card\">\n"
"        <div class=\"card-title\"><span>RAM Utilization</span><span id=\"mem-free\">--- MB free</span></div>\n"
"        <div class=\"card-value\" id=\"mem-used\">--- MB</div>\n"
"        <div class=\"progress-bar\"><div class=\"progress-fill\" id=\"mem-fill\" style=\"width: 50%\"></div></div>\n"
"        <div class=\"card-sub\" id=\"mem-zram\">ZRAM Pool: --- MB</div>\n"
"      </div>\n"
"      <div class=\"card\">\n"
"        <div class=\"card-title\"><span>Storage (ux0:)</span><span id=\"stor-mode\">RW</span></div>\n"
"        <div class=\"card-value\" id=\"stor-used\">--- GB</div>\n"
"        <div class=\"progress-bar\"><div class=\"progress-fill\" id=\"stor-fill\" style=\"width: 30%\"></div></div>\n"
"        <div class=\"card-sub\" id=\"stor-free\">Free space: --- GB</div>\n"
"      </div>\n"
"      <div class=\"card\">\n"
"        <div class=\"card-title\"><span>Wireless & Bluetooth</span><span id=\"net-iface\">mlan0</span></div>\n"
"        <div class=\"card-value\" id=\"net-ssid\" style=\"font-size: 1.15rem;\">Disconnected</div>\n"
"        <div class=\"card-sub\" id=\"bt-status\">Bluetooth: Radio Off</div>\n"
"        <div class=\"card-sub\" id=\"sys-uptime\" style=\"margin-top: 4px;\">Uptime: ---</div>\n"
"      </div>\n"
"    </div>\n"
"    <div class=\"card\" style=\"margin-bottom: 24px;\">\n"
"      <div class=\"card-title\"><span>Handheld Control Actions</span><span>Hardware Controls</span></div>\n"
"      <div class=\"actions-grid\">\n"
"        <button class=\"btn\" onclick=\"doAction('cpu', '444')\">CPU: 444 MHz (Turbo)</button>\n"
"        <button class=\"btn\" onclick=\"doAction('cpu', '333')\">CPU: 333 MHz (Standard)</button>\n"
"        <button class=\"btn\" onclick=\"doAction('cpu', '111')\">CPU: 111 MHz (Powersave)</button>\n"
"        <button class=\"btn\" onclick=\"doAction('led', 'heartbeat')\">LED: Heartbeat</button>\n"
"        <button class=\"btn\" onclick=\"doAction('led', 'disk')\">LED: Disk Activity</button>\n"
"        <button class=\"btn\" onclick=\"doAction('led', 'off')\">LED: Off</button>\n"
"        <button class=\"btn\" onclick=\"doAction('storage', 'toggle')\" id=\"btn-stor-toggle\">Storage: Lock / Unlock</button>\n"
"        <button class=\"btn\" onclick=\"doAction('display', 'sleep')\">Screen Sleep</button>\n"
"        <button class=\"btn btn-danger\" onclick=\"if(confirm('Reboot PlayStation Vita back to VitaOS?')) doAction('power', 'reboot')\">Reboot to VitaOS</button>\n"
"        <button class=\"btn btn-danger\" onclick=\"if(confirm('Power off the console completely?')) doAction('power', 'poweroff')\">Power Off</button>\n"
"      </div>\n"
"    </div>\n"
"  </div>\n"
"  <div id=\"files\" class=\"tab-content\">\n"
"    <div class=\"dropzone\" id=\"dropzone\">\n"
"      <div class=\"dropzone-icon\">&#8682;</div>\n"
"      <h3 style=\"font-size: 1rem; margin-bottom: 4px;\">Drag and drop files here to upload</h3>\n"
"      <p style=\"font-size: 0.8rem; color: var(--text-muted);\">or click to browse from your device</p>\n"
"      <input type=\"file\" id=\"file-input\" multiple style=\"display: none;\">\n"
"      <div id=\"upload-progress\" style=\"display: none; margin-top: 14px;\">\n"
"        <div class=\"progress-bar\"><div class=\"progress-fill\" id=\"upload-fill\" style=\"width: 0%\"></div></div>\n"
"        <p id=\"upload-status\" style=\"font-size: 0.75rem; color: var(--cyan); margin-top: 4px;\">Uploading...</p>\n"
"      </div>\n"
"    </div>\n"
"    <div class=\"breadcrumbs\" id=\"breadcrumbs\"></div>\n"
"    <div style=\"display: flex; gap: 8px; margin-bottom: 12px;\">\n"
"      <button class=\"btn btn-sm btn-primary\" onclick=\"promptNewFolder()\">+ New Folder</button>\n"
"      <button class=\"btn btn-sm\" onclick=\"loadFiles(currentPath)\">Refresh</button>\n"
"    </div>\n"
"    <div class=\"card\" style=\"padding: 0; overflow-x: auto;\">\n"
"      <table class=\"file-table\">\n"
"        <thead>\n"
"          <tr>\n"
"            <th>Name</th>\n"
"            <th style=\"width: 120px;\">Size</th>\n"
"            <th style=\"width: 180px;\">Modified</th>\n"
"            <th style=\"width: 140px; text-align: right;\">Actions</th>\n"
"          </tr>\n"
"        </thead>\n"
"        <tbody id=\"file-list\"></tbody>\n"
"      </table>\n"
"    </div>\n"
"  </div>\n"
"  <div id=\"terminal\" class=\"tab-content\">\n"
"    <div class=\"terminal-window\">\n"
"      <div class=\"term-header\">\n"
"        <span>PlayStation Vita Terminal Console</span>\n"
"        <span>/bin/sh (root@vita)</span>\n"
"      </div>\n"
"      <div class=\"term-output\" id=\"term-output\">PlayStation Vita Linux 6.12 Web Console\nType a command below and press Enter.\n\n</div>\n"
"      <div class=\"term-input-row\">\n"
"        <span class=\"term-prompt\">root@vita:~#</span>\n"
"        <input type=\"text\" class=\"term-input\" id=\"term-input\" placeholder=\"Type command (e.g. ps aux, df -h, dmesg)...\" autocomplete=\"off\">\n"
"      </div>\n"
"    </div>\n"
"    <div class=\"term-shortcuts\">\n"
"      <button class=\"btn btn-sm\" onclick=\"runCommand('vita-config status')\">config status</button>\n"
"      <button class=\"btn btn-sm\" onclick=\"runCommand('vita-wifi status')\">wifi status</button>\n"
"      <button class=\"btn btn-sm\" onclick=\"runCommand('vita-bluetooth status')\">bluetooth status</button>\n"
"      <button class=\"btn btn-sm\" onclick=\"runCommand('df -h')\">df -h</button>\n"
"      <button class=\"btn btn-sm\" onclick=\"runCommand('free -m')\">free -m</button>\n"
"      <button class=\"btn btn-sm\" onclick=\"runCommand('dmesg | tail -n 25')\">dmesg</button>\n"
"    </div>\n"
"  </div>\n"
"</div>\n"
"<div id=\"toast\"></div>\n"
"<script>\n"
"let currentPath = '/mnt/ux0';\n"
"let cmdHistory = [];\n"
"let historyIdx = -1;\n"
"\n"
"function showTab(id) {\n"
"  document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));\n"
"  document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));\n"
"  event.target.classList.add('active');\n"
"  document.getElementById(id).classList.add('active');\n"
"  if (id === 'files') loadFiles(currentPath);\n"
"}\n"
"\n"
"function showToast(msg) {\n"
"  const t = document.getElementById('toast');\n"
"  t.textContent = msg;\n"
"  t.style.display = 'block';\n"
"  setTimeout(() => { t.style.display = 'none'; }, 3000);\n"
"}\n"
"\n"
"async function updateStatus() {\n"
"  try {\n"
"    const res = await fetch('/api/status');\n"
"    const d = await res.json();\n"
"    document.getElementById('hdr-model').textContent = d.model || 'PlayStation Vita';\n"
"    document.getElementById('hdr-ip').textContent = d.network ? d.network.ip || '127.0.0.1' : '127.0.0.1';\n"
"    document.getElementById('hdr-cpu').textContent = (d.cpu ? d.cpu.frequency_mhz + ' MHz' : '---');\n"
"    document.getElementById('hdr-storage').textContent = 'ux0: ' + (d.storage && d.storage.ux0_ro ? 'RO' : 'RW');\n"
"    if (d.cpu) {\n"
"      document.getElementById('cpu-freq').textContent = d.cpu.frequency_mhz + ' MHz';\n"
"      document.getElementById('cpu-gov').textContent = d.cpu.governor;\n"
"      const pct = Math.min(100, Math.round((d.cpu.frequency_mhz / 444) * 100));\n"
"      document.getElementById('cpu-fill').style.width = pct + '%';\n"
"      document.getElementById('cpu-load').textContent = 'Load avg: ' + d.cpu.load_avg.join(', ');\n"
"    }\n"
"    if (d.memory) {\n"
"      document.getElementById('mem-used').textContent = d.memory.used_mb + ' MB';\n"
"      document.getElementById('mem-free').textContent = d.memory.free_mb + ' MB free';\n"
"      const memPct = Math.min(100, Math.round((d.memory.used_mb / d.memory.total_mb) * 100));\n"
"      document.getElementById('mem-fill').style.width = memPct + '%';\n"
"      document.getElementById('mem-zram').textContent = 'ZRAM Pool: ' + d.memory.zram_total_mb + ' MB';\n"
"    }\n"
"    if (d.storage) {\n"
"      document.getElementById('stor-used').textContent = d.storage.ux0_used_gb + ' GB / ' + d.storage.ux0_total_gb + ' GB';\n"
"      document.getElementById('stor-mode').textContent = d.storage.ux0_ro ? 'Read-Only (Protected)' : 'Read-Write';\n"
"      document.getElementById('stor-fill').style.width = d.storage.ux0_pct + '%';\n"
"      document.getElementById('stor-free').textContent = 'Free space: ' + d.storage.ux0_free_gb + ' GB';\n"
"      const tBtn = document.getElementById('btn-stor-toggle');\n"
"      if (tBtn) tBtn.textContent = 'Storage: Set ' + (d.storage.ux0_ro ? 'Read-Write (Unlock)' : 'Read-Only (Lock)');\n"
"    }\n"
"    if (d.network) {\n"
"      document.getElementById('net-ssid').textContent = d.network.ssid || (d.network.ip ? 'Connected (' + d.network.ip + ')' : 'Disconnected');\n"
"      document.getElementById('net-iface').textContent = d.network.interface || 'mlan0';\n"
"    }\n"
"    if (d.bluetooth) {\n"
"      document.getElementById('bt-status').textContent = 'Bluetooth: ' + (d.bluetooth.powered ? 'ON (' + d.bluetooth.connected_count + ' active)' : 'Radio OFF');\n"
"    }\n"
"    if (d.uptime) {\n"
"      document.getElementById('sys-uptime').textContent = 'Uptime: ' + d.uptime;\n"
"    }\n"
"  } catch(e) {}\n"
"}\n"
"\n"
"async function doAction(action, val) {\n"
"  try {\n"
"    const res = await fetch('/api/action', {\n"
"      method: 'POST',\n"
"      headers: { 'Content-Type': 'application/json' },\n"
"      body: JSON.stringify({ action: action, value: val })\n"
"    });\n"
"    const data = await res.json();\n"
"    showToast(data.message || 'Action executed successfully');\n"
"    setTimeout(updateStatus, 500);\n"
"  } catch(e) {\n"
"    showToast('Failed to execute action: ' + e);\n"
"  }\n"
"}\n"
"\n"
"function renderBreadcrumbs(path) {\n"
"  const bc = document.getElementById('breadcrumbs');\n"
"  bc.innerHTML = '';\n"
"  const parts = path.split('/').filter(p => p.length > 0);\n"
"  let accum = '';\n"
"  const rootSpan = document.createElement('span');\n"
"  rootSpan.className = 'crumb';\n"
"  rootSpan.textContent = '/';\n"
"  rootSpan.onclick = () => loadFiles('/');\n"
"  bc.appendChild(rootSpan);\n"
"  parts.forEach((p, idx) => {\n"
"    accum += '/' + p;\n"
"    const sep = document.createElement('span');\n"
"    sep.textContent = ' > ';\n"
"    bc.appendChild(sep);\n"
"    const span = document.createElement('span');\n"
"    span.className = 'crumb';\n"
"    span.textContent = p;\n"
"    const target = accum;\n"
"    span.onclick = () => loadFiles(target);\n"
"    bc.appendChild(span);\n"
"  });\n"
"}\n"
"\n"
"function formatSize(bytes) {\n"
"  if (bytes === 0) return '0 B';\n"
"  const k = 1024;\n"
"  const dm = 1;\n"
"  const sizes = ['B', 'KB', 'MB', 'GB'];\n"
"  const i = Math.floor(Math.log(bytes) / Math.log(k));\n"
"  return parseFloat((bytes / Math.pow(k, i)).toFixed(dm)) + ' ' + sizes[i];\n"
"}\n"
"\n"
"async function loadFiles(path) {\n"
"  currentPath = path;\n"
"  renderBreadcrumbs(path);\n"
"  const tb = document.getElementById('file-list');\n"
"  tb.innerHTML = '<tr><td colspan=\"4\" style=\"text-align: center; color: var(--text-muted);\">Loading files...</td></tr>';\n"
"  try {\n"
"    const res = await fetch('/api/files?path=' + encodeURIComponent(path));\n"
"    const items = await res.json();\n"
"    tb.innerHTML = '';\n"
"    if (path !== '/' && path !== '') {\n"
"      const parent = path.substring(0, path.lastIndexOf('/')) || '/';\n"
"      const row = document.createElement('tr');\n"
"      row.innerHTML = `<td colspan=\"4\"><div class=\"file-name\" onclick=\"loadFiles('${parent}')\">📁 .. (Up one directory)</div></td>`;\n"
"      tb.appendChild(row);\n"
"    }\n"
"    if (items.length === 0) {\n"
"      tb.innerHTML += '<tr><td colspan=\"4\" style=\"text-align:center; color: var(--text-muted);\">Directory is empty</td></tr>';\n"
"      return;\n"
"    }\n"
"    items.forEach(item => {\n"
"      const row = document.createElement('tr');\n"
"      const icon = item.is_dir ? '📁' : '📄';\n"
"      const targetPath = (path.endsWith('/') ? path : path + '/') + item.name;\n"
"      const nameHtml = item.is_dir\n"
"        ? `<div class=\"file-name\" onclick=\"loadFiles('${targetPath}')\">${icon} ${item.name}</div>`\n"
"        : `<a class=\"file-name\" href=\"/download?file=${encodeURIComponent(targetPath)}\" download=\"${item.name}\">${icon} ${item.name}</a>`;\n"
"      const actionsHtml = item.is_dir\n"
"        ? `<button class=\"btn btn-sm btn-danger\" onclick=\"deleteItem('${targetPath}')\">Delete</button>`\n"
"        : `<a class=\"btn btn-sm btn-primary\" href=\"/download?file=${encodeURIComponent(targetPath)}\" download=\"${item.name}\">Download</a>\n"
"           <button class=\"btn btn-sm btn-danger\" onclick=\"deleteItem('${targetPath}')\">Delete</button>`;\n"
"      row.innerHTML = `<td>${nameHtml}</td><td>${item.is_dir ? '-' : formatSize(item.size)}</td><td>${item.mtime}</td><td style=\"text-align: right; white-space: nowrap;\">${actionsHtml}</td>`;\n"
"      tb.appendChild(row);\n"
"    });\n"
"  } catch(e) {\n"
"    tb.innerHTML = `<tr><td colspan=\"4\" style=\"color: var(--red);\">Error loading directory: ${e}</td></tr>`;\n"
"  }\n"
"}\n"
"\n"
"async function deleteItem(path) {\n"
"  if (!confirm(`Are you sure you want to delete '${path}'?`)) return;\n"
"  try {\n"
"    const res = await fetch('/api/delete?file=' + encodeURIComponent(path), { method: 'POST' });\n"
"    const data = await res.json();\n"
"    showToast(data.message || 'Deleted successfully');\n"
"    loadFiles(currentPath);\n"
"  } catch(e) {\n"
"    showToast('Failed to delete: ' + e);\n"
"  }\n"
"}\n"
"\n"
"async function promptNewFolder() {\n"
"  const name = prompt('Enter new folder name:');\n"
"  if (!name) return;\n"
"  try {\n"
"    const res = await fetch('/api/mkdir?path=' + encodeURIComponent(currentPath) + '&name=' + encodeURIComponent(name), { method: 'POST' });\n"
"    const data = await res.json();\n"
"    showToast(data.message || 'Folder created');\n"
"    loadFiles(currentPath);\n"
"  } catch(e) {\n"
"    showToast('Failed to create folder: ' + e);\n"
"  }\n"
"}\n"
"\n"
"const dropzone = document.getElementById('dropzone');\n"
"const fileInput = document.getElementById('file-input');\n"
"dropzone.onclick = () => fileInput.click();\n"
"dropzone.ondragover = (e) => { e.preventDefault(); dropzone.classList.add('dragover'); };\n"
"dropzone.ondragleave = () => dropzone.classList.remove('dragover');\n"
"dropzone.ondrop = (e) => {\n"
"  e.preventDefault();\n"
"  dropzone.classList.remove('dragover');\n"
"  if (e.dataTransfer.files.length > 0) uploadFiles(e.dataTransfer.files);\n"
"};\n"
"fileInput.onchange = () => {\n"
"  if (fileInput.files.length > 0) uploadFiles(fileInput.files);\n"
"};\n"
"\n"
"async function uploadFiles(files) {\n"
"  const prog = document.getElementById('upload-progress');\n"
"  const fill = document.getElementById('upload-fill');\n"
"  const status = document.getElementById('upload-status');\n"
"  prog.style.display = 'block';\n"
"  for (let i = 0; i < files.length; i++) {\n"
"    const file = files[i];\n"
"    status.textContent = `Uploading ${file.name} (${i+1}/${files.length})...`;\n"
"    fill.style.width = '20%';\n"
"    const fd = new FormData();\n"
"    fd.append('file', file, file.name);\n"
"    try {\n"
"      const res = await fetch('/api/upload?path=' + encodeURIComponent(currentPath), {\n"
"        method: 'POST',\n"
"        body: fd\n"
"      });\n"
"      fill.style.width = '100%';\n"
"      const d = await res.json();\n"
"      showToast(`Uploaded ${file.name} successfully`);\n"
"    } catch(e) {\n"
"      showToast(`Failed to upload ${file.name}: ${e}`);\n"
"    }\n"
"  }\n"
"  setTimeout(() => {\n"
"    prog.style.display = 'none';\n"
"    fill.style.width = '0%';\n"
"    loadFiles(currentPath);\n"
"    updateStatus();\n"
"  }, 800);\n"
"}\n"
"\n"
"const termOut = document.getElementById('term-output');\n"
"const termIn = document.getElementById('term-input');\n"
"termIn.onkeydown = (e) => {\n"
"  if (e.key === 'Enter') {\n"
"    const cmd = termIn.value.trim();\n"
"    if (cmd) {\n"
"      cmdHistory.push(cmd);\n"
"      historyIdx = cmdHistory.length;\n"
"      runCommand(cmd);\n"
"      termIn.value = '';\n"
"    }\n"
"  } else if (e.key === 'ArrowUp') {\n"
"    if (historyIdx > 0) {\n"
"      historyIdx--;\n"
"      termIn.value = cmdHistory[historyIdx];\n"
"    }\n"
"  } else if (e.key === 'ArrowDown') {\n"
"    if (historyIdx < cmdHistory.length - 1) {\n"
"      historyIdx++;\n"
"      termIn.value = cmdHistory[historyIdx];\n"
"    } else {\n"
"      historyIdx = cmdHistory.length;\n"
"      termIn.value = '';\n"
"    }\n"
"  }\n"
"};\n"
"\n"
"async function runCommand(cmd) {\n"
"  termOut.textContent += `\\nroot@vita:~# ${cmd}\\n`;\n"
"  termOut.scrollTop = termOut.scrollHeight;\n"
"  try {\n"
"    const res = await fetch('/api/terminal', {\n"
"      method: 'POST',\n"
"      headers: { 'Content-Type': 'application/json' },\n"
"      body: JSON.stringify({ command: cmd })\n"
"    });\n"
"    const d = await res.json();\n"
"    termOut.textContent += d.output || '[No output]\\n';\n"
"    termOut.scrollTop = termOut.scrollHeight;\n"
"  } catch(e) {\n"
"    termOut.textContent += `Error: ${e}\\n`;\n"
"    termOut.scrollTop = termOut.scrollHeight;\n"
"  }\n"
"}\n"
"\n"
"updateStatus();\n"
"setInterval(updateStatus, 3000);\n"
"</script>\n"
"</body>\n"
"</html>\n";

/* -------------------------------------------------------------------------- */
/* API Handlers                                                               */
/* -------------------------------------------------------------------------- */

static void handle_api_status(int fd) {
    char model[128] = "PlayStation Vita";
    FILE *mfp = fopen("/proc/device-tree/model", "r");
    if (mfp) {
        if (fgets(model, sizeof(model), mfp) != NULL) {
            model[strcspn(model, "\r\n")] = 0;
        }
        fclose(mfp);
    } else {
        if (access("/dev/i2c-1", F_OK) == 0) {
            strncpy(model, "PS Vita 2000 (Slim / LCD)", sizeof(model) - 1);
        } else {
            strncpy(model, "PS Vita 1000 (Fat / OLED)", sizeof(model) - 1);
        }
    }

    /* CPU frequency */
    long cur_freq = 444;
    FILE *cfp = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "r");
    if (cfp) {
        long khz = 0;
        if (fscanf(cfp, "%ld", &khz) == 1 && khz > 0) cur_freq = khz / 1000;
        fclose(cfp);
    }
    char governor[64] = "ondemand";
    FILE *gfp = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "r");
    if (gfp) {
        if (fgets(governor, sizeof(governor), gfp)) {
            governor[strcspn(governor, "\r\n")] = 0;
        }
        fclose(gfp);
    }

    /* Load avg */
    double l1 = 0, l2 = 0, l3 = 0;
    FILE *lfp = fopen("/proc/loadavg", "r");
    if (lfp) {
        fscanf(lfp, "%lf %lf %lf", &l1, &l2, &l3);
        fclose(lfp);
    }

    /* RAM & ZRAM */
    long total_mem = 0, free_mem = 0, buffers = 0, cached = 0;
    FILE *memfp = fopen("/proc/meminfo", "r");
    if (memfp) {
        char key[64];
        long val;
        while (fscanf(memfp, "%63s %ld kB", key, &val) == 2) {
            if (strcmp(key, "MemTotal:") == 0) total_mem = val / 1024;
            else if (strcmp(key, "MemFree:") == 0) free_mem = val / 1024;
            else if (strcmp(key, "Buffers:") == 0) buffers = val / 1024;
            else if (strcmp(key, "Cached:") == 0) cached = val / 1024;
        }
        fclose(memfp);
    }
    long used_mem = total_mem - (free_mem + buffers + cached);
    if (used_mem < 0) used_mem = 0;

    long zram_size = 0;
    FILE *zfp = fopen("/sys/block/zram0/disksize", "r");
    if (zfp) {
        long bytes = 0;
        if (fscanf(zfp, "%ld", &bytes) == 1) zram_size = bytes / (1024 * 1024);
        fclose(zfp);
    }

    /* Storage */
    int ux0_ro = is_ux0_readonly();
    struct statvfs sv;
    double total_gb = 0, used_gb = 0, free_gb = 0;
    int pct = 0;
    if (statvfs("/mnt/ux0", &sv) == 0 && sv.f_blocks > 0) {
        total_gb = (double)(sv.f_blocks * sv.f_frsize) / (1024.0 * 1024.0 * 1024.0);
        free_gb = (double)(sv.f_bavail * sv.f_frsize) / (1024.0 * 1024.0 * 1024.0);
        used_gb = total_gb - free_gb;
        if (total_gb > 0) pct = (int)((used_gb / total_gb) * 100.0);
    }

    /* Network */
    char ip_str[64] = "127.0.0.1";
    char ssid[128] = "";
    FILE *ip_pipe = popen("ip -4 addr show mlan0 2>/dev/null | awk '/inet / {print $2}' | cut -d/ -f1", "r");
    if (ip_pipe) {
        if (fgets(ip_str, sizeof(ip_str), ip_pipe)) {
            ip_str[strcspn(ip_str, "\r\n")] = 0;
        }
        pclose(ip_pipe);
    }
    FILE *ssid_pipe = popen("wpa_cli -i mlan0 status 2>/dev/null | awk -F'=' '/^ssid=/ {print $2}'", "r");
    if (ssid_pipe) {
        if (fgets(ssid, sizeof(ssid), ssid_pipe)) {
            ssid[strcspn(ssid, "\r\n")] = 0;
        }
        pclose(ssid_pipe);
    }

    /* Bluetooth */
    int bt_pwr = 0;
    int bt_conn = 0;
    FILE *bt_pipe = popen("bluetoothctl show 2>/dev/null | grep -q 'Powered: yes' && echo 1 || echo 0", "r");
    if (bt_pipe) {
        fscanf(bt_pipe, "%d", &bt_pwr);
        pclose(bt_pipe);
    }
    if (bt_pwr) {
        FILE *bc_pipe = popen("bluetoothctl devices Connected 2>/dev/null | wc -l", "r");
        if (bc_pipe) {
            fscanf(bc_pipe, "%d", &bt_conn);
            pclose(bc_pipe);
        }
    }

    /* Uptime */
    char uptime_str[64] = "0 min";
    double up_sec = 0;
    FILE *ufp = fopen("/proc/uptime", "r");
    if (ufp) {
        if (fscanf(ufp, "%lf", &up_sec) == 1) {
            int hrs = (int)(up_sec / 3600.0);
            int mins = (int)(((long)up_sec % 3600) / 60);
            snprintf(uptime_str, sizeof(uptime_str), "%dh %dm", hrs, mins);
        }
        fclose(ufp);
    }

    char json[2048];
    snprintf(json, sizeof(json),
        "{\n"
        "  \"model\": \"%s\",\n"
        "  \"uptime\": \"%s\",\n"
        "  \"cpu\": {\n"
        "    \"frequency_mhz\": %ld,\n"
        "    \"governor\": \"%s\",\n"
        "    \"load_avg\": [%.2f, %.2f, %.2f]\n"
        "  },\n"
        "  \"memory\": {\n"
        "    \"total_mb\": %ld,\n"
        "    \"used_mb\": %ld,\n"
        "    \"free_mb\": %ld,\n"
        "    \"zram_total_mb\": %ld\n"
        "  },\n"
        "  \"storage\": {\n"
        "    \"ux0_ro\": %s,\n"
        "    \"ux0_total_gb\": %.1f,\n"
        "    \"ux0_used_gb\": %.1f,\n"
        "    \"ux0_free_gb\": %.1f,\n"
        "    \"ux0_pct\": %d\n"
        "  },\n"
        "  \"network\": {\n"
        "    \"ip\": \"%s\",\n"
        "    \"ssid\": \"%s\",\n"
        "    \"interface\": \"mlan0\"\n"
        "  },\n"
        "  \"bluetooth\": {\n"
        "    \"powered\": %s,\n"
        "    \"connected_count\": %d\n"
        "  }\n"
        "}\n",
        model, uptime_str, cur_freq, governor, l1, l2, l3,
        total_mem, used_mem, free_mem, zram_size,
        ux0_ro ? "true" : "false", total_gb, used_gb, free_gb, pct,
        ip_str, ssid,
        bt_pwr ? "true" : "false", bt_conn);

    send_json(fd, 200, json);
}

static void handle_api_files(int fd, const char *query) {
    char raw_path[MAX_PATH_LEN] = "";
    const char *q = strstr(query, "path=");
    if (q) {
        url_decode(raw_path, q + 5);
        char *amp = strchr(raw_path, '&');
        if (amp) *amp = '\0';
    }

    char req_path[MAX_PATH_LEN];
    sanitize_path(req_path, sizeof(req_path), g_docroot, raw_path);

    DIR *dir = opendir(req_path);
    if (!dir) {
        /* Fallback if default docroot is unmounted */
        if (strcmp(req_path, DEFAULT_DOCROOT) == 0) {
            dir = opendir(FALLBACK_DOCROOT);
            if (!dir) {
                send_error(fd, 404, "Directory not found");
                return;
            }
        } else {
            send_error(fd, 404, "Directory not found");
            return;
        }
    }

    char *json = malloc(65536);
    if (!json) {
        closedir(dir);
        send_error(fd, 500, "Out of memory");
        return;
    }

    size_t cap = 65536;
    size_t len = snprintf(json, cap, "[\n");
    struct dirent *ent;
    int first = 1;

    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        char full[MAX_PATH_LEN * 2];
        snprintf(full, sizeof(full), "%s/%s", req_path, ent->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;

        int is_dir = S_ISDIR(st.st_mode);
        char date_str[64];
        struct tm *tm_info = localtime(&st.st_mtime);
        strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M", tm_info);

        char entry[1024];
        snprintf(entry, sizeof(entry),
            "%s  {\"name\": \"%s\", \"is_dir\": %s, \"size\": %lld, \"mtime\": \"%s\"}",
            first ? "" : ",\n",
            ent->d_name,
            is_dir ? "true" : "false",
            (long long)st.st_size,
            date_str);
        first = 0;

        size_t elen = strlen(entry);
        if (len + elen + 16 > cap) {
            cap *= 2;
            char *tmp = realloc(json, cap);
            if (!tmp) break;
            json = tmp;
        }
        strcpy(json + len, entry);
        len += elen;
    }
    closedir(dir);

    strcpy(json + len, "\n]\n");
    send_json(fd, 200, json);
    free(json);
}

static void handle_api_action(int fd, const char *body) {
    char action[64] = "";
    char value[128] = "";

    const char *ap = strstr(body, "\"action\":");
    if (ap) {
        sscanf(ap + 9, " \"%63[^\"]\"", action);
    }
    const char *vp = strstr(body, "\"value\":");
    if (vp) {
        sscanf(vp + 8, " \"%127[^\"]\"", value);
    }

    char msg[256] = "Action processed";
    if (strcmp(action, "cpu") == 0) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "/usr/bin/vita-cpu set %s >/dev/null 2>&1 || /usr/bin/vita-cpu %s >/dev/null 2>&1", value, value);
        system(cmd);
        snprintf(msg, sizeof(msg), "CPU profile set to %s MHz", value);
    } else if (strcmp(action, "led") == 0) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "/usr/bin/vita-led %s >/dev/null 2>&1", value);
        system(cmd);
        snprintf(msg, sizeof(msg), "PlayStation LED mode set to %s", value);
    } else if (strcmp(action, "storage") == 0) {
        if (strcmp(value, "toggle") == 0) {
            if (is_ux0_readonly()) {
                system("/usr/bin/vita-storage rw ux0 -q 2>/dev/null");
                snprintf(msg, sizeof(msg), "Storage unlocked: ux0 is now Read-Write");
            } else {
                sync();
                system("/usr/bin/vita-storage ro ux0 -q 2>/dev/null");
                snprintf(msg, sizeof(msg), "Storage locked: ux0 is now Read-Only (Safe)");
            }
        } else if (strcmp(value, "rw") == 0) {
            system("/usr/bin/vita-storage rw ux0 -q 2>/dev/null");
            snprintf(msg, sizeof(msg), "Storage unlocked: ux0 is now Read-Write");
        } else {
            sync();
            system("/usr/bin/vita-storage ro ux0 -q 2>/dev/null");
            snprintf(msg, sizeof(msg), "Storage locked: ux0 is now Read-Only (Safe)");
        }
    } else if (strcmp(action, "display") == 0) {
        if (strcmp(value, "sleep") == 0) {
            system("/usr/bin/vita-powerd sleep 2>/dev/null || /usr/bin/vita-brightness set 0 2>/dev/null");
            snprintf(msg, sizeof(msg), "Display put to sleep");
        } else {
            system("/usr/bin/vita-powerd wake 2>/dev/null || /usr/bin/vita-brightness max 2>/dev/null");
            snprintf(msg, sizeof(msg), "Display woken");
        }
    } else if (strcmp(action, "power") == 0) {
        if (strcmp(value, "reboot") == 0) {
            send_json(fd, 200, "{\"success\":true,\"message\":\"Rebooting PlayStation Vita to VitaOS...\"}");
            sync();
            sleep(1);
            system("reboot");
            return;
        } else if (strcmp(value, "poweroff") == 0) {
            send_json(fd, 200, "{\"success\":true,\"message\":\"Shutting down PlayStation Vita...\"}");
            sync();
            sleep(1);
            system("poweroff");
            return;
        }
    }

    char resp[512];
    snprintf(resp, sizeof(resp), "{\"success\":true,\"message\":\"%s\"}\n", msg);
    send_json(fd, 200, resp);
}

static void handle_api_terminal(int fd, const char *body) {
    char cmd[1024] = "";
    const char *cp = strstr(body, "\"command\":");
    if (cp) {
        cp += 10;
        while (*cp == ' ' || *cp == '\"') cp++;
        size_t idx = 0;
        while (*cp && *cp != '\"' && idx < sizeof(cmd) - 1) {
            if (*cp == '\\' && *(cp + 1) == '\"') {
                cmd[idx++] = '\"';
                cp += 2;
            } else {
                cmd[idx++] = *cp++;
            }
        }
        cmd[idx] = '\0';
    }

    if (cmd[0] == '\0') {
        send_json(fd, 200, "{\"output\":\"[Empty command]\\n\"}\n");
        return;
    }

    char wrapped_cmd[1200];
    snprintf(wrapped_cmd, sizeof(wrapped_cmd), "%s 2>&1", cmd);

    FILE *pipe = popen(wrapped_cmd, "r");
    if (!pipe) {
        send_error(fd, 500, "Failed to execute command");
        return;
    }

    char *buf = malloc(65536);
    if (!buf) {
        pclose(pipe);
        send_error(fd, 500, "Out of memory");
        return;
    }

    size_t n = fread(buf, 1, 65530, pipe);
    buf[n] = '\0';
    pclose(pipe);

    /* Escape JSON characters */
    char *escaped = malloc(n * 2 + 128);
    if (!escaped) {
        free(buf);
        send_error(fd, 500, "Out of memory");
        return;
    }

    size_t e_idx = 0;
    for (size_t i = 0; i < n; i++) {
        char c = buf[i];
        if (c == '\"') { escaped[e_idx++] = '\\'; escaped[e_idx++] = '\"'; }
        else if (c == '\\') { escaped[e_idx++] = '\\'; escaped[e_idx++] = '\\'; }
        else if (c == '\n') { escaped[e_idx++] = '\\'; escaped[e_idx++] = 'n'; }
        else if (c == '\r') { escaped[e_idx++] = '\\'; escaped[e_idx++] = 'r'; }
        else if (c == '\t') { escaped[e_idx++] = '\\'; escaped[e_idx++] = 't'; }
        else if ((unsigned char)c >= 32) { escaped[e_idx++] = c; }
    }
    escaped[e_idx] = '\0';

    char *json = malloc(e_idx + 128);
    snprintf(json, e_idx + 128, "{\"output\":\"%s\"}\n", escaped);
    send_json(fd, 200, json);

    free(json);
    free(escaped);
    free(buf);
}

static void handle_api_mkdir(int fd, const char *query) {
    char path[MAX_PATH_LEN] = DEFAULT_DOCROOT;
    char name[256] = "";

    const char *qp = strstr(query, "path=");
    if (qp) {
        url_decode(path, qp + 5);
        char *amp = strchr(path, '&');
        if (amp) *amp = '\0';
    }
    const char *qn = strstr(query, "name=");
    if (qn) {
        url_decode(name, qn + 5);
        char *amp = strchr(name, '&');
        if (amp) *amp = '\0';
    }

    if (name[0] == '\0') {
        send_error(fd, 400, "Missing directory name");
        return;
    }

    char target[MAX_PATH_LEN * 2];
    snprintf(target, sizeof(target), "%s/%s", path, name);

    int was_ro = is_ux0_readonly();
    if (was_ro) system("/usr/bin/vita-storage rw ux0 -q 2>/dev/null");

    int res = mkdir(target, 0755);
    sync();

    if (was_ro) system("/usr/bin/vita-storage ro ux0 -q 2>/dev/null");

    if (res == 0) {
        send_json(fd, 200, "{\"success\":true,\"message\":\"Folder created successfully\"}\n");
    } else {
        send_error(fd, 500, strerror(errno));
    }
}

static void handle_api_delete(int fd, const char *query) {
    char file_path[MAX_PATH_LEN] = "";
    const char *q = strstr(query, "file=");
    if (q) {
        url_decode(file_path, q + 5);
        char *amp = strchr(file_path, '&');
        if (amp) *amp = '\0';
    }

    if (file_path[0] == '\0') {
        send_error(fd, 400, "Missing file path");
        return;
    }

    int was_ro = is_ux0_readonly();
    if (was_ro) system("/usr/bin/vita-storage rw ux0 -q 2>/dev/null");

    struct stat st;
    int res = -1;
    if (stat(file_path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            char rm_cmd[MAX_PATH_LEN + 32];
            snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf \"%s\"", file_path);
            res = system(rm_cmd);
        } else {
            res = unlink(file_path);
        }
    }
    sync();

    if (was_ro) system("/usr/bin/vita-storage ro ux0 -q 2>/dev/null");

    if (res == 0) {
        send_json(fd, 200, "{\"success\":true,\"message\":\"Item deleted successfully\"}\n");
    } else {
        send_error(fd, 500, "Failed to delete item");
    }
}

static void handle_download(int fd, const char *query) {
    char file_path[MAX_PATH_LEN] = "";
    const char *q = strstr(query, "file=");
    if (q) {
        url_decode(file_path, q + 5);
        char *amp = strchr(file_path, '&');
        if (amp) *amp = '\0';
    }

    int f = open(file_path, O_RDONLY);
    if (f < 0) {
        send_error(fd, 404, "File not found");
        return;
    }

    struct stat st;
    fstat(f, &st);

    const char *bname = strrchr(file_path, '/');
    bname = bname ? bname + 1 : file_path;

    char header[1024];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Server: %s\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: %lld\r\n"
        "Content-Disposition: attachment; filename=\"%s\"\r\n"
        "Connection: close\r\n"
        "\r\n",
        SERVER_NAME, (long long)st.st_size, bname);
    write(fd, header, hlen);

    char buf[BUFFER_SIZE];
    ssize_t bytes;
    while ((bytes = read(f, buf, sizeof(buf))) > 0) {
        write(fd, buf, bytes);
    }
    close(f);
}

/* Streaming multipart/form-data upload handler */
static void handle_upload(int fd, const char *query, const char *headers, const char *body_start, size_t initial_body_len, size_t content_len) {
    char dest_dir[MAX_PATH_LEN] = DEFAULT_DOCROOT;
    const char *q = strstr(query, "path=");
    if (q) {
        url_decode(dest_dir, q + 5);
        char *amp = strchr(dest_dir, '&');
        if (amp) *amp = '\0';
    }

    /* Extract boundary */
    char boundary[256] = "";
    const char *bp = strstr(headers, "boundary=");
    if (bp) {
        sscanf(bp + 9, "%255s", boundary);
        char *semi = strchr(boundary, ';');
        if (semi) *semi = '\0';
        char *cr = strchr(boundary, '\r');
        if (cr) *cr = '\0';
    }

    if (boundary[0] == '\0') {
        send_error(fd, 400, "Missing multipart boundary");
        return;
    }

    char bound_pattern[300];
    snprintf(bound_pattern, sizeof(bound_pattern), "--%s", boundary);

    /* Read initial body chunk if not yet received */
    char buf[BUFFER_SIZE];
    size_t total_read = initial_body_len;

    char *work_buf = malloc(BUFFER_SIZE * 4);
    if (!work_buf) {
        send_error(fd, 500, "Out of memory");
        return;
    }

    memcpy(work_buf, body_start, initial_body_len);
    size_t work_len = initial_body_len;

    /* Fill buffer until we find the filename */
    while (work_len < 1024 && total_read < content_len) {
        ssize_t r = read(fd, buf, sizeof(buf));
        if (r <= 0) break;
        memcpy(work_buf + work_len, buf, r);
        work_len += r;
        total_read += r;
    }
    work_buf[work_len] = '\0';

    char filename[256] = "upload.bin";
    const char *fp = strstr(work_buf, "filename=\"");
    if (fp) {
        sscanf(fp + 10, "%255[^\"]", filename);
        /* Sanitize filename */
        const char *bn = strrchr(filename, '/');
        if (!bn) bn = strrchr(filename, '\\');
        if (bn) memmove(filename, bn + 1, strlen(bn + 1) + 1);
    }

    /* Find header end (\r\n\r\n) */
    char *data_start = strstr(work_buf, "\r\n\r\n");
    if (!data_start) {
        free(work_buf);
        send_error(fd, 400, "Malformed multipart payload");
        return;
    }
    data_start += 4;
    size_t header_len = data_start - work_buf;
    size_t payload_in_buf = work_len - header_len;

    char dest_file[MAX_PATH_LEN * 2];
    snprintf(dest_file, sizeof(dest_file), "%s/%s", dest_dir, filename);

    int was_ro = is_ux0_readonly();
    if (was_ro) system("/usr/bin/vita-storage rw ux0 -q 2>/dev/null");

    int out_fd = open(dest_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) {
        if (was_ro) system("/usr/bin/vita-storage ro ux0 -q 2>/dev/null");
        free(work_buf);
        send_error(fd, 500, "Failed to open destination file for writing");
        return;
    }

    /* Stream remaining data to file, stopping before final boundary */
    size_t bound_len = strlen(bound_pattern);
    size_t written = 0;

    /* Check if boundary is already in work_buf */
    char *bend = memmem(data_start, payload_in_buf, bound_pattern, bound_len);
    if (bend) {
        size_t write_sz = bend - data_start;
        /* Strip trailing \r\n */
        if (write_sz >= 2 && data_start[write_sz - 2] == '\r' && data_start[write_sz - 1] == '\n') {
            write_sz -= 2;
        }
        write(out_fd, data_start, write_sz);
        written += write_sz;
    } else {
        /* Write what we have except possible trailing partial boundary */
        size_t safe_write = (payload_in_buf > bound_len + 4) ? (payload_in_buf - (bound_len + 4)) : 0;
        if (safe_write > 0) {
            write(out_fd, data_start, safe_write);
            written += safe_write;
            memmove(work_buf, data_start + safe_write, payload_in_buf - safe_write);
            work_len = payload_in_buf - safe_write;
        } else {
            memmove(work_buf, data_start, payload_in_buf);
            work_len = payload_in_buf;
        }

        while (total_read < content_len) {
            ssize_t r = read(fd, buf, sizeof(buf));
            if (r <= 0) break;
            total_read += r;

            memcpy(work_buf + work_len, buf, r);
            work_len += r;

            bend = memmem(work_buf, work_len, bound_pattern, bound_len);
            if (bend) {
                size_t write_sz = bend - work_buf;
                if (write_sz >= 2 && work_buf[write_sz - 2] == '\r' && work_buf[write_sz - 1] == '\n') {
                    write_sz -= 2;
                }
                write(out_fd, work_buf, write_sz);
                written += write_sz;
                break;
            } else {
                safe_write = (work_len > bound_len + 4) ? (work_len - (bound_len + 4)) : 0;
                if (safe_write > 0) {
                    write(out_fd, work_buf, safe_write);
                    written += safe_write;
                    memmove(work_buf, work_buf + safe_write, work_len - safe_write);
                    work_len -= safe_write;
                }
            }
        }
    }

    close(out_fd);
    sync();

    if (was_ro) system("/usr/bin/vita-storage ro ux0 -q 2>/dev/null");
    free(work_buf);

    char resp[512];
    snprintf(resp, sizeof(resp), "{\"success\":true,\"filename\":\"%s\",\"size\":%zu}\n", filename, written);
    send_json(fd, 200, resp);
}

/* -------------------------------------------------------------------------- */
/* Main Connection Processor                                                  */
/* -------------------------------------------------------------------------- */

static void process_connection(int client_fd) {
    char header_buf[4096];
    size_t header_len = 0;

    /* Read HTTP Request Headers */
    while (header_len < sizeof(header_buf) - 1) {
        ssize_t r = read(client_fd, header_buf + header_len, 1);
        if (r <= 0) break;
        header_len += r;
        header_buf[header_len] = '\0';
        if (header_len >= 4 && strcmp(header_buf + header_len - 4, "\r\n\r\n") == 0) {
            break;
        }
    }

    if (header_len == 0) {
        close(client_fd);
        return;
    }

    /* Parse Request Line */
    char method[16] = "";
    char uri[MAX_PATH_LEN] = "";
    char version[16] = "";
    sscanf(header_buf, "%15s %1023s %15s", method, uri, version);

    /* Separate Path and Query */
    char path[MAX_PATH_LEN] = "";
    char query[MAX_PATH_LEN] = "";
    char *qmark = strchr(uri, '?');
    if (qmark) {
        size_t plen = qmark - uri;
        if (plen >= sizeof(path)) plen = sizeof(path) - 1;
        memcpy(path, uri, plen);
        path[plen] = '\0';
        snprintf(query, sizeof(query), "%s", qmark + 1);
    } else {
        snprintf(path, sizeof(path), "%s", uri);
    }

    /* Content-Length */
    size_t content_len = 0;
    const char *cl = strcasestr(header_buf, "Content-Length:");
    if (cl) {
        sscanf(cl + 15, " %zu", &content_len);
    }

    /* Dispatch Endpoints */
    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
            send_response(client_fd, 200, "OK", "text/html; charset=utf-8", HTML_INDEX, strlen(HTML_INDEX));
        } else if (strcmp(path, "/api/status") == 0) {
            handle_api_status(client_fd);
        } else if (strcmp(path, "/api/files") == 0) {
            handle_api_files(client_fd, query);
        } else if (strcmp(path, "/download") == 0) {
            handle_download(client_fd, query);
        } else {
            send_error(client_fd, 404, "Not Found");
        }
    } else if (strcmp(method, "POST") == 0) {
        if (strcmp(path, "/api/upload") == 0) {
            /* Streaming upload */
            handle_upload(client_fd, query, header_buf, "", 0, content_len);
        } else {
            /* Read body into buffer */
            char *body = malloc(content_len + 1);
            if (!body) {
                send_error(client_fd, 500, "Out of memory");
                close(client_fd);
                return;
            }
            size_t bread = 0;
            while (bread < content_len) {
                ssize_t r = read(client_fd, body + bread, content_len - bread);
                if (r <= 0) break;
                bread += r;
            }
            body[bread] = '\0';

            if (strcmp(path, "/api/action") == 0) {
                handle_api_action(client_fd, body);
            } else if (strcmp(path, "/api/terminal") == 0) {
                handle_api_terminal(client_fd, body);
            } else if (strcmp(path, "/api/mkdir") == 0) {
                handle_api_mkdir(client_fd, query);
            } else if (strcmp(path, "/api/delete") == 0) {
                handle_api_delete(client_fd, query);
            } else {
                send_error(client_fd, 404, "Not Found");
            }
            free(body);
        }
    } else {
        send_error(client_fd, 405, "Method Not Allowed");
    }

    close(client_fd);
}

/* -------------------------------------------------------------------------- */
/* Main Server Loop                                                           */
/* -------------------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    int daemon_mode = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            g_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            strncpy(g_docroot, argv[++i], sizeof(g_docroot) - 1);
        } else if (strcmp(argv[i], "-daemon") == 0 || strcmp(argv[i], "-b") == 0) {
            daemon_mode = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: vita-webui-server [-p port] [-d docroot] [-daemon]\n");
            printf("  -p <port>     Port to bind (default: 8080)\n");
            printf("  -d <docroot>  Base directory for file manager (default: /mnt/ux0)\n");
            printf("  -daemon       Run in background\n");
            return 0;
        }
    }

    if (daemon_mode) {
        if (daemon(1, 0) < 0) {
            perror("daemon");
            return 1;
        }
    }

    signal(SIGCHLD, sigchld_handler);
    signal(SIGPIPE, SIG_IGN);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(g_port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 32) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("vita-webui-server listening on 0.0.0.0:%d (docroot: %s)\n", g_port, g_docroot);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        pid_t pid = fork();
        if (pid == 0) {
            /* Child handles connection */
            close(server_fd);
            process_connection(client_fd);
            exit(0);
        } else if (pid > 0) {
            /* Parent closes client socket */
            close(client_fd);
        } else {
            perror("fork");
            close(client_fd);
        }
    }

    close(server_fd);
    return 0;
}
