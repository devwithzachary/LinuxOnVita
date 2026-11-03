#include "updater.h"
#include "gui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/net/http.h>

/* Memory buffer for HTTP JSON responses */
typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} MemoryBuffer;

/* Context for streaming file downloads */
typedef struct {
    SceUID fd;
    curl_off_t total_bytes;
    curl_off_t current_bytes;
    uint64_t last_draw_tick;
    int is_cancelled;
    const char *tag_name;
} DownloadContext;

static int s_net_initialized = 0;
static void *s_net_memory_pool = NULL;

/* Network subsystem initialization */
static int net_subsystem_init(void) {
    if (s_net_initialized) {
        return 0;
    }

    /* Load network kernel modules */
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);

    int pool_size = 4 * 1024 * 1024;
    s_net_memory_pool = malloc(pool_size);
    if (!s_net_memory_pool) {
        return -1;
    }

    SceNetInitParam net_param;
    memset(&net_param, 0, sizeof(net_param));
    net_param.memory = s_net_memory_pool;
    net_param.size = pool_size;
    net_param.flags = 0;

    int ret = sceNetInit(&net_param);
    if (ret < 0 && ret != 0x80410101) {
        free(s_net_memory_pool);
        s_net_memory_pool = NULL;
        return ret;
    }

    ret = sceNetCtlInit();
    if (ret < 0 && ret != 0x80410101) {
        /* Continue even if already initialized */
    }

    sceHttpInit(2 * 1024 * 1024);
    curl_global_init(CURL_GLOBAL_ALL);

    s_net_initialized = 1;
    return 0;
}

/* Network subsystem shutdown */
static void net_subsystem_term(void) {
    if (!s_net_initialized) {
        return;
    }

    curl_global_cleanup();
    sceHttpTerm();
    sceNetCtlTerm();
    sceNetTerm();

    if (s_net_memory_pool) {
        free(s_net_memory_pool);
        s_net_memory_pool = NULL;
    }

    sceSysmoduleUnloadModule(SCE_SYSMODULE_HTTP);
    sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);

    s_net_initialized = 0;
}

/* Check if Vita has an active network connection */
static int is_network_online(void) {
    int state = 0;
    int ret = sceNetCtlInetGetState(&state);
    if (ret >= 0 && state == SCE_NETCTL_STATE_CONNECTED) {
        return 1;
    }
    return 0;
}

/* Memory buffer write callback for cURL */
static size_t mem_write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    MemoryBuffer *mem = (MemoryBuffer *)userp;

    if (mem->size + realsize + 1 > mem->capacity) {
        size_t new_cap = (mem->capacity == 0) ? 16384 : mem->capacity * 2;
        while (mem->size + realsize + 1 > new_cap) {
            new_cap *= 2;
        }
        char *ptr = (char *)realloc(mem->data, new_cap);
        if (!ptr) {
            return 0;
        }
        mem->data = ptr;
        mem->capacity = new_cap;
    }

    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;

    return realsize;
}

/* Disk file write callback for streaming downloads */
static size_t file_write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t bytes = size * nmemb;
    DownloadContext *ctx = (DownloadContext *)userp;

    if (ctx->fd >= 0) {
        int written = sceIoWrite(ctx->fd, contents, bytes);
        if (written > 0) {
            ctx->current_bytes += written;
            return (size_t)written;
        }
        return 0;
    }
    return bytes;
}

/* Transfer progress and cancellation check callback */
static int xfer_progress_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal;
    (void)ulnow;
    DownloadContext *ctx = (DownloadContext *)clientp;

    /* Check for user cancellation via Circle button or Touch */
    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    sceCtrlPeekBufferPositive(0, &pad, 1);

    GuiTouchState ts;
    gui_touch_poll(&ts);

    if ((pad.buttons & (SCE_CTRL_CIRCLE | SCE_CTRL_START)) || ts.just_pressed) {
        ctx->is_cancelled = 1;
        return 1; /* Aborts cURL transfer immediately */
    }

    /* Redraw modal at ~30 FPS intervals */
    uint64_t now = sceKernelGetProcessTimeWide();
    if (now - ctx->last_draw_tick > 33000 || dlnow == dltotal) {
        ctx->last_draw_tick = now;

        float progress = 0.0f;
        char status_buf[128];

        if (dltotal > 0) {
            progress = (float)dlnow / (float)dltotal;
            snprintf(status_buf, sizeof(status_buf), "%.1f MB / %.1f MB (%d%%)",
                     (float)dlnow / (1024.0f * 1024.0f),
                     (float)dltotal / (1024.0f * 1024.0f),
                     (int)(progress * 100.0f));
        } else {
            snprintf(status_buf, sizeof(status_buf), "Downloaded: %.1f MB", (float)dlnow / (1024.0f * 1024.0f));
        }

        char file_buf[128];
        snprintf(file_buf, sizeof(file_buf), "LinuxOnVita.vpk -> %s", UPDATE_TARGET_DIR);

        gui_draw_progress_modal("DOWNLOADING UPDATE", file_buf, progress, status_buf, "PRESS CIRCLE OR TAP TO CANCEL");
    }

    return 0;
}

