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

#define MAX_WLAN 30

void export_wifi(const char *output)
{
    FILE *fp;
    char path[64];

    char ssid[64];
    char psk[128];
    char wep_key[128];
    int wifi_security;

    fp = fopen(output, "w");
    if (!fp)
        return;

    fprintf(fp,
        "# LinuxOnVita - Wi-Fi Configuration\n"
        "# Exported from VitaOS\n"
        "ctrl_interface=/var/run/wpa_supplicant\n"
        "update_config=1\n"
        "\n"
    );

    for (int i = 1; i <= MAX_WLAN; i++) {

        snprintf(path, sizeof(path),
                 "/CONFIG/NET/%02d/WIFI", i);

        memset(ssid, 0, sizeof(ssid));
        memset(psk, 0, sizeof(psk));
        memset(wep_key, 0, sizeof(wep_key));
        wifi_security = 0;

        if (sceRegMgrGetKeyStr(
                path,
                "ssid",
                ssid,
                sizeof(ssid) - 1) < 0) {
            continue;
        }

        if (ssid[0] == '\0')
            continue;

        sceRegMgrGetKeyInt(
            path,
            "wifi_security",
            &wifi_security
        );

        sceRegMgrGetKeyStr(
            path,
            "wpa_key",
            psk,
            sizeof(psk) - 1
        );

        sceRegMgrGetKeyStr(
            path,
            "wep_key",
            wep_key,
            sizeof(wep_key) - 1
        );

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
                wep_key
            );

        } else if (psk[0] != '\0') {

            fprintf(fp,
                "network={\n"
                "    ssid=\"%s\"\n"
                "    psk=\"%s\"\n"
                "    key_mgmt=WPA-PSK\n"
                "}\n"
                "\n",
                ssid,
                psk
            );

        } else {

            fprintf(fp,
                "network={\n"
                "    ssid=\"%s\"\n"
                "    key_mgmt=NONE\n"
                "}\n"
                "\n",
                ssid
            );
        }
    }

    fclose(fp);
}
