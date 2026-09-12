include $(TOPDIR)/rules.mk

PKG_NAME:=quectel-cm
PKG_VERSION:=1.6.0.24
PKG_RELEASE:=1

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/kmilo17pet/quectel-cm.git
PKG_SOURCE_DATE:=2026-03-24
PKG_SOURCE_VERSION:=b0bd2aada32e5820663be672c4ae3415b43c45e6
PKG_MIRROR_HASH:=ede267fc4f3b9cb0557adb0a1fe0eaca91714036c08d85d1f00d46360dd15f28

PKG_MAINTAINER:=Photonicat
PKG_LICENSE:=Quectel
PKG_LICENSE_FILES:=NOTICE
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/quectel-cm
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Quectel Connect Manager for the Photonicat modem backend
  URL:=https://github.com/kmilo17pet/quectel-cm
  DEPENDS:=+kmod-usb-wdm +kmod-usb-net-qmi-wwan \
	+kmod-usb-net-cdc-mbim +kmod-usb-net-cdc-ether
endef

define Package/quectel-cm/description
  Quectel Connect Manager invoked by the official pcat-manager backend for
  supported Quectel USB modems.
endef

define Build/Compile
	+$(MAKE) $(PKG_JOBS) -C $(PKG_BUILD_DIR) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) -Wall" \
		release
endef

define Package/quectel-cm/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/quectel-CM $(1)/usr/bin/quectel-cm
endef

$(eval $(call BuildPackage,quectel-cm))
