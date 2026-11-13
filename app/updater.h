#ifndef UPDATER_H
#define UPDATER_H

#include <stdint.h>

/* Current application release version */
#define CURRENT_APP_VERSION "v1.6.0-BETA5"

/* GitHub Repository Release Endpoints */
#define GITHUB_RELEASES_API "https://api.github.com/repos/devwithzachary/LinuxOnVita/releases/latest"
#define GITHUB_FALLBACK_VPK_URL "https://github.com/devwithzachary/LinuxOnVita/releases/latest/download/LinuxOnVita.vpk"

/* Target VPK destination path for VitaShell */
#define UPDATE_TARGET_DIR "ux0:data"
#define UPDATE_TARGET_VPK "ux0:data/LinuxOnVita.vpk"
#define UPDATE_TEMP_VPK   "ux0:data/LinuxOnVita.vpk.tmp"

/* Checks for GitHub releases, displays visual progress modals, and downloads updates */
void check_for_updates(void);

#endif /* UPDATER_H */

