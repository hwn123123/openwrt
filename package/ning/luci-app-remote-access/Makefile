include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-remote-access
PKG_VERSION:=1.0.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-3.0-only
PKG_MAINTAINER:=ning
PKGARCH:=all

include $(INCLUDE_DIR)/package.mk

define Package/luci-app-remote-access
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=Secure remote access center
  DEPENDS:=+luci-base +rpcd +tailscale +ip-full +jsonfilter
endef

define Package/luci-app-remote-access/description
  Independent Tailscale remote-access manager with a standard LuCI page and
  an optional Photonicat 2 vendor-web extension. Both interfaces share UCI.
endef

define Package/luci-app-remote-access/conffiles
/etc/config/remote_access
endef

define Build/Compile
endef

define Package/luci-app-remote-access/install
	$(INSTALL_DIR) $(1)/etc/config $(1)/etc/init.d $(1)/usr/libexec
	$(INSTALL_CONF) ./files/remote_access.config $(1)/etc/config/remote_access
	$(INSTALL_BIN) ./files/remote-access.init $(1)/etc/init.d/remote-access
	$(INSTALL_BIN) ./files/remote-access $(1)/usr/libexec/remote-access
	$(INSTALL_DIR) $(1)/usr/share/luci/menu.d $(1)/usr/share/rpcd/acl.d
	$(INSTALL_CONF) ./root/usr/share/luci/menu.d/luci-app-remote-access.json $(1)/usr/share/luci/menu.d/
	$(INSTALL_CONF) ./root/usr/share/rpcd/acl.d/luci-app-remote-access.json $(1)/usr/share/rpcd/acl.d/
	$(INSTALL_DIR) $(1)/www/luci-static/resources/view/remote-access
	$(INSTALL_DATA) ./htdocs/luci-static/resources/view/remote-access/overview.js $(1)/www/luci-static/resources/view/remote-access/
	$(INSTALL_DIR) $(1)/usr/share/pcat-manager-web/extensions/remote-access
	$(INSTALL_DATA) ./pcat/manifest.json ./pcat/module.py ./pcat/page.html $(1)/usr/share/pcat-manager-web/extensions/remote-access/
endef

define Package/luci-app-remote-access/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] || {
	/etc/init.d/remote-access enable 2>/dev/null || true
	/etc/init.d/rpcd restart 2>/dev/null || true
	[ ! -x /etc/init.d/pcat-manager-web ] || /etc/init.d/pcat-manager-web restart 2>/dev/null || true
}
exit 0
endef

$(eval $(call BuildPackage,luci-app-remote-access))
