.PHONY: all clean install

# THIS_DIR := $(dir $(realpath $(firstword $(MAKEFILE_LIST))))

$(info $(TOOLCHAINDIR))
$(info $(LINUXDIR))

all:
	PATH=$(TOOLCHAINDIR):$(PATH) $(MAKE) -C $(shell readlink -e ..) \
	  KERNEL_PATH=$(LINUXDIR) HAFNIUM_PATH=$(HAFNIUMDIR) all

clean:
	PATH=$(TOOLCHAINDIR):$(PATH) $(MAKE) -C $(shell readlink -e ..) \
	  KERNEL_PATH=$(LINUXDIR) HAFNIUM_PATH=$(HAFNIUMDIR) clean

install:
	PATH=$(TOOLCHAINDIR):$(PATH) $(MAKE) -C $(LINUXDIR) \
	  INSTALL_MOD_PATH=$(TARGETDIR) M=$(shell readlink -e ..) \
	  modules_install

# temp:
# 	@readlink -e $(THIS_DIR)/../../../../toolchains/aarch64/bin/
# 	@readlink -e $(THIS_DIR)/../../../../linux/
# 	@readlink -e $(THIS_DIR)/../
# 	@PATH=$(shell readlink -e $(THIS_DIR)/../../../../toolchains/aarch64/bin/):$(PATH) printenv PATH
