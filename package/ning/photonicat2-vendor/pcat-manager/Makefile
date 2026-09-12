include $(TOPDIR)/rules.mk

PKG_NAME:=pcat-manager
PKG_VERSION:=2.0.0
PKG_RELEASE:=7

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

define Package/pcat-manager
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Official Photonicat system and modem manager
  URL:=https://github.com/photonicat/rockchip_rk3568_pcat_manager
  DEPENDS:=@TARGET_rockchip_armv8 \
	+glib2 +libjson-c +libusb-1.0 +zlib \
	+kmod-photonicat-pm +quectel-cm \
	+kmod-usb-acm +kmod-usb-serial-option \
	+kmod-usb-net-cdc-ncm +kmod-usb-net-rndis
endef

define Package/pcat-manager/description
  Official Photonicat backend, including the vendor modem manager, cellular
  dialer, PMU updater and the control socket used by the vendor Web UI.
endef

define Package/pcat-manager/conffiles
/etc/pcat-manager.conf
/etc/pcat-manager-userdata.conf
/etc/pcat-apn.conf
endef

define Package/pcat-manager/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/pcat-manager $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/fm350-mm $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/pcat-pmu-updater $(1)/usr/bin/
	$(INSTALL_DIR) $(1)/etc
	$(INSTALL_CONF) ./files/pcat-manager.conf $(1)/etc/pcat-manager.conf
	$(INSTALL_CONF) ./files/pcat-apn.conf $(1)/etc/pcat-apn.conf
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/pcat-manager.init $(1)/etc/init.d/pcat-manager
endef

$(eval $(call BuildPackage,pcat-manager))
