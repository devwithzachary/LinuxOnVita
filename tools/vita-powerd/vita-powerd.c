/*
 * vita-powerd.c - PlayStation Vita Display Sleep, Auto-Dimming & Power Daemon
 *
 * Part of LinuxOnVita (GPL-3.0)
 *
 * Responsibilities:
 *   - OLED & LCD burn-in prevention: Inactivity detection across all input devices.
 *   - Automated Inactivity Timeout:
 *       * 3 minutes (180s): Dim display brightness to 20% PWM.
 *       * 5 minutes (300s): Blank display completely (sleep mode) while keeping daemons running.
 *       * Touching screen or pressing any button instantly wakes display back to previous level.
 *   - Power Button Integration (KEY_POWER):
 *       * Short press (< 3s): Instantly toggle display sleep / wake.
 *       * Long press (>= 3s): Safe, graceful system shutdown (poweroff).
 *   - CLI controls & state telemetry:
 *       * vita-powerd status
 *       * vita-powerd sleep
 *       * vita-powerd wake
 *       * vita-powerd dim
 *       * vita-powerd reload
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define POWERD_PID_FILE "/var/run/vita-powerd.pid"
#define POWERD_STATE_FILE "/var/run/vita-powerd.state"
#define POWERD_CONF_FILE "/etc/vita-powerd.conf"
#define BRIGHTNESS_STATE_FILE "/var/run/vita_brightness"
#define FB_BLANK_PATH "/sys/class/graphics/fb0/blank"
#define PS_LED_BRIGHTNESS "/sys/class/leds/gpio-led-ps-button-blue/brightness"

#define MAX_INPUT_DEVS 32
#define STICK_CENTER 128
#define STICK_DEADZONE 25

typedef enum {
    STATE_AWAKE = 0,
    STATE_DIMMED = 1,
    STATE_SLEEP = 2
} display_state_t;

/* Daemon Configuration (Defaults) */
static int g_dim_timeout = 180;        /* 3 minutes */
static int g_sleep_timeout = 300;      /* 5 minutes */
static int g_dim_brightness = 20;      /* 20% */
static int g_low_battery_pct = 15;     /* 15% */
static int g_crit_battery_pct = 5;     /* 5% */
static int g_power_hold_secs = 3;      /* 3 seconds */

/* Runtime State */
static volatile sig_atomic_t g_running = 1;
static volatile sig_atomic_t g_force_sleep = 0;
static volatile sig_atomic_t g_force_wake = 0;
static volatile sig_atomic_t g_reload_conf = 0;

static display_state_t g_state = STATE_AWAKE;
static int g_saved_brightness = 100;
static time_t g_last_activity = 0;
static bool g_power_pressed = false;
static time_t g_power_press_time = 0;
static bool g_shutdown_initiated = false;

/* Battery telemetry */
static int g_battery_pct = -1;
static bool g_battery_charging = false;
static bool g_battery_monitored = false;
static int g_led_blink_counter = 0;

struct input_dev_node {
    int fd;
    int index;
    char name[128];
};

static struct input_dev_node g_devs[MAX_INPUT_DEVS];
static int g_num_devs = 0;

/* Function prototypes */
static void load_config(void);
static void write_state(void);
static void apply_brightness(int pct);
static void blank_screen(bool blank);
static void set_led_state(int on);
static void check_battery(void);
static void trigger_shutdown(const char *reason);
static void transition_to(display_state_t new_state);
static void scan_input_devices(void);
static void close_input_devices(void);

/* Signal handlers */
static void sig_handler(int sig) {
    switch (sig) {
        case SIGTERM:
        case SIGINT:
            g_running = 0;
            break;
        case SIGUSR1:
            g_force_sleep = 1;
            break;
        case SIGUSR2:
            g_force_wake = 1;
            break;
        case SIGHUP:
            g_reload_conf = 1;
            break;
    }
}

