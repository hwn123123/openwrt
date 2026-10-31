# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-cellular-webui
PKG_VERSION:=1.0.0
PKG_RELEASE:=60
PKG_PO_VERSION:=$(PKG_VERSION)-r$(PKG_RELEASE)
PKG_LICENSE:=GPL-2.0-only AND MIT
PKG_LICENSE_FILES:=LICENSE.reference-ui

LUCI_NAME:=luci-app-xm14pro-mobile
LUCI_TYPE:=app
LUCI_BASENAME:=xm14pro-mobile
LUCI_TITLE:=Native Xiaomi 14 Pro mobile-network controls
LUCI_DESCRIPTION:=Native LuCI pages for the Xiaomi 14 Pro phone modem. No iframe or separate HTTP service.
LUCI_DEPENDS:=+xm14pro-cellular +xm14pro-bonding +luci-base +luci-proto-modemmanager +rpcd +uci +jshn +jsonfilter +px5g-mbedtls
LUCI_PKGARCH:=all
LUCI_MAINTAINER:=ning
LUCI_MINIFY_CSS:=0

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot signature
