# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-usb-gadget
PKG_VERSION:=0.8
PKG_RELEASE:=2
PKG_LICENSE:=GPL-2.0-only
PKGARCH:=all

include $(INCLUDE_DIR)/package.mk

define Package/xm14pro-usb-gadget
  SECTION:=base
  CATEGORY:=Base system
  TITLE:=Xiaomi 14 Pro stock NCM USB network composition
endef

define Package/xm14pro-usb-gadget/description
 Reproduces the stock Xiaomi/Qualcomm ncm branch from init.qcom.usb.rc while
 leaving interface addressing, DHCP, firewall and routing under nwrt control.
 The package never formats storage and never accesses Android userdata.
endef

define Package/xm14pro-usb-gadget/conffiles
/etc/config/xm14usb
endef

define Build/Compile
endef

define Package/xm14pro-usb-gadget/install
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/etc/config/xm14usb $(1)/etc/config/xm14usb
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-usb-gadget \
		$(1)/etc/init.d/xm14pro-usb-gadget
endef

$(eval $(call BuildPackage,xm14pro-usb-gadget))
