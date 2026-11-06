# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-power-profile
PKG_VERSION:=1.1.0
PKG_RELEASE:=3
PKG_LICENSE:=GPL-2.0-only Apache-2.0

# Preserve the Android PIE metadata consumed by Bionic's linker.
RSTRIP:=:

include $(INCLUDE_DIR)/package.mk

define Package/xm14pro-power-profile
  SECTION:=base
  CATEGORY:=Base system
  TITLE:=NWRT CPU and automatic suspend policy
  DEPENDS:=@TARGET_xm14pro_armv8 +jsonfilter +iw-full +xm14pro-telephony-sidecar
endef

define Package/xm14pro-power-profile/description
 Apply the Xiaomi 14 Pro WALT governor, core parking and cluster idle settings.
 Own the framework-side automatic suspend registration and activity wake lock
 through the unmodified stock Android SystemSuspend service.
endef

define Build/Compile
endef

define Package/xm14pro-power-profile/conffiles
/etc/config/xm14power
endef

define Package/xm14pro-power-profile/install
	$(INSTALL_DIR) $(1)/etc/init.d $(1)/etc/config
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-power-profile $(1)/etc/init.d/xm14pro-power-profile
	$(INSTALL_DATA) ./files/etc/config/xm14power $(1)/etc/config/xm14power
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) ./files/usr/sbin/xm14pro-power-sample $(1)/usr/sbin/xm14pro-power-sample
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-qwes/android-root/vendor/bin
	$(INSTALL_BIN) ./files/android-root/vendor/bin/nwrt-power-policy \
		$(1)/usr/libexec/xm14pro-qwes/android-root/vendor/bin/nwrt-power-policy
endef

$(eval $(call BuildPackage,xm14pro-power-profile))
