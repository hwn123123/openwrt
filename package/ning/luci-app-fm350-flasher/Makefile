include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-fm350-flasher
PKG_VERSION:=1.0.0
PKG_RELEASE:=30

PKG_LICENSE:=GPL-3.0-only
PKG_MAINTAINER:=ning
PKGARCH:=all

include $(INCLUDE_DIR)/package.mk

define Package/luci-app-fm350-flasher
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=Safe FM350-GL firmware backup and flasher
  DEPENDS:=+luci-base +rpcd +mtkclient-fm350 +kmod-usb-acm
endef

define Package/luci-app-fm350-flasher/description
 Target-independent LuCI workflow for importing FM350 firmware archives,
 making or importing a complete NAND backup, and updating only non-NV
 firmware partitions with the signed XML V6 FLASH-UPDATE scatter workflow in one DA session. Hardware with
 a WWAN rfkill node is power-cycled automatically; other hardware uses manual
 modem power cycling.
endef

define Package/luci-app-fm350-flasher/conffiles
/etc/config/fm350-flasher
endef

define Build/Compile
endef

define Package/luci-app-fm350-flasher/install
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/fm350-flasher.config $(1)/etc/config/fm350-flasher
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/fm350-flasher.init $(1)/etc/init.d/fm350-flasher
	$(INSTALL_DIR) $(1)/usr/libexec
	$(INSTALL_BIN) ./root/usr/libexec/fm350-flasher \
		$(1)/usr/libexec/fm350-flasher
	$(INSTALL_DIR) $(1)/usr/share/luci/menu.d
	$(INSTALL_CONF) ./root/usr/share/luci/menu.d/luci-app-fm350-flasher.json \
		$(1)/usr/share/luci/menu.d/luci-app-fm350-flasher.json
	$(INSTALL_DIR) $(1)/usr/share/rpcd/acl.d
	$(INSTALL_CONF) ./root/usr/share/rpcd/acl.d/luci-app-fm350-flasher.json \
		$(1)/usr/share/rpcd/acl.d/luci-app-fm350-flasher.json
	$(INSTALL_DIR) $(1)/www/luci-static/resources/view/fm350-flasher
	$(INSTALL_DATA) ./htdocs/luci-static/resources/view/fm350-flasher/overview.js \
		$(1)/www/luci-static/resources/view/fm350-flasher/overview.js
	$(INSTALL_DATA) ./htdocs/luci-static/resources/view/fm350-flasher/overview.css \
		$(1)/www/luci-static/resources/view/fm350-flasher/overview.css
endef

define Package/luci-app-fm350-flasher/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] || {
	/etc/init.d/fm350-flasher enable 2>/dev/null || true
	/etc/init.d/fm350-flasher start 2>/dev/null || true
	/etc/init.d/rpcd restart 2>/dev/null || true
}
exit 0
endef

$(eval $(call BuildPackage,luci-app-fm350-flasher))
