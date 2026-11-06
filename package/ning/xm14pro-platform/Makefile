# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-platform
PKG_VERSION:=1.0
PKG_RELEASE:=18
PKG_LICENSE:=GPL-2.0-only

include $(INCLUDE_DIR)/package.mk

define Package/xm14pro-platform
  SECTION:=base
  CATEGORY:=Base system
  TITLE:=Xiaomi 14 Pro stock-kernel platform integration
  DEPENDS:=@TARGET_xm14pro_armv8 +libubox +ubus +jsonfilter +iw-full +wifi-scripts +wpad-openssl +resize2fs \
	+@DRIVER_11AC_SUPPORT +@DRIVER_11AX_SUPPORT +@DRIVER_11BE_SUPPORT
endef

define Package/xm14pro-platform/description
 Mounts the original Xiaomi firmware partitions read-only and exposes
 non-sensitive platform diagnostics for the nwrt LuCI integration.
endef

define Package/xm14pro-platform/conffiles
/etc/config/xm14wifi
endef

define Build/Compile
endef

define Package/xm14pro-platform/install
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/etc/config/xm14wifi $(1)/etc/config/xm14wifi
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-platform $(1)/etc/init.d/xm14pro-platform
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-adsp $(1)/etc/init.d/xm14pro-adsp
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) ./files/usr/sbin/xm14pro-network-status $(1)/usr/sbin/xm14pro-network-status
	$(INSTALL_BIN) ./files/usr/sbin/xm14pro-wifi-profile $(1)/usr/sbin/xm14pro-wifi-profile
endef

$(eval $(call BuildPackage,xm14pro-platform))
