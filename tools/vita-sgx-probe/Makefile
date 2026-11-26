obj-m += vita_sgx_probe.o
obj-m += vita_gpu_diag.o
obj-m += vita_sgx_wake.o

KDIR ?= ../../sources/vita-linux-port/linux_vita
ARCH ?= arm
CROSS_COMPILE ?= arm-linux-gnueabihf-

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
