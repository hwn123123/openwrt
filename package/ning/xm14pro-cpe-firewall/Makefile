# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-cpe-firewall
PKG_VERSION:=1.0
PKG_RELEASE:=4
PKG_LICENSE:=GPL-2.0-only
PKGARCH:=all

include $(INCLUDE_DIR)/package.mk

define Package/xm14pro-cpe-firewall
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=Firewall
  TITLE:=Xiaomi 14 Pro CPE forwarding and NAT policy
  DEPENDS:=@TARGET_xm14pro_armv8 +xm14pro-iptables +jsonfilter +ubus +uci
endef

define Package/xm14pro-cpe-firewall/description
 Idempotent IPv4 forwarding, cellular masquerading, WAN management isolation
 and TCP MSS handling for the Xiaomi 14 Pro Wi-Fi and USB CPE networks.
endef

define Build/Compile
endef

define Package/xm14pro-cpe-firewall/conffiles
/etc/config/xm14cpe
endef

define Package/xm14pro-cpe-firewall/install
	$(INSTALL_DIR) $(1)/etc/config $(1)/etc/init.d $(1)/etc/hotplug.d/iface $(1)/usr/sbin
	$(INSTALL_CONF) ./files/etc/config/xm14cpe $(1)/etc/config/xm14cpe
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-cpe-firewall $(1)/etc/init.d/xm14pro-cpe-firewall
	$(INSTALL_DATA) ./files/etc/hotplug.d/iface/95-xm14pro-cpe $(1)/etc/hotplug.d/iface/95-xm14pro-cpe
	$(INSTALL_BIN) ./files/usr/sbin/xm14pro-cpe-firewall $(1)/usr/sbin/xm14pro-cpe-firewall
endef

$(eval $(call BuildPackage,xm14pro-cpe-firewall))
