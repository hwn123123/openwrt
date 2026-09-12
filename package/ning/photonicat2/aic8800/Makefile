include $(TOPDIR)/rules.mk

PKG_NAME:=aic8800
PKG_SOURCE_DATE:=2026-09-02
PKG_SOURCE_VERSION:=516e3b087763d80c44f5e3b6d2dd63e0d925c91d
PKG_RELEASE:=2

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/radxa-pkg/aic8800.git
PKG_MIRROR_HASH:=96c326b05894384744e2a634a105e57dbba541c003227f695f40ec0537e708e1
PKG_LICENSE:=GPL-2.0-only
PKG_LICENSE_FILES:=LICENSE
PKG_MAINTAINER:=ning
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/kernel.mk
include $(INCLUDE_DIR)/package.mk

AIC_USB_DIR:=$(PKG_BUILD_DIR)/src/USB/driver_fw/drivers/aic8800
AIC_FW_DIR:=$(PKG_BUILD_DIR)/src/USB/driver_fw/fw

define KernelPackage/aic8800-usb
  SUBMENU:=Wireless Drivers
  TITLE:=AIC8800 USB wireless driver for Photonicat 2
  DEPENDS:=@TARGET_rockchip_armv8 @USB_SUPPORT +kmod-cfg80211 +kmod-usb-core +aic8800-usb-firmware
  FILES:= \
	$(AIC_USB_DIR)/aic_load_fw/aic_load_fw.ko \
	$(AIC_USB_DIR)/aic8800_fdrv/aic8800_fdrv.ko
  AUTOLOAD:=$(call AutoProbe,aic_load_fw aic8800_fdrv)
endef

define KernelPackage/aic8800-usb/description
  AICsemi AIC8800 USB fullmac driver used by the fixed onboard USB 1-1.3
  radio in Photonicat 2. This package is separate from PCIe MT7927 support.
endef

define Package/aic8800-usb-firmware
  SECTION:=firmware
  CATEGORY:=Firmware
  TITLE:=AIC8800 USB firmware for Photonicat 2
  PKGARCH:=all
endef

define Package/aic8800-usb-firmware/description
  Firmware, calibration and power-limit data for AIC8800 USB chip variants.
endef

STAMP_CONFIGURED_DEPENDS := $(STAGING_DIR)/usr/include/mac80211-backport/backport/autoconf.h

NOSTDINC_FLAGS := \
	$(KERNEL_NOSTDINC_FLAGS) \
	-I$(STAGING_DIR)/usr/include/mac80211-backport \
	-I$(STAGING_DIR)/usr/include/mac80211-backport/uapi \
	-I$(STAGING_DIR)/usr/include/mac80211 \
	-I$(STAGING_DIR)/usr/include/mac80211/uapi \
	-include backport/backport.h

define Build/Compile
	+$(KERNEL_MAKE) $(PKG_JOBS) \
		M="$(AIC_USB_DIR)" \
		NOSTDINC_FLAGS="$(NOSTDINC_FLAGS)" \
		CONFIG_AIC_LOADFW_SUPPORT=m \
		CONFIG_AIC8800_WLAN_SUPPORT=m \
		CONFIG_PLATFORM_UBUNTU=y \
		CONFIG_USE_FW_REQUEST=n \
		USER_EXTRA_CFLAGS="-Wno-error -Wno-unused-label -Wno-implicit-fallthrough" \
		modules
endef

define Package/aic8800-usb-firmware/install
	$(INSTALL_DIR) $(1)/lib/firmware/aic8800
	$(CP) $(AIC_FW_DIR)/aic8800/. $(1)/lib/firmware/aic8800/
	$(INSTALL_DIR) $(1)/lib/firmware/aic8800D80
	$(CP) $(AIC_FW_DIR)/aic8800D80/. $(1)/lib/firmware/aic8800D80/
	$(INSTALL_DIR) $(1)/lib/firmware/aic8800D80N
	$(CP) $(AIC_FW_DIR)/aic8800D80N/. $(1)/lib/firmware/aic8800D80N/
	$(INSTALL_DIR) $(1)/lib/firmware/aic8800D80X2
	$(CP) $(AIC_FW_DIR)/aic8800D80X2/. $(1)/lib/firmware/aic8800D80X2/
	$(INSTALL_DIR) $(1)/lib/firmware/aic8800DC
	$(CP) $(AIC_FW_DIR)/aic8800DC/. $(1)/lib/firmware/aic8800DC/
	$(INSTALL_DIR) $(1)/lib/firmware/aic8800DLN
	$(CP) $(AIC_FW_DIR)/aic8800DLN/. $(1)/lib/firmware/aic8800DLN/
endef

$(eval $(call KernelPackage,aic8800-usb))
$(eval $(call BuildPackage,aic8800-usb-firmware))
