# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-power-profile
PKG_VERSION:=1.1.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-only

include $(INCLUDE_DIR)/package.mk

define Package/xm14pro-power-profile
  SECTION:=base
  CATEGORY:=Base system
  TITLE:=Xiaomi 14 Pro NWRT CPU power profile
  DEPENDS:=@TARGET_xm14pro_armv8 +jsonfilter +iw-full
endef

define Package/xm14pro-power-profile/description
 Apply the Xiaomi 14 Pro WALT governor, core parking and cluster idle settings.
 The stock kernel leaves core masks unset and cluster idle disabled.
endef

define Build/Compile
endef

define Package/xm14pro-power-profile/install
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-power-profile $(1)/etc/init.d/xm14pro-power-profile
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) ./files/usr/sbin/xm14pro-power-sample $(1)/usr/sbin/xm14pro-power-sample
endef

$(eval $(call BuildPackage,xm14pro-power-profile))
