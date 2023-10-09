################################################################################
#
# Hafnium Kernel Module
#
################################################################################

BRKERNMOD_VERSION = 1.0
BRKERNMOD_SITE = $(shell readlink -e $(BR2_EXTERNAL_BRKERNMOD_PATH)/..)
BRKERNMOD_SITE_METHOD = local

define BRKERNMOD_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)/brkernmod all \
	  TOOLCHAINDIR=$(shell readlink -e $(BR2_EXTERNAL_BRKERNMOD_PATH)/../../../../toolchains/aarch64/bin/) \
	  LINUXDIR=$(shell readlink -e $(BR2_EXTERNAL_BRKERNMOD_PATH)/../../../../linux/) \
	  HAFNIUMDIR=$(shell readlink -e $(BR2_EXTERNAL_BRKERNMOD_PATH)/../../../../hafnium/)
endef

define BRKERNMOD_INSTALL_TARGET_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)/brkernmod install \
	  TOOLCHAINDIR=$(shell readlink -e $(BR2_EXTERNAL_BRKERNMOD_PATH)/../../../../toolchains/aarch64/bin/) \
	  LINUXDIR=$(shell readlink -e $(BR2_EXTERNAL_BRKERNMOD_PATH)/../../../../linux/) \
	  TARGETDIR=$(TARGET_DIR)
endef


# $(info vvvvvvvvv)
# $(info BRKERNMOD_SITE=$(BRKERNMOD_SITE))
# $(info BRKERNMOD_BUILD_CMDS=$(BRKERNMOD_BUILD_CMDS))
# $(info BRKERNMOD_INSTALL_TARGET_CMDS=$(BRKERNMOD_INSTALL_TARGET_CMDS))
# $(info ^^^^^^^^^)
$(eval $(generic-package))
