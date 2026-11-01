# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-wifi-clients-acl
PKG_VERSION:=1.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-only

include $(INCLUDE_DIR)/package.mk

define Package/xm14pro-wifi-clients-acl
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Xiaomi 14 Pro LuCI associated-client permission
  DEPENDS:=@TARGET_xm14pro_armv8 +rpcd
endef

define Package/xm14pro-wifi-clients-acl/description
 Grants read-only hostapd station access to authenticated LuCI users so
 the stock Wireless and Status pages can show associated clients.
endef

define Build/Compile
endef

define Package/xm14pro-wifi-clients-acl/install
	$(INSTALL_DIR) $(1)/usr/share/rpcd/acl.d
	$(INSTALL_DATA) ./files/usr/share/rpcd/acl.d/xm14pro-wifi-clients.json \
		$(1)/usr/share/rpcd/acl.d/xm14pro-wifi-clients.json
endef

$(eval $(call BuildPackage,xm14pro-wifi-clients-acl))
