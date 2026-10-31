# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-defaults
PKG_VERSION:=1.0
PKG_RELEASE:=7
PKG_LICENSE:=GPL-2.0-only
PKGARCH:=all

include $(INCLUDE_DIR)/package.mk

define Package/xm14pro-defaults
  SECTION:=base
  CATEGORY:=Base system
  TITLE:=nwrt regional and Xiaomi 14 Pro management defaults
  DEPENDS:=+uci +zoneinfo-asia +dockerd
endef

define Package/xm14pro-defaults/description
 First-boot nwrt identity, Simplified Chinese LuCI language,
 Asia/Shanghai timezone and an automatic USB CPE network link.
endef

define Build/Compile
endef

define Package/xm14pro-defaults/conffiles
/etc/config/system
/etc/config/network
endef

define Package/xm14pro-defaults/install
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/etc/config/system $(1)/etc/config/system
	$(INSTALL_CONF) ./files/etc/config/network $(1)/etc/config/network
	$(INSTALL_DIR) $(1)/etc/uci-defaults
	$(INSTALL_BIN) ./files/90-xm14pro-defaults $(1)/etc/uci-defaults/90-xm14pro-defaults
	$(INSTALL_BIN) ./files/91-xm14pro-network $(1)/etc/uci-defaults/91-xm14pro-network
	$(INSTALL_BIN) ./files/92-xm14pro-docker $(1)/etc/uci-defaults/92-xm14pro-docker
endef

$(eval $(call BuildPackage,xm14pro-defaults))
