include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-photonicat2-base
PKG_VERSION:=1.0.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-3.0-only
PKG_MAINTAINER:=ning
PKG_FILE_DEPENDS += $(TOPDIR)/package/ning/photonicat2/htdocs/luci-static/resources/view/photonicat2/overview.js \
	$(TOPDIR)/package/ning/photonicat2/root/usr/share/luci/menu.d/luci-app-photonicat2-base.json

include $(INCLUDE_DIR)/package.mk

define Package/luci-app-photonicat2-base
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=Photonicat2功能
  HIDDEN:=1
  TITLE:=Photonicat 2 native LuCI device overview
  DEPENDS:=+photonicat2-native-core
endef

define Package/luci-app-photonicat2-base/description
  Shared native LuCI assets, access policy and device overview for the
  independent Photonicat 2 control pages.
endef

define Build/Compile
endef

define Package/luci-app-photonicat2-base/install
	$(INSTALL_DIR) $(1)/www/luci-static/resources/view/photonicat2
	$(INSTALL_DATA) $(TOPDIR)/package/ning/photonicat2/htdocs/luci-static/resources/view/photonicat2/overview.js \
		$(1)/www/luci-static/resources/view/photonicat2/overview.js
	$(INSTALL_DIR) $(1)/usr/share/luci/menu.d
	$(INSTALL_DATA) $(TOPDIR)/package/ning/photonicat2/root/usr/share/luci/menu.d/luci-app-photonicat2-base.json \
		$(1)/usr/share/luci/menu.d/luci-app-photonicat2-base.json
endef

define Package/luci-app-photonicat2-base/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] || /etc/init.d/rpcd restart 2>/dev/null || true
exit 0
endef

$(eval $(call BuildPackage,luci-app-photonicat2-base))
