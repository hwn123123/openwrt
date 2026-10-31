# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-battery
PKG_VERSION:=1.0.0
PKG_RELEASE:=8
PKG_LICENSE:=GPL-2.0-only

LUCI_NAME:=luci-app-xm14pro-battery
LUCI_TYPE:=app
LUCI_BASENAME:=xm14pro-battery
LUCI_TITLE:=Xiaomi battery status and controls
LUCI_DESCRIPTION:=Real power-supply telemetry and capability-gated battery controls for the Xiaomi 14 Pro.
LUCI_DEPENDS:=+luci-base +rpcd +jshn +uci
LUCI_PKGARCH:=all
LUCI_MAINTAINER:=ning
LUCI_MINIFY_CSS:=0

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot package scanner signature

