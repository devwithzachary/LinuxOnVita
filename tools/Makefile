CROSS_COMPILE ?= arm-linux-gnueabihf-
CC = $(CROSS_COMPILE)gcc
CFLAGS ?= -O2 -Wall -Wextra -std=gnu99 -static

TARGETS = fbkeyboard/fbkeyboard vita-input-mapper/vita-input-mapper fbdoom/fbdoom

.PHONY: all clean fbdoom/fbdoom

all: $(TARGETS)

fbkeyboard/fbkeyboard: fbkeyboard/fbkeyboard.c
	$(CC) $(CFLAGS) -o $@ $<

vita-input-mapper/vita-input-mapper: vita-input-mapper/vita-input-mapper.c
	$(CC) $(CFLAGS) -o $@ $<

fbdoom/fbdoom:
	$(MAKE) -C fbdoom CROSS_COMPILE=$(CROSS_COMPILE) NOSDL=1

clean:
	rm -f fbkeyboard/fbkeyboard vita-input-mapper/vita-input-mapper
	$(MAKE) -C fbdoom clean || true