static void load_config(void) {
    FILE *f = fopen(POWERD_CONF_FILE, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *ptr = line;
        while (*ptr == ' ' || *ptr == '\t') ptr++;
        if (*ptr == '#' || *ptr == '\n' || *ptr == '\0') continue;

        char key[64], val[64];
        if (sscanf(ptr, "%63[^=]=%63s", key, val) == 2) {
            int num = atoi(val);
            if (strcmp(key, "DIM_TIMEOUT") == 0) {
                if (num >= 0) g_dim_timeout = num;
            } else if (strcmp(key, "SLEEP_TIMEOUT") == 0) {
                if (num >= 0) g_sleep_timeout = num;
            } else if (strcmp(key, "DIM_BRIGHTNESS") == 0) {
                if (num >= 1 && num <= 100) g_dim_brightness = num;
            } else if (strcmp(key, "LOW_BATTERY_PCT") == 0) {
                if (num >= 1 && num <= 50) g_low_battery_pct = num;
            } else if (strcmp(key, "CRIT_BATTERY_PCT") == 0) {
                if (num >= 1 && num <= 20) g_crit_battery_pct = num;
            } else if (strcmp(key, "POWER_HOLD_SECS") == 0) {
                if (num >= 1 && num <= 10) g_power_hold_secs = num;
            }
        }
    }
    fclose(f);
}

static int get_current_brightness(void) {
    FILE *f = fopen(BRIGHTNESS_STATE_FILE, "r");
    if (f) {
        int val = 100;
        if (fscanf(f, "%d", &val) == 1) {
            fclose(f);
            if (val > 0 && val <= 100) return val;
        } else {
            fclose(f);
        }
    }
    return 100;
}

static void apply_brightness(int pct) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "/usr/bin/vita-brightness set %d >/dev/null 2>&1", pct);
    int res = system(cmd);
    (void)res;
}

static void blank_screen(bool blank) {
    int fd = open(FB_BLANK_PATH, O_WRONLY);
    if (fd >= 0) {
        const char *val = blank ? "1\n" : "0\n";
        ssize_t w = write(fd, val, 2);
        (void)w;
        close(fd);
    }

    /* X11 DPMS sync if running */
    if (blank) {
        int res = system("xset dpms force off >/dev/null 2>&1 || true");
        (void)res;
    } else {
        int res = system("xset dpms force on >/dev/null 2>&1 || true");
        (void)res;
    }
}

static void set_led_state(int on) {
    int fd = open(PS_LED_BRIGHTNESS, O_WRONLY);
    if (fd >= 0) {
        const char *val = on ? "1\n" : "0\n";
        ssize_t w = write(fd, val, 2);
        (void)w;
        close(fd);
    }
}

static void write_state(void) {
    FILE *f = fopen(POWERD_STATE_FILE, "w");
    if (!f) return;

    time_t now = time(NULL);
    int idle_secs = (int)(now - g_last_activity);
    const char *state_str = "AWAKE";
    if (g_state == STATE_DIMMED) state_str = "DIMMED";
    else if (g_state == STATE_SLEEP) state_str = "SLEEP";

    fprintf(f, "STATE=%s\n", state_str);
    fprintf(f, "PID=%d\n", getpid());
    fprintf(f, "IDLE_SECONDS=%d\n", idle_secs);
    fprintf(f, "DIM_TIMEOUT=%d\n", g_dim_timeout);
    fprintf(f, "SLEEP_TIMEOUT=%d\n", g_sleep_timeout);
    fprintf(f, "DIM_BRIGHTNESS=%d\n", g_dim_brightness);
    fprintf(f, "SAVED_BRIGHTNESS=%d\n", g_saved_brightness);
    fprintf(f, "POWER_HOLD_SECS=%d\n", g_power_hold_secs);
    if (g_battery_monitored && g_battery_pct >= 0) {
        fprintf(f, "BATTERY_PCT=%d\n", g_battery_pct);
        fprintf(f, "BATTERY_CHARGING=%d\n", g_battery_charging ? 1 : 0);
    } else {
        fprintf(f, "BATTERY_PCT=-1\n");
        fprintf(f, "BATTERY_CHARGING=0\n");
    }
    fclose(f);
}

static void transition_to(display_state_t new_state) {
    if (g_state == new_state) return;

    if (new_state == STATE_DIMMED) {
        if (g_state == STATE_AWAKE) {
            int curr = get_current_brightness();
            if (curr > g_dim_brightness) {
                g_saved_brightness = curr;
            }
        }
        apply_brightness(g_dim_brightness);
        g_state = STATE_DIMMED;
    } else if (new_state == STATE_SLEEP) {
        if (g_state == STATE_AWAKE) {
            int curr = get_current_brightness();
            if (curr > 0) {
                g_saved_brightness = curr;
            }
        }
        apply_brightness(0);
        blank_screen(true);
        g_state = STATE_SLEEP;
    } else if (new_state == STATE_AWAKE) {
        blank_screen(false);
        int target = g_saved_brightness > 0 ? g_saved_brightness : 100;
        apply_brightness(target);
        g_last_activity = time(NULL);
        g_state = STATE_AWAKE;
    }

    write_state();
}