/* Simple JSON string extractor */
static int parse_json_string(const char *json, const char *key, char *out, size_t out_len) {
    char search_pattern[64];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\":", key);

    const char *pos = strstr(json, search_pattern);
    if (!pos) {
        return -1;
    }

    pos += strlen(search_pattern);
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }

    if (*pos != '\"') {
        return -1;
    }
    pos++;

    size_t idx = 0;
    while (*pos && *pos != '\"' && idx < out_len - 1) {
        if (*pos == '\\' && *(pos + 1)) {
            pos++;
        }
        out[idx++] = *pos++;
    }
    out[idx] = '\0';

    return (idx > 0) ? 0 : -1;
}

/* Find direct download URL for LinuxOnVita.vpk in GitHub release JSON */
static int find_vpk_asset_url(const char *json, char *url_out, size_t url_out_len) {
    const char *pos = json;

    while ((pos = strstr(pos, "\"browser_download_url\":")) != NULL) {
        pos += strlen("\"browser_download_url\":");
        while (*pos == ' ' || *pos == '\t') {
            pos++;
        }

        if (*pos == '\"') {
            pos++;
            const char *end = strchr(pos, '\"');
            if (end) {
                size_t len = end - pos;
                if (strstr(pos, ".vpk") != NULL && strstr(pos, ".vpk") < end) {
                    if (len >= url_out_len) {
                        len = url_out_len - 1;
                    }
                    strncpy(url_out, pos, len);
                    url_out[len] = '\0';
                    return 0;
                }
            }
        }
    }

    return -1;
}

