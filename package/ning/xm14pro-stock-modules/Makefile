# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-stock-modules
PKG_VERSION:=3.0.308.0
PKG_RELEASE:=25
PKG_LICENSE:=Proprietary
PKGARCH:=aarch64_generic

# Preserve Xiaomi's signed vendor modules byte-for-byte.
RSTRIP:=:

include $(INCLUDE_DIR)/package.mk

# These vendor_dlkm modules depend on Xiaomi's early-boot modules, which are
# already loaded from vendor_boot before the nwrt rootfs starts.  They are not
# OpenWrt shared-library packages, so the generic package dependency scanner
# cannot resolve them.  Keep this exception local to this binary-module package.
define CheckDependencies
endef

define Package/xm14pro-stock-modules
  SECTION:=kernel
  CATEGORY:=Kernel modules
  SUBMENU:=Other modules
  TITLE:=Xiaomi 14 Pro stock phone hardware modules
  DEPENDS:=@TARGET_xm14pro +kmod +xm14pro-platform
endef

define Package/xm14pro-stock-modules/description
 Stock vendor_dlkm module closures required by the Xiaomi 14 Pro ADSP/audio,
 BT/FM Slimbus, Synaptics touch panel, Wi-Fi 7, Qualcomm IPA/rmnet, NFC/eSE
 and infrared hardware. NFC and infrared remain opt-in until their complete
 stock userspace closures pass a physical cold-boot test. The loader never
 starts a remote processor directly.
endef

define Build/Compile
endef

define Package/xm14pro-stock-modules/conffiles
/etc/config/xm14modules
endef

define Package/xm14pro-stock-modules/install
	$(INSTALL_DIR) $(1)/usr/lib/xm14pro/stock-modules
	$(INSTALL_DATA) ./files/usr/lib/xm14pro/stock-modules/*.ko \
		$(1)/usr/lib/xm14pro/stock-modules/
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) ./files/usr/sbin/xm14pro-stock-modules \
		$(1)/usr/sbin/xm14pro-stock-modules
	$(INSTALL_DIR) $(1)/usr/lib/firmware
	$(INSTALL_DATA) ./files/usr/lib/firmware/shennong_syna_thp_config_*.ini \
		./files/usr/lib/firmware/shennong_test_limits_S3910P_*.csv \
		./files/usr/lib/firmware/synaptics_spi_shennong_*.img \
		$(1)/usr/lib/firmware/
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro
	ln -sf /sbin/kmod $(1)/usr/libexec/xm14pro/insmod
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro/wifi-ready \
		$(1)/usr/libexec/xm14pro/wifi-ready
	$(INSTALL_DIR) $(1)/usr/lib/firmware/wlan/qca_cld/kiwi_v2
	$(INSTALL_DATA) ./files/usr/lib/firmware/wlan/qca_cld/kiwi_v2/WCNSS_qcom_cfg.ini \
		$(1)/usr/lib/firmware/wlan/qca_cld/kiwi_v2/
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-stock-modules \
		$(1)/etc/init.d/xm14pro-stock-modules
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/etc/config/xm14modules \
		$(1)/etc/config/xm14modules
endef

$(eval $(call BuildPackage,xm14pro-stock-modules))
