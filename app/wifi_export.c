/*
 * LinuxOnVita - PlayStation Vita Linux Bootstrapper & Installer
 *
 * Original kplugin loader by xerpi (2016)
 * Linux theming and file checks by CreepNT (2018)
 * Updates by DvaMishkiLapa (2020)
 * Storage mount selection and on-device installer by DevWithZachary (2026)
 * Wi-Fi VitaOS extractor and automatic wpa_supplicant generator by rompelhd (2026)
 *
 * Licensed under the GNU General Public License v3.0 (GPL-3.0)
 */

#include <vitasdk.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define MAX_WLAN 30
#define MAX_EXISTING_NETWORKS 128
#define MAX_SSID_LENGTH 64

static char *trim(char *str) {
    char *end;

    while (isspace((unsigned char)*str))
        str++;

    if (*str == '\0')
        return str;

    end = str + strlen(str) - 1;

    while (end > str && isspace((unsigned char)*end))
        end--;

    *(end + 1) = '\0';

    return str;
}

static void unquote_ssid(char *ssid) {
    size_t len;

    if (!ssid)
        return;

    len = strlen(ssid);

    if (len >= 2 && ssid[0] == '"' && ssid[len - 1] == '"') {
        memmove(ssid, ssid + 1, len - 2);
        ssid[len - 2] = '\0';
    }
}

static int ssid_exists(char existing[][MAX_SSID_LENGTH], int count,
                       const char *ssid) {
    int i;

    for (i = 0; i < count; i++) {
        if (strcmp(existing[i], ssid) == 0)
            return 1;
    }

    return 0;
}

static int read_existing_config(const char *path,
                                char existing[][MAX_SSID_LENGTH],
                                int *existing_count) {
    FILE *fp;
    char line[512];
    int valid_ctrl = 0;
    int valid_update = 0;
    int in_network = 0;

    *existing_count = 0;

    fp = fopen(path, "r");

    if (!fp)
        return 0;

    while (fgets(line, sizeof(line), fp)) {
        char *p = trim(line);

        if (strncmp(p, "ctrl_interface=", 15) == 0) {
            if (strcmp(p + 15, "/var/run/wpa_supplicant") == 0)
                valid_ctrl = 1;
        }

        if (strncmp(p, "update_config=", 14) == 0) {
            if (strcmp(p + 14, "1") == 0)
                valid_update = 1;
        }

        if (strcmp(p, "network={") == 0) {
            in_network = 1;
            continue;
        }

        if (in_network) {
            if (strcmp(p, "}") == 0) {
                in_network = 0;
                continue;
            }

            if (strncmp(p, "ssid=", 5) == 0) {
                char ssid[MAX_SSID_LENGTH];

                strncpy(ssid, p + 5, sizeof(ssid) - 1);
                ssid[sizeof(ssid) - 1] = '\0';

                p = trim(ssid);
                unquote_ssid(p);

                if (p[0] != '\0' &&
                    !ssid_exists(existing, *existing_count, p)) {
                    if (*existing_count < MAX_EXISTING_NETWORKS) {
                        strncpy(existing[*existing_count], p,
                                MAX_SSID_LENGTH - 1);
                        existing[*existing_count][MAX_SSID_LENGTH - 1] = '\0';
                        (*existing_count)++;
                    }
                }
            }
        }
    }

    fclose(fp);

    if (!valid_ctrl || !valid_update)
        return 0;

    return 1;
}

static int copy_file(const char *src, const char *dst) {
    FILE *in;
    FILE *out;
    char buffer[1024];
    size_t size;

    in = fopen(src, "r");

    if (!in)
        return 0;

    out = fopen(dst, "w");

    if (!out) {
        fclose(in);
        return 0;
    }

    while ((size = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, size, out) != size) {
            fclose(in);
            fclose(out);
            return 0;
        }
    }

    fclose(in);
    fclose(out);

    return 1;
}

void export_wifi(const char *output) {
    FILE *fp;
    char temp_path[256];
    char existing[MAX_EXISTING_NETWORKS][MAX_SSID_LENGTH];
    int existing_count = 0;
    int existing_valid;

    existing_valid = read_existing_config(output, existing, &existing_count);

    snprintf(temp_path, sizeof(temp_path), "%s.tmp", output);

    if (existing_valid) {
        if (!copy_file(output, temp_path))
            return;
    } else {
        fp = fopen(temp_path, "w");

        if (!fp)
            return;

        fprintf(fp,
                "ctrl_interface=/var/run/wpa_supplicant\n"
                "update_config=1\n"
                "\n");

        fclose(fp);
        existing_count = 0;
    }

    fp = fopen(temp_path, "a");

    if (!fp) {
        remove(temp_path);
        return;
    }

    for (int i = 1; i <= MAX_WLAN; i++) {
        char path[64];
        char ssid[64];
        char psk[128];
        char wep_key[128];
        int wifi_security;

        snprintf(path, sizeof(path), "/CONFIG/NET/%02d/WIFI", i);

        memset(ssid, 0, sizeof(ssid));
        memset(psk, 0, sizeof(psk));
        memset(wep_key, 0, sizeof(wep_key));

        wifi_security = 0;

        if (sceRegMgrGetKeyStr(path, "ssid", ssid, sizeof(ssid) - 1) < 0)
            continue;

        if (ssid[0] == '\0')
            continue;

        sceRegMgrGetKeyInt(path, "wifi_security", &wifi_security);
        sceRegMgrGetKeyStr(path, "wpa_key", psk, sizeof(psk) - 1);
        sceRegMgrGetKeyStr(path, "wep_key", wep_key, sizeof(wep_key) - 1);

        if (ssid_exists(existing, existing_count, ssid))
            continue;

        if (existing_count < MAX_EXISTING_NETWORKS) {
            snprintf(existing[existing_count], MAX_SSID_LENGTH, "%s", ssid);
            existing_count++;
        }

        if (wep_key[0] != '\0') {
            fprintf(fp,
                    "network={\n"
                    "    ssid=\"%s\"\n"
                    "    key_mgmt=NONE\n"
                    "    wep_key0=\"%s\"\n"
                    "    wep_tx_keyidx=0\n"
                    "}\n"
                    "\n",
                    ssid,
                    wep_key);
        } else if (psk[0] != '\0') {
            fprintf(fp,
                    "network={\n"
                    "    ssid=\"%s\"\n"
                    "    psk=\"%s\"\n"
                    "    key_mgmt=WPA-PSK\n"
                    "}\n"
                    "\n",
                    ssid,
                    psk);
        } else {
            fprintf(fp,
                    "network={\n"
                    "    ssid=\"%s\"\n"
                    "    key_mgmt=NONE\n"
                    "}\n"
                    "\n",
                    ssid);
        }
    }

    fclose(fp);
    remove(output);

    if (rename(temp_path, output) != 0)
        remove(temp_path);
}
