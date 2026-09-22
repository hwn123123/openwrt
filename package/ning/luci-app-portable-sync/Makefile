include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-portable-sync
PKG_VERSION:=1.0.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-3.0-only
PKG_MAINTAINER:=ning
PKGARCH:=all

include $(INCLUDE_DIR)/package.mk

define Package/luci-app-portable-sync
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=Portable Syncthing drive
  DEPENDS:=+luci-base +rpcd +syncthing
endef

define Package/luci-app-portable-sync/description
  Syncthing service manager for a portable encrypted sync drive, with both
  generic LuCI and optional Photonicat 2 interfaces.
endef

define Build/Compile
endef

define Package/luci-app-portable-sync/install
	$(INSTALL_DIR) $(1)/usr/share/luci/menu.d $(1)/usr/share/rpcd/acl.d
	$(INSTALL_CONF) ./root/usr/share/luci/menu.d/luci-app-portable-sync.json $(1)/usr/share/luci/menu.d/
	$(INSTALL_CONF) ./root/usr/share/rpcd/acl.d/luci-app-portable-sync.json $(1)/usr/share/rpcd/acl.d/
	$(INSTALL_DIR) $(1)/www/luci-static/resources/view/portable-sync
	$(INSTALL_DATA) ./htdocs/luci-static/resources/view/portable-sync/overview.js $(1)/www/luci-static/resources/view/portable-sync/
	$(INSTALL_DIR) $(1)/usr/share/pcat-manager-web/extensions/portable-sync
	$(INSTALL_DATA) ./pcat/manifest.json ./pcat/module.py ./pcat/page.html $(1)/usr/share/pcat-manager-web/extensions/portable-sync/
endef

define Package/luci-app-portable-sync/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] || {
	/etc/init.d/rpcd restart 2>/dev/null || true
	[ ! -x /etc/init.d/pcat-manager-web ] || /etc/init.d/pcat-manager-web restart 2>/dev/null || true
}
exit 0
endef

$(eval $(call BuildPackage,luci-app-portable-sync))
