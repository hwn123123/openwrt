# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-camera-webui
PKG_VERSION:=1.1.0
PKG_RELEASE:=4
PKG_LICENSE:=GPL-2.0-only

LUCI_NAME:=luci-app-xm14pro-camera
LUCI_TYPE:=app
LUCI_BASENAME:=xm14pro-camera
LUCI_TITLE:=Xiaomi 14 Pro camera hardware manager
LUCI_DESCRIPTION:=Camera snapshot, photo gallery, device status and diagnostics.
LUCI_DEPENDS:=+xm14pro-camera-kernel +xm14pro-camera-sidecar +luci-base +rpcd
LUCI_PKGARCH:=all
LUCI_MAINTAINER:=ning

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot package scanner signature
