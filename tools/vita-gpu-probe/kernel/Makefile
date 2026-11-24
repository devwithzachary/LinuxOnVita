TARGET = gpu_probe
OBJS   = gpu_probe.o

LIBS = -lSceSysclibForDriver_stub -lSceSysmemForDriver_stub \
	-lSceIofilemgrForDriver_stub -lSceThreadmgrForDriver_stub

PREFIX  = arm-vita-eabi
CC      = $(PREFIX)-gcc
CFLAGS  = -Wl,-q -Wall -Wextra -O2 -nostartfiles -mcpu=cortex-a9 -mthumb-interwork

all: $(TARGET).skprx

%.skprx: %.velf
	vita-make-fself -c $< $@

%.velf: %.elf
	vita-elf-create -e $(TARGET).yml $< $@

$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

clean:
	@rm -f $(TARGET).skprx $(TARGET).velf $(TARGET).elf $(OBJS)

