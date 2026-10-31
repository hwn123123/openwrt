# SPDX-License-Identifier: BSD-3-Clause
include $(TOPDIR)/rules.mk

PKG_NAME:=qcom-pd-mapper
PKG_VERSION:=1.1
PKG_RELEASE:=1

PKG_SOURCE:=pd-mapper-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=https://codeload.github.com/linux-msm/pd-mapper/tar.gz/refs/tags/v$(PKG_VERSION)?
PKG_HASH:=7654a7fb8b70b0c7670f98ad9313649abc48eabcef84dad086ed01408fd538a2
PKG_BUILD_DIR:=$(BUILD_DIR)/pd-mapper-$(PKG_VERSION)

PKG_LICENSE:=BSD-3-Clause
PKG_LICENSE_FILES:=LICENSE
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/qcom-pd-mapper
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Qualcomm protection-domain mapper
  URL:=https://github.com/linux-msm/pd-mapper
  DEPENDS:=+qcom-qrtr +liblzma
endef

define Package/qcom-pd-mapper/description
 Publishes the Qualcomm service-registry map discovered from the exact stock
 Xiaomi firmware JSON files over QRTR.
endef

define Build/Compile
	+$(MAKE_VARS) $(MAKE) $(PKG_JOBS) -C $(PKG_BUILD_DIR) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) -I$(STAGING_DIR)/usr/include" \
		LDFLAGS="$(TARGET_LDFLAGS) -lqrtr -llzma"
endef

define Package/qcom-pd-mapper/install
	$(INSTALL_DIR) $(1)/usr/bin $(1)/usr/libexec $(1)/etc/init.d
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/pd-mapper $(1)/usr/bin/pd-mapper
	$(INSTALL_BIN) ./files/usr/libexec/qcom-pd-mapper-run $(1)/usr/libexec/qcom-pd-mapper-run
	$(INSTALL_BIN) ./files/etc/init.d/qcom-pd-mapper $(1)/etc/init.d/qcom-pd-mapper
endef

$(eval $(call BuildPackage,qcom-pd-mapper))
