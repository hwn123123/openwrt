# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-boot-shim
PKG_VERSION:=0.25
PKG_RELEASE:=3
PKG_LICENSE:=GPL-2.0-only

include $(INCLUDE_DIR)/package.mk

define Package/xm14pro-boot-shim
  SECTION:=base
  CATEGORY:=Base system
  TITLE:=Xiaomi 14 Pro nwrt early boot shim
  DEPENDS:=@TARGET_xm14pro_armv8
endef

define Package/xm14pro-boot-shim/description
 Static PID 1 used by the Xiaomi 14 Pro B-slot boot chain to locate and mount
 the independent 200 GiB linux_root filesystem. It never formats storage.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/xm14pro-boot-shim.c $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) -static -std=c11 -Wall -Wextra -Werror \
		-ffunction-sections -fdata-sections \
		-Wl,--gc-sections \
		-o $(PKG_BUILD_DIR)/xm14pro-boot-shim \
		$(PKG_BUILD_DIR)/xm14pro-boot-shim.c
endef

define Package/xm14pro-boot-shim/install
	$(INSTALL_DIR) $(1)/usr/libexec/xm14
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xm14pro-boot-shim $(1)/usr/libexec/xm14/boot-shim
endef

$(eval $(call BuildPackage,xm14pro-boot-shim))
