#!/bin/sh
# Vita Linux Welcome Banner

if [ -t 0 ]; then
    if [ "$(tty 2>/dev/null)" = "/dev/tty1" ]; then
        clear 2>/dev/null || printf '\033[2J\033[H'
    fi

    export PS1='\[\033[01;32m\]root@vita\[\033[00m\]:\[\033[01;34m\]\w\[\033[00m\]# '

    HN=$(hostname 2>/dev/null)
    if [ -z "$HN" ] || [ "$HN" = "(none)" ]; then
        hostname -F /etc/hostname 2>/dev/null || hostname vita 2>/dev/null || true
        HN=$(hostname 2>/dev/null)
    fi

    echo "  ========================================================"
    echo "   🎮 Welcome to PlayStation Vita Linux 6.12!             "
    echo "  ========================================================"
    echo ""
    echo "   * System:     ARMv7 Cortex-A9 Quad-Core (SMP active)   "
    echo "   * Kernel:     $(uname -r)                              "
    echo "   * Hostname:   $HN                                      "

    IP=$(ip -4 addr show mlan0 2>/dev/null | grep -o 'inet [0-9.]*' | cut -d' ' -f2)
    [ -z "$IP" ] && IP=$(ip -4 addr show wlan0 2>/dev/null | grep -o 'inet [0-9.]*' | cut -d' ' -f2)
    if [ -n "$IP" ]; then
        echo "   * Wi-Fi IP:   $IP (SSH: ssh root@$IP)                 "
    else
        echo "   * Wi-Fi IP:   Connecting in background...             "
    fi
    echo ""
    echo "   Useful Commands:"
    echo "     - vita-wifi       : Scan, connect, and manage Wi-Fi networks"
    echo "     - vita-doom       : Play DOOM under Linux Framebuffer (/dev/fb0)"
    echo "     - vita-brightness : Control screen brightness / backlight"
    echo "     - alpine-chroot   : Enter Alpine Linux with 'apk' package manager"
    echo "     - reboot          : Clean hardware reset back to VitaOS"
    echo "  ========================================================"
    echo ""
fi
