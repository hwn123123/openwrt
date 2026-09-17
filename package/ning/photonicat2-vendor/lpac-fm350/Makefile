include $(TOPDIR)/rules.mk

PKG_NAME:=lpac-fm350
PKG_VERSION:=2.3.0
PKG_RELEASE:=1

PKG_SOURCE:=lpac-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=https://codeload.github.com/estkme-group/lpac/tar.gz/refs/tags/v$(PKG_VERSION)?
PKG_HASH:=661dffbd1e9e5732dab4a0bb0a9837d4906c8c66bd748bda262fe3e8d3e420f6
PKG_BUILD_DIR:=$(BUILD_DIR)/lpac-$(PKG_VERSION)

PKG_LICENSE:=AGPL-3.0-only LGPL-2.0-only
PKG_MAINTAINER:=Photonicat 2 nwrt

CMAKE_INSTALL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/lpac-fm350
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=FM350 eUICC profile manager
  URL:=https://github.com/estkme-group/lpac
  DEPENDS:=@TARGET_rockchip_armv8 +libcurl +flock
endef

define Package/lpac-fm350/description
  lpac with the AT APDU backend enabled and FM350 response compatibility.
endef

TARGET_CFLAGS += $(FPIC)
CMAKE_OPTIONS += \
  -DLPAC_WITH_APDU_PCSC=OFF \
  -DLPAC_WITH_APDU_AT=ON \
  -DLPAC_WITH_APDU_UQMI=OFF \
  -DLPAC_WITH_APDU_MBIM=OFF \
  -DLPAC_WITH_APDU_QMI_QRTR=OFF

define Package/lpac-fm350/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/lpac $(1)/usr/bin/lpac-fm350
endef

$(eval $(call BuildPackage,lpac-fm350))