static void trigger_shutdown(const char *reason) {
    if (g_shutdown_initiated) return;
    g_shutdown_initiated = true;

    /* Write emergency notice to kmsg and console */
    int kmsg = open("/dev/kmsg", O_WRONLY);
    if (kmsg >= 0) {
        dprintf(kmsg, "<1>vita-powerd: Safe graceful system shutdown initiated: %s\n", reason);
        close(kmsg);
    }

    FILE *c = fopen("/dev/console", "w");
    if (c) {
        fprintf(c, "\n\033[1;31m[vita-powerd] Safe graceful system shutdown: %s\033[0m\n", reason);
        fclose(c);
    }

    /* Visual confirmation: 3 fast LED blinks */
    for (int i = 0; i < 3; i++) {
        set_led_state(0);
        usleep(100000);
        set_led_state(1);
        usleep(100000);
    }

    int res = system("/usr/bin/poweroff &");
    (void)res;
}

static void check_battery(void) {
    DIR *d = opendir("/sys/class/power_supply");
    if (!d) return;

    struct dirent *de;
    char path[512];
    char buf[64];
    bool found = false;

    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;

        snprintf(path, sizeof(path), "/sys/class/power_supply/%s/capacity", de->d_name);
        FILE *f = fopen(path, "r");
        if (f) {
            if (fgets(buf, sizeof(buf), f)) {
                g_battery_pct = atoi(buf);
                found = true;
            }
            fclose(f);
        }

        snprintf(path, sizeof(path), "/sys/class/power_supply/%s/status", de->d_name);
        f = fopen(path, "r");
        g_battery_charging = false;
        if (f) {
            if (fgets(buf, sizeof(buf), f)) {
                if (strstr(buf, "Charging") || strstr(buf, "Full")) {
                    g_battery_charging = true;
                }
            }
            fclose(f);
        }

        if (found) break;
    }
    closedir(d);

    g_battery_monitored = found;

    if (!found) return;

    /* Emergency critical battery protection */
    if (!g_battery_charging && g_battery_pct >= 0 && g_battery_pct <= g_crit_battery_pct) {
        trigger_shutdown("Critical battery level reached (<= 5%)");
        return;
    }

    /* Low battery alert: Blink PS button LED */
    if (!g_battery_charging && g_battery_pct >= 0 && g_battery_pct <= g_low_battery_pct) {
        g_led_blink_counter++;
        set_led_state(g_led_blink_counter % 2 == 0);
    } else {
        /* Normal operation or charging: Keep LED steady ON */
        set_led_state(1);
    }
}

static void scan_input_devices(void) {
    close_input_devices();
    char path[256];
    char name[128];

    for (int i = 0; i < 32 && g_num_devs < MAX_INPUT_DEVS; i++) {
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        memset(name, 0, sizeof(name));
        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
            snprintf(name, sizeof(name), "event%d", i);
        }

        g_devs[g_num_devs].fd = fd;
        g_devs[g_num_devs].index = i;
        snprintf(g_devs[g_num_devs].name, sizeof(g_devs[g_num_devs].name), "%s", name);
        g_num_devs++;
    }
}

static void close_input_devices(void) {
    for (int i = 0; i < g_num_devs; i++) {
        if (g_devs[i].fd >= 0) {
            close(g_devs[i].fd);
            g_devs[i].fd = -1;
        }
    }
    g_num_devs = 0;
}

