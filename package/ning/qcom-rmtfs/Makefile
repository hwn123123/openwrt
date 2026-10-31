# SPDX-License-Identifier: BSD-3-Clause
include $(TOPDIR)/rules.mk

PKG_NAME:=qcom-rmtfs
PKG_VERSION:=1.3
PKG_RELEASE:=1

PKG_SOURCE:=rmtfs-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=https://codeload.github.com/linux-msm/rmtfs/tar.gz/refs/tags/v$(PKG_VERSION)?
PKG_HASH:=4b6c398806a1046e2256a685d592833de0f37b7245b8adc4797e1791f0e117ee
PKG_BUILD_DIR:=$(BUILD_DIR)/rmtfs-$(PKG_VERSION)

PKG_LICENSE:=BSD-3-Clause
PKG_LICENSE_FILES:=LICENSE
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/qcom-rmtfs
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Qualcomm remote modem filesystem service
  URL:=https://github.com/linux-msm/rmtfs
  DEPENDS:=+qcom-qrtr +libudev-zero
endef

define Package/qcom-rmtfs/description
 Serves the modem EFS partitions over QRTR.  Xiaomi 14 Pro starts it in
 read-only shadow mode by default so first-stage nwrt testing cannot persist
 changes to the Android modemst/fsg/fsc partitions.
endef

define Build/Compile
	+$(MAKE_VARS) $(MAKE) $(PKG_JOBS) -C $(PKG_BUILD_DIR) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) -I$(STAGING_DIR)/usr/include" \
		LDFLAGS="$(TARGET_LDFLAGS) -lqrtr -ludev -lpthread"
endef

define Package/qcom-rmtfs/install
	$(INSTALL_DIR) $(1)/usr/bin $(1)/etc/init.d
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/rmtfs $(1)/usr/bin/rmtfs
	$(INSTALL_BIN) ./files/etc/init.d/qcom-rmtfs $(1)/etc/init.d/qcom-rmtfs
endef

$(eval $(call BuildPackage,qcom-rmtfs))
