# SPDX-License-Identifier: BSD-3-Clause
include $(TOPDIR)/rules.mk

PKG_NAME:=qcom-tqftpserv
PKG_VERSION:=1.2
PKG_RELEASE:=2

PKG_SOURCE:=tqftpserv-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=https://codeload.github.com/linux-msm/tqftpserv/tar.gz/refs/tags/v$(PKG_VERSION)?
PKG_HASH:=a3850c34e48f23b7bc360d70f1e91700de2f8c60af003eda93e14182f8cf6af1
PKG_BUILD_DIR:=$(BUILD_DIR)/tqftpserv-$(PKG_VERSION)

PKG_LICENSE:=BSD-3-Clause
PKG_LICENSE_FILES:=LICENSE
PKG_INSTALL:=1
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/meson.mk

define Package/qcom-tqftpserv
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Qualcomm QRTR TFTP firmware service
  URL:=https://github.com/linux-msm/tqftpserv
  DEPENDS:=+qcom-qrtr +libzstd
endef

define Package/qcom-tqftpserv/description
 Serves read-only stock firmware and nwrt-local modem scratch files to the
 Qualcomm remote processors over QRTR.
endef

# tqftpserv v1.2 leaves its Meson variable undefined when both systemd and
# this option are absent.  The generated unit is intentionally not packaged;
# nwrt uses the procd service below.
MESON_ARGS += -Dsystemd-unit-prefix=/usr/lib/systemd/system

define Package/qcom-tqftpserv/install
	$(INSTALL_DIR) $(1)/usr/bin $(1)/etc/init.d
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/tqftpserv $(1)/usr/bin/tqftpserv
	$(INSTALL_BIN) ./files/etc/init.d/qcom-tqftpserv $(1)/etc/init.d/qcom-tqftpserv
endef

$(eval $(call BuildPackage,qcom-tqftpserv))