static int run_daemon(void) {
    /* Write PID file */
    FILE *pf = fopen(POWERD_PID_FILE, "w");
    if (pf) {
        fprintf(pf, "%d\n", getpid());
        fclose(pf);
    }

    /* Setup signal handlers */
    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);
    signal(SIGUSR1, sig_handler);
    signal(SIGUSR2, sig_handler);
    signal(SIGHUP, sig_handler);

    load_config();
    g_saved_brightness = get_current_brightness();
    g_last_activity = time(NULL);
    write_state();

    scan_input_devices();

    time_t last_scan_time = g_last_activity;
    time_t last_battery_time = g_last_activity;
    time_t last_state_write = g_last_activity;

    struct pollfd fds[MAX_INPUT_DEVS];

    while (g_running) {
        time_t now = time(NULL);

        /* Handle signals */
        if (g_reload_conf) {
            g_reload_conf = 0;
            load_config();
        }
        if (g_force_sleep) {
            g_force_sleep = 0;
            transition_to(STATE_SLEEP);
        }
        if (g_force_wake) {
            g_force_wake = 0;
            transition_to(STATE_AWAKE);
        }

        /* Periodic input device re-scan (every 20 seconds) */
        if (now - last_scan_time >= 20 || g_num_devs == 0) {
            last_scan_time = now;
            scan_input_devices();
        }

        /* Periodic battery telemetry check (every 5 seconds) */
        if (now - last_battery_time >= 5) {
            last_battery_time = now;
            check_battery();
        }

        /* Periodic state file refresh (every 5 seconds) */
        if (now - last_state_write >= 5) {
            last_state_write = now;
            write_state();
        }

        /* Inactivity Timeout Evaluation */
        int idle_secs = (int)(now - g_last_activity);
        if (g_state == STATE_AWAKE) {
            if (g_dim_timeout > 0 && idle_secs >= g_dim_timeout) {
                transition_to(STATE_DIMMED);
            } else if (g_sleep_timeout > 0 && idle_secs >= g_sleep_timeout) {
                transition_to(STATE_SLEEP);
            }
        } else if (g_state == STATE_DIMMED) {
            if (g_sleep_timeout > 0 && idle_secs >= g_sleep_timeout) {
                transition_to(STATE_SLEEP);
            }
        }

        /* Power button long-press evaluation */
        if (g_power_pressed) {
            if (now - g_power_press_time >= g_power_hold_secs) {
                g_power_pressed = false;
                trigger_shutdown("Power button long-press (>= 3s)");
            }
        }

        /* Setup poll fds */
        for (int i = 0; i < g_num_devs; i++) {
            fds[i].fd = g_devs[i].fd;
            fds[i].events = POLLIN;
            fds[i].revents = 0;
        }

        int ret = poll(fds, g_num_devs, 250);
        if (ret < 0) {
            if (errno == EINTR) continue;
            usleep(100000);
            continue;
        }

        if (ret == 0) {
            /* Poll timeout (250ms elapsed) - continue loop to check timers */
            continue;
        }

        /* Process pending events */
        for (int i = 0; i < g_num_devs; i++) {
            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                /* Dev error or disconnect, force re-scan */
                last_scan_time = 0;
                continue;
            }

            if (fds[i].revents & POLLIN) {
                struct input_event ev;
                while (read(g_devs[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
                    if (ev.type == EV_KEY) {
                        if (ev.code == KEY_POWER) {
                            if (ev.value == 1) {
                                /* Power button pressed down */
                                g_power_pressed = true;
                                g_power_press_time = time(NULL);
                            } else if (ev.value == 0) {
                                /* Power button released */
                                if (g_power_pressed) {
                                    g_power_pressed = false;
                                    time_t duration = time(NULL) - g_power_press_time;
                                    if (duration < g_power_hold_secs) {
                                        /* Short press: toggle sleep / wake */
                                        if (g_state == STATE_SLEEP) {
                                            transition_to(STATE_AWAKE);
                                        } else {
                                            transition_to(STATE_SLEEP);
                                        }
                                    }
                                }
                            }
                            continue; /* Do not treat power button as regular wake activity */
                        }

                        /* Any other key press or release resets inactivity and wakes display */
                        if (ev.value == 0 || ev.value == 1) {
                            g_last_activity = time(NULL);
                            if (g_state != STATE_AWAKE) {
                                transition_to(STATE_AWAKE);
                            }
                        }
                    } else if (ev.type == EV_ABS) {
                        if (ev.code == ABS_X || ev.code == ABS_Y ||
                            ev.code == ABS_RX || ev.code == ABS_RY) {
                            /* Analog sticks: ignore slight jitter around center */
                            if (abs(ev.value - STICK_CENTER) > STICK_DEADZONE) {
                                g_last_activity = time(NULL);
                                if (g_state != STATE_AWAKE) {
                                    transition_to(STATE_AWAKE);
                                }
                            }
                        } else {
                            /* Touchscreen or D-Pad ABS: user activity */
                            g_last_activity = time(NULL);
                            if (g_state != STATE_AWAKE) {
                                transition_to(STATE_AWAKE);
                            }
                        }
                    } else if (ev.type == EV_REL) {
                        /* Relative mouse motion */
                        if (abs(ev.value) > 1) {
                            g_last_activity = time(NULL);
                            if (g_state != STATE_AWAKE) {
                                transition_to(STATE_AWAKE);
                            }
                        }
                    }
                }
            }
        }
    }

    /* Daemon exiting */
    transition_to(STATE_AWAKE);
    set_led_state(1);
    close_input_devices();
    unlink(POWERD_PID_FILE);
    unlink(POWERD_STATE_FILE);
    return 0;
}

