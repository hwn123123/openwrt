# SPDX-License-Identifier: GPL-3.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-openclash-core
PKG_VERSION:=2026.10.12
PKG_RELEASE:=1
PKG_LICENSE:=GPL-3.0-only
PKG_SOURCE:=clash-linux-arm64.tar.gz
PKG_SOURCE_URL:=https://raw.githubusercontent.com/vernesong/OpenClash/70e44a6f730a11730a1daa6b0a71a7634c87fe68/master/meta/
PKG_HASH:=aae510c46001b8784b1480be94c3c740db9b387e5419604abaafa4189cd99c6d
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKGARCH:=aarch64_generic

include $(INCLUDE_DIR)/package.mk

define Package/xm14pro-openclash-core
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=Web Servers/Proxies
  TITLE:=Pinned Mihomo core for Xiaomi 14 Pro OpenClash
  DEPENDS:=@TARGET_xm14pro_armv8 +luci-app-openclash
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	tar -xzf $(DL_DIR)/$(PKG_SOURCE) -C $(PKG_BUILD_DIR) --no-same-owner
endef

define Build/Compile
endef

define Package/xm14pro-openclash-core/install
	$(INSTALL_DIR) $(1)/etc/openclash/core
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/clash $(1)/etc/openclash/core/clash_meta
endef

$(eval $(call BuildPackage,xm14pro-openclash-core))
