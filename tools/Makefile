CROSS_COMPILE ?= arm-linux-gnueabihf-
CC = $(CROSS_COMPILE)gcc
CFLAGS ?= -O2 -Wall -Wextra -std=gnu99 -static

TARGETS = fbkeyboard/fbkeyboard vita-input-mapper/vita-input-mapper fbdoom/fbdoom vita-tun/vita-tun vita-powerd/vita-powerd vita-webui/vita-webui-server vita-gui/vita-wifi-gui vita-gui/vita-cpu-gui vita-gui/vita-reboot-gui vita-gui/vita-welcome-gui

.PHONY: all clean fbdoom/fbdoom

all: $(TARGETS)

fbkeyboard/fbkeyboard: fbkeyboard/fbkeyboard.c
	$(CC) $(CFLAGS) -o $@ $<

vita-input-mapper/vita-input-mapper: vita-input-mapper/vita-input-mapper.c
	$(CC) $(CFLAGS) -o $@ $< -lm

vita-tun/vita-tun: vita-tun/vita-tun.c
	$(CC) $(CFLAGS) -o $@ $<

vita-powerd/vita-powerd: vita-powerd/vita-powerd.c
	$(CC) $(CFLAGS) -o $@ $<

vita-webui/vita-webui-server: vita-webui/vita-webui-server.c
	$(CC) $(CFLAGS) -o $@ $<

vita-gui/vita-wifi-gui: vita-gui/vita-wifi-gui.c vita-gui/x11_mini.c ../app/debugScreenFont.c
	$(CC) $(CFLAGS) -o $@ vita-gui/vita-wifi-gui.c vita-gui/x11_mini.c ../app/debugScreenFont.c

vita-gui/vita-cpu-gui: vita-gui/vita-cpu-gui.c vita-gui/x11_mini.c ../app/debugScreenFont.c
	$(CC) $(CFLAGS) -o $@ vita-gui/vita-cpu-gui.c vita-gui/x11_mini.c ../app/debugScreenFont.c

vita-gui/vita-reboot-gui: vita-gui/vita-reboot-gui.c vita-gui/x11_mini.c ../app/debugScreenFont.c
	$(CC) $(CFLAGS) -o $@ vita-gui/vita-reboot-gui.c vita-gui/x11_mini.c ../app/debugScreenFont.c

vita-gui/vita-welcome-gui: vita-gui/vita-welcome-gui.c vita-gui/x11_mini.c ../app/debugScreenFont.c
	$(CC) $(CFLAGS) -o $@ vita-gui/vita-welcome-gui.c vita-gui/x11_mini.c ../app/debugScreenFont.c

fbdoom/fbdoom:
	$(MAKE) -C fbdoom CROSS_COMPILE=$(CROSS_COMPILE) NOSDL=1

clean:
	rm -f fbkeyboard/fbkeyboard vita-input-mapper/vita-input-mapper vita-tun/vita-tun vita-powerd/vita-powerd vita-webui/vita-webui-server vita-gui/vita-gui vita-gui/vita-wifi-gui vita-gui/vita-cpu-gui vita-gui/vita-reboot-gui vita-gui/vita-welcome-gui
	$(MAKE) -C fbdoom clean || true
