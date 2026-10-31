# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-openclash-compat
PKG_VERSION:=1.0
PKG_RELEASE:=3
PKG_LICENSE:=GPL-2.0-only
PKGARCH:=all

include $(INCLUDE_DIR)/package.mk

define Package/xm14pro-openclash-compat
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=Web Servers/Proxies
  TITLE:=Xiaomi 14 Pro OpenClash network compatibility
  DEPENDS:=@TARGET_xm14pro_armv8 +ip-full +jsonfilter +ubus +uci +xm14pro-iptables
endef

define Package/xm14pro-openclash-compat/description
 Update-safe adapter which maps the Xiaomi 14 Pro wifi_lan interface to
 OpenClash without modifying OpenClash or the base nwrt network layout.
endef

define Build/Compile
endef

define Package/xm14pro-openclash-compat/conffiles
/etc/config/xm14openclash
endef

define Package/xm14pro-openclash-compat/install
	$(INSTALL_DIR) $(1)/etc/config $(1)/etc/init.d $(1)/etc/hotplug.d/iface $(1)/usr/sbin
	$(INSTALL_CONF) ./files/etc/config/xm14openclash $(1)/etc/config/xm14openclash
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-openclash-compat $(1)/etc/init.d/xm14pro-openclash-compat
	$(INSTALL_DATA) ./files/etc/hotplug.d/iface/96-xm14pro-openclash-compat \
		$(1)/etc/hotplug.d/iface/96-xm14pro-openclash-compat
	$(INSTALL_BIN) ./files/usr/sbin/xm14pro-openclash-compat \
		$(1)/usr/sbin/xm14pro-openclash-compat
endef

$(eval $(call BuildPackage,xm14pro-openclash-compat))
