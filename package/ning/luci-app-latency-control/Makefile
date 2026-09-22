include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-latency-control
PKG_VERSION:=1.0.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-3.0-only
PKG_MAINTAINER:=ning
PKGARCH:=all

include $(INCLUDE_DIR)/package.mk

define Package/luci-app-latency-control
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=Game and video latency control
  DEPENDS:=+luci-base +rpcd +sqm-scripts
endef

define Package/luci-app-latency-control/description
  CAKE/SQM latency control with independent LuCI and optional Photonicat 2 UI.
endef

define Build/Compile
endef

define Package/luci-app-latency-control/install
	$(INSTALL_DIR) $(1)/etc/uci-defaults
	$(INSTALL_BIN) ./files/99-latency-control $(1)/etc/uci-defaults/
	$(INSTALL_DIR) $(1)/usr/share/luci/menu.d $(1)/usr/share/rpcd/acl.d
	$(INSTALL_CONF) ./root/usr/share/luci/menu.d/luci-app-latency-control.json $(1)/usr/share/luci/menu.d/
	$(INSTALL_CONF) ./root/usr/share/rpcd/acl.d/luci-app-latency-control.json $(1)/usr/share/rpcd/acl.d/
	$(INSTALL_DIR) $(1)/www/luci-static/resources/view/latency-control
	$(INSTALL_DATA) ./htdocs/luci-static/resources/view/latency-control/overview.js $(1)/www/luci-static/resources/view/latency-control/
	$(INSTALL_DIR) $(1)/usr/share/pcat-manager-web/extensions/latency-control
	$(INSTALL_DATA) ./pcat/manifest.json ./pcat/module.py ./pcat/page.html $(1)/usr/share/pcat-manager-web/extensions/latency-control/
endef

define Package/luci-app-latency-control/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] || {
	/etc/init.d/rpcd restart 2>/dev/null || true
	[ ! -x /etc/init.d/pcat-manager-web ] || /etc/init.d/pcat-manager-web restart 2>/dev/null || true
}
exit 0
endef

$(eval $(call BuildPackage,luci-app-latency-control))
