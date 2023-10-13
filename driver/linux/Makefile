# SPDX-FileCopyrightText: 2018 The Hafnium Authors.
# SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.
# SPDX-License-Identifier: GPL-2.0-only

$(info QW)
$(info $(HAFNIUM_PATH))
HAFNIUM_PATH ?= $(CURDIR)/../..

ifneq ($(KERNELRELEASE),)

obj-m += hafnium.o

hafnium-y += main.o
hafnium-y += vmlib/aarch64/call.o
hafnium-y += vmlib/ffa.o

ccflags-y = -I$(HAFNIUM_PATH)/inc/vmapi -I$(M)/inc

else

KERNEL_PATH ?= $(HAFNIUM_PATH)/third_party/linux
ARCH ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-
CHECKPATCH ?= $(KERNEL_PATH)/scripts/checkpatch.pl -q

all:
	cp -r $(HAFNIUM_PATH)/vmlib/ $(CURDIR)
	$(MAKE) -C $(KERNEL_PATH) HAFNIUM_PATH=$(HAFNIUM_PATH) M=$(CURDIR) O=$(O) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KERNEL_PATH) HAFNIUM_PATH=$(HAFNIUM_PATH) M=$(CURDIR) O=$(O) clean
	rm -rf vmlib

checkpatch:
	$(CHECKPATCH) -f main.c

endif