/* CLI command helpers */
static int send_signal_to_daemon(int sig) {
    FILE *f = fopen(POWERD_PID_FILE, "r");
    if (!f) return -1;
    int pid = -1;
    if (fscanf(f, "%d", &pid) != 1) {
        fclose(f);
        return -1;
    }
    fclose(f);
    if (pid <= 0) return -1;
    return kill(pid, sig);
}

static void cmd_status(void) {
    printf("==================================================\n");
    printf(" PlayStation Vita Power Daemon (vita-powerd)\n");
    printf("==================================================\n");

    FILE *pf = fopen(POWERD_PID_FILE, "r");
    int pid = -1;
    if (pf) {
        if (fscanf(pf, "%d", &pid) != 1) pid = -1;
        fclose(pf);
    }

    if (pid <= 0 || kill(pid, 0) != 0) {
        printf(" Status:             Stopped (Daemon is not running)\n");
        printf(" PID File:           %s\n", POWERD_PID_FILE);
        printf("==================================================\n");
        printf(" To start daemon:    /usr/bin/vita-powerd --daemon\n");
        printf("==================================================\n");
        return;
    }

    printf(" Status:             Running (PID: %d)\n", pid);

    FILE *sf = fopen(POWERD_STATE_FILE, "r");
    if (sf) {
        char line[256];
        char state_str[64] = "UNKNOWN";
        int idle = -1, dim_t = -1, sleep_t = -1, dim_b = -1, saved_b = -1;
        int batt_pct = -1, batt_charging = 0;

        while (fgets(line, sizeof(line), sf)) {
            char k[64], v[64];
            if (sscanf(line, "%63[^=]=%63s", k, v) == 2) {
                if (strcmp(k, "STATE") == 0) snprintf(state_str, sizeof(state_str), "%s", v);
                else if (strcmp(k, "IDLE_SECONDS") == 0) idle = atoi(v);
                else if (strcmp(k, "DIM_TIMEOUT") == 0) dim_t = atoi(v);
                else if (strcmp(k, "SLEEP_TIMEOUT") == 0) sleep_t = atoi(v);
                else if (strcmp(k, "DIM_BRIGHTNESS") == 0) dim_b = atoi(v);
                else if (strcmp(k, "SAVED_BRIGHTNESS") == 0) saved_b = atoi(v);
                else if (strcmp(k, "BATTERY_PCT") == 0) batt_pct = atoi(v);
                else if (strcmp(k, "BATTERY_CHARGING") == 0) batt_charging = atoi(v);
            }
        }
        fclose(sf);

        printf(" Display State:      %s\n", state_str);
        if (idle >= 0) {
            int mins = idle / 60;
            int secs = idle % 60;
            printf(" Idle Duration:      %dm %02ds (%d seconds)\n", mins, secs, idle);
        }
        if (dim_t > 0) {
            printf(" Inactivity Dim:     %d seconds (%d minutes -> %d%% brightness)\n",
                   dim_t, dim_t / 60, dim_b);
        } else {
            printf(" Inactivity Dim:     Disabled\n");
        }
        if (sleep_t > 0) {
            printf(" Inactivity Sleep:   %d seconds (%d minutes -> Blank display)\n",
                   sleep_t, sleep_t / 60);
        } else {
            printf(" Inactivity Sleep:   Disabled\n");
        }
        if (saved_b > 0) {
            printf(" Active Brightness:  %d%%\n", saved_b);
        }
        printf(" Power Key Short:    Toggle Display Sleep / Wake\n");
        printf(" Power Key Long:     3 Seconds -> Graceful Shutdown (poweroff)\n");
        if (batt_pct >= 0) {
            printf(" Battery Level:      %d%% (%s)\n", batt_pct,
                   batt_charging ? "Charging" : "Discharging");
        }
    }
    printf("==================================================\n");
}

