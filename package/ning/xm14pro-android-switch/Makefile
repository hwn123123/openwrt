# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-android-switch
PKG_VERSION:=1.0.0
PKG_RELEASE:=13
PKG_LICENSE:=LicenseRef-Xiaomi-Qualcomm-Proprietary

# These are Android bionic executables and libraries. OpenWrt's sstrip removes
# section metadata that Android's linker validates, so preserve the vendor
# files byte-for-byte in this package.
RSTRIP:=:

LUCI_NAME:=luci-app-xm14pro-android-switch
LUCI_TYPE:=app
LUCI_BASENAME:=xm14pro-android-switch
LUCI_TITLE:=Switch Xiaomi 14 Pro to Android slot A
LUCI_DESCRIPTION:=Switch to Android or enter the shared A/B recovery selector.
LUCI_DEPENDS:=@TARGET_xm14pro_armv8 +luci-base +rpcd +xm14pro-qwes
LUCI_PKGARCH:=aarch64_generic
LUCI_MAINTAINER:=ning

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot package scanner signature
