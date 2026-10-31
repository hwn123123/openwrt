# SPDX-License-Identifier: BSD-3-Clause
include $(TOPDIR)/rules.mk

PKG_NAME:=qcom-qrtr
PKG_VERSION:=1.2
PKG_RELEASE:=1

PKG_SOURCE:=qrtr-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=https://codeload.github.com/linux-msm/qrtr/tar.gz/refs/tags/v$(PKG_VERSION)?
PKG_HASH:=7a82bf80246fe71287b13c66c0466208822e8337fcd4aaf302eee6c5fcb48a52
PKG_BUILD_DIR:=$(BUILD_DIR)/qrtr-$(PKG_VERSION)

PKG_LICENSE:=BSD-3-Clause
PKG_LICENSE_FILES:=LICENSE
PKG_INSTALL:=1
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/meson.mk

define Package/qcom-qrtr
  SECTION:=libs
  CATEGORY:=Libraries
  TITLE:=Qualcomm QRTR userspace library and diagnostics
  URL:=https://github.com/linux-msm/qrtr
endef

define Package/qcom-qrtr/description
 Linux userspace support for Qualcomm IPC Router sockets.  The kernel in the
 Xiaomi boot chain owns the QRTR name service; this package supplies libqrtr
 plus qrtr-lookup and qrtr-cfg for modem services and diagnostics.
endef

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include $(1)/usr/lib/pkgconfig $(1)/usr/lib
	$(CP) $(PKG_INSTALL_DIR)/usr/include/libqrtr.h $(1)/usr/include/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libqrtr.so* $(1)/usr/lib/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/pkgconfig/qrtr.pc $(1)/usr/lib/pkgconfig/
endef

define Package/qcom-qrtr/install
	$(INSTALL_DIR) $(1)/usr/lib $(1)/usr/bin
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libqrtr.so.* $(1)/usr/lib/
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/qrtr-lookup $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/qrtr-cfg $(1)/usr/bin/
endef

$(eval $(call BuildPackage,qcom-qrtr))
