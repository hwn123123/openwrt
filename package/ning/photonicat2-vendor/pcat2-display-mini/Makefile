include $(TOPDIR)/rules.mk

PKG_NAME:=pcat2-display-mini
PKG_VERSION:=1.0.0
PKG_RELEASE:=21

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/photonicat/photonicat2_mini_display
PKG_SOURCE_DATE:=2026-08-19
PKG_SOURCE_VERSION:=bd1e054b1e874d9846875e65abcf807a8784643c
PKG_MIRROR_HASH:=3b9e280ef3e9ee51b378661a43ad79ad907e93bf2025b6e3b75c5083e9a58c7b

PKG_LICENSE:=GPL-3.0
PKG_MAINTAINER:=Photonicat
PKG_BUILD_DEPENDS:=golang/host
PKG_BUILD_PARALLEL:=1
PKG_BUILD_FLAGS:=no-mips16

GO_PKG:=github.com/photonicat/photonicat2_mini_display
GO_PKG_BUILD_PKG:=$(GO_PKG)
GO_PKG_LDFLAGS_X:=main.version=$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk
include $(TOPDIR)/feeds/packages/lang/golang/golang-package.mk

define Package/pcat2-display-mini
  TITLE:=Official Photonicat 2 display service
  SECTION:=utils
  CATEGORY:=Utilities
  DEPENDS:=$(GO_ARCH_DEPENDS) +pcat-sidecar
  URL:=https://github.com/photonicat/photonicat2_mini_display
endef

define Package/pcat2-display-mini/description
  Official Photonicat 2 mini-display service with local read-only SMS sidecar
  integration and incoming-message screen notification.
endef

define Package/pcat2-display-mini/install
	$(call GoPackage/Package/Install/Bin,$(1))
	$(INSTALL_DIR) $(1)/etc
	$(INSTALL_DIR) $(1)/usr/share/pcat2_mini_display/assets/fonts
	$(INSTALL_DIR) $(1)/usr/share/pcat2_mini_display/assets/html
	$(INSTALL_DIR) $(1)/usr/share/pcat2_mini_display/assets/svg
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/assets/fonts/*.* \
		$(1)/usr/share/pcat2_mini_display/assets/fonts
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/assets/html/*.* \
		$(1)/usr/share/pcat2_mini_display/assets/html
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/assets/svg/*.* \
		$(1)/usr/share/pcat2_mini_display/assets/svg
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/config.json \
		$(1)/etc/pcat2_mini_display-config.json
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/pcat2-display-mini.init \
		$(1)/etc/init.d/pcat2-display-mini
endef

define Package/pcat2-display-mini/conffiles
/etc/pcat2_mini_display-user_config.json
endef

$(eval $(call GoBinPackage,pcat2-display-mini))
$(eval $(call BuildPackage,pcat2-display-mini))
