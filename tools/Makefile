CROSS_COMPILE ?= arm-linux-gnueabihf-
CC = $(CROSS_COMPILE)gcc
CFLAGS ?= -O2 -Wall -Wextra -std=gnu99 -static

TARGETS = fbkeyboard/fbkeyboard vita-input-mapper/vita-input-mapper

.PHONY: all clean

all: $(TARGETS)

fbkeyboard/fbkeyboard: fbkeyboard/fbkeyboard.c
	$(CC) $(CFLAGS) -o $@ $<

vita-input-mapper/vita-input-mapper: vita-input-mapper/vita-input-mapper.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS)
