include $(TOPDIR)/rules.mk

PKG_NAME:=photonicat-pm
PKG_VERSION:=2.0.0
PKG_RELEASE:=1

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/photonicat/rockchip_rk3568_pcat_manager.git
PKG_SOURCE_DATE:=2026-08-26
PKG_SOURCE_VERSION:=0e61a4abf35c423420310311f1b2b059deba0cac
PKG_MIRROR_HASH:=5fd231401ceaedfcadb1bd3c8788f1ae283be9701b2e22cf2c5553a7209f7458

PKG_MAINTAINER:=Photonicat
PKG_LICENSE:=GPL-2.0-or-later
PKG_LICENSE_FILES:=LICENSE

include $(INCLUDE_DIR)/kernel.mk
include $(INCLUDE_DIR)/package.mk

define KernelPackage/photonicat-pm
  SUBMENU:=Other modules
  TITLE:=Official Photonicat power, battery and fan driver
  DEPENDS:=@TARGET_rockchip_armv8 +kmod-hwmon-core +kmod-thermal
  FILES:=$(PKG_BUILD_DIR)/mods/photonicat-pm.ko
  AUTOLOAD:=$(call AutoProbe,photonicat-pm)
endef

define KernelPackage/photonicat-pm/description
  Official Photonicat PMU driver. The only local patch adapts two kernel APIs
  removed after the manufacturer's original kernel version.
endef

define Build/Compile
	+$(KERNEL_MAKE) $(PKG_JOBS) \
		M="$(PKG_BUILD_DIR)/mods" \
		modules
endef

$(eval $(call KernelPackage,photonicat-pm))
