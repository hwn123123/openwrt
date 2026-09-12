include $(TOPDIR)/rules.mk

PCAT_ROOT?=$(TOPDIR)/package/ning/photonicat2

PKG_NAME:=$(PCAT_PACKAGE)
PKG_VERSION:=1.0.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-3.0-only
PKG_MAINTAINER:=ning
PKG_FILE_DEPENDS += $(PCAT_ROOT)/htdocs/luci-static/resources/view/photonicat2/$(PCAT_VIEW).js \
	$(PCAT_ROOT)/menus/$(PKG_NAME).json \
	$(PCAT_ROOT)/luci-package.mk

include $(INCLUDE_DIR)/package.mk

define Package/$(PKG_NAME)
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=Photonicat2功能
  HIDDEN:=1
  TITLE:=$(PCAT_TITLE)
  DEPENDS:=+photonicat2-native-core $(PCAT_DEPENDS)
endef

define Package/$(PKG_NAME)/description
  Independent native LuCI page for $(PCAT_TITLE).
endef

define Build/Compile
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/www/luci-static/resources/view/photonicat2
	$(INSTALL_DATA) $(PCAT_ROOT)/htdocs/luci-static/resources/view/photonicat2/$(PCAT_VIEW).js \
		$(1)/www/luci-static/resources/view/photonicat2/$(PCAT_VIEW).js
	$(INSTALL_DIR) $(1)/usr/share/luci/menu.d
	$(INSTALL_DATA) $(PCAT_ROOT)/menus/$(PKG_NAME).json \
		$(1)/usr/share/luci/menu.d/$(PKG_NAME).json
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
