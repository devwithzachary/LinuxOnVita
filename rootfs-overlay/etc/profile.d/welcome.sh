#!/bin/sh
# Vita Linux Welcome Banner

if [ -t 0 ]; then
    if [ "$(tty 2>/dev/null)" = "/dev/tty1" ]; then
        # Limit console scrolling to rows 1-21 so text never touches the keyboard (rows 22-34)
        printf '\033[2J\033[1;21r\033[1;1H'
    fi

    echo "  ========================================================"
    echo "   🎮 Welcome to PlayStation Vita Linux 6.12!             "
    echo "  ========================================================"
    echo ""
    echo "   * System:     ARMv7 Cortex-A9 Quad-Core (SMP active)   "
    echo "   * Kernel:     $(uname -r)                              "
    echo "   * Hostname:   $(hostname)                              "

    IP=$(ip -4 addr show mlan0 2>/dev/null | grep -o 'inet [0-9.]*' | cut -d' ' -f2)
    [ -z "$IP" ] && IP=$(ip -4 addr show wlan0 2>/dev/null | grep -o 'inet [0-9.]*' | cut -d' ' -f2)
    if [ -n "$IP" ]; then
        echo "   * Wi-Fi IP:   $IP (SSH: ssh root@$IP)                 "
    else
        echo "   * Wi-Fi IP:   Connecting in background...             "
    fi
    echo ""
    echo "   Useful Commands:"
    echo "     - alpine-chroot   : Enter Alpine Linux with 'apk' package manager"
    echo "     - mount /mnt/ur0  : Mount Vita internal storage"
    echo "     - reboot          : Clean hardware reset back to VitaOS"
    echo "  ========================================================"
    echo ""
fi
