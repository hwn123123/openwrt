# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-bonding
PKG_VERSION:=1.0.0
PKG_RELEASE:=9
PKG_LICENSE:=GPL-2.0-only

include $(INCLUDE_DIR)/package.mk

define Package/xm14pro-bonding
  SECTION:=net
  CATEGORY:=Network
  TITLE:=NWRT dual-SIM local connection load balancing
  DEPENDS:=@TARGET_xm14pro_armv8 +ip-full +xm14pro-iptables +xm14pro-cellular +xm14pro-nicm
endef

define Package/xm14pro-bonding/description
 Runs two independent cellular data sessions and distributes new IPv4
 connections between them with persistent per-connection routing marks.
endef

define Package/xm14pro-bonding/conffiles
/etc/config/xm14bonding
endef

define Build/Compile
endef

define Package/xm14pro-bonding/install
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/etc/config/xm14bonding $(1)/etc/config/xm14bonding
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-bonding $(1)/etc/init.d/xm14pro-bonding
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-bonding
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-bonding/run $(1)/usr/libexec/xm14pro-bonding/run
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-bonding/control $(1)/usr/libexec/xm14pro-bonding/control
endef

$(eval $(call BuildPackage,xm14pro-bonding))
