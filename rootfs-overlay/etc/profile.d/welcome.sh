#!/bin/sh
# Vita Linux Welcome Banner

if [ -t 0 ]; then
    # Prevent duplicate execution within the same shell session (e.g. redundant sourcing or subshells)
    if [ -n "$_VITA_WELCOME_SHOWN" ]; then
        return 0 2>/dev/null || exit 0
    fi
    export _VITA_WELCOME_SHOWN=1

    if [ "$(tty 2>/dev/null)" = "/dev/tty1" ]; then
        clear 2>/dev/null || printf '\033[2J\033[H'
    fi

    export PS1='\[\033[01;32m\]root@vita\[\033[00m\]:\[\033[01;34m\]\w\[\033[00m\]# '

    HN=$(hostname 2>/dev/null)
    if [ -z "$HN" ] || [ "$HN" = "(none)" ]; then
        hostname -F /etc/hostname 2>/dev/null || hostname vita 2>/dev/null || true
        HN=$(hostname 2>/dev/null)
    fi

    IP=$(ip -4 addr show mlan0 2>/dev/null | grep -o 'inet [0-9.]*' | cut -d' ' -f2)
    [ -z "$IP" ] && IP=$(ip -4 addr show wlan0 2>/dev/null | grep -o 'inet [0-9.]*' | cut -d' ' -f2)


    echo "  ========================================================"
    echo "   Welcome to PlayStation Vita Linux 6.12!                "
    echo "  ========================================================"
    echo ""
    echo "   * System:     ARMv7 Cortex-A9 Quad-Core (SMP active)   "
    echo "   * Kernel:     $(uname -r)                              "
    echo "   * Hostname:   $HN                                      "

    if [ -n "$IP" ]; then
        echo "   * Wi-Fi IP:   $IP (SSH: ssh root@$IP)                 "
    else
        echo "   * Wi-Fi IP:   Connecting in background...             "
    fi
    echo ""
    echo "   Type 'help' for a list of useful commands and built-in software."
    echo "  ========================================================"
    echo ""
fi

# Intercept 'help' to route to vita-help while preserving builtin help for shell keywords
help() {
    if [ "$#" -eq 0 ]; then
        /usr/bin/vita-help
    elif [ -x "/usr/bin/$1" ] || [ "$1" = "wifi" ] || [ "$1" = "network" ] || [ "$1" = "bluetooth" ] || [ "$1" = "bt" ] || [ "$1" = "hardware" ] || [ "$1" = "power" ] || [ "$1" = "desktop" ] || [ "$1" = "storage" ] || [ "$1" = "memory" ] || [ "$1" = "audio" ] || [ "$1" = "gaming" ] || [ "$1" = "doom" ]; then
        /usr/bin/vita-help "$@"
    elif type builtin >/dev/null 2>&1 && builtin help "$1" >/dev/null 2>&1; then
        builtin help "$@"
    else
        /usr/bin/vita-help "$@"
    fi
}
alias help='help'
