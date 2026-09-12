include $(TOPDIR)/rules.mk

PKG_NAME:=pcat-sidecar
PKG_VERSION:=2.0.0
PKG_RELEASE:=3
PKG_LICENSE:=GPL-3.0-only
PKGARCH:=all

include $(INCLUDE_DIR)/package.mk

define Package/pcat-sidecar
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Photonicat local data sidecar
  DEPENDS:=@TARGET_rockchip_armv8 +python3 +python3-sqlite3
endef

define Package/pcat-sidecar/description
  Local-only data service for the Photonicat front-panel display and Web UI.
  It reads the vendor SMS database and stores seven days of device telemetry
  without opening a modem serial port.
endef

define Build/Compile
endef

define Package/pcat-sidecar/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) ./files/pcat-sidecar.py $(1)/usr/sbin/pcat-sidecar
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/pcat-sidecar.init $(1)/etc/init.d/pcat-sidecar
endef

$(eval $(call BuildPackage,pcat-sidecar))
