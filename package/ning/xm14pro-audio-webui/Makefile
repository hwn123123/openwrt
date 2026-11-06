# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-audio-webui
PKG_VERSION:=1.0.0
PKG_RELEASE:=5
PKG_LICENSE:=GPL-2.0-only

LUCI_NAME:=luci-app-xm14pro-audio
LUCI_TYPE:=app
LUCI_BASENAME:=xm14pro-audio
LUCI_TITLE:=Xiaomi 14 Pro audio manager
LUCI_DESCRIPTION:=Xiaomi 14 Pro audio routing, volume and hardware controls.
LUCI_DEPENDS:=+xm14pro-audio-sidecar +luci-base +rpcd +uci
LUCI_PKGARCH:=all
LUCI_MAINTAINER:=ning
LUCI_MINIFY_CSS:=0

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot package scanner signature
