include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-photonicat2
PKG_VERSION:=1.0.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-3.0-only
PKG_MAINTAINER:=ning
PKGARCH:=all
PKG_FILE_DEPENDS += $(TOPDIR)/package/ning/photonicat2/root/usr/share/luci/menu.d/luci-app-photonicat2-root.json
PKG_CONFIG_DEPENDS += CONFIG_PACKAGE_luci-app-photonicat2_MODULE_CORE \
	CONFIG_PACKAGE_luci-app-photonicat2_MODULE_BASE \
	CONFIG_PACKAGE_luci-app-photonicat2_MODULE_WIRELESS \
	CONFIG_PACKAGE_luci-app-photonicat2_MODULE_LED \
	CONFIG_PACKAGE_luci-app-photonicat2_MODULE_BUZZER \
	CONFIG_PACKAGE_luci-app-photonicat2_MODULE_CPU \
	CONFIG_PACKAGE_luci-app-photonicat2_MODULE_FAN \
	CONFIG_PACKAGE_luci-app-photonicat2_MODULE_CHARGE \
	CONFIG_PACKAGE_luci-app-photonicat2_MODULE_POWER \
	CONFIG_PACKAGE_luci-app-photonicat2_MODULE_SCREEN \
	CONFIG_PACKAGE_luci-app-photonicat2_MODULE_BATTERY

include $(INCLUDE_DIR)/package.mk

define Package/luci-app-photonicat2
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=Photonicat 2 native LuCI module bundle
  DEPENDS:=
endef

define Package/luci-app-photonicat2/description
  Photonicat 2 native LuCI bundle. Select this package first, then choose
  the individual pages in its submenu; every page is enabled by default.
endef

define Package/luci-app-photonicat2/config
menu "Photonicat2 modules"
  depends on PACKAGE_luci-app-photonicat2

config PACKAGE_luci-app-photonicat2_MODULE_CORE
  bool "Hardware backend (required by pages)"
  default y
  select PACKAGE_photonicat2-native-core

config PACKAGE_luci-app-photonicat2_MODULE_BASE
  bool "Device overview"
  default y
  select PACKAGE_luci-app-photonicat2-base

config PACKAGE_luci-app-photonicat2_MODULE_WIRELESS
  bool "Built-in wireless"
  default y
  select PACKAGE_luci-app-photonicat2-wireless

config PACKAGE_luci-app-photonicat2_MODULE_LED
  bool "Power LED control"
  default y
  select PACKAGE_luci-app-photonicat2-led

config PACKAGE_luci-app-photonicat2_MODULE_BUZZER
  bool "Buzzer control"
  default y
  select PACKAGE_luci-app-photonicat2-buzzer

config PACKAGE_luci-app-photonicat2_MODULE_CPU
  bool "CPU power saving"
  default y
  select PACKAGE_luci-app-photonicat2-cpu

config PACKAGE_luci-app-photonicat2_MODULE_FAN
  bool "Smart fan"
  default y
  select PACKAGE_luci-app-photonicat2-fan

config PACKAGE_luci-app-photonicat2_MODULE_CHARGE
  bool "Charge limit"
  default y
  select PACKAGE_luci-app-photonicat2-charge

config PACKAGE_luci-app-photonicat2_MODULE_POWER
  bool "Power on/off policy"
  default y
  select PACKAGE_luci-app-photonicat2-power

config PACKAGE_luci-app-photonicat2_MODULE_SCREEN
  bool "Mini screen settings and editor"
  default y
  select PACKAGE_luci-app-photonicat2-screen

config PACKAGE_luci-app-photonicat2_MODULE_BATTERY
  bool "Battery statistics"
  default y
  select PACKAGE_luci-app-photonicat2-battery

endmenu
endef

define Build/Compile
endef

define Package/luci-app-photonicat2/install
	$(INSTALL_DIR) $(1)/usr/share/luci/menu.d
	$(INSTALL_DATA) $(TOPDIR)/package/ning/photonicat2/root/usr/share/luci/menu.d/luci-app-photonicat2-root.json \
		$(1)/usr/share/luci/menu.d/luci-app-photonicat2-root.json
endef

$(eval $(call BuildPackage,luci-app-photonicat2))
