include $(TOPDIR)/rules.mk

PKG_NAME:=photonicat2-native-core
PKG_VERSION:=1.0.0
PKG_RELEASE:=2
PKG_LICENSE:=GPL-3.0-only
PKG_MAINTAINER:=ning
PKG_FILE_DEPENDS += $(TOPDIR)/package/ning/photonicat2/src/pcat-socket.c \
	$(TOPDIR)/package/ning/photonicat2/root/etc/config/photonicat2 \
	$(TOPDIR)/package/ning/photonicat2/root/etc/init.d/photonicat2 \
	$(TOPDIR)/package/ning/photonicat2/root/etc/uci-defaults/95-photonicat2-onboard-wifi \
	$(TOPDIR)/package/ning/photonicat2/root/usr/libexec/photonicat2ctl \
	$(TOPDIR)/package/ning/photonicat2/root/usr/sbin/photonicat2d \
	$(TOPDIR)/package/ning/photonicat2/htdocs/luci-static/resources/photonicat2/common.js \
	$(TOPDIR)/package/ning/photonicat2/htdocs/luci-static/resources/photonicat2/photonicat2.css \
	$(TOPDIR)/package/ning/photonicat2/root/usr/share/rpcd/acl.d/luci-app-photonicat2-base.json

include $(INCLUDE_DIR)/package.mk

define Package/photonicat2-native-core
  SECTION:=utils
  CATEGORY:=Utilities
  HIDDEN:=1
  TITLE:=Photonicat 2 native hardware service
  DEPENDS:=@TARGET_rockchip_armv8 +jq +curl +pcat-manager +pcat2-display-mini +kmod-photonicat-pm +luci-base +rpcd +cgi-io
endef

define Package/photonicat2-native-core/description
  Shared PMU, fan, CPU, battery history and mini-display backend used by
  the independent Photonicat 2 LuCI applications. It does not install or
  start the vendor web interface and does not change the LuCI port.
endef

define Package/photonicat2-native-core/conffiles
/etc/config/photonicat2
/etc/pcat2_mini_display-user_config.json
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(TOPDIR)/package/ning/photonicat2/src/pcat-socket.c $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) $(TARGET_LDFLAGS) \
		-Wall -Wextra -O2 -std=c99 \
		-o $(PKG_BUILD_DIR)/photonicat2-socket \
		$(PKG_BUILD_DIR)/pcat-socket.c
endef

define Package/photonicat2-native-core/install
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) $(TOPDIR)/package/ning/photonicat2/root/etc/config/photonicat2 $(1)/etc/config/photonicat2
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) $(TOPDIR)/package/ning/photonicat2/root/etc/init.d/photonicat2 $(1)/etc/init.d/photonicat2
	$(INSTALL_DIR) $(1)/etc/uci-defaults
	$(INSTALL_BIN) $(TOPDIR)/package/ning/photonicat2/root/etc/uci-defaults/95-photonicat2-onboard-wifi \
		$(1)/etc/uci-defaults/95-photonicat2-onboard-wifi
	$(INSTALL_DIR) $(1)/usr/libexec
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/photonicat2-socket $(1)/usr/libexec/photonicat2-socket
	$(INSTALL_BIN) $(TOPDIR)/package/ning/photonicat2/root/usr/libexec/photonicat2ctl $(1)/usr/libexec/photonicat2ctl
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(TOPDIR)/package/ning/photonicat2/root/usr/sbin/photonicat2d $(1)/usr/sbin/photonicat2d
	$(INSTALL_DIR) $(1)/www/luci-static/resources/photonicat2
	$(INSTALL_DATA) $(TOPDIR)/package/ning/photonicat2/htdocs/luci-static/resources/photonicat2/* \
		$(1)/www/luci-static/resources/photonicat2/
	$(INSTALL_DIR) $(1)/usr/share/rpcd/acl.d
	$(INSTALL_DATA) $(TOPDIR)/package/ning/photonicat2/root/usr/share/rpcd/acl.d/luci-app-photonicat2-base.json \
		$(1)/usr/share/rpcd/acl.d/luci-app-photonicat2-base.json
endef

define Package/photonicat2-native-core/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] || {
	/etc/init.d/photonicat2 enable 2>/dev/null || true
	/etc/init.d/photonicat2 restart 2>/dev/null || true
}
exit 0
endef

$(eval $(call BuildPackage,photonicat2-native-core))
