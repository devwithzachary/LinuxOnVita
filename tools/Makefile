CROSS_COMPILE ?= arm-linux-gnueabihf-
CC = $(CROSS_COMPILE)gcc
CFLAGS ?= -O2 -Wall -Wextra -std=gnu99 -static

TARGETS = fbkeyboard/fbkeyboard vita-input-mapper/vita-input-mapper fbdoom/fbdoom vita-tun/vita-tun vita-powerd/vita-powerd

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

fbdoom/fbdoom:
	$(MAKE) -C fbdoom CROSS_COMPILE=$(CROSS_COMPILE) NOSDL=1

clean:
	rm -f fbkeyboard/fbkeyboard vita-input-mapper/vita-input-mapper vita-tun/vita-tun vita-powerd/vita-powerd
	$(MAKE) -C fbdoom clean || true
