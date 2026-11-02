# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-display-webui
PKG_VERSION:=1.0.0
PKG_RELEASE:=16
PKG_PO_VERSION:=$(PKG_VERSION)-r$(PKG_RELEASE)
PKG_LICENSE:=GPL-2.0-only

LUCI_NAME:=luci-app-xm14pro-display
LUCI_TYPE:=app
LUCI_BASENAME:=xm14pro-display
LUCI_TITLE:=Xiaomi native-screen manager
LUCI_DESCRIPTION:=LuCI controls for the Xiaomi 14 Pro native nwrt display.
LUCI_DEPENDS:=+xm14pro-display +luci-base +rpcd +uci
LUCI_PKGARCH:=all
LUCI_MAINTAINER:=ning
LUCI_MINIFY_CSS:=0

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot package scanner signature