/* Check GitHub for new releases and handle download */
void check_for_updates(void) {
    gui_draw_progress_modal("CHECKING FOR UPDATES", "Initializing network...", 0.15f, "Connecting to network stack...", NULL);

    if (net_subsystem_init() < 0) {
        gui_draw_progress_modal("NETWORK ERROR", NULL, 0.0f, "Failed to initialize VitaOS network stack.", "TAP SCREEN OR PRESS CROSS TO RETURN");
        sceKernelDelayThread(400 * 1000);
        gui_wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START);
        return;
    }

    if (!is_network_online()) {
        gui_draw_progress_modal("WI-FI DISCONNECTED", NULL, 0.0f, "Please connect to Wi-Fi in VitaOS Settings and try again.", "TAP SCREEN OR PRESS CROSS TO RETURN");
        sceKernelDelayThread(400 * 1000);
        gui_wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START);
        net_subsystem_term();
        return;
    }

    gui_draw_progress_modal("CHECKING FOR UPDATES", "Querying GitHub API...", 0.40f, "Fetching latest release information...", NULL);

    MemoryBuffer resp;
    resp.data = (char *)malloc(16384);
    resp.size = 0;
    resp.capacity = 16384;
    if (resp.data) {
        resp.data[0] = '\0';
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        free(resp.data);
        gui_draw_progress_modal("UPDATE ERROR", NULL, 0.0f, "Failed to initialize cURL engine.", "TAP SCREEN OR PRESS CROSS TO RETURN");
        sceKernelDelayThread(400 * 1000);
        gui_wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_CIRCLE);
        net_subsystem_term();
        return;
    }

    /* Configure GitHub Releases API Request */
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/vnd.github.v3+json");
    headers = curl_slist_append(headers, "User-Agent: LinuxOnVita-Installer/1.6.0 (PlayStation Vita)");

    curl_easy_setopt(curl, CURLOPT_URL, GITHUB_RELEASES_API);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, mem_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&resp);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "LinuxOnVita-Installer/1.6.0 (PlayStation Vita)");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    char latest_tag[64] = {0};
    char release_name[128] = {0};
    char download_url[512] = {0};

    int api_success = 0;

    if (res == CURLE_OK && http_code == 200 && resp.data) {
        if (parse_json_string(resp.data, "tag_name", latest_tag, sizeof(latest_tag)) == 0) {
            parse_json_string(resp.data, "name", release_name, sizeof(release_name));
            if (find_vpk_asset_url(resp.data, download_url, sizeof(download_url)) == 0) {
                api_success = 1;
            }
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(resp.data);

    /* If API is rate-limited or fails, fallback to direct GitHub releases URL */
    if (!api_success) {
        if (res == CURLE_OK && http_code == 403) {
            /* GitHub API rate limit reached, offer direct download */
            strncpy(latest_tag, "Latest Release", sizeof(latest_tag) - 1);
            strncpy(download_url, GITHUB_FALLBACK_VPK_URL, sizeof(download_url) - 1);
            api_success = 1;
        } else {
            gui_draw_progress_modal("CHECK FAILED", "Could not reach GitHub Releases.", 0.0f, "Please verify your Wi-Fi internet connection.", "TAP SCREEN OR PRESS CROSS TO RETURN");
            sceKernelDelayThread(400 * 1000);
            gui_wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START);
            net_subsystem_term();
            return;
        }
    }

    /* Check if already on latest version */
    if (strcmp(latest_tag, CURRENT_APP_VERSION) == 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "You have the latest version installed (%s).", CURRENT_APP_VERSION);
        gui_draw_progress_modal("UP TO DATE", msg, 1.0f, "No newer release available on GitHub.", "PRESS CROSS TO RE-DOWNLOAD | CIRCLE TO RETURN");

        uint32_t btn = gui_wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START);
        if (btn & (SCE_CTRL_CIRCLE | SCE_CTRL_START)) {
            net_subsystem_term();
            return;
        }
    } else {
        /* Newer version available prompt */
        char update_msg[128];
        snprintf(update_msg, sizeof(update_msg), "Available: %s  |  Current: %s", latest_tag, CURRENT_APP_VERSION);

        gui_draw_progress_modal("NEW UPDATE AVAILABLE!", update_msg, 0.0f, "Download the latest LinuxOnVita.vpk to ux0:data/?", "PRESS CROSS TO DOWNLOAD | CIRCLE TO CANCEL");

        uint32_t btn = gui_wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START);
        if (btn & (SCE_CTRL_CIRCLE | SCE_CTRL_START)) {
            net_subsystem_term();
            return;
        }
    }

    /* Proceed to download update VPK */
    sceIoMkdir(UPDATE_TARGET_DIR, 0777);

    SceUID out_fd = sceIoOpen(UPDATE_TEMP_VPK, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (out_fd < 0) {
        gui_draw_progress_modal("STORAGE ERROR", NULL, 0.0f, "Could not open ux0:data/ for writing.", "TAP SCREEN OR PRESS CROSS TO RETURN");
        sceKernelDelayThread(400 * 1000);
        gui_wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_CIRCLE);
        net_subsystem_term();
        return;
    }

    DownloadContext dl_ctx;
    dl_ctx.fd = out_fd;
    dl_ctx.total_bytes = 0;
    dl_ctx.current_bytes = 0;
    dl_ctx.last_draw_tick = 0;
    dl_ctx.is_cancelled = 0;
    dl_ctx.tag_name = latest_tag;

    curl = curl_easy_init();
    if (!curl) {
        sceIoClose(out_fd);
        sceIoRemove(UPDATE_TEMP_VPK);
        gui_draw_progress_modal("DOWNLOAD ERROR", NULL, 0.0f, "Failed to initialize download transfer.", "TAP SCREEN OR PRESS CROSS TO RETURN");
        sceKernelDelayThread(400 * 1000);
        gui_wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_CIRCLE);
        net_subsystem_term();
        return;
    }

    gui_draw_progress_modal("STARTING DOWNLOAD", "Connecting to GitHub Releases CDN...", 0.0f, "Preparing file stream...", "PRESS CIRCLE TO CANCEL");

    curl_easy_setopt(curl, CURLOPT_URL, download_url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (PlayStation Vita 3.60) AppleWebKit/537.73");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&dl_ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xfer_progress_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void *)&dl_ctx);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    sceIoClose(out_fd);

    if (dl_ctx.is_cancelled) {
        sceIoRemove(UPDATE_TEMP_VPK);
        gui_draw_progress_modal("DOWNLOAD CANCELLED", NULL, 0.0f, "Update download was cancelled by user.", "TAP SCREEN OR PRESS CROSS TO RETURN");
        sceKernelDelayThread(500 * 1000);
        gui_wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START);
    } else if (res == CURLE_OK && (http_code == 200 || http_code == 302) && dl_ctx.current_bytes > 1024 * 1024) {
        /* Rename temporary file to target VPK */
        sceIoRemove(UPDATE_TARGET_VPK);
        sceIoRename(UPDATE_TEMP_VPK, UPDATE_TARGET_VPK);

        char complete_msg[128];
        snprintf(complete_msg, sizeof(complete_msg), "Saved to %s (%.1f MB)", UPDATE_TARGET_VPK, (float)dl_ctx.current_bytes / (1024.0f * 1024.0f));

        gui_draw_progress_modal("UPDATE DOWNLOAD COMPLETE!", complete_msg, 1.0f, "Install with VitaShell to apply the new version.", "TAP SCREEN OR PRESS CROSS TO FINISH");
        sceKernelDelayThread(600 * 1000);
        gui_wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START);
    } else {
        sceIoRemove(UPDATE_TEMP_VPK);
        char err_msg[128];
        snprintf(err_msg, sizeof(err_msg), "Download failed: %s (HTTP %ld)", curl_easy_strerror(res), http_code);
        gui_draw_progress_modal("DOWNLOAD FAILED", err_msg, 0.0f, "Please check connection and try again.", "TAP SCREEN OR PRESS CROSS TO RETURN");
        sceKernelDelayThread(500 * 1000);
        gui_wait_button_press(SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START);
    }

    net_subsystem_term();
}

