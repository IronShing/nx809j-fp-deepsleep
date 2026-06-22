# Out-of-tree build for the nx809j_fp_deepsleep kprobe module.
# KDIR must point at a prepared 6.12.x kernel build tree (modules_prepare done,
# Module.symvers present). For a loadable result on 6.12.23-android16-OP-WILD,
# build against that kernel's artifacts (see .github/workflows/build-ko.yml).
obj-m += fp_deepsleep.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