static void show_help(void) {
    printf("Usage: vita-powerd [COMMAND | OPTIONS]\n\n");
    printf("Display sleep, auto-dimming & power management daemon for PS Vita Linux.\n\n");
    printf("Commands:\n");
    printf("  status         - Show power daemon status, idle time, and display state\n");
    printf("  sleep          - Immediately blank display and put screen to sleep\n");
    printf("  wake           - Immediately wake display and restore active brightness\n");
    printf("  dim            - Immediately dim display to configured dim level (20%%)\n");
    printf("  reload         - Reload configuration from %s\n", POWERD_CONF_FILE);
    printf("  stop           - Stop the running vita-powerd daemon\n\n");
    printf("Options:\n");
    printf("  -d, --daemon   - Run as background daemon (default if no command given)\n");
    printf("  -f, --foreground - Run in foreground (debug mode)\n");
    printf("  -h, --help     - Show this help message\n\n");
    printf("Ergonomic Shortcuts:\n");
    printf("  Power Button Short Press  : Instantly toggle display sleep / wake\n");
    printf("  Power Button Long (3s)    : Clean graceful system shutdown (poweroff)\n");
    printf("  Touch / Buttons / Sticks  : Instantly wakes display from sleep or dim\n");
}

int main(int argc, char *argv[]) {
    if (argc > 1) {
        const char *cmd = argv[1];
        if (strcmp(cmd, "status") == 0 || strcmp(cmd, "info") == 0) {
            cmd_status();
            return 0;
        } else if (strcmp(cmd, "sleep") == 0) {
            if (send_signal_to_daemon(SIGUSR1) == 0) {
                printf("Sent sleep command to vita-powerd.\n");
            } else {
                printf("vita-powerd not running. Setting brightness to 0 directly...\n");
                apply_brightness(0);
                blank_screen(true);
            }
            return 0;
        } else if (strcmp(cmd, "wake") == 0) {
            if (send_signal_to_daemon(SIGUSR2) == 0) {
                printf("Sent wake command to vita-powerd.\n");
            } else {
                printf("vita-powerd not running. Restoring brightness directly...\n");
                blank_screen(false);
                apply_brightness(100);
            }
            return 0;
        } else if (strcmp(cmd, "dim") == 0) {
            printf("Dimming display...\n");
            apply_brightness(g_dim_brightness);
            return 0;
        } else if (strcmp(cmd, "reload") == 0) {
            if (send_signal_to_daemon(SIGHUP) == 0) {
                printf("Sent reload configuration signal to vita-powerd.\n");
            } else {
                fprintf(stderr, "Error: vita-powerd daemon is not running.\n");
                return 1;
            }
            return 0;
        } else if (strcmp(cmd, "stop") == 0) {
            if (send_signal_to_daemon(SIGTERM) == 0) {
                printf("Sent termination signal to vita-powerd.\n");
            } else {
                printf("vita-powerd is not running.\n");
            }
            return 0;
        } else if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "help") == 0) {
            show_help();
            return 0;
        } else if (strcmp(cmd, "--foreground") == 0 || strcmp(cmd, "-f") == 0) {
            printf("Starting vita-powerd in foreground mode...\n");
            return run_daemon();
        }
    }

    /* Default or --daemon: check if already running */
    FILE *pf = fopen(POWERD_PID_FILE, "r");
    if (pf) {
        int existing_pid = -1;
        if (fscanf(pf, "%d", &existing_pid) == 1 && existing_pid > 0 && kill(existing_pid, 0) == 0) {
            fclose(pf);
            fprintf(stderr, "vita-powerd is already running with PID %d.\n", existing_pid);
            return 1;
        }
        fclose(pf);
    }

    /* Daemonize */
    if (daemon(0, 0) < 0) {
        perror("daemon");
        return 1;
    }

    return run_daemon();
}
