include $(TOPDIR)/rules.mk

PKG_NAME:=pcat-manager-custom
PKG_VERSION:=2.0.0
PKG_RELEASE:=97

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/photonicat/rockchip_rk3568_pcat_manager.git
PKG_SOURCE_DATE:=2026-08-26
PKG_SOURCE_VERSION:=0e61a4abf35c423420310311f1b2b059deba0cac
PKG_MIRROR_HASH:=8ca68a19da4e5f2fb5e27d789c95de281bab0fd8cbe4257c5bc9465ce5e6320b

PKG_MAINTAINER:=Photonicat
PKG_LICENSE:=GPL-3.0-only
PKG_LICENSE_FILES:=LICENSE
PKG_INSTALL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/meson.mk

define Build/Prepare
	$(call Build/Prepare/Default)
	$(RM) $(PKG_BUILD_DIR)/src/modem-manager.c \
		$(PKG_BUILD_DIR)/src/modem-manager.h \
		$(PKG_BUILD_DIR)/src/fm350-mm.c
endef

define Package/pcat-manager-custom
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Photonicat 2 system management service
  URL:=https://github.com/photonicat/rockchip_rk3568_pcat_manager
	DEPENDS:=@TARGET_rockchip_armv8 \
	+glib2 +libjson-c \
	+curl +ethtool +kmod-rfkill-gpio
endef

define Package/pcat-manager-custom/description
  Official Photonicat service providing the power, battery, charger, RTC,
  scheduled shutdown and fan-control backend used by the Photonicat web
  interface. Cellular dialing is provided separately by QModem.
endef

define Package/pcat-manager-custom/conffiles
/etc/pcat-manager.conf
/etc/pcat-manager-userdata.conf
endef

define Package/pcat-manager-custom/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/pcat-manager $(1)/usr/bin/
	$(INSTALL_DIR) $(1)/etc
	$(INSTALL_CONF) ./files/pcat-manager.conf $(1)/etc/pcat-manager.conf
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/pcat-manager.init $(1)/etc/init.d/pcat-manager
endef

$(eval $(call BuildPackage,pcat-manager-custom))
