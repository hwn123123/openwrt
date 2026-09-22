include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-push-center
PKG_VERSION:=1.0.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-3.0-only
PKG_MAINTAINER:=ning
PKGARCH:=all

include $(INCLUDE_DIR)/package.mk

define Package/luci-app-push-center
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=Mobile push notification center
  DEPENDS:=+luci-base +rpcd +curl +ca-bundle
endef

define Package/luci-app-push-center/description
  Phone notifications through ntfy, Telegram or a generic webhook. Includes
  standard LuCI and optional Photonicat 2 pages backed by one UCI config.
endef

define Package/luci-app-push-center/conffiles
/etc/config/push_center
endef

define Build/Compile
endef

define Package/luci-app-push-center/install
	$(INSTALL_DIR) $(1)/etc/config $(1)/etc/init.d $(1)/etc/hotplug.d/iface $(1)/usr/libexec
	$(INSTALL_CONF) ./files/push_center.config $(1)/etc/config/push_center
	$(INSTALL_BIN) ./files/push-center.init $(1)/etc/init.d/push-center
	$(INSTALL_BIN) ./files/95-push-center $(1)/etc/hotplug.d/iface/95-push-center
	$(INSTALL_BIN) ./files/push-center $(1)/usr/libexec/push-center
	$(INSTALL_DIR) $(1)/usr/share/luci/menu.d $(1)/usr/share/rpcd/acl.d
	$(INSTALL_CONF) ./root/usr/share/luci/menu.d/luci-app-push-center.json $(1)/usr/share/luci/menu.d/
	$(INSTALL_CONF) ./root/usr/share/rpcd/acl.d/luci-app-push-center.json $(1)/usr/share/rpcd/acl.d/
	$(INSTALL_DIR) $(1)/www/luci-static/resources/view/push-center
	$(INSTALL_DATA) ./htdocs/luci-static/resources/view/push-center/overview.js $(1)/www/luci-static/resources/view/push-center/
	$(INSTALL_DIR) $(1)/usr/share/pcat-manager-web/extensions/push-center
	$(INSTALL_DATA) ./pcat/manifest.json ./pcat/module.py ./pcat/page.html $(1)/usr/share/pcat-manager-web/extensions/push-center/
endef

define Package/luci-app-push-center/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] || {
	/etc/init.d/push-center enable 2>/dev/null || true
	/etc/init.d/rpcd restart 2>/dev/null || true
	[ ! -x /etc/init.d/pcat-manager-web ] || /etc/init.d/pcat-manager-web restart 2>/dev/null || true
}
exit 0
endef

$(eval $(call BuildPackage,luci-app-push-center))
